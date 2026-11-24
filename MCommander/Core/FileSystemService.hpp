#pragma once

#include "Models.hpp"

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mc {

enum class TransferStatus {
    Success,
    Cancelled,
    ConflictNeedsDecision,
    Error
};

struct TransferOutcome {
    TransferStatus status = TransferStatus::Success;
    std::string errorMessage;
    std::string conflictSourcePath;
    std::string conflictDestPath;
    std::vector<std::string> remainingSourcePaths;
};

class FileSystemService {
public:
    static std::vector<FileItem> listDirectorySync(const std::string& directoryPath, bool showHidden, std::string& errorOut);
    static std::optional<int64_t> availableCapacity(const std::string& directoryPath);
    static int64_t calculateDirectorySizeSync(const std::string& directoryPath);
    static std::vector<FavoriteLocation> defaultFavorites();

    static bool createDirectorySync(const std::string& name, const std::string& parentPath, std::string& createdPathOut, std::string& errorOut);
    static bool renameItemSync(const std::string& sourcePath, const std::string& newName, std::string& destPathOut, std::string& errorOut);
    static bool createLinkSync(const std::string& targetPath, const std::string& linkPath, bool hardLink, bool relativeSymlink, std::string& errorOut);
    static bool permanentlyDeleteSync(const std::vector<std::string>& paths, std::string& errorOut);
    static bool trashItemsSync(const std::vector<std::string>& paths, std::string& errorOut);

    static TransferOutcome transferItemsSync(
        const std::vector<std::string>& sourcePaths,
        const std::string& destinationDirectory,
        bool isMove,
        ConflictResolutionPolicy policy,
        const std::atomic<bool>& cancelFlag,
        const std::function<void(const FileOperationProgress&)>& onProgress
    );
};

} // namespace mc
