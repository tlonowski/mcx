#include "ToolsService.hpp"
#include "../Platform/MacBridge.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

namespace fs = std::filesystem;

namespace mc {

namespace {

std::string trimWhitespace(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

bool containsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        }
    );
    return it != haystack.end();
}

bool globMatchCaseInsensitive(const std::string& text, const std::string& pattern) {
    size_t ti = 0, pi = 0;
    size_t starIdx = std::string::npos;
    size_t matchIdx = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() &&
            (pattern[pi] == '?' ||
             std::tolower(static_cast<unsigned char>(pattern[pi])) ==
                 std::tolower(static_cast<unsigned char>(text[ti])))) {
            ++ti;
            ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starIdx = pi++;
            matchIdx = ti;
        } else if (starIdx != std::string::npos) {
            pi = starIdx + 1;
            ti = ++matchIdx;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }
    return pi == pattern.size();
}

std::string replaceAllLiteral(std::string str, const std::string& from, const std::string& to, bool caseSensitive) {
    if (from.empty()) return str;
    if (caseSensitive) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
        return str;
    }

    std::string lowerStr = toLowerASCII(str);
    std::string lowerFrom = toLowerASCII(from);
    std::string result;
    result.reserve(str.size());
    size_t pos = 0;
    while (true) {
        size_t found = lowerStr.find(lowerFrom, pos);
        if (found == std::string::npos) {
            result.append(str, pos, str.size() - pos);
            break;
        }
        result.append(str, pos, found - pos);
        result.append(to);
        pos = found + from.size();
    }
    return result;
}

std::string toUpperASCII(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::string capitalizeWords(std::string s) {
    bool newWord = true;
    for (char& ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isspace(uc) || ch == '_' || ch == '-' || ch == '.') {
            newWord = true;
        } else if (newWord) {
            ch = static_cast<char>(std::toupper(uc));
            newWord = false;
        } else {
            ch = static_cast<char>(std::tolower(uc));
        }
    }
    return s;
}

bool fileContainsTextStream(const fs::path& path, const std::string& query) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    const size_t chunkSize = 65536;
    const size_t overlap = query.size() > 1 ? query.size() - 1 : 0;
    std::string buffer(chunkSize + overlap, '\0');
    size_t carry = 0;

    while (in) {
        in.read(buffer.data() + carry, static_cast<std::streamsize>(chunkSize));
        std::streamsize bytesRead = in.gcount();
        if (bytesRead <= 0) break;

        size_t totalLen = carry + static_cast<size_t>(bytesRead);
        std::string_view view(buffer.data(), totalLen);
        auto it = std::search(
            view.begin(), view.end(),
            query.begin(), query.end(),
            [](unsigned char a, unsigned char b) {
                return std::tolower(a) == std::tolower(b);
            }
        );
        if (it != view.end()) return true;

        if (overlap > 0 && totalLen >= overlap) {
            std::memmove(buffer.data(), buffer.data() + totalLen - overlap, overlap);
            carry = overlap;
        } else {
            carry = 0;
        }
    }
    return false;
}

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string bytesToHexLower(const unsigned char* data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[(data[i] >> 4) & 0xF]);
        out.push_back(hex[data[i] & 0xF]);
    }
    return out;
}

} // namespace

bool ToolsService::matchesWildcard(const std::string& text, const std::string& pattern) {
    std::string trimmed = trimWhitespace(pattern);
    if (trimmed.empty() || trimmed == "*" || trimmed == "*.*") {
        return true;
    }

    size_t start = 0;
    while (start < trimmed.size()) {
        size_t end = trimmed.find_first_of("; ", start);
        if (end == std::string::npos) end = trimmed.size();
        std::string sub = trimWhitespace(trimmed.substr(start, end - start));
        if (!sub.empty()) {
            if (sub.find('*') == std::string::npos && sub.find('?') == std::string::npos) {
                if (containsCaseInsensitive(text, sub)) return true;
            } else {
                if (globMatchCaseInsensitive(text, sub)) return true;
            }
        }
        start = end + 1;
    }
    return false;
}

std::pair<std::unordered_set<std::string>, std::unordered_set<std::string>>
ToolsService::compareDirectories(const std::vector<FileItem>& left, const std::vector<FileItem>& right) {
    std::unordered_map<std::string, const FileItem*> leftMap;
    std::unordered_map<std::string, const FileItem*> rightMap;
    leftMap.reserve(left.size());
    rightMap.reserve(right.size());

    for (const auto& item : left) {
        if (!item.isParentEntry) leftMap[item.name] = &item;
    }
    for (const auto& item : right) {
        if (!item.isParentEntry) rightMap[item.name] = &item;
    }

    std::unordered_set<std::string> leftDiffs;
    std::unordered_set<std::string> rightDiffs;

    for (const auto& item : left) {
        if (item.isParentEntry) continue;
        auto it = rightMap.find(item.name);
        if (it != rightMap.end()) {
            const FileItem* counterpart = it->second;
            if (!item.isNavigableDirectory() && !counterpart->isNavigableDirectory()) {
                if (item.size.has_value() && counterpart->size.has_value() && *item.size != *counterpart->size) {
                    leftDiffs.insert(item.path);
                    rightDiffs.insert(counterpart->path);
                } else if (item.modificationTime.has_value() && counterpart->modificationTime.has_value()) {
                    double diff = std::difftime(*item.modificationTime, *counterpart->modificationTime);
                    if (std::abs(diff) > 2.0) {
                        if (diff > 0) leftDiffs.insert(item.path);
                        else rightDiffs.insert(counterpart->path);
                    }
                }
            }
        } else {
            leftDiffs.insert(item.path);
        }
    }

    for (const auto& item : right) {
        if (item.isParentEntry) continue;
        if (!leftMap.contains(item.name)) {
            rightDiffs.insert(item.path);
        }
    }

    return {std::move(leftDiffs), std::move(rightDiffs)};
}

