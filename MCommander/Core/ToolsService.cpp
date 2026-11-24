#include "ToolsService.hpp"

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

} // namespace mc
