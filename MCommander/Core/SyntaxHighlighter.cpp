#include "SyntaxHighlighter.hpp"
#include "Models.hpp"

#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace mc {

namespace {

constexpr uint32_t col32(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(r);
}

bool isIdentStart(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) || c == '_' || c == '$';
}

bool isIdentChar(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_' || c == '-';
}

const std::unordered_set<std::string>& cppKeywords() {
    static const std::unordered_set<std::string> s = {
        "alignas", "alignof", "and", "asm", "auto", "break", "case", "catch", "class",
        "const", "constexpr", "consteval", "continue", "co_await", "co_return", "co_yield",
        "default", "delete", "do", "else", "enum", "explicit", "export", "extern", "false",
        "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new",
        "noexcept", "nullptr", "operator", "or", "override", "private", "protected", "public",
        "return", "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
        "template", "this", "throw", "true", "try", "typedef", "typeid", "typename", "union",
        "using", "virtual", "volatile", "while", "import", "func", "let", "var", "function"
    };
    return s;
}

const std::unordered_set<std::string>& cppTypes() {
    static const std::unordered_set<std::string> s = {
        "bool", "char", "double", "float", "int", "int8_t", "int16_t", "int32_t", "int64_t",
        "long", "short", "size_t", "ssize_t", "std", "string", "uint8_t", "uint16_t",
        "uint32_t", "uint64_t", "unsigned", "vector", "void", "optional", "shared_ptr",
        "unique_ptr", "ImVec2", "ImVec4", "ImU32", "NSString", "NSURL"
    };
    return s;
}

const std::unordered_set<std::string>& luaKeywords() {
    static const std::unordered_set<std::string> s = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
        "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
    };
    return s;
}

const std::unordered_set<std::string>& shellKeywords() {
    static const std::unordered_set<std::string> s = {
        "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case",
        "esac", "in", "function", "return", "local", "export", "set", "unset", "exit",
        "echo", "printf", "cd", "add", "set", "remove", "print", "get", "enable", "disable"
    };
    return s;
}

} // namespace

SyntaxLanguage detectSyntaxLanguage(const std::string& fileNameOrPath) {
    std::string ext = toLowerASCII(fs::path(fileNameOrPath).extension().string());
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());

    if (ext == "c" || ext == "cc" || ext == "cpp" || ext == "cxx" ||
        ext == "h" || ext == "hh" || ext == "hpp" || ext == "hxx" ||
        ext == "m" || ext == "mm" || ext == "swift" || ext == "js" || ext == "ts") {
        return SyntaxLanguage::Cpp;
    }
    if (ext == "lua") return SyntaxLanguage::Lua;
    if (ext == "json") return SyntaxLanguage::Json;
    if (ext == "sh" || ext == "zsh" || ext == "bash" || ext == "fish" || ext == "rsc" || ext == "conf") {
        return SyntaxLanguage::Shell;
    }
    if (ext == "md" || ext == "markdown") return SyntaxLanguage::Markdown;
    if (ext == "xml" || ext == "plist" || ext == "html" || ext == "htm" || ext == "xcproj" || ext == "svg") {
        return SyntaxLanguage::Xml;
    }
    return SyntaxLanguage::Plain;
}

uint32_t syntaxTokenColor(SyntaxTokenKind kind) {
    switch (kind) {
        case SyntaxTokenKind::Normal:       return col32(0xF8, 0xF8, 0xF2);
        case SyntaxTokenKind::Keyword:      return col32(0xF9, 0x26, 0x72);
        case SyntaxTokenKind::Type:         return col32(0x66, 0xD9, 0xEF);
        case SyntaxTokenKind::String:       return col32(0xE6, 0xDB, 0x74);
        case SyntaxTokenKind::Number:       return col32(0xAE, 0x81, 0xFF);
        case SyntaxTokenKind::Comment:      return col32(0x75, 0x71, 0x5E);
        case SyntaxTokenKind::Preprocessor: return col32(0xA6, 0xE2, 0x2E);
    }
    return col32(0xF8, 0xF8, 0xF2);
}