std::vector<SyncComparisonRow>
ToolsService::buildSyncComparison(const std::vector<FileItem>& left, const std::vector<FileItem>& right) {
    std::unordered_map<std::string, const FileItem*> leftMap;
    std::unordered_map<std::string, const FileItem*> rightMap;
    std::vector<std::string> allNames;

    for (const auto& item : left) {
        if (item.isParentEntry || item.isNavigableDirectory()) continue;
        leftMap[item.name] = &item;
        allNames.push_back(item.name);
    }
    for (const auto& item : right) {
        if (item.isParentEntry || item.isNavigableDirectory()) continue;
        rightMap[item.name] = &item;
        if (!leftMap.contains(item.name)) {
            allNames.push_back(item.name);
        }
    }

    std::sort(allNames.begin(), allNames.end());
    std::vector<SyncComparisonRow> rows;
    rows.reserve(allNames.size());

    for (const auto& name : allNames) {
        const FileItem* l = leftMap.contains(name) ? leftMap[name] : nullptr;
        const FileItem* r = rightMap.contains(name) ? rightMap[name] : nullptr;
        SyncComparisonRow row;
        row.relativeName = name;
        if (l) {
            row.leftPath = l->path;
            row.leftSize = l->size;
            row.leftTime = l->modificationTime;
        }
        if (r) {
            row.rightPath = r->path;
            row.rightSize = r->size;
            row.rightTime = r->modificationTime;
        }

        if (l && !r) {
            row.direction = 1; // Left -> Right
            row.selected = true;
        } else if (!l && r) {
            row.direction = -1; // Right -> Left
            row.selected = true;
        } else if (l && r) {
            double tDiff = (l->modificationTime && r->modificationTime)
                ? std::difftime(*l->modificationTime, *r->modificationTime)
                : 0.0;
            if (std::abs(tDiff) > 2.0) {
                row.direction = (tDiff > 0) ? 1 : -1;
                row.selected = true;
            } else if (l->size != r->size) {
                row.direction = 1;
                row.selected = true;
            } else {
                row.direction = 0;
                row.selected = false;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<MultiRenamePreviewRow>
ToolsService::previewMultiRename(const std::vector<FileItem>& items, const MultiRenameConfig& config) {
    std::vector<MultiRenamePreviewRow> rows;
    rows.reserve(items.size());
    std::unordered_map<std::string, int> proposedCounts;

    auto applyTokens = [](std::string mask,
                          const std::string& baseName,
                          const std::string& ext,
                          const std::string& counter,
                          const std::string& ymd,
                          const std::string& hms) {
        mask = replaceAllLiteral(std::move(mask), "[N]", baseName, true);
        mask = replaceAllLiteral(std::move(mask), "[E]", ext, true);
        mask = replaceAllLiteral(std::move(mask), "[C]", counter, true);
        mask = replaceAllLiteral(std::move(mask), "[YMD]", ymd, true);
        mask = replaceAllLiteral(std::move(mask), "[hms]", hms, true);
        return mask;
    };

    auto replaceText = [](const std::string& source,
                          const std::string& search,
                          const std::string& replacement,
                          bool useRegex,
                          bool caseSensitive) {
        if (search.empty()) return source;
        if (useRegex) {
            try {
                auto flags = std::regex::ECMAScript;
                if (!caseSensitive) flags |= std::regex::icase;
                std::regex re(search, flags);
                return std::regex_replace(source, re, replacement);
            } catch (...) {
                return source;
            }
        }
        return replaceAllLiteral(source, search, replacement, caseSensitive);
    };

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (item.isParentEntry) continue;
        fs::path p(item.path);
        std::string baseName = item.isNavigableDirectory() ? item.name : p.stem().string();
        std::string ext = item.isNavigableDirectory() ? "" : item.fileExtension;

        int counterVal = config.counterStart + static_cast<int>(i) * config.counterStep;
        char counterBuf[32];
        int digits = std::max(1, std::min(12, config.counterDigits));
        std::snprintf(counterBuf, sizeof(counterBuf), "%0*d", digits, counterVal);

        std::time_t modT = item.modificationTime.value_or(std::time(nullptr));
        std::tm tmLocal{};
        localtime_r(&modT, &tmLocal);
        char ymdBuf[16], hmsBuf[16];
        std::snprintf(ymdBuf, sizeof(ymdBuf), "%04d%02d%02d",
                      tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday);
        std::snprintf(hmsBuf, sizeof(hmsBuf), "%02d%02d%02d",
                      tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec);

        std::string newBase = applyTokens(
            config.nameMask.empty() ? "[N]" : config.nameMask,
            baseName, ext, counterBuf, ymdBuf, hmsBuf
        );
        std::string newExt = applyTokens(
            config.extensionMask,
            baseName, ext, counterBuf, ymdBuf, hmsBuf
        );

        if (!config.searchText.empty()) {
            newBase = replaceText(newBase, config.searchText, config.replaceText, config.useRegex, config.caseSensitive);
            newExt = replaceText(newExt, config.searchText, config.replaceText, config.useRegex, config.caseSensitive);
        }

        switch (config.caseMode) {
            case MultiRenameCaseMode::Unchanged:
                break;
            case MultiRenameCaseMode::Lowercase:
                newBase = toLowerASCII(std::move(newBase));
                newExt = toLowerASCII(std::move(newExt));
                break;
            case MultiRenameCaseMode::Uppercase:
                newBase = toUpperASCII(std::move(newBase));
                newExt = toUpperASCII(std::move(newExt));
                break;
            case MultiRenameCaseMode::Capitalized:
                newBase = capitalizeWords(std::move(newBase));
                newExt = toLowerASCII(std::move(newExt));
                break;
        }

        while (!newExt.empty() && newExt.front() == '.') newExt.erase(newExt.begin());
        while (!newExt.empty() && newExt.back() == '.') newExt.pop_back();

        std::string finalName = newExt.empty() ? newBase : (newBase + "." + newExt);
        proposedCounts[toLowerASCII(finalName)] += 1;

        MultiRenamePreviewRow row;
        row.originalPath = item.path;
        row.originalName = item.name;
        row.proposedName = finalName;
        row.hasConflict = finalName.empty() || finalName.find('/') != std::string::npos;
        row.isChanged = (finalName != item.name);
        rows.push_back(std::move(row));
    }

    for (auto& row : rows) {
        bool isDup = proposedCounts[toLowerASCII(row.proposedName)] > 1;
        bool exists = false;
        if (row.isChanged && !row.hasConflict) {
            std::error_code ec;
            fs::path dest = fs::path(row.originalPath).parent_path() / row.proposedName;
            exists = fs::exists(dest, ec);
        }
        row.hasConflict = row.hasConflict || isDup || exists;
    }

    return rows;
}

int ToolsService::applyMultiRename(const std::vector<MultiRenamePreviewRow>& rows, std::string& errorOut) {
    int count = 0;
    std::error_code ec;
    for (const auto& row : rows) {
        if (!row.isChanged || row.hasConflict) continue;
        fs::path src(row.originalPath);
        fs::path dst = src.parent_path() / row.proposedName;
        fs::rename(src, dst, ec);
        if (ec) {
            errorOut = ec.message();
            return count;
        }
        ++count;
    }
    return count;
}

void ToolsService::searchFilesSync(
    const std::string& rootPath,
    const std::string& namePattern,
    const std::string& contentQuery,
    bool includeHidden,
    const std::atomic<bool>& cancelFlag,
    const std::function<void(SearchResultItem)>& onMatch
) {
    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::path(rootPath), ec);
    if (ec) root = fs::path(rootPath);
    std::string rootStr = root.string();

    std::string trimmedContent = trimWhitespace(contentQuery);
    int matchesFound = 0;
    constexpr int maxMatches = 1500;

    fs::recursive_directory_iterator it(
        root,
        fs::directory_options::skip_permission_denied,
        ec
    );
    if (ec) return;

    const fs::recursive_directory_iterator endIt;
    while (it != endIt) {
        if (cancelFlag.load(std::memory_order_relaxed) || matchesFound >= maxMatches) {
            break;
        }
        const auto& entry = *it;
        std::string name = entry.path().filename().string();
        if (!includeHidden && !name.empty() && name.front() == '.') {
            std::error_code dirEc;
            if (entry.is_directory(dirEc)) {
                it.disable_recursion_pending();
            }
            it.increment(ec);
            if (ec) ec.clear();
            continue;
        }

        if (!matchesWildcard(name, namePattern)) {
            it.increment(ec);
            if (ec) ec.clear();
            continue;
        }

        std::error_code statEc;
        bool isDir = entry.is_directory(statEc);
        std::optional<int64_t> size;
        if (!isDir) {
            uintmax_t sz = entry.file_size(statEc);
            if (!statEc) size = static_cast<int64_t>(sz);
        }

        if (!trimmedContent.empty()) {
            if (isDir || (size.has_value() && *size > 5'000'000) ||
                !fileContainsTextStream(entry.path(), trimmedContent)) {
                it.increment(ec);
                if (ec) ec.clear();
                continue;
            }
        }

        std::string parentStr = entry.path().parent_path().string();
        std::string relative;
        if (parentStr == rootStr) {
            relative = ".";
        } else if (parentStr.size() > rootStr.size() &&
                   parentStr.compare(0, rootStr.size(), rootStr) == 0 &&
                   parentStr[rootStr.size()] == '/') {
            relative = parentStr.substr(rootStr.size() + 1);
        } else {
            relative = parentStr;
        }

        SearchResultItem item;
        item.path = entry.path().string();
        item.name = std::move(name);
        item.relativeFolder = std::move(relative);
        item.isDirectory = isDir;
        item.size = size;

        auto ftime = entry.last_write_time(statEc);
        if (!statEc) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            item.modificationTime = std::chrono::system_clock::to_time_t(sctp);
        }

        onMatch(std::move(item));
        ++matchesFound;

        it.increment(ec);
        if (ec) ec.clear();
    }
}

CommandExecutionResult ToolsService::runShellCommandSync(
    const std::string& command,
    const std::string& workingDirectory
) {
    CommandExecutionResult res;
    res.command = command;
    res.workingDirectory = workingDirectory;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        res.output = "Błąd tworzenia potoku (pipe).";
        res.exitCode = -1;
        return res;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        res.output = "Błąd fork().";
        res.exitCode = -1;
        return res;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (!workingDirectory.empty()) {
            chdir(workingDirectory.c_str());
        }
        execl("/bin/zsh", "zsh", "-lc", command.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    std::string out;
    char buf[4096];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        res.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res.exitCode = 128 + WTERMSIG(status);
    } else {
        res.exitCode = -1;
    }
    res.output = out.empty() ? "(polecenie zakończone bez wyjścia)" : std::move(out);
    return res;
}

std::string ToolsService::formatHexDump(const std::vector<uint8_t>& data, size_t maxBytes) {
    size_t limit = std::min(data.size(), maxBytes);
    std::string result;
    result.reserve(limit * 5);

    constexpr size_t bytesPerRow = 16;
    char lineBuf[128];

    for (size_t offset = 0; offset < limit; offset += bytesPerRow) {
        size_t end = std::min(offset + bytesPerRow, limit);
        int pos = std::snprintf(lineBuf, sizeof(lineBuf), "%08zX  ", offset);

        for (size_t i = 0; i < bytesPerRow; ++i) {
            if (offset + i < end) {
                pos += std::snprintf(lineBuf + pos, sizeof(lineBuf) - static_cast<size_t>(pos),
                                     "%02X%s", data[offset + i], (i == 7 ? "  " : " "));
            } else {
                pos += std::snprintf(lineBuf + pos, sizeof(lineBuf) - static_cast<size_t>(pos),
                                     "  %s", (i == 7 ? "  " : " "));
            }
        }

        pos += std::snprintf(lineBuf + pos, sizeof(lineBuf) - static_cast<size_t>(pos), " |");
        for (size_t i = 0; i < end - offset; ++i) {
            uint8_t b = data[offset + i];
            lineBuf[pos++] = (b >= 32 && b <= 126) ? static_cast<char>(b) : '.';
        }
        lineBuf[pos++] = '|';
        lineBuf[pos++] = '\n';
        result.append(lineBuf, static_cast<size_t>(pos));
    }

    if (data.size() > maxBytes) {
        result.append("\n--- Podgląd HEX ograniczony do pierwszych ");
        result.append(std::to_string(maxBytes));
        result.append(" bajtów ---");
    }
    return result;
}

std::string ToolsService::find7zBinary() {
    static const char* candidates[] = {
        "/opt/homebrew/bin/7zz",
        "/opt/homebrew/bin/7z",
        "/usr/local/bin/7zz",
        "/usr/local/bin/7z",
        "/opt/homebrew/bin/7za"
    };
    std::error_code ec;
    for (const char* c : candidates) {
        if (fs::exists(c, ec)) return c;
    }
    return "";
}

bool ToolsService::packArchiveSync(
    const std::vector<std::string>& sourcePaths,
    const std::string& archiveOutputPath,
    const PackArchiveConfig& config,
    std::string& errorOut
) {
    if (sourcePaths.empty()) {
        errorOut = "Brak plików do spakowania.";
        return false;
    }
    if (!config.password.empty() && config.password != config.passwordConfirm) {
        errorOut = "Hasła nie są identyczne.";
        return false;
    }

    fs::path firstParent = fs::path(sourcePaths.front()).parent_path();
    std::string bin7z = find7zBinary();
    std::string cmd;

    if (config.format == ArchiveFormat::SevenZip) {
        if (bin7z.empty()) {
            errorOut = "Nie znaleziono programu 7zz (/opt/homebrew/bin/7zz).";
            return false;
        }
        int level = std::clamp(config.compressionLevel, 0, 9);
        cmd = shellQuote(bin7z) + " a -t7z -mx=" + std::to_string(level) + " -y";
        if (!config.password.empty()) {
            cmd += " -p" + shellQuote(config.password);
            if (config.encryptHeaders) {
                cmd += " -mhe=on";
            }
        }
        cmd += " " + shellQuote(archiveOutputPath);
    } else if (config.format == ArchiveFormat::Zip) {
        if (!bin7z.empty()) {
            int level = std::clamp(config.compressionLevel, 0, 9);
            cmd = shellQuote(bin7z) + " a -tzip -mx=" + std::to_string(level) + " -y";
            if (!config.password.empty()) {
                cmd += " -mem=AES256 -p" + shellQuote(config.password);
            }
            cmd += " " + shellQuote(archiveOutputPath);
        } else {
            if (!config.password.empty()) {
                errorOut = "Szyfrowanie ZIP AES-256 wymaga pakietu 7zz.";
                return false;
            }
            cmd = "/usr/bin/zip -rq " + shellQuote(archiveOutputPath);
        }
    } else {
        if (!config.password.empty()) {
            errorOut = "Format .tar.gz nie obsługuje szyfrowania hasłem. Wybierz .7z lub .zip.";
            return false;
        }
        cmd = "/usr/bin/tar -czf " + shellQuote(archiveOutputPath);
    }

    for (const auto& p : sourcePaths) {
        fs::path fp(p);
        cmd += " " + shellQuote(fp.filename().string());
    }

    auto res = runShellCommandSync(cmd, firstParent.string());
    if (res.exitCode != 0) {
        errorOut = res.output;
        return false;
    }
    return true;
}

bool ToolsService::unpackArchiveSync(
    const std::string& archivePath,
    const std::string& destinationDirectory,
    const std::string& password,
    std::string& errorOut
) {
    std::string lower = toLowerASCII(archivePath);
    std::string bin7z = find7zBinary();
    std::string cmd;

    bool isTarball = lower.ends_with(".tar.gz") || lower.ends_with(".tgz") ||
                     lower.ends_with(".tar.bz2") || lower.ends_with(".tbz2") ||
                     lower.ends_with(".tar.xz") || lower.ends_with(".txz") ||
                     lower.ends_with(".tar");

    if (isTarball) {
        cmd = "/usr/bin/tar -xf " + shellQuote(archivePath) + " -C " + shellQuote(destinationDirectory);
    } else if (!bin7z.empty()) {
        // Use 7zz for .7z, .zip, .rar, .gz, .xz, .bz2 (supports AES-256 passwords)
        cmd = shellQuote(bin7z) + " x -y -p" + shellQuote(password) +
              " -o" + shellQuote(destinationDirectory) + " " + shellQuote(archivePath);
    } else if (lower.ends_with(".zip")) {
        cmd = "/usr/bin/unzip -o " + shellQuote(archivePath) + " -d " + shellQuote(destinationDirectory);
    } else {
        errorOut = "Nie znaleziono narzędzia 7zz do rozpakowania archiwum.";
        return false;
    }

    auto res = runShellCommandSync(cmd, destinationDirectory);
    if (res.exitCode != 0) {
        if (res.output.find("Wrong password") != std::string::npos ||
            res.output.find("Can not open encrypted archive") != std::string::npos) {
            errorOut = "Archiwum jest zaszyfrowane lub podano błędne hasło.";
        } else {
            errorOut = res.output;
        }
        return false;
    }
    return true;
}

std::string ToolsService::listArchiveContentsSync(
    const std::string& archivePath,
    const std::string& password
) {
    std::string bin7z = find7zBinary();
    if (!bin7z.empty()) {
        std::string cmd = shellQuote(bin7z) + " l -p" + shellQuote(password) + " " + shellQuote(archivePath);
        auto res = runShellCommandSync(cmd, fs::path(archivePath).parent_path().string());
        return res.output;
    }
    return "(Zainstaluj 7zz aby podglądać zawartość archiwum)";
}

std::vector<FileItem> ToolsService::listArchiveEntriesSync(
    const std::string& archivePath,
    const std::string& archiveSubdir,
    const std::string& password,
    std::string& errorOut
) {
    std::vector<FileItem> items;
    std::string bin7z = find7zBinary();
    if (bin7z.empty()) {
        errorOut = "Brak 7zz (/opt/homebrew/bin/7zz) do przeglądania archiwum.";
        return items;
    }

    std::string cmd = shellQuote(bin7z) + " l -slt -p" + shellQuote(password) + " " + shellQuote(archivePath);
    auto res = runShellCommandSync(cmd, fs::path(archivePath).parent_path().string());
    if (res.exitCode != 0) {
        errorOut = res.output;
        return items;
    }

    std::string prefix = archiveSubdir;
    while (!prefix.empty() && prefix.front() == '/') prefix.erase(prefix.begin());
    while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
    if (!prefix.empty()) prefix.push_back('/');

    std::unordered_set<std::string> seenDirs;
    std::istringstream iss(res.output);
    std::string line;
    bool inEntries = false;

    std::string curPath;
    int64_t curSize = 0;
    bool curIsDir = false;
    std::string curDate;

    auto flushEntry = [&]() {
        if (curPath.empty()) return;
        std::string norm = curPath;
        while (!norm.empty() && norm.back() == '/') norm.pop_back();
        if (norm.empty()) { curPath.clear(); return; }

        if (!prefix.empty()) {
            if (norm.rfind(prefix, 0) != 0) { curPath.clear(); return; }
            norm = norm.substr(prefix.size());
        }
        if (norm.empty()) { curPath.clear(); return; }

        size_t slash = norm.find('/');
        if (slash != std::string::npos) {
            std::string topDir = norm.substr(0, slash);
            if (seenDirs.insert(topDir).second) {
                FileItem d;
                d.path = prefix + topDir;
                d.name = topDir;
                d.displayStem = topDir;
                d.isDirectory = true;
                d.posixPermissions = "drwxr-xr-x";
                d.typeDescription = "Folder (archiwum)";
                items.push_back(std::move(d));
            }
        } else {
            if (curIsDir) {
                if (seenDirs.insert(norm).second) {
                    FileItem d;
                    d.path = prefix + norm;
                    d.name = norm;
                    d.displayStem = norm;
                    d.isDirectory = true;
                    d.posixPermissions = "drwxr-xr-x";
                    d.typeDescription = "Folder (archiwum)";
                    items.push_back(std::move(d));
                }
            } else {
                FileItem f;
                f.path = prefix + norm;
                f.name = norm;
                fs::path fp(norm);
                std::string ext = fp.extension().string();
                if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
                f.fileExtension = ext;
                f.displayStem = (!ext.empty() && norm != "." + ext)
                    ? norm.substr(0, norm.size() - ext.size() - 1)
                    : norm;
                f.isDirectory = false;
                f.size = curSize;
                f.posixPermissions = "-rw-r--r--";
                f.typeDescription = "Plik w archiwum";
                items.push_back(std::move(f));
            }
        }

        curPath.clear();
        curSize = 0;
        curIsDir = false;
        curDate.clear();
    };

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "----------") {
            inEntries = true;
            flushEntry();
            continue;
        }
        if (!inEntries) continue;
        if (line.empty()) {
            flushEntry();
            continue;
        }
        if (line.rfind("Path = ", 0) == 0) curPath = line.substr(7);
        else if (line.rfind("Size = ", 0) == 0) {
            try { curSize = std::stoll(line.substr(7)); } catch (...) { curSize = 0; }
        }
        else if (line.rfind("Folder = +", 0) == 0) curIsDir = true;
        else if (line.rfind("Attributes = D", 0) == 0) curIsDir = true;
        else if (line.rfind("Modified = ", 0) == 0) curDate = line.substr(11);
    }
    flushEntry();
    return items;
}

