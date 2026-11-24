#include "PaneState.hpp"
#include "ToolsService.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace mc {

namespace {

int compareStringsNatural(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            while (i < a.size() && a[i] == '0') ++i;
            while (j < b.size() && b[j] == '0') ++j;
            size_t si = i, sj = j;
            while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) ++i;
            while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) ++j;
            size_t lenA = i - si;
            size_t lenB = j - sj;
            if (lenA != lenB) return lenA < lenB ? -1 : 1;
            int cmp = a.compare(si, lenA, b, sj, lenB);
            if (cmp != 0) return cmp < 0 ? -1 : 1;
        } else {
            int la = std::tolower(ca);
            int lb = std::tolower(cb);
            if (la != lb) return la < lb ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i == a.size() && j == b.size()) return 0;
    return i == a.size() ? -1 : 1;
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

int findIndexByPath(const std::vector<FileItem>& items, const std::string& path) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].path == path) return static_cast<int>(i);
    }
    return -1;
}

FileItem makeParentEntry(const std::string& currentPath) {
    FileItem up;
    up.path = PaneTabState::kParentEntryPath;
    up.name = "..";
    up.displayStem = "..";
    up.fileExtension = "";
    up.posixPermissions = "drwxr-xr-x";
    up.typeDescription = "Katalog nadrzędny (" + fs::path(currentPath).parent_path().string() + ")";
    up.isParentEntry = true;
    up.isDirectory = true;
    return up;
}

} // namespace

std::string PaneTabState::title() const {
    if (isInsideArchive()) {
        return "[" + fs::path(archivePath).filename().string() + "]";
    }
    if (currentPath == "/" || currentPath.empty()) return "root (/)";
    std::string name = fs::path(currentPath).filename().string();
    return name.empty() ? "root (/)" : name;
}

std::vector<FileItem> PaneTabState::selectedItems() const {
    std::vector<FileItem> out;
    if (!selectedPaths.empty()) {
        for (const auto& item : displayedItems) {
            if (!item.isParentEntry && selectedPaths.contains(item.path)) {
                out.push_back(item);
            }
        }
        return out;
    }
    const FileItem* cur = cursorItem();
    if (cur && !cur->isParentEntry) {
        out.push_back(*cur);
    }
    return out;
}

const FileItem* PaneTabState::cursorItem() const {
    if (!cursorPath.empty()) {
        int idx = findIndexByPath(displayedItems, cursorPath);
        if (idx >= 0) return &displayedItems[static_cast<size_t>(idx)];
    }
    if (!displayedItems.empty()) {
        return &displayedItems.front();
    }
    return nullptr;
}

const FileItem* PaneTabState::primaryTargetItem() const {
    const FileItem* cur = cursorItem();
    if (cur && !cur->isParentEntry) return cur;
    for (const auto& item : displayedItems) {
        if (!item.isParentEntry && selectedPaths.contains(item.path)) return &item;
    }
    return nullptr;
}

PaneFooterStats PaneTabState::computeFooterStats() const {
    PaneFooterStats st;
    for (const auto& item : displayedItems) {
        if (item.isParentEntry) continue;
        int64_t bytes = 0;
        if (item.size.has_value()) {
            bytes = *item.size;
        } else {
            auto it = directorySizes.find(item.path);
            if (it != directorySizes.end()) bytes = it->second;
        }

        if (item.isNavigableDirectory()) {
            st.totalDirs++;
        } else {
            st.totalFiles++;
        }
        st.totalBytes += bytes;

        if (selectedPaths.contains(item.path)) {
            if (item.isNavigableDirectory()) {
                st.selectedDirs++;
            } else {
                st.selectedFiles++;
            }
            st.selectedBytes += bytes;
        }
    }
    return st;
}

void PaneTabState::updateItems(
    std::vector<FileItem> newItems,
    std::optional<int64_t> availableSpace,
    bool preserveSelection
) {
    items = std::move(newItems);
    availableSpaceBytes = availableSpace;
    errorMessage.clear();
    highlightedDiffPaths.clear();
    recomputeDisplayedItems();

    std::unordered_set<std::string> validPaths;
    validPaths.reserve(displayedItems.size());
    for (const auto& item : displayedItems) {
        validPaths.insert(item.path);
    }

    if (preserveSelection) {
        std::erase_if(selectedPaths, [&](const std::string& p) { return !validPaths.contains(p) || p == kParentEntryPath; });
        if (!cursorPath.empty() && !validPaths.contains(cursorPath)) {
            cursorPath = displayedItems.empty() ? "" : displayedItems.front().path;
        }
    } else {
        selectedPaths.clear();
        cursorPath = displayedItems.empty() ? "" : displayedItems.front().path;
    }

    if (cursorPath.empty() && !displayedItems.empty()) {
        cursorPath = displayedItems.front().path;
    }
}

