#pragma once

#include "FileSystemService.hpp"
#include "Models.hpp"
#include "PaneState.hpp"
#include "ToolsService.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mc {

enum class DialogKind {
    None,
    NewFolder,
    Rename,
    TransferConfirm,
    DeleteConfirm,
    Conflict,
    GoToPath,
    SelectByPattern,
    MultiRename,
    SearchFiles,
    Viewer,
    Editor,
    SyncDirs,
    PackArchive,
    UnpackArchive,
    Chmod,
    Checksums,
    Help,
    UserMenu,
    CreateLink,
    DiffViewer
};

struct QuickViewState {
    bool active = false;
    std::string loadedPath;
    bool hexMode = false;
    bool isBinary = false;
    bool isDirectory = false;
    int64_t fileCount = 0;
    int64_t dirCount = 0;
    int64_t totalBytes = 0;
    std::string textContent;
    std::string hexContent;
};

struct DialogState {
    DialogKind kind = DialogKind::None;
    PaneSide side = PaneSide::Left;
    std::string parentPath;
    std::string inputBuffer;
    std::string secondaryBuffer;
    FileItem targetItem;
    std::vector<FileItem> targetItems;
    std::string destinationPath;
    bool isMove = false;
    bool isSelectingPattern = true;
    ConflictResolutionPolicy conflictPolicy = ConflictResolutionPolicy::Ask;

    // Conflict
    std::string conflictSourcePath;
    std::string conflictDestPath;
    std::vector<std::string> conflictRemainingPaths;

    // Multi-rename
    MultiRenameConfig multiRenameConfig;
    std::vector<MultiRenamePreviewRow> multiRenamePreview;

    // Search
    std::string searchRootPath;
    std::string searchPattern = "*";
    std::string searchContent;
    bool searchIncludeHidden = false;
    std::vector<SearchResultItem> searchResults;
    bool isSearching = false;
    std::shared_ptr<std::atomic<bool>> searchCancelFlag;

    // Viewer / Editor
    bool viewerHexMode = false;
    std::string viewerText;
    std::string viewerHexText;
    std::string viewerSearchQuery;
    std::string editorBuffer;
    bool editorDirty = false;

    // Synchronize Directories
    std::string syncLeftDir;
    std::string syncRightDir;
    std::vector<SyncComparisonRow> syncRows;
    bool syncShowEqual = false;
    bool syncShowLeftToRight = true;
    bool syncShowRightToLeft = true;

    // Archive Pack / Unpack
    PackArchiveConfig packConfig;
    std::string unpackPassword;
    bool unpackShowPassword = false;

    // Chmod
    uint16_t chmodMode = 0755;
    bool chmodRecursive = false;

    // Checksums
    std::vector<ChecksumEntry> checksumEntries;
    bool isComputingChecksums = false;

    // Midnight Commander: Create Symlink / Hardlink
    bool linkIsHard = false;
    bool linkIsRelative = false;

    // Midnight Commander: Diff Viewer
    std::string diffLeftPath;
    std::string diffRightPath;
    std::string diffOutput;
};

class CommanderEngine {
public:
    PaneState leftPane;
    PaneState rightPane;
    PaneSide activeSide = PaneSide::Left;

    bool showHiddenFiles = false;
    bool showInspector = false;
    float splitRatio = 0.5f;
    float fontScale = 1.0f;
    bool subshellFullScreen = false; // Ctrl+O Midnight Commander subshell
    bool infoPanelActive = false;    // Ctrl+X I Midnight Commander Info panel
    QuickViewState quickView;

    std::vector<FavoriteLocation> favorites;
    std::vector<MountedVolume> volumes;

    DialogState dialog;

    std::string commandInput;
    std::vector<std::string> commandHistory;
    int commandHistoryPos = -1;
    std::optional<CommandExecutionResult> lastCommandResult;
    bool isRunningCommand = false;
    bool isCommandOutputVisible = false;
    bool requestCommandFocus = false;
    bool requestFilterFocus = false;
    bool requestScrollToCursorLeft = false;
    bool requestScrollToCursorRight = false;
    bool requestHotlistPopup = false;
    bool requestHistoryPopup = false;

    std::optional<FileOperationProgress> operationProgress;
    std::string toastMessage;
    std::string errorAlertMessage;

    CommanderEngine();
    ~CommanderEngine();

    void saveSettings() const;
    void tick();