bool ToolsService::extractArchiveEntriesSync(
    const std::string& archivePath,
    const std::vector<std::string>& internalPaths,
    const std::string& destinationDirectory,
    const std::string& password,
    std::string& errorOut
) {
    std::string bin7z = find7zBinary();
    if (bin7z.empty()) {
        errorOut = "Brak 7zz (/opt/homebrew/bin/7zz).";
        return false;
    }
    std::string cmd = shellQuote(bin7z) + " x -y -p" + shellQuote(password) +
                      " -o" + shellQuote(destinationDirectory) + " " + shellQuote(archivePath);
    for (const auto& ip : internalPaths) {
        cmd += " " + shellQuote(ip);
    }
    auto res = runShellCommandSync(cmd, destinationDirectory);
    if (res.exitCode != 0) {
        errorOut = res.output;
        return false;
    }
    return true;
}

std::vector<UserMenuItem> ToolsService::loadUserMenu() {
    std::string cfgDir = platform::homeDirectory() + "/.config/mcx";
    std::string menuFile = cfgDir + "/menu.json";
    std::error_code ec;

    if (!fs::exists(menuFile, ec)) {
        fs::create_directories(cfgDir, ec);
        std::ofstream out(menuFile);
        if (out) {
            out << R"({
  "1": "Spakuj zaznaczone do .7z (AES-256)|__MCX_PACK__",
  "2": "Rozpakuj archiwum do 2. panelu|__MCX_UNPACK__",
  "3": "Porównaj pliki (diff -u)|__MCX_DIFF__",
  "4": "Utwórz Symlink w 2. panelu|__MCX_SYMLINK__",
  "5": "Utwórz Hardlink w 2. panelu|__MCX_HARDLINK__",
  "6": "Zmień uprawnienia POSIX (chmod)|__MCX_CHMOD__",
  "7": "Oblicz sumy SHA-256 / MD5|__MCX_CHECKSUMS__",
  "8": "git status|git status",
  "9": "git log (ostatnie 10)|git log -n 10 --oneline",
  "0": "Lista plików (ls -la)|ls -la"
}
)";
        }
    }

    std::unordered_map<std::string, std::string> raw;
    std::vector<UserMenuItem> items;
    if (fs::exists(menuFile, ec)) {
        // Parse flat JSON via simple line-based or reuse Localization parser
        std::ifstream in(menuFile);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        // Quick flat key:value extraction
        size_t pos = 0;
        while ((pos = content.find('"', pos)) != std::string::npos) {
            size_t kEnd = content.find('"', pos + 1);
            if (kEnd == std::string::npos) break;
            std::string key = content.substr(pos + 1, kEnd - pos - 1);
            size_t colon = content.find(':', kEnd + 1);
            if (colon == std::string::npos) break;
            size_t vStart = content.find('"', colon + 1);
            if (vStart == std::string::npos) break;
            size_t vEnd = content.find('"', vStart + 1);
            if (vEnd == std::string::npos) break;
            std::string val = content.substr(vStart + 1, vEnd - vStart - 1);
            pos = vEnd + 1;

            size_t sep = val.find('|');
            if (sep != std::string::npos) {
                UserMenuItem u;
                u.key = key;
                u.label = val.substr(0, sep);
                u.command = val.substr(sep + 1);
                items.push_back(std::move(u));
            }
        }
    }
    return items;
}

