#pragma once

#include "Models.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mc {

class ToolsService {
public:
    static bool matchesWildcard(const std::string& text, const std::string& pattern);

    static std::pair<std::unordered_set<std::string>, std::unordered_set<std::string>>
    compareDirectories(const std::vector<FileItem>& left, const std::vector<FileItem>& right);

    static std::vector<SyncComparisonRow>
    buildSyncComparison(const std::vector<FileItem>& left, const std::vector<FileItem>& right);

    static std::vector<MultiRenamePreviewRow>
    previewMultiRename(const std::vector<FileItem>& items, const MultiRenameConfig& config);

    static int applyMultiRename(const std::vector<MultiRenamePreviewRow>& rows, std::string& errorOut);

    static void searchFilesSync(
        const std::string& rootPath,
        const std::string& namePattern,
        const std::string& contentQuery,
        bool includeHidden,
        const std::atomic<bool>& cancelFlag,
        const std::function<void(SearchResultItem)>& onMatch
    );

    static CommandExecutionResult runShellCommandSync(
        const std::string& command,
        const std::string& workingDirectory
    );

    static std::string formatHexDump(const std::vector<uint8_t>& data, size_t maxBytes = 32768);

    // Total Commander extra tools: Archive Pack/Unpack (7z, zip, tar.gz with AES-256), Checksums, Chmod
    static std::string find7zBinary();

    static bool packArchiveSync(
        const std::vector<std::string>& sourcePaths,
        const std::string& archiveOutputPath,
        const PackArchiveConfig& config,
        std::string& errorOut
    );

    static bool unpackArchiveSync(
        const std::string& archivePath,
        const std::string& destinationDirectory,
        const std::string& password,
        std::string& errorOut
    );

    static std::string listArchiveContentsSync(
        const std::string& archivePath,
        const std::string& password = ""
    );

    static std::vector<FileItem> listArchiveEntriesSync(
        const std::string& archivePath,
        const std::string& archiveSubdir,
        const std::string& password,
        std::string& errorOut
    );

    static bool extractArchiveEntriesSync(
        const std::string& archivePath,
        const std::vector<std::string>& internalPaths,
        const std::string& destinationDirectory,
        const std::string& password,
        std::string& errorOut
    );

    static std::vector<UserMenuItem> loadUserMenu();

    static std::string diffFilesSync(
        const std::string& leftFilePath,
        const std::string& rightFilePath
    );

    static std::vector<ChecksumEntry> computeChecksumsSync(
        const std::vector<std::string>& filePaths
    );

    static bool changePermissionsSync(
        const std::vector<std::string>& paths,
        uint16_t mode,
        bool recursive,
        std::string& errorOut
    );

    // SFTP / SSH Virtual File System
    static std::vector<FileItem> listRemoteDirectorySync(
        const RemoteConnection& conn,
        bool includeHidden,
        std::string& errorOut
    );

    static bool downloadFromRemoteSync(
        const RemoteConnection& conn,
        const std::vector<std::string>& remotePaths,
        const std::vector<bool>& isDirectoryFlags,
        const std::string& localDestDir,
        std::string& errorOut
    );

    static bool uploadToRemoteSync(
        const RemoteConnection& conn,
        const std::vector<std::string>& localPaths,
        const std::string& remoteDestDir,
        std::string& errorOut
    );

    static bool createRemoteDirectorySync(
        const RemoteConnection& conn,
        const std::string& dirName,
        std::string& errorOut
    );

    static bool renameRemoteItemSync(
        const RemoteConnection& conn,
        const std::string& oldRemotePath,
        const std::string& newName,
        std::string& errorOut
    );

    static bool deleteRemoteItemsSync(
        const RemoteConnection& conn,
        const std::vector<std::string>& remotePaths,
        const std::vector<bool>& isDirectoryFlags,
        std::string& errorOut
    );
};

} // namespace mc
