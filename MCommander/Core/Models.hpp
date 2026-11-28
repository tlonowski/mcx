#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace mc {

enum class PaneSide {
    Left,
    Right
};

inline PaneSide oppositeSide(PaneSide side) {
    return side == PaneSide::Left ? PaneSide::Right : PaneSide::Left;
}

enum class FileSortField {
    Name,
    Extension,
    Size,
    ModificationDate,
    Unsorted
};

enum class PanelViewMode {
    Full,       // Nazwa | Rozsz. | Rozmiar | Data | Atryb.
    Brief,      // Kompaktowa lista wielokolumnowa / skrócona
    QuickView   // Podgląd Ctrl+Q w drugim panelu
};

struct FileItem {
    std::string path;
    std::string name;
    std::string displayStem;
    std::string fileExtension;
    std::string posixPermissions;
    std::string ownerGroup;
    std::string symlinkTarget;
    uint64_t inode = 0;
    uint16_t posixMode = 0;
    std::string typeDescription;
    bool isParentEntry = false; // [..]
    bool isDirectory = false;
    bool isPackage = false;
    bool isHidden = false;
    bool isSymbolicLink = false;
    bool isExecutable = false;
    std::optional<int64_t> size;
    std::optional<std::time_t> modificationTime;
    std::optional<std::time_t> creationTime;

    bool isNavigableDirectory() const {
        return isParentEntry || (isDirectory && !isPackage);
    }

    bool isArchive() const;
    bool isTextEditable() const;
    std::string formattedSize() const;
    std::string formattedDate() const;
};

struct FavoriteLocation {
    std::string id;
    std::string title;
    std::string path;
    bool isSystemDefault = false;
};

struct MountedVolume {
    std::string path;
    std::string name;
    std::optional<int64_t> availableCapacity;
    std::optional<int64_t> totalCapacity;
    bool isRemovable = false;
    bool isInternal = true;

    std::string shortFreeSpace() const;
    std::string capacitySummary() const;
    std::optional<double> usedFraction() const;
};

enum class ConflictResolutionPolicy {
    Ask,
    Overwrite,
    Skip,
    KeepBoth
};

enum class ArchiveFormat {
    SevenZip, // .7z (LZMA2 + AES-256 + -mhe=on)
    Zip,      // .zip (Deflate / AES-256)
    TarGz     // .tar.gz
};

struct PackArchiveConfig {
    ArchiveFormat format = ArchiveFormat::SevenZip;
    int compressionLevel = 5; // 0..9
    std::string password;
    std::string passwordConfirm;
    bool encryptHeaders = true; // -mhe=on (hide filenames in .7z)
    bool showPassword = false;
};

struct FileOperationProgress {
    std::string title;
    std::string currentItemName;
    int completedCount = 0;
    int totalCount = 0;
    int64_t currentBytes = 0;
    int64_t totalBytes = 0;
    bool inBackground = false;

    float fraction() const {
        if (totalBytes > 0) {
            return std::min(1.0f, static_cast<float>(currentBytes) / static_cast<float>(totalBytes));
        }
        if (totalCount <= 0) return 0.0f;
        return static_cast<float>(completedCount) / static_cast<float>(totalCount);
    }
};

struct UserMenuItem {
    std::string key;
    std::string label;
    std::string command;
    bool runInSubshell = false;
};

struct RemoteConnection {
    std::string scheme = "sftp"; // "sftp" or "sh"
    std::string user;
    std::string host;
    int port = 22;
    std::string remotePath = "/";

    bool isValid() const { return !host.empty(); }
    std::string targetSpec() const { return user.empty() ? host : (user + "@" + host); }
    std::string uri() const {
        std::string out = scheme + "://" + targetSpec();
        if (port != 22 && port > 0) out += ":" + std::to_string(port);
        if (remotePath.empty() || remotePath.front() != '/') out += "/";
        out += remotePath;
        return out;
    }
};

struct QueuedTransferJob {
    uint64_t id = 0;
    std::vector<std::string> sourcePaths;
    std::string destinationPath;
    bool isMove = false;
    ConflictResolutionPolicy conflictPolicy = ConflictResolutionPolicy::Ask;
    std::string label;
};

bool parseRemoteUri(const std::string& input, RemoteConnection& out);

struct SearchResultItem {
    std::string path;
    std::string name;
    std::string relativeFolder;
    bool isDirectory = false;
    std::optional<int64_t> size;
    std::optional<std::time_t> modificationTime;
};

enum class MultiRenameCaseMode {
    Unchanged,
    Lowercase,
    Uppercase,
    Capitalized
};

struct MultiRenameConfig {
    std::string nameMask = "[N]";
    std::string extensionMask = "[E]";
    std::string searchText;
    std::string replaceText;
    bool useRegex = false;
    bool caseSensitive = false;
    int counterStart = 1;
    int counterStep = 1;
    int counterDigits = 2;
    MultiRenameCaseMode caseMode = MultiRenameCaseMode::Unchanged;
};

struct MultiRenamePreviewRow {
    std::string originalPath;
    std::string originalName;
    std::string proposedName;
    bool hasConflict = false;
    bool isChanged = false;
};

struct CommandExecutionResult {
    std::string command;
    std::string workingDirectory;
    std::string output;
    int exitCode = 0;
};

struct SyncComparisonRow {
    std::string relativeName;
    std::string leftPath;
    std::string rightPath;
    std::optional<int64_t> leftSize;
    std::optional<int64_t> rightSize;
    std::optional<std::time_t> leftTime;
    std::optional<std::time_t> rightTime;
    // -1: right newer / only right, 0: equal, +1: left newer / only left, 2: different
    int direction = 0;
    bool selected = true;
};

struct ChecksumEntry {
    std::string fileName;
    std::string sha256;
    std::string md5;
    int64_t size = 0;
};

struct PaneSavedSettings {
    std::vector<std::string> tabPaths;
    int activeTabIndex = 0;
    FileSortField sortField = FileSortField::Name;
    bool sortAscending = true;
    bool briefMode = false;
};

struct AppSettings {
    PaneSavedSettings leftPane;
    PaneSavedSettings rightPane;
    PaneSide activeSide = PaneSide::Left;
    float splitRatio = 0.5f;
    bool showHiddenFiles = false;
    bool showInspector = false;
    float fontScale = 1.0f;
    std::string language = "pl";
};

std::string formatByteCount(int64_t bytes);
std::string formatExactBytes(int64_t bytes);
std::string formatTimestamp(std::time_t t);
std::string toLowerASCII(std::string s);

} // namespace mc