std::string ToolsService::diffFilesSync(
    const std::string& leftFilePath,
    const std::string& rightFilePath
) {
    std::string cmd = "/usr/bin/diff -u " + shellQuote(leftFilePath) + " " + shellQuote(rightFilePath);
    auto res = runShellCommandSync(cmd, "/");
    if (res.exitCode == 0) {
        return "=== Pliki są identyczne (brak różnic) ===\nLewy : " + leftFilePath + "\nPrawy: " + rightFilePath;
    }
    return res.output;
}

std::vector<ChecksumEntry> ToolsService::computeChecksumsSync(
    const std::vector<std::string>& filePaths
) {
    std::vector<ChecksumEntry> results;
    for (const auto& p : filePaths) {
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;

        CC_SHA256_CTX shaCtx;
        CC_SHA256_Init(&shaCtx);
        CC_MD5_CTX md5Ctx;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CC_MD5_Init(&md5Ctx);
#pragma clang diagnostic pop

        int64_t totalBytes = 0;
        char buf[65536];
        while (in) {
            in.read(buf, sizeof(buf));
            std::streamsize n = in.gcount();
            if (n <= 0) break;
            totalBytes += n;
            CC_SHA256_Update(&shaCtx, buf, static_cast<CC_LONG>(n));
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            CC_MD5_Update(&md5Ctx, buf, static_cast<CC_LONG>(n));
#pragma clang diagnostic pop
        }

        unsigned char shaDigest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256_Final(shaDigest, &shaCtx);

        unsigned char md5Digest[CC_MD5_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CC_MD5_Final(md5Digest, &md5Ctx);
#pragma clang diagnostic pop

        ChecksumEntry entry;
        entry.fileName = fs::path(p).filename().string();
        entry.sha256 = bytesToHexLower(shaDigest, CC_SHA256_DIGEST_LENGTH);
        entry.md5 = bytesToHexLower(md5Digest, CC_MD5_DIGEST_LENGTH);
        entry.size = totalBytes;
        results.push_back(std::move(entry));
    }
    return results;
}

bool ToolsService::changePermissionsSync(
    const std::vector<std::string>& paths,
    uint16_t mode,
    bool recursive,
    std::string& errorOut
) {
    std::error_code ec;
    for (const auto& p : paths) {
        if (chmod(p.c_str(), static_cast<mode_t>(mode)) != 0) {
            errorOut = std::strerror(errno);
            return false;
        }
        if (recursive && fs::is_directory(p, ec)) {
            fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator endIt;
            while (!ec && it != endIt) {
                chmod(it->path().c_str(), static_cast<mode_t>(mode));
                it.increment(ec);
                if (ec) ec.clear();
            }
        }
    }
    return true;
}

namespace {

std::string sftpQuote(const std::string& p) {
    std::string out = "\"";
    for (char c : p) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string joinRemotePath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == "/") return "/" + name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

bool runSftpBatchSync(
    const RemoteConnection& conn,
    const std::string& batchContent,
    std::string& outputOut,
    std::string& errorOut
) {
    char tmpl[] = "/tmp/mcx-sftp-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        errorOut = "Nie udało się utworzyć pliku tymczasowego sftp.";
        return false;
    }
    std::string batchPath(tmpl);
    {
        FILE* f = fdopen(fd, "w");
        if (!f) {
            close(fd);
            std::error_code ec;
            fs::remove(batchPath, ec);
            errorOut = "Nie udało się zapisać skryptu sftp.";
            return false;
        }
        fwrite(batchContent.data(), 1, batchContent.size(), f);
        fclose(f);
    }

    std::string cmd = "/usr/bin/sftp -b " + shellQuote(batchPath) +
                      " -P " + std::to_string(conn.port) +
                      " -o BatchMode=yes" +
                      " -o ConnectTimeout=8" +
                      " -o StrictHostKeyChecking=accept-new" +
                      " -o ControlMaster=auto" +
                      " -o ControlPath=/tmp/mcx-ssh-%C" +
                      " -o ControlPersist=120" +
                      " " + shellQuote(conn.targetSpec());

    auto res = ToolsService::runShellCommandSync(cmd, "/tmp");
    std::error_code ec;
    fs::remove(batchPath, ec);

    outputOut = res.output;
    if (res.exitCode != 0) {
        errorOut = res.output.empty()
            ? ("Błąd połączenia SFTP z " + conn.targetSpec() + " (kod " + std::to_string(res.exitCode) + ")")
            : res.output;
        return false;
    }
    return true;
}

std::optional<std::time_t> parseLsDate(const std::string& mon, const std::string& dayStr, const std::string& timeOrYear) {
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int mIdx = -1;
    for (int i = 0; i < 12; ++i) {
        if (mon == months[i]) { mIdx = i; break; }
    }
    if (mIdx < 0) return std::nullopt;

    std::time_t now = std::time(nullptr);
    std::tm tmNow{};
    localtime_r(&now, &tmNow);

    std::tm tmVal{};
    tmVal.tm_mon = mIdx;
    tmVal.tm_mday = std::atoi(dayStr.c_str());
    tmVal.tm_isdst = -1;

    auto colon = timeOrYear.find(':');
    if (colon != std::string::npos) {
        tmVal.tm_year = tmNow.tm_year;
        tmVal.tm_hour = std::atoi(timeOrYear.substr(0, colon).c_str());
        tmVal.tm_min = std::atoi(timeOrYear.substr(colon + 1).c_str());
    } else {
        int yr = std::atoi(timeOrYear.c_str());
        tmVal.tm_year = (yr > 1900) ? (yr - 1900) : tmNow.tm_year;
    }
    return std::mktime(&tmVal);
}

} // namespace