std::vector<SyntaxSpan> tokenizeSyntaxLine(const std::string& line, SyntaxLanguage lang, bool& inBlockComment) {
    std::vector<SyntaxSpan> spans;
    if (line.empty()) return spans;
    if (lang == SyntaxLanguage::Plain) {
        spans.push_back({0, line.size(), SyntaxTokenKind::Normal});
        return spans;
    }

    size_t n = line.size();
    size_t i = 0;

    if (lang == SyntaxLanguage::Markdown) {
        if (line[0] == '#') {
            spans.push_back({0, n, SyntaxTokenKind::Keyword});
            return spans;
        }
        if (line.rfind("```", 0) == 0 || line.rfind("> ", 0) == 0) {
            spans.push_back({0, n, SyntaxTokenKind::Comment});
            return spans;
        }
    }

    while (i < n) {
        if (inBlockComment) {
            auto endPos = line.find("*/", i);
            if (endPos == std::string::npos) {
                spans.push_back({i, n - i, SyntaxTokenKind::Comment});
                break;
            }
            spans.push_back({i, endPos + 2 - i, SyntaxTokenKind::Comment});
            i = endPos + 2;
            inBlockComment = false;
            continue;
        }

        // Line comments
        if ((lang == SyntaxLanguage::Cpp && i + 1 < n && line[i] == '/' && line[i + 1] == '/') ||
            (lang == SyntaxLanguage::Lua && i + 1 < n && line[i] == '-' && line[i + 1] == '-') ||
            (lang == SyntaxLanguage::Shell && line[i] == '#')) {
            spans.push_back({i, n - i, SyntaxTokenKind::Comment});
            break;
        }

        // Block comments (C/C++)
        if (lang == SyntaxLanguage::Cpp && i + 1 < n && line[i] == '/' && line[i + 1] == '*') {
            auto endPos = line.find("*/", i + 2);
            if (endPos == std::string::npos) {
                spans.push_back({i, n - i, SyntaxTokenKind::Comment});
                inBlockComment = true;
                break;
            }
            spans.push_back({i, endPos + 2 - i, SyntaxTokenKind::Comment});
            i = endPos + 2;
            continue;
        }

        // Preprocessor (#include, #define...)
        if (lang == SyntaxLanguage::Cpp && line[i] == '#' && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])))) {
            spans.push_back({i, n - i, SyntaxTokenKind::Preprocessor});
            break;
        }

        // XML / HTML tags
        if (lang == SyntaxLanguage::Xml && line[i] == '<') {
            size_t endTag = line.find('>', i);
            if (endTag != std::string::npos) {
                spans.push_back({i, endTag + 1 - i, SyntaxTokenKind::Keyword});
                i = endTag + 1;
                continue;
            }
        }

        // Strings
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            size_t start = i++;
            while (i < n) {
                if (line[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (line[i] == quote) {
                    ++i;
                    break;
                }
                ++i;
            }
            // JSON keys before ':'
            SyntaxTokenKind k = SyntaxTokenKind::String;
            if (lang == SyntaxLanguage::Json && quote == '"') {
                size_t j = i;
                while (j < n && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                if (j < n && line[j] == ':') k = SyntaxTokenKind::Type;
            }
            spans.push_back({start, i - start, k});
            continue;
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(line[i]))) {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '.')) ++i;
            spans.push_back({start, i - start, SyntaxTokenKind::Number});
            continue;
        }

        // Identifiers / Keywords
        if (isIdentStart(line[i])) {
            size_t start = i++;
            while (i < n && isIdentChar(line[i])) ++i;
            std::string word = line.substr(start, i - start);
            SyntaxTokenKind k = SyntaxTokenKind::Normal;
            if (lang == SyntaxLanguage::Cpp) {
                if (cppKeywords().contains(word)) k = SyntaxTokenKind::Keyword;
                else if (cppTypes().contains(word)) k = SyntaxTokenKind::Type;
            } else if (lang == SyntaxLanguage::Lua) {
                if (luaKeywords().contains(word)) k = SyntaxTokenKind::Keyword;
            } else if (lang == SyntaxLanguage::Shell) {
                if (shellKeywords().contains(word)) k = SyntaxTokenKind::Keyword;
                else if (word.front() == '$') k = SyntaxTokenKind::Type;
            } else if (lang == SyntaxLanguage::Json) {
                if (word == "true" || word == "false" || word == "null") k = SyntaxTokenKind::Number;
            }
            spans.push_back({start, i - start, k});
            continue;
        }

        // Other punctuation/whitespace
        size_t start = i++;
        while (i < n && !isIdentStart(line[i]) && !std::isdigit(static_cast<unsigned char>(line[i])) &&
               line[i] != '"' && line[i] != '\'' && line[i] != '/' && line[i] != '#' && line[i] != '<' && line[i] != '-') {
            ++i;
        }
        spans.push_back({start, i - start, SyntaxTokenKind::Normal});
    }

    return spans;
}

} // namespace mc
