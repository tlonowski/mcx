#pragma once

#include "Models.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mc {

struct PaneFooterStats {
    size_t totalFiles = 0;
    size_t totalDirs = 0;
    int64_t totalBytes = 0;
    size_t selectedFiles = 0;
    size_t selectedDirs = 0;
    int64_t selectedBytes = 0;
};

struct PaneTabState {
    uint64_t id = 0;
    std::string currentPath;
    std::string archivePath;   // Non-empty when browsing inside an archive (VFS)
    std::string archiveSubdir; // Relative folder inside archive
    std::vector<FileItem> items;
    std::vector<FileItem> displayedItems;
    std::string filterText;
    FileSortField sortField = FileSortField::Name;
    bool sortAscending = true;
    bool briefMode = false;

    std::unordered_set<std::string> selectedPaths;
    std::unordered_set<std::string> highlightedDiffPaths;
    std::string cursorPath;
    std::string selectionAnchorPath;
    std::unordered_map<std::string, int64_t> directorySizes;

    std::vector<std::string> historyBack;
    std::vector<std::string> historyForward;

    bool isLoading = false;
    std::string errorMessage;
    std::optional<int64_t> availableSpaceBytes;

    static constexpr const char* kParentEntryPath = "__MC_PARENT_ENTRY__";

    std::string title() const;
    bool isInsideArchive() const { return !archivePath.empty(); }
    bool canGoBack() const { return !historyBack.empty(); }
    bool canGoForward() const { return !historyForward.empty(); }
    bool canGoUp() const { return isInsideArchive() || currentPath != "/"; }

    std::vector<FileItem> selectedItems() const;
    const FileItem* cursorItem() const;
    const FileItem* primaryTargetItem() const;
    PaneFooterStats computeFooterStats() const;

    void updateItems(std::vector<FileItem> newItems, std::optional<int64_t> availableSpace, bool preserveSelection = true);
    void recomputeDisplayedItems();

    // Total Commander selection & cursor methods
    void setCursorOnly(const std::string& path);
    void selectSingle(const std::string& path);
    void toggleSelection(const std::string& path);
    void selectRange(const std::string& path);
    void selectAll();
    void deselectAll();
    void invertSelection();
    int selectByPattern(const std::string& pattern, bool select);
    void selectSameExtension(bool select);

    void moveCursor(int delta, bool toggleMarkOnMove);
    void moveCursorToEdge(bool top, bool extendSelection);
    std::optional<FileItem> toggleCursorSelection(bool advanceCursor);
    void toggleSort(FileSortField field);
};

class PaneState {
public:
    PaneSide side;
    std::vector<PaneTabState> tabs;
    uint64_t activeTabID = 0;

    PaneState(PaneSide side, const std::string& initialPath);

    PaneTabState& activeTab();
    const PaneTabState& activeTab() const;

    PaneTabState& addTab(const std::string& path);
    void closeTab(uint64_t tabID);
    void selectTab(uint64_t tabID);
    void selectAdjacentTab(int offset);

private:
    static inline uint64_t nextTabID_ = 1;
};

} // namespace mc