std::vector<FileItem> ToolsService::listRemoteDirectorySync(
    const RemoteConnection& conn,
    bool includeHidden,
    std::string& errorOut
) {
    std::vector<FileItem> items;
    std::string remoteDir = conn.remotePath.empty() ? "/" : conn.remotePath;
    std::string batch = "ls -la " + sftpQuote(remoteDir) + "\n";

    std::string out;
    if (!runSftpBatchSync(conn, batch, out, errorOut)) {
        return items;
    }

    FileItem up;
    up.path = "__MC_PARENT_ENTRY__";
    up.name = "..";
    up.displayStem = "..";
    up.posixPermissions = "drwxr-xr-x";
    up.typeDescription = (remoteDir == "/") ? "Rozłącz SFTP" : "Katalog nadrzędny";
    up.isParentEntry = true;
    up.isDirectory = true;
    items.push_back(std::move(up));

    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("sftp>", 0) == 0 || line.rfind("total ", 0) == 0) continue;

        char first = line[0];
        if (first != '-' && first != 'd' && first != 'l' && first != 'b' && first != 'c' && first != 'p' && first != 's') {
            continue;
        }
        if (line.size() < 10) continue;

        std::vector<std::string> tokens;
        size_t i = 0;
        while (i < line.size() && tokens.size() < 8) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            if (i >= line.size()) break;
            size_t start = i;
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            tokens.push_back(line.substr(start, i - start));
        }
        if (tokens.size() < 8) continue;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size()) continue;

        std::string name = line.substr(i);
        bool isSym = (first == 'l');
        if (isSym) {
            auto arrow = name.find(" -> ");
            if (arrow != std::string::npos) name = name.substr(0, arrow);
        }
        if (name == "." || name == ".." || name.empty()) continue;
        if (!includeHidden && name.front() == '.') continue;

        bool isDir = (first == 'd');
        int64_t sz = 0;
        try { sz = std::stoll(tokens[4]); } catch (...) { sz = 0; }

        FileItem item;
        item.name = name;
        item.path = joinRemotePath(remoteDir, name);
        item.isDirectory = isDir;
        item.posixPermissions = tokens[0];
        item.modificationTime = parseLsDate(tokens[5], tokens[6], tokens[7]);

        if (isDir) {
            item.displayStem = name;
            item.typeDescription = "Folder (SFTP)";
        } else {
            item.size = sz;
            auto dot = name.rfind('.');
            if (dot != std::string::npos && dot > 0 && dot + 1 < name.size()) {
                item.displayStem = name.substr(0, dot);
                item.fileExtension = name.substr(dot + 1);
            } else {
                item.displayStem = name;
            }
            item.typeDescription = item.fileExtension.empty() ? "Plik (SFTP)" : (toLowerASCII(item.fileExtension) + " (SFTP)");
        }
        items.push_back(std::move(item));
    }
    return items;
}