void PaneTabState::recomputeDisplayedItems() {
    std::string trimmed = filterText;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();

    std::vector<FileItem> filtered;
    filtered.reserve(items.size());
    for (const auto& item : items) {
        if (trimmed.empty() || containsCaseInsensitive(item.name, trimmed) || ToolsService::matchesWildcard(item.name, trimmed)) {
            filtered.push_back(item);
        }
    }

    if (sortField != FileSortField::Unsorted) {
        std::sort(filtered.begin(), filtered.end(), [&](const FileItem& lhs, const FileItem& rhs) {
            bool lhsDir = lhs.isNavigableDirectory();
            bool rhsDir = rhs.isNavigableDirectory();
            if (lhsDir != rhsDir) {
                return lhsDir;
            }

            int cmp = 0;
            switch (sortField) {
                case FileSortField::Name:
                case FileSortField::Unsorted:
                    cmp = compareStringsNatural(lhs.name, rhs.name);
                    break;
                case FileSortField::Extension:
                    cmp = compareStringsNatural(lhs.fileExtension, rhs.fileExtension);
                    if (cmp == 0) cmp = compareStringsNatural(lhs.name, rhs.name);
                    break;
                case FileSortField::Size: {
                    int64_t lSize = lhs.size.value_or(directorySizes.contains(lhs.path) ? directorySizes.at(lhs.path) : -1);
                    int64_t rSize = rhs.size.value_or(directorySizes.contains(rhs.path) ? directorySizes.at(rhs.path) : -1);
                    if (lSize != rSize) cmp = (lSize < rSize) ? -1 : 1;
                    else cmp = compareStringsNatural(lhs.name, rhs.name);
                    break;
                }
                case FileSortField::ModificationDate: {
                    std::time_t lTime = lhs.modificationTime.value_or(0);
                    std::time_t rTime = rhs.modificationTime.value_or(0);
                    if (lTime != rTime) cmp = (lTime < rTime) ? -1 : 1;
                    else cmp = compareStringsNatural(lhs.name, rhs.name);
                    break;
                }
            }
            return sortAscending ? (cmp < 0) : (cmp > 0);
        });
    }

    displayedItems.clear();
    displayedItems.reserve(filtered.size() + 1);
    if (canGoUp()) {
        displayedItems.push_back(makeParentEntry(currentPath));
    }
    for (auto& it : filtered) {
        displayedItems.push_back(std::move(it));
    }
}

void PaneTabState::setCursorOnly(const std::string& path) {
    cursorPath = path;
    selectionAnchorPath = path;
}

void PaneTabState::selectSingle(const std::string& path) {
    cursorPath = path;
    selectionAnchorPath = path;
    selectedPaths.clear();
    if (path != kParentEntryPath) {
        selectedPaths.insert(path);
    }
}

void PaneTabState::toggleSelection(const std::string& path) {
    cursorPath = path;
    selectionAnchorPath = path;
    if (path == kParentEntryPath) return;
    if (selectedPaths.contains(path)) {
        selectedPaths.erase(path);
    } else {
        selectedPaths.insert(path);
    }
}

void PaneTabState::selectRange(const std::string& path) {
    int targetIdx = findIndexByPath(displayedItems, path);
    if (targetIdx < 0) return;
    const std::string& anchor = !selectionAnchorPath.empty() ? selectionAnchorPath : cursorPath;
    int anchorIdx = anchor.empty() ? targetIdx : findIndexByPath(displayedItems, anchor);
    if (anchorIdx < 0) anchorIdx = targetIdx;
    int lower = std::min(anchorIdx, targetIdx);
    int upper = std::max(anchorIdx, targetIdx);
    for (int i = lower; i <= upper; ++i) {
        const auto& it = displayedItems[static_cast<size_t>(i)];
        if (!it.isParentEntry) {
            selectedPaths.insert(it.path);
        }
    }
    if (selectionAnchorPath.empty()) {
        selectionAnchorPath = anchor;
    }
    cursorPath = path;
}

void PaneTabState::selectAll() {
    selectedPaths.clear();
    for (const auto& item : displayedItems) {
        if (!item.isParentEntry) {
            selectedPaths.insert(item.path);
        }
    }
    if (cursorPath.empty() && !displayedItems.empty()) {
        cursorPath = displayedItems.front().path;
    }
}

void PaneTabState::deselectAll() {
    selectedPaths.clear();
}

void PaneTabState::invertSelection() {
    std::unordered_set<std::string> next;
    for (const auto& item : displayedItems) {
        if (!item.isParentEntry && !selectedPaths.contains(item.path)) {
            next.insert(item.path);
        }
    }
    selectedPaths = std::move(next);
}