    PaneState& activePane() { return activeSide == PaneSide::Left ? leftPane : rightPane; }
    PaneState& inactivePane() { return activeSide == PaneSide::Left ? rightPane : leftPane; }
    PaneState& pane(PaneSide side) { return side == PaneSide::Left ? leftPane : rightPane; }

    void setActiveSide(PaneSide side) { activeSide = side; }
    void switchPane() { activeSide = oppositeSide(activeSide); }
    void toggleHiddenFiles();
    void toggleQuickView();
    void toggleInfoPanel();
    void toggleSubshell();
    void openCursorDirInOppositePane(); // Alt+O (Midnight Commander)
    void updateQuickViewIfNeeded();

    void refreshAllVisibleTabs();
    void refreshPane(std::optional<PaneSide> side = std::nullopt);
    void loadDirectory(PaneSide side, uint64_t tabID, const std::string& path, bool preserveSelection = true, std::string selectAfterPath = "");

    void navigate(const std::string& path, std::optional<PaneSide> side = std::nullopt, bool recordHistory = true);
    void goUp(std::optional<PaneSide> side = std::nullopt);
    void goBack(std::optional<PaneSide> side = std::nullopt);
    void goForward(std::optional<PaneSide> side = std::nullopt);
    void goToRoot(std::optional<PaneSide> side = std::nullopt);

    void openItem(const FileItem& item, std::optional<PaneSide> side = std::nullopt);
    void openSelected(std::optional<PaneSide> side = std::nullopt);
    void toggleSpaceSelection(bool advanceCursor = false, std::optional<PaneSide> side = std::nullopt);
    void calculateAllDirectorySizes(std::optional<PaneSide> side = std::nullopt);

    void addTab(std::optional<PaneSide> side = std::nullopt, const std::string& path = "");
    void closeTab(uint64_t tabID, PaneSide side);
    void selectTab(uint64_t tabID, PaneSide side);

    void swapPanes();
    void syncInactivePaneToActive();
    void compareDirectories();

    // Triggers
    void triggerF3View();
    void triggerQuickLook();
    void triggerF4Edit();
    void triggerF5Copy();
    void triggerF6Move();
    void triggerRename();
    void triggerF7NewFolder();
    void triggerF8Delete();
    void triggerGoToPath();
    void triggerSelectByPattern(bool isSelecting);
    void triggerMultiRename();
    void triggerFindFiles();
    void triggerSyncDirs();
    void triggerPackArchive();
    void triggerUnpackArchive();
    void triggerChmod();
    void triggerChecksums();
    void triggerF1Help();
    void triggerF2UserMenu();
    void triggerCreateLink(bool hardLink, bool relativeSymlink);
    void triggerDiffFiles();

    // Actions
    void createFolder(const std::string& name, const std::string& parentPath, PaneSide side);
    void renameItem(const FileItem& item, const std::string& newName, PaneSide side);
    void createLink();
    void runQuickCommandInActiveDir(const std::string& cmd, const std::string& title);
    void deleteItems(const std::vector<FileItem>& items, bool permanently, PaneSide side);

    void startTransfer(const std::vector<std::string>& sourcePaths, const std::string& destDir, bool isMove, ConflictResolutionPolicy policy);
    void resolveConflict(ConflictResolutionPolicy policy);
    void cancelActiveTransfer();

    void applyPatternSelection(const std::string& pattern, bool isSelecting);
    void updateMultiRenamePreview();
    void applyMultiRename();

    void executeSyncDirs();
    void executePackArchive();
    void executeUnpackArchive();
    void executeChmod();

    void startSearch();
    void stopSearch();
    void navigateToSearchResult(const SearchResultItem& result);

    void openViewer(const FileItem& item);
    void openEditor(const FileItem& item);
    void saveEditor();

    void insertCursorNameToCommandLine(bool fullPath);
    void runCommandInput();

    void loadFavorites();
    void pinFolderToFavorites(const std::string& path);
    void removeFavorite(const std::string& id);
    void refreshVolumes();
    void ejectVolume(const MountedVolume& volume);

    void copySelectedPathsToClipboard();
    void showToast(const std::string& message);

private:
    std::mutex queueMutex_;
    std::vector<std::function<void()>> mainQueue_;
    std::shared_ptr<std::atomic<bool>> transferCancelFlag_;
    std::chrono::steady_clock::time_point toastExpiry_;

    void postMain(std::function<void()> fn);
    void refreshOppositeIfSameFolder(const std::string& folderPath, PaneSide sourceSide);
};

} // namespace mc