bool ToolsService::downloadFromRemoteSync(
    const RemoteConnection& conn,
    const std::vector<std::string>& remotePaths,
    const std::vector<bool>& isDirectoryFlags,
    const std::string& localDestDir,
    std::string& errorOut
) {
    if (remotePaths.empty()) return true;
    std::string batch = "lcd " + sftpQuote(localDestDir) + "\n";
    for (size_t i = 0; i < remotePaths.size(); ++i) {
        bool isDir = (i < isDirectoryFlags.size()) ? isDirectoryFlags[i] : false;
        batch += std::string(isDir ? "get -r " : "get ") + sftpQuote(remotePaths[i]) + "\n";
    }
    std::string out;
    return runSftpBatchSync(conn, batch, out, errorOut);
}

bool ToolsService::uploadToRemoteSync(
    const RemoteConnection& conn,
    const std::vector<std::string>& localPaths,
    const std::string& remoteDestDir,
    std::string& errorOut
) {
    if (localPaths.empty()) return true;
    std::string dest = remoteDestDir.empty() ? "/" : remoteDestDir;
    std::string batch = "cd " + sftpQuote(dest) + "\n";
    std::error_code ec;
    for (const auto& lp : localPaths) {
        bool isDir = fs::is_directory(lp, ec);
        batch += std::string(isDir ? "put -r " : "put ") + sftpQuote(lp) + "\n";
    }
    std::string out;
    return runSftpBatchSync(conn, batch, out, errorOut);
}