int PaneTabState::selectByPattern(const std::string& pattern, bool select) {
    int matched = 0;
    for (const auto& item : displayedItems) {
        if (item.isParentEntry) continue;
        if (ToolsService::matchesWildcard(item.name, pattern)) {
            ++matched;
            if (select) selectedPaths.insert(item.path);
            else selectedPaths.erase(item.path);
        }
    }
    return matched;
}

void PaneTabState::selectSameExtension(bool select) {
    const FileItem* cur = cursorItem();
    if (!cur || cur->isParentEntry) return;
    std::string targetExt = toLowerASCII(cur->fileExtension);
    for (const auto& item : displayedItems) {
        if (item.isParentEntry || item.isNavigableDirectory()) continue;
        if (toLowerASCII(item.fileExtension) == targetExt) {
            if (select) selectedPaths.insert(item.path);
            else selectedPaths.erase(item.path);
        }
    }
}

void PaneTabState::moveCursor(int delta, bool toggleMarkOnMove) {
    if (displayedItems.empty()) return;
    int currentIdx = cursorPath.empty() ? 0 : findIndexByPath(displayedItems, cursorPath);
    if (currentIdx < 0) currentIdx = 0;

    if (toggleMarkOnMove) {
        const auto& curItem = displayedItems[static_cast<size_t>(currentIdx)];
        if (!curItem.isParentEntry) {
            if (selectedPaths.contains(curItem.path)) selectedPaths.erase(curItem.path);
            else selectedPaths.insert(curItem.path);
        }
    }

    int nextIdx = std::clamp(currentIdx + delta, 0, static_cast<int>(displayedItems.size()) - 1);
    cursorPath = displayedItems[static_cast<size_t>(nextIdx)].path;
}

void PaneTabState::moveCursorToEdge(bool top, bool extendSelection) {
    if (displayedItems.empty()) return;
    const std::string& edgePath = top ? displayedItems.front().path : displayedItems.back().path;
    if (extendSelection) {
        selectRange(edgePath);
    } else {
        cursorPath = edgePath;
    }
}

std::optional<FileItem> PaneTabState::toggleCursorSelection(bool advanceCursor) {
    if (displayedItems.empty()) return std::nullopt;
    int currentIdx = cursorPath.empty() ? 0 : findIndexByPath(displayedItems, cursorPath);
    if (currentIdx < 0) currentIdx = 0;
    FileItem item = displayedItems[static_cast<size_t>(currentIdx)];
    if (!item.isParentEntry) {
        if (selectedPaths.contains(item.path)) {
            selectedPaths.erase(item.path);
        } else {
            selectedPaths.insert(item.path);
        }
    }
    if (advanceCursor) {
        int nextIdx = std::min(currentIdx + 1, static_cast<int>(displayedItems.size()) - 1);
        cursorPath = displayedItems[static_cast<size_t>(nextIdx)].path;
    }
    return item;
}

void PaneTabState::toggleSort(FileSortField field) {
    if (sortField == field) {
        sortAscending = !sortAscending;
    } else {
        sortField = field;
        sortAscending = true;
    }
    recomputeDisplayedItems();
}

// MARK: - PaneState

PaneState::PaneState(PaneSide side, const std::string& initialPath) : side(side) {
    PaneTabState tab;
    tab.id = nextTabID_++;
    tab.currentPath = initialPath;
    activeTabID = tab.id;
    tabs.push_back(std::move(tab));
}

PaneTabState& PaneState::activeTab() {
    for (auto& t : tabs) {
        if (t.id == activeTabID) return t;
    }
    return tabs.front();
}

const PaneTabState& PaneState::activeTab() const {
    for (const auto& t : tabs) {
        if (t.id == activeTabID) return t;
    }
    return tabs.front();
}

PaneTabState& PaneState::addTab(const std::string& path) {
    PaneTabState t;
    t.id = nextTabID_++;
    t.currentPath = path;
    activeTabID = t.id;
    tabs.push_back(std::move(t));
    return tabs.back();
}

void PaneState::closeTab(uint64_t tabID) {
    if (tabs.size() <= 1) return;
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].id == tabID) {
            tabs.erase(tabs.begin() + static_cast<ptrdiff_t>(i));
            if (activeTabID == tabID) {
                size_t fallback = std::min(i, tabs.size() - 1);
                activeTabID = tabs[fallback].id;
            }
            return;
        }
    }
}

void PaneState::selectTab(uint64_t tabID) {
    for (const auto& t : tabs) {
        if (t.id == tabID) {
            activeTabID = tabID;
            return;
        }
    }
}

void PaneState::selectAdjacentTab(int offset) {
    if (tabs.size() <= 1) return;
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].id == activeTabID) {
            int n = static_cast<int>(tabs.size());
            int next = (static_cast<int>(i) + offset + n) % n;
            activeTabID = tabs[static_cast<size_t>(next)].id;
            return;
        }
    }
}

} // namespace mc
