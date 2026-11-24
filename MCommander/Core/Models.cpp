#include "Models.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_set>

namespace mc {

std::string toLowerASCII(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string formatExactBytes(int64_t bytes) {
    if (bytes < 0) return "0";
    std::string raw = std::to_string(bytes);
    std::string out;
    int count = 0;
    for (auto it = raw.rbegin(); it != raw.rend(); ++it) {
        if (count > 0 && count % 3 == 0) {
            out.push_back(' ');
        }
        out.push_back(*it);
        ++count;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string formatByteCount(int64_t bytes) {
    if (bytes < 0) return "—";
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double val = static_cast<double>(bytes);
    int unit = 0;
    while (val >= 1024.0 && unit < 5) {
        val /= 1024.0;
        ++unit;
    }
    char buf[64];
    if (val >= 100.0 || unit == 0) {
        std::snprintf(buf, sizeof(buf), "%.0f %s", val, units[unit]);
    } else if (val >= 10.0) {
        std::snprintf(buf, sizeof(buf), "%.1f %s", val, units[unit]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f %s", val, units[unit]);
    }
    return buf;
}

std::string formatTimestamp(std::time_t t) {
    if (t <= 0) return "—";
    std::tm tmLocal{};
    localtime_r(&t, &tmLocal);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  tmLocal.tm_year + 1900,
                  tmLocal.tm_mon + 1,
                  tmLocal.tm_mday,
                  tmLocal.tm_hour,
                  tmLocal.tm_min);
    return buf;
}

bool FileItem::isArchive() const {
    if (isNavigableDirectory()) return false;
    static const std::unordered_set<std::string> archExts = {
        "zip", "tar", "gz", "tgz", "bz2", "tbz2", "xz", "txz", "7z", "rar"
    };
    return archExts.contains(toLowerASCII(fileExtension));
}

bool FileItem::isTextEditable() const {
    if (isNavigableDirectory()) return false;
    static const std::unordered_set<std::string> editableExts = {
        "txt", "md", "markdown", "swift", "py", "js", "ts", "tsx", "jsx",
        "json", "yaml", "yml", "toml", "xml", "html", "htm", "css", "scss",
        "sh", "zsh", "bash", "fish", "rb", "go", "rs", "c", "h", "cpp", "hpp",
        "cc", "hh", "cxx", "m", "mm", "java", "kt", "sql", "csv", "log",
        "ini", "conf", "env", "plist", "xcconfig", "xcproj", "gitignore",
        "dockerfile", "makefile", "cmake", ""
    };
    const std::string lower = toLowerASCII(fileExtension);
    if (!editableExts.contains(lower)) return false;
    if (lower.empty() && size.has_value() && *size > 2'000'000) {
        return false;
    }
    return true;
}

std::string FileItem::formattedSize() const {
    if (isParentEntry) return "<UP>";
    if (isNavigableDirectory()) return "<DIR>";
    if (!size.has_value()) return "—";
    return formatExactBytes(*size);
}

std::string FileItem::formattedDate() const {
    if (isParentEntry || !modificationTime.has_value()) return "—";
    return formatTimestamp(*modificationTime);
}

std::string MountedVolume::shortFreeSpace() const {
    if (!availableCapacity.has_value()) return "";
    return formatByteCount(*availableCapacity);
}

std::string MountedVolume::capacitySummary() const {
    if (!availableCapacity.has_value()) return "";
    std::string freeText = formatByteCount(*availableCapacity);
    if (totalCapacity.has_value() && *totalCapacity > 0) {
        return freeText + " / " + formatByteCount(*totalCapacity);
    }
    return freeText + " wolne";
}

std::optional<double> MountedVolume::usedFraction() const {
    if (!totalCapacity.has_value() || *totalCapacity <= 0 || !availableCapacity.has_value()) {
        return std::nullopt;
    }
    int64_t used = std::max<int64_t>(0, *totalCapacity - *availableCapacity);
    double frac = static_cast<double>(used) / static_cast<double>(*totalCapacity);
    return std::clamp(frac, 0.0, 1.0);
}

} // namespace mc