bool ToolsService::createRemoteDirectorySync(
    const RemoteConnection& conn,
    const std::string& dirName,
    std::string& errorOut
) {
    std::string fullPath = joinRemotePath(conn.remotePath, dirName);
    std::string batch = "mkdir " + sftpQuote(fullPath) + "\n";
    std::string out;
    return runSftpBatchSync(conn, batch, out, errorOut);
}

bool ToolsService::renameRemoteItemSync(
    const RemoteConnection& conn,
    const std::string& oldRemotePath,
    const std::string& newName,
    std::string& errorOut
) {
    std::string parent = fs::path(oldRemotePath).parent_path().string();
    std::string newPath = joinRemotePath(parent.empty() ? "/" : parent, newName);
    std::string batch = "rename " + sftpQuote(oldRemotePath) + " " + sftpQuote(newPath) + "\n";
    std::string out;
    return runSftpBatchSync(conn, batch, out, errorOut);
}

bool ToolsService::deleteRemoteItemsSync(
    const RemoteConnection& conn,
    const std::vector<std::string>& remotePaths,
    const std::vector<bool>& isDirectoryFlags,
    std::string& errorOut
) {
    if (remotePaths.empty()) return true;
    std::string batch;
    for (size_t i = 0; i < remotePaths.size(); ++i) {
        bool isDir = (i < isDirectoryFlags.size()) ? isDirectoryFlags[i] : false;
        batch += std::string(isDir ? "rmdir " : "rm ") + sftpQuote(remotePaths[i]) + "\n";
    }
    std::string out;
    return runSftpBatchSync(conn, batch, out, errorOut);
}

} // namespace mc
