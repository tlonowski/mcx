#include "Localization.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace mc {

namespace {

void skipWhitespaceAndComments(const std::string& src, size_t& pos) {
    while (pos < src.size()) {
        char c = src[pos];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++pos;
        } else if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/') {
            pos += 2;
            while (pos < src.size() && src[pos] != '\n') ++pos;
        } else {
            break;
        }
    }
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool parseJsonString(const std::string& src, size_t& pos, std::string& out) {
    if (pos >= src.size() || src[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < src.size()) {
        char c = src[pos++];
        if (c == '"') return true;
        if (c == '\\') {
            if (pos >= src.size()) return false;
            char esc = src[pos++];
            switch (esc) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > src.size()) return false;
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = src[pos++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
                        else return false;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    out.push_back(esc);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

} // namespace

Localization& Localization::instance() {
    static Localization inst;
    return inst;
}

bool Localization::parseFlatJsonFile(const std::string& filePath, std::unordered_map<std::string, std::string>& outMap) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();

    size_t pos = 0;
    skipWhitespaceAndComments(src, pos);
    if (pos >= src.size() || src[pos] != '{') return false;
    ++pos;

    while (pos < src.size()) {
        skipWhitespaceAndComments(src, pos);
        if (pos < src.size() && src[pos] == '}') return true;

        std::string key;
        if (!parseJsonString(src, pos, key)) return false;

        skipWhitespaceAndComments(src, pos);
        if (pos >= src.size() || src[pos] != ':') return false;
        ++pos;

        skipWhitespaceAndComments(src, pos);
        std::string val;
        if (pos < src.size() && src[pos] == '"') {
            if (!parseJsonString(src, pos, val)) return false;
            outMap[key] = std::move(val);
        } else {
            // Skip non-string value gracefully
            while (pos < src.size() && src[pos] != ',' && src[pos] != '}') ++pos;
        }

        skipWhitespaceAndComments(src, pos);
        if (pos < src.size() && src[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < src.size() && src[pos] == '}') {
            return true;
        }
        break;
    }
    return !outMap.empty();
}

void Localization::initialize(const std::vector<std::string>& searchDirs, const std::string& preferredCode) {
    locales_.clear();
    std::error_code ec;

    for (const auto& dir : searchDirs) {
        if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".json") continue;

            std::string code = entry.path().stem().string();
            bool duplicate = false;
            for (const auto& existing : locales_) {
                if (existing.code == code) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            std::unordered_map<std::string, std::string> tmp;
            if (parseFlatJsonFile(entry.path().string(), tmp)) {
                LocaleInfo info;
                info.code = code;
                auto itName = tmp.find("_language_name");
                info.displayName = (itName != tmp.end() && !itName->second.empty()) ? itName->second : code;
                info.filePath = entry.path().string();
                locales_.push_back(std::move(info));
            }
        }
    }

    std::sort(locales_.begin(), locales_.end(), [](const LocaleInfo& a, const LocaleInfo& b) {
        return a.code < b.code;
    });

    // Load English as fallback if available
    fallbackMap_.clear();
    for (const auto& loc : locales_) {
        if (loc.code == "en") {
            parseFlatJsonFile(loc.filePath, fallbackMap_);
            break;
        }
    }

    if (!setLanguage(preferredCode)) {
        if (!setLanguage("en") && !locales_.empty()) {
            setLanguage(locales_.front().code);
        }
    }
}

bool Localization::setLanguage(const std::string& code) {
    for (const auto& loc : locales_) {
        if (loc.code == code) {
            std::unordered_map<std::string, std::string> loaded;
            if (parseFlatJsonFile(loc.filePath, loaded)) {
                activeMap_ = std::move(loaded);
                currentCode_ = code;
                return true;
            }
        }
    }
    return false;
}

const char* Localization::tr(const char* key) const {
    if (!key) return "";
    auto it = activeMap_.find(key);
    if (it != activeMap_.end()) return it->second.c_str();
    auto fit = fallbackMap_.find(key);
    if (fit != fallbackMap_.end()) return fit->second.c_str();
    return key;
}

std::string Localization::trStr(const char* key) const {
    return std::string(tr(key));
}

} // namespace mc
