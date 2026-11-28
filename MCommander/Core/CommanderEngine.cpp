#include "CommanderEngine.hpp"
#include "Localization.hpp"
#include "../Platform/MacBridge.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace mc {

namespace {

std::string initialRightPath(const std::string& home) {
    std::error_code ec;
    std::string desktop = home + "/Desktop";
    if (fs::exists(desktop, ec) && fs::is_directory(desktop, ec)) {
        return desktop;
    }
    return home;
}

std::string normalizeDirPath(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(path), ec);
    if (ec) return path;
    return p.string();
}

bool isImageExtension(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
           ext == "bmp" || ext == "tga" || ext == "webp" || ext == "tif" ||
           ext == "tiff" || ext == "heic" || ext == "ico";
}

} // namespace

CommanderEngine::CommanderEngine()
    : leftPane(PaneSide::Left, platform::homeDirectory()),
      rightPane(PaneSide::Right, initialRightPath(platform::homeDirectory())) {
    AppSettings saved = platform::loadAppSettings();
    Localization::instance().initialize(platform::localeSearchDirectories(), saved.language);
    activeSide = saved.activeSide;
    splitRatio = saved.splitRatio;
    fontScale = saved.fontScale;
    showHiddenFiles = saved.showHiddenFiles;
    showInspector = saved.showInspector;

    auto restorePane = [](PaneState& pane, const PaneSavedSettings& ps, const std::string& fallbackDir) {
        std::error_code ec;
        std::vector<std::string> validPaths;
        for (const auto& p : ps.tabPaths) {
            if (!p.empty() && fs::exists(p, ec) && fs::is_directory(p, ec)) {
                validPaths.push_back(p);
            }
        }
        if (!validPaths.empty()) {
            pane.tabs.clear();
            for (const auto& p : validPaths) {
                auto& tab = pane.addTab(p);
                tab.sortField = ps.sortField;
                tab.sortAscending = ps.sortAscending;
                tab.briefMode = ps.briefMode;
            }
            int idx = std::clamp(ps.activeTabIndex, 0, static_cast<int>(pane.tabs.size()) - 1);
            pane.activeTabID = pane.tabs[static_cast<size_t>(idx)].id;
        } else {
            auto& tab = pane.activeTab();
            tab.currentPath = fallbackDir;
            tab.sortField = ps.sortField;
            tab.sortAscending = ps.sortAscending;
            tab.briefMode = ps.briefMode;
        }
    };

    std::string home = platform::homeDirectory();
    restorePane(leftPane, saved.leftPane, home);
    restorePane(rightPane, saved.rightPane, initialRightPath(home));

    loadFavorites();
    refreshVolumes();
    refreshAllVisibleTabs();
}

CommanderEngine::~CommanderEngine() {
    shuttingDown_.store(true);
    if (transferCancelFlag_) transferCancelFlag_->store(true);

    std::vector<std::thread> toJoin;
    {
        std::lock_guard<std::mutex> lock(workersMutex_);
        toJoin = std::move(workers_);
    }
    for (auto& t : toJoin) {
        if (t.joinable()) t.join();
    }

    if (quickView.imageTexture) {
        platform::releaseMetalTexture(quickView.imageTexture);
        quickView.imageTexture = nullptr;
    }
    if (dialog.viewerImageTexture) {
        platform::releaseMetalTexture(dialog.viewerImageTexture);
        dialog.viewerImageTexture = nullptr;
    }
    saveSettings();
}

void CommanderEngine::saveSettings() const {
    AppSettings s;
    auto capturePane = [](const PaneState& pane) {
        PaneSavedSettings ps;
        for (size_t i = 0; i < pane.tabs.size(); ++i) {
            ps.tabPaths.push_back(pane.tabs[i].currentPath);
            if (pane.tabs[i].id == pane.activeTabID) {
                ps.activeTabIndex = static_cast<int>(i);
            }
        }
        const auto& active = pane.activeTab();
        ps.sortField = active.sortField;
        ps.sortAscending = active.sortAscending;
        ps.briefMode = active.briefMode;
        return ps;
    };
    s.leftPane = capturePane(leftPane);
    s.rightPane = capturePane(rightPane);
    s.activeSide = activeSide;
    s.splitRatio = splitRatio;
    s.showHiddenFiles = showHiddenFiles;
    s.showInspector = showInspector;
    s.fontScale = fontScale;
    s.language = Localization::instance().currentLanguage();
    platform::saveAppSettings(s);
}

void CommanderEngine::spawnWorker(std::function<void()> fn) {
    if (shuttingDown_.load()) return;
    std::lock_guard<std::mutex> lock(workersMutex_);
    if (shuttingDown_.load()) return;
    workers_.emplace_back([fn = std::move(fn)]() { fn(); });
}

void CommanderEngine::postMain(std::function<void()> fn) {
    if (shuttingDown_.load()) return;
    std::lock_guard<std::mutex> lock(queueMutex_);
    mainQueue_.push_back(std::move(fn));
}

void CommanderEngine::tick() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        tasks.swap(mainQueue_);
    }
    for (auto& fn : tasks) {
        if (fn) fn();
    }

    if (!toastMessage.empty() && std::chrono::steady_clock::now() >= toastExpiry_) {
        toastMessage.clear();
    }

    if (quickView.active) {
        updateQuickViewIfNeeded();
    }
}

void CommanderEngine::toggleHiddenFiles() {
    showHiddenFiles = !showHiddenFiles;
    refreshAllVisibleTabs();
}

void CommanderEngine::toggleQuickView() {
    quickView.active = !quickView.active;
    if (quickView.active) {
        infoPanelActive = false;
        quickView.loadedPath.clear();
        updateQuickViewIfNeeded();
        showToast("Szybki podgląd (Ctrl+Q): WŁĄCZONY");
    } else {
        showToast("Szybki podgląd (Ctrl+Q): WYŁĄCZONY");
    }
}

void CommanderEngine::toggleInfoPanel() {
    infoPanelActive = !infoPanelActive;
    if (infoPanelActive) {
        quickView.active = false;
        showToast("Panel Info MC (Ctrl+X I): WŁĄCZONY");
    } else {
        showToast("Panel Info MC (Ctrl+X I): WYŁĄCZONY");
    }
}

void CommanderEngine::toggleSubshell() {
    subshellFullScreen = !subshellFullScreen;
    if (subshellFullScreen) {
        requestCommandFocus = true;
    }
}

void CommanderEngine::openCursorDirInOppositePane() {
    const FileItem* cur = activePane().activeTab().cursorItem();
    PaneSide opp = oppositeSide(activeSide);
    PaneSide keepActive = activeSide;
    if (cur && !cur->isParentEntry && cur->isNavigableDirectory()) {
        navigate(cur->path, opp, true);
    } else if (cur && cur->isParentEntry) {
        std::string parent = fs::path(activePane().activeTab().currentPath).parent_path().string();
        if (parent.empty()) parent = "/";
        navigate(parent, opp, true);
    } else {
        navigate(activePane().activeTab().currentPath, opp, true);
    }
    activeSide = keepActive;
    showToast("Otwarto w drugim panelu (Alt+O)");
}

void CommanderEngine::updateQuickViewIfNeeded() {
    const auto& tab = activePane().activeTab();
    const FileItem* item = tab.cursorItem();
    if (!item || item->isParentEntry) {
        quickView.loadedPath = "";
        quickView.textContent = "Ustaw kursor na pliku lub folderze, aby zobaczyć szybki podgląd (Ctrl+Q / ⌘Q).";
        quickView.hexContent.clear();
        quickView.isDirectory = false;
        quickView.isImage = false;
        if (quickView.imageTexture) {
            platform::releaseMetalTexture(quickView.imageTexture);
            quickView.imageTexture = nullptr;
        }
        return;
    }
    std::string targetKey = tab.isRemote()
        ? (tab.remote->uri() + "::" + item->path)
        : (tab.isInsideArchive() ? (tab.archivePath + "::" + item->path) : item->path);
    if (quickView.loadedPath == targetKey) return;
    quickView.loadedPath = targetKey;

    if (quickView.imageTexture) {
        platform::releaseMetalTexture(quickView.imageTexture);
        quickView.imageTexture = nullptr;
    }
    quickView.isImage = false;
    quickView.imageWidth = 0;
    quickView.imageHeight = 0;

    if (tab.isRemote()) {
        quickView.isDirectory = item->isNavigableDirectory();
        quickView.isBinary = false;
        quickView.hexMode = false;
        quickView.textContent = "(Zdalny plik SFTP — użyj F3 aby pobrać do podglądu lub F5 aby skopiować)";
        quickView.hexContent.clear();
        return;
    }

    if (tab.isInsideArchive()) {
        quickView.isDirectory = item->isNavigableDirectory();
        quickView.isBinary = false;
        quickView.hexMode = false;
        quickView.textContent = "(Podgląd niedostępny wewnątrz archiwum — użyj F5 aby rozpakować)";
        quickView.hexContent.clear();
        return;
    }

    if (item->isNavigableDirectory()) {
        quickView.isDirectory = true;
        quickView.fileCount = 0;
        quickView.dirCount = 0;
        quickView.totalBytes = 0;
        std::string dirPath = item->path;
        spawnWorker([this, targetKey, dirPath]() {
            int64_t dirs = 0, files = 0, total = 0;
            std::error_code ec;
            fs::directory_iterator it(dirPath, fs::directory_options::skip_permission_denied, ec);
            const fs::directory_iterator endIt;
            while (!ec && it != endIt) {
                std::error_code statEc;
                if (it->is_directory(statEc)) {
                    dirs++;
                } else {
                    files++;
                    uintmax_t sz = it->file_size(statEc);
                    if (!statEc) total += static_cast<int64_t>(sz);
                }
                it.increment(ec);
                if (ec) ec.clear();
            }
            postMain([this, targetKey, dirs, files, total]() {
                if (quickView.loadedPath != targetKey) return;
                quickView.dirCount = dirs;
                quickView.fileCount = files;
                quickView.totalBytes = total;
            });
        });
        return;
    }

    quickView.isDirectory = false;
    if (isImageExtension(item->name) && mtlDevice != nullptr) {
        quickView.isImage = true;
        quickView.isBinary = false;
        quickView.hexMode = false;
        quickView.textContent = "Ładowanie obrazu…";
        quickView.hexContent.clear();
        std::string imgPath = item->path;
        void* dev = mtlDevice;
        spawnWorker([this, targetKey, imgPath, dev]() {
            int w = 0, h = 0;
            void* tex = platform::loadMetalTexture(imgPath, dev, w, h);
            postMain([this, targetKey, tex, w, h]() {
                if (quickView.loadedPath != targetKey) {
                    if (tex) platform::releaseMetalTexture(tex);
                    return;
                }
                quickView.imageTexture = tex;
                quickView.imageWidth = w;
                quickView.imageHeight = h;
                if (!tex) {
                    quickView.isImage = false;
                    quickView.textContent = "(Nie udało się wczytać obrazu)";
                } else {
                    quickView.textContent.clear();
                }
            });
        });
        return;
    }

    std::string filePath = item->path;
    bool isArch = item->isArchive();
    quickView.textContent = "Ładowanie podglądu…";
    quickView.hexContent.clear();
    spawnWorker([this, targetKey, filePath, isArch]() {
        if (isArch) {
            std::string listing = ToolsService::listArchiveContentsSync(filePath);
            postMain([this, targetKey, listing = std::move(listing)]() mutable {
                if (quickView.loadedPath != targetKey) return;
                quickView.isBinary = false;
                quickView.hexMode = false;
                quickView.textContent = std::move(listing);
                quickView.hexContent.clear();
            });
            return;
        }

        std::ifstream in(filePath, std::ios::binary);
        if (!in) {
            postMain([this, targetKey]() {
                if (quickView.loadedPath != targetKey) return;
                quickView.textContent = "(Brak dostępu lub nie można odczytać pliku)";
                quickView.hexContent.clear();
            });
            return;
        }

        constexpr size_t maxBytes = 65536;
        std::vector<uint8_t> buf(maxBytes);
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(maxBytes));
        buf.resize(static_cast<size_t>(in.gcount()));

        std::string hex = ToolsService::formatHexDump(buf, 16384);
        bool bin = false;
        size_t check = std::min<size_t>(buf.size(), 2048);
        for (size_t i = 0; i < check; ++i) {
            if (buf[i] == 0) { bin = true; break; }
        }
        std::string text;
        if (bin) {
            text = "(Plik binarny — widok HEX)";
        } else {
            text.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
        }

        postMain([this, targetKey, bin, hex = std::move(hex), text = std::move(text)]() mutable {
            if (quickView.loadedPath != targetKey) return;
            quickView.hexContent = std::move(hex);
            quickView.isBinary = bin;
            if (bin) quickView.hexMode = true;
            quickView.textContent = std::move(text);
        });
    });
}

void CommanderEngine::refreshAllVisibleTabs() {
    loadDirectory(PaneSide::Left, leftPane.activeTab().id, leftPane.activeTab().currentPath, true);
    loadDirectory(PaneSide::Right, rightPane.activeTab().id, rightPane.activeTab().currentPath, true);
}

void CommanderEngine::refreshPane(std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    if (tab.isRemote()) {
        loadRemoteDirectory(targetSide, tab.id, *tab.remote);
        return;
    }
    if (tab.isInsideArchive()) {
        loadArchiveDirectory(targetSide, tab.id, tab.archivePath, tab.archiveSubdir);
        return;
    }
    loadDirectory(targetSide, tab.id, tab.currentPath, true);
}

void CommanderEngine::loadDirectory(
    PaneSide side,
    uint64_t tabID,
    const std::string& path,
    bool preserveSelection,
    std::string selectAfterPath
) {
    auto& p = pane(side);
    for (auto& t : p.tabs) {
        if (t.id == tabID) {
            t.isLoading = true;
            break;
        }
    }
    bool hidden = showHiddenFiles;

    spawnWorker([this, side, tabID, path, hidden, preserveSelection, selectAfterPath = std::move(selectAfterPath)]() {
        std::string err;
        auto items = FileSystemService::listDirectorySync(path, hidden, err);
        auto freeSpace = FileSystemService::availableCapacity(path);

        postMain([this, side, tabID, path, items = std::move(items), freeSpace, err = std::move(err), preserveSelection, selectAfterPath]() mutable {
            auto& pState = pane(side);
            for (auto& t : pState.tabs) {
                if (t.id == tabID && t.currentPath == path) {
                    t.isLoading = false;
                    if (!err.empty()) {
                        t.errorMessage = err;
                    } else {
                        t.updateItems(std::move(items), freeSpace, preserveSelection);
                        if (!selectAfterPath.empty()) {
                            for (const auto& it : t.displayedItems) {
                                if (it.path == selectAfterPath) {
                                    t.setCursorOnly(selectAfterPath);
                                    if (side == PaneSide::Left) requestScrollToCursorLeft = true;
                                    else requestScrollToCursorRight = true;
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }
            }
        });
    });
}

void CommanderEngine::navigate(const std::string& path, std::optional<PaneSide> side, bool recordHistory) {
    RemoteConnection remoteConn;
    if (parseRemoteUri(path, remoteConn)) {
        connectRemote(path, side);
        return;
    }

    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    std::string normalized = normalizeDirPath(path);

    std::error_code ec;
    if (!fs::exists(normalized, ec) || !fs::is_directory(normalized, ec)) {
        showToast("Ścieżka nie jest dostępna: " + normalized);
        return;
    }

    if (recordHistory && normalizeDirPath(tab.currentPath) != normalized) {
        tab.historyBack.push_back(tab.currentPath);
        tab.historyForward.clear();
    }

    tab.remote.reset();
    tab.archivePath.clear();
    tab.archiveSubdir.clear();
    tab.currentPath = normalized;
    tab.filterText.clear();
    activeSide = targetSide;
    saveSettings();
    loadDirectory(targetSide, tab.id, normalized, false);
}

void CommanderEngine::goUp(std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    if (!tab.canGoUp()) return;

    if (tab.isRemote()) {
        activeSide = targetSide;
        std::string curRemote = tab.remote->remotePath;
        if (curRemote.empty() || curRemote == "/") {
            tab.remote.reset();
            tab.filterText.clear();
            loadDirectory(targetSide, tab.id, tab.currentPath, false);
        } else {
            RemoteConnection nextConn = *tab.remote;
            std::string parentRemote = fs::path(curRemote).parent_path().string();
            if (parentRemote.empty()) parentRemote = "/";
            nextConn.remotePath = parentRemote;
            tab.filterText.clear();
            loadRemoteDirectory(targetSide, tab.id, nextConn, curRemote);
        }
        return;
    }

    if (tab.isInsideArchive()) {
        activeSide = targetSide;
        if (tab.archiveSubdir.empty()) {
            std::string prevArchive = tab.archivePath;
            tab.archivePath.clear();
            tab.archiveSubdir.clear();
            tab.filterText.clear();
            loadDirectory(targetSide, tab.id, tab.currentPath, false, prevArchive);
        } else {
            auto slash = tab.archiveSubdir.rfind('/');
            std::string parentSubdir = (slash == std::string::npos) ? "" : tab.archiveSubdir.substr(0, slash);
            tab.filterText.clear();
            loadArchiveDirectory(targetSide, tab.id, tab.archivePath, parentSubdir);
        }
        return;
    }

    std::string previousPath = tab.currentPath;
    std::string parentPath = fs::path(previousPath).parent_path().string();
    if (parentPath.empty()) parentPath = "/";

    tab.historyBack.push_back(previousPath);
    tab.historyForward.clear();
    tab.currentPath = parentPath;
    tab.filterText.clear();
    activeSide = targetSide;
    loadDirectory(targetSide, tab.id, parentPath, false, previousPath);
}

void CommanderEngine::goBack(std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    if (tab.historyBack.empty()) return;

    std::string prev = tab.historyBack.back();
    tab.historyBack.pop_back();
    tab.historyForward.push_back(tab.currentPath);
    tab.currentPath = prev;
    tab.filterText.clear();
    activeSide = targetSide;
    loadDirectory(targetSide, tab.id, prev, false);
}

void CommanderEngine::goForward(std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    if (tab.historyForward.empty()) return;

    std::string next = tab.historyForward.back();
    tab.historyForward.pop_back();
    tab.historyBack.push_back(tab.currentPath);
    tab.currentPath = next;
    tab.filterText.clear();
    activeSide = targetSide;
    loadDirectory(targetSide, tab.id, next, false);
}

void CommanderEngine::goToRoot(std::optional<PaneSide> side) {
    navigate("/", side);
}

void CommanderEngine::loadArchiveDirectory(PaneSide side, uint64_t tabID, const std::string& archivePath, const std::string& archiveSubdir, const std::string& password) {
    for (auto& t : pane(side).tabs) {
        if (t.id == tabID) {
            t.isLoading = true;
            t.errorMessage.clear();
            break;
        }
    }
    spawnWorker([this, side, tabID, archivePath, archiveSubdir, password]() {
        std::string err;
        auto items = ToolsService::listArchiveEntriesSync(archivePath, archiveSubdir, password, err);
        postMain([this, side, tabID, archivePath, archiveSubdir, items = std::move(items), err = std::move(err)]() mutable {
            for (auto& t : pane(side).tabs) {
                if (t.id == tabID) {
                    t.isLoading = false;
                    if (!err.empty()) {
                        t.errorMessage = err;
                        showToast(err);
                    } else {
                        t.archivePath = archivePath;
                        t.archiveSubdir = archiveSubdir;
                        t.items = std::move(items);
                        t.selectedPaths.clear();
                        t.recomputeDisplayedItems();
                        t.cursorPath = t.displayedItems.empty() ? "" : t.displayedItems.front().path;
                    }
                    break;
                }
            }
        });
    });
}

void CommanderEngine::loadRemoteDirectory(PaneSide side, uint64_t tabID, const RemoteConnection& conn, std::string selectAfterPath) {
    for (auto& t : pane(side).tabs) {
        if (t.id == tabID) {
            t.isLoading = true;
            t.errorMessage.clear();
            break;
        }
    }
    bool hidden = showHiddenFiles;
    spawnWorker([this, side, tabID, conn, hidden, selectAfterPath = std::move(selectAfterPath)]() {
        std::string err;
        auto items = ToolsService::listRemoteDirectorySync(conn, hidden, err);
        postMain([this, side, tabID, conn, items = std::move(items), err = std::move(err), selectAfterPath]() mutable {
            for (auto& t : pane(side).tabs) {
                if (t.id == tabID) {
                    t.isLoading = false;
                    if (!err.empty()) {
                        t.errorMessage = err;
                        errorAlertMessage = err;
                    } else {
                        t.archivePath.clear();
                        t.archiveSubdir.clear();
                        t.remote = conn;
                        t.items = std::move(items);
                        t.selectedPaths.clear();
                        t.availableSpaceBytes.reset();
                        t.recomputeDisplayedItems();
                        t.cursorPath = t.displayedItems.empty() ? "" : t.displayedItems.front().path;
                        if (!selectAfterPath.empty()) {
                            for (const auto& it : t.displayedItems) {
                                if (it.path == selectAfterPath) {
                                    t.setCursorOnly(selectAfterPath);
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }
            }
        });
    });
}

void CommanderEngine::connectRemote(const std::string& uri, std::optional<PaneSide> side) {
    RemoteConnection conn;
    if (!parseRemoteUri(uri, conn)) {
        showToast("Nieprawidłowy adres (użyj np. sh://user@host/ lub sftp://user@host/)");
        return;
    }
    dialog.kind = DialogKind::None;
    PaneSide targetSide = side.value_or(activeSide);
    activeSide = targetSide;
    auto& tab = pane(targetSide).activeTab();
    tab.filterText.clear();
    showToast("Łączenie z " + conn.targetSpec() + "…");
    loadRemoteDirectory(targetSide, tab.id, conn);
}

void CommanderEngine::openItem(const FileItem& item, std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    if (item.isParentEntry) {
        goUp(targetSide);
        return;
    }
    if (tab.isRemote()) {
        if (item.isNavigableDirectory()) {
            RemoteConnection nextConn = *tab.remote;
            nextConn.remotePath = item.path;
            activeSide = targetSide;
            tab.filterText.clear();
            loadRemoteDirectory(targetSide, tab.id, nextConn);
        } else {
            showToast("Użyj F3 (podgląd) lub F5 (pobierz plik)");
        }
        return;
    }
    if (tab.isInsideArchive()) {
        if (item.isNavigableDirectory()) {
            std::string nextSubdir = tab.archiveSubdir.empty() ? item.name : (tab.archiveSubdir + "/" + item.name);
            activeSide = targetSide;
            loadArchiveDirectory(targetSide, tab.id, tab.archivePath, nextSubdir);
        } else {
            showToast("Użyj F5 aby rozpakować plik z archiwum");
        }
        return;
    }
    if (item.isNavigableDirectory()) {
        navigate(item.path, targetSide);
    } else if (item.isArchive()) {
        activeSide = targetSide;
        loadArchiveDirectory(targetSide, tab.id, item.path, "");
    } else {
        platform::openItem(item.path);
    }
}

void CommanderEngine::openSelected(std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    const auto* cur = pane(targetSide).activeTab().cursorItem();
    if (cur) {
        openItem(*cur, targetSide);
    }
}

void CommanderEngine::toggleSpaceSelection(bool advanceCursor, std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    uint64_t tabID = tab.id;
    auto toggled = tab.toggleCursorSelection(advanceCursor);
    if (!toggled.has_value() || toggled->isParentEntry || !toggled->isNavigableDirectory()) return;
    if (tab.directorySizes.contains(toggled->path)) return;

    std::string dirPath = toggled->path;
    spawnWorker([this, targetSide, tabID, dirPath]() {
        int64_t bytes = FileSystemService::calculateDirectorySizeSync(dirPath);
        postMain([this, targetSide, tabID, dirPath, bytes]() {
            for (auto& t : pane(targetSide).tabs) {
                if (t.id == tabID) {
                    t.directorySizes[dirPath] = bytes;
                    if (t.sortField == FileSortField::Size) {
                        t.recomputeDisplayedItems();
                    }
                    break;
                }
            }
        });
    });
}

void CommanderEngine::calculateAllDirectorySizes(std::optional<PaneSide> side) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& tab = pane(targetSide).activeTab();
    uint64_t tabID = tab.id;
    std::vector<std::string> dirs;
    for (const auto& item : tab.displayedItems) {
        if (!item.isParentEntry && item.isNavigableDirectory()) {
            dirs.push_back(item.path);
        }
    }
    if (dirs.empty()) return;
    showToast("Obliczanie rozmiaru " + std::to_string(dirs.size()) + " katalogów…");

    spawnWorker([this, targetSide, tabID, dirs = std::move(dirs)]() {
        for (const auto& d : dirs) {
            int64_t bytes = FileSystemService::calculateDirectorySizeSync(d);
            postMain([this, targetSide, tabID, d, bytes]() {
                for (auto& t : pane(targetSide).tabs) {
                    if (t.id == tabID) {
                        t.directorySizes[d] = bytes;
                        if (t.sortField == FileSortField::Size) {
                            t.recomputeDisplayedItems();
                        }
                        break;
                    }
                }
            });
        }
        postMain([this]() {
            showToast("Obliczono rozmiary katalogów");
        });
    });
}

void CommanderEngine::addTab(std::optional<PaneSide> side, const std::string& path) {
    PaneSide targetSide = side.value_or(activeSide);
    auto& p = pane(targetSide);
    std::string targetPath = path.empty() ? p.activeTab().currentPath : path;
    auto& newTab = p.addTab(targetPath);
    activeSide = targetSide;
    loadDirectory(targetSide, newTab.id, targetPath, false);
}

void CommanderEngine::closeTab(uint64_t tabID, PaneSide side) {
    pane(side).closeTab(tabID);
}

void CommanderEngine::selectTab(uint64_t tabID, PaneSide side) {
    pane(side).selectTab(tabID);
    activeSide = side;
}

void CommanderEngine::swapPanes() {
    std::string leftPath = leftPane.activeTab().currentPath;
    std::string rightPath = rightPane.activeTab().currentPath;
    navigate(rightPath, PaneSide::Left, true);
    navigate(leftPath, PaneSide::Right, true);
    showToast("Zamieniono panele (⌘U)");
}

void CommanderEngine::syncInactivePaneToActive() {
    std::string activePath = activePane().activeTab().currentPath;
    PaneSide targetSide = oppositeSide(activeSide);
    navigate(activePath, targetSide, true);
    showToast("Target = Source");
}

void CommanderEngine::compareDirectories() {
    auto& lTab = leftPane.activeTab();
    auto& rTab = rightPane.activeTab();
    auto [lDiffs, rDiffs] = ToolsService::compareDirectories(lTab.items, rTab.items);

    lTab.highlightedDiffPaths = lDiffs;
    rTab.highlightedDiffPaths = rDiffs;
    lTab.selectedPaths = lDiffs;
    rTab.selectedPaths = rDiffs;

    size_t total = lDiffs.size() + rDiffs.size();
    if (total == 0) {
        showToast("Katalogi są identyczne");
    } else {
        showToast("Zaznaczono " + std::to_string(total) + " różniących się plików");
    }
}

// MARK: - Triggers

void CommanderEngine::triggerF3View() {
    auto& tab = activePane().activeTab();
    const FileItem* item = tab.primaryTargetItem();
    if (!item) return;

    if (item->isNavigableDirectory()) {
        PaneSide s = activeSide;
        uint64_t tabID = tab.id;
        std::string dirPath = item->path;
        std::string dirName = item->name;
        showToast("Obliczanie rozmiaru „" + dirName + "”…");
        spawnWorker([this, s, tabID, dirPath, dirName]() {
            int64_t bytes = FileSystemService::calculateDirectorySizeSync(dirPath);
            postMain([this, s, tabID, dirPath, dirName, bytes]() {
                for (auto& t : pane(s).tabs) {
                    if (t.id == tabID) {
                        t.directorySizes[dirPath] = bytes;
                        if (t.sortField == FileSortField::Size) t.recomputeDisplayedItems();
                        break;
                    }
                }
                showToast(dirName + ": " + formatByteCount(bytes));
            });
        });
        return;
    }

    if (tab.isRemote() && !item->isNavigableDirectory()) {
        RemoteConnection conn = *tab.remote;
        FileItem remoteItem = *item;
        showToast("Pobieranie do podglądu: " + remoteItem.name + "…");
        spawnWorker([this, conn, remoteItem]() {
            std::string tmpDir = "/tmp";
            std::string err;
            bool ok = ToolsService::downloadFromRemoteSync(conn, {remoteItem.path}, {false}, tmpDir, err);
            postMain([this, ok, err = std::move(err), remoteItem, tmpPath = "/tmp/" + remoteItem.name]() {
                if (!ok) {
                    errorAlertMessage = err;
                    return;
                }
                FileItem localView = remoteItem;
                localView.path = tmpPath;
                openViewer(localView);
            });
        });
        return;
    }

    openViewer(*item);
}

void CommanderEngine::triggerQuickLook() {
    const FileItem* item = activePane().activeTab().primaryTargetItem();
    if (item) {
        platform::showQuickLook(item->path);
    }
}

void CommanderEngine::triggerF4Edit() {
    const FileItem* item = activePane().activeTab().primaryTargetItem();
    if (!item) return;
    if (item->isNavigableDirectory()) {
        showToast("Nie można edytować folderu");
        return;
    }
    if (!item->isTextEditable()) {
        platform::openItem(item->path);
        return;
    }
    openEditor(*item);
}

void CommanderEngine::triggerF5Copy() {
    auto& tab = activePane().activeTab();
    auto& oppTab = inactivePane().activeTab();
    auto items = tab.selectedItems();
    if (items.empty()) {
        showToast("Brak wybranych plików do skopiowania");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::TransferConfirm;
    dialog.targetItems = std::move(items);
    dialog.destinationPath = oppTab.isRemote() ? oppTab.remote->uri() : oppTab.currentPath;
    dialog.isMove = false;
    dialog.conflictPolicy = ConflictResolutionPolicy::Ask;
    dialog.transferInBackground = false;
    if (tab.isInsideArchive()) {
        dialog.fromArchivePath = tab.archivePath;
    }
    if (tab.isRemote()) {
        dialog.fromRemote = tab.remote;
    }
    if (oppTab.isRemote()) {
        dialog.toRemote = oppTab.remote;
    }
}

void CommanderEngine::triggerF6Move() {
    if (activePane().activeTab().isInsideArchive()) {
        showToast("Przenoszenie bezpośrednio z archiwum nie jest obsługiwane — użyj F5");
        return;
    }
    if (activePane().activeTab().isRemote() || inactivePane().activeTab().isRemote()) {
        showToast("Dla SFTP użyj F5 (Kopiuj) oraz F8 (Usuń)");
        return;
    }
    auto items = activePane().activeTab().selectedItems();
    if (items.empty()) {
        showToast("Brak wybranych plików do przeniesienia");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::TransferConfirm;
    dialog.targetItems = std::move(items);
    dialog.destinationPath = inactivePane().activeTab().currentPath;
    dialog.isMove = true;
    dialog.conflictPolicy = ConflictResolutionPolicy::Ask;
    dialog.transferInBackground = false;
}

void CommanderEngine::triggerNewFile() {
    if (activePane().activeTab().isInsideArchive()) {
        showToast("Nie można tworzyć plików wewnątrz archiwum");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::CreateFile;
    dialog.side = activeSide;
    dialog.parentPath = activePane().activeTab().currentPath;
    dialog.inputBuffer = "nowy_plik.txt";
}

void CommanderEngine::createEmptyFileAndEdit(const std::string& name, const std::string& parentPath, PaneSide side) {
    if (name.empty()) {
        showToast("Podaj nazwę pliku");
        return;
    }
    std::string targetPath = (fs::path(parentPath) / name).string();
    std::error_code ec;
    if (!fs::exists(targetPath, ec)) {
        std::ofstream out(targetPath, std::ios::binary);
        if (!out) {
            errorAlertMessage = "Nie udało się utworzyć pliku: " + targetPath;
            dialog.kind = DialogKind::None;
            return;
        }
    }
    dialog.kind = DialogKind::None;
    refreshPane(side);
    FileItem item;
    item.name = name;
    item.path = targetPath;
    item.isDirectory = false;
    openEditor(item);
}

void CommanderEngine::triggerRename() {
    const FileItem* item = activePane().activeTab().primaryTargetItem();
    if (!item) return;
    dialog = DialogState{};
    dialog.kind = DialogKind::Rename;
    dialog.side = activeSide;
    dialog.targetItem = *item;
    dialog.inputBuffer = item->name;
}

void CommanderEngine::triggerF7NewFolder() {
    dialog = DialogState{};
    dialog.kind = DialogKind::NewFolder;
    dialog.side = activeSide;
    dialog.parentPath = activePane().activeTab().currentPath;
    dialog.inputBuffer = "Nowy folder";
}

void CommanderEngine::triggerF8Delete() {
    auto items = activePane().activeTab().selectedItems();
    if (items.empty()) {
        showToast("Brak zaznaczonych pozycji");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::DeleteConfirm;
    dialog.side = activeSide;
    dialog.targetItems = std::move(items);
}

void CommanderEngine::triggerGoToPath() {
    dialog = DialogState{};
    dialog.kind = DialogKind::GoToPath;
    dialog.side = activeSide;
    auto& tab = activePane().activeTab();
    dialog.inputBuffer = tab.isRemote() ? tab.remote->uri() : tab.currentPath;
}

void CommanderEngine::triggerConnectRemote() {
    dialog = DialogState{};
    dialog.kind = DialogKind::ConnectRemote;
    dialog.side = activeSide;
    auto& tab = activePane().activeTab();
    dialog.inputBuffer = tab.isRemote() ? tab.remote->uri() : "sh://admin@192.168.88.1/";
}

void CommanderEngine::triggerSelectByPattern(bool isSelecting) {
    dialog = DialogState{};
    dialog.kind = DialogKind::SelectByPattern;
    dialog.isSelectingPattern = isSelecting;
    dialog.inputBuffer = "*.*";
}

void CommanderEngine::triggerMultiRename() {
    auto items = activePane().activeTab().selectedItems();
    if (items.empty()) {
        showToast("Zaznacz pliki do wielokrotnej zmiany nazw");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::MultiRename;
    dialog.targetItems = std::move(items);
    dialog.multiRenameConfig = MultiRenameConfig{};
    updateMultiRenamePreview();
}

void CommanderEngine::triggerFindFiles() {
    dialog = DialogState{};
    dialog.kind = DialogKind::SearchFiles;
    dialog.searchRootPath = activePane().activeTab().currentPath;
    dialog.searchPattern = "*";
}

void CommanderEngine::triggerSyncDirs() {
    dialog = DialogState{};
    dialog.kind = DialogKind::SyncDirs;
    dialog.syncLeftDir = leftPane.activeTab().currentPath;
    dialog.syncRightDir = rightPane.activeTab().currentPath;
    dialog.syncRows = ToolsService::buildSyncComparison(leftPane.activeTab().items, rightPane.activeTab().items);
}

void CommanderEngine::triggerPackArchive() {
    auto items = activePane().activeTab().selectedItems();
    if (items.empty()) {
        showToast("Zaznacz pliki do spakowania (⌥F5)");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::PackArchive;
    dialog.targetItems = std::move(items);
    std::string baseName = dialog.targetItems.size() == 1
        ? fs::path(dialog.targetItems.front().path).stem().string()
        : fs::path(activePane().activeTab().currentPath).filename().string();
    if (baseName.empty()) baseName = "archiwum";
    dialog.packConfig = PackArchiveConfig{};
    dialog.packConfig.format = ArchiveFormat::SevenZip;
    dialog.destinationPath = (fs::path(inactivePane().activeTab().currentPath) / (baseName + ".7z")).string();
}

void CommanderEngine::triggerUnpackArchive() {
    const FileItem* item = activePane().activeTab().primaryTargetItem();
    if (!item || !item->isArchive()) {
        showToast("Ustaw kursor na archiwum (.7z, .zip, .tar.gz, .rar) aby rozpakować (⌥F9)");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::UnpackArchive;
    dialog.targetItem = *item;
    dialog.destinationPath = inactivePane().activeTab().currentPath;
}

void CommanderEngine::triggerChmod() {
    auto items = activePane().activeTab().selectedItems();
    if (items.empty()) {
        showToast("Zaznacz elementy do zmiany atrybutów");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::Chmod;
    dialog.chmodMode = items.front().posixMode != 0 ? items.front().posixMode : 0755;
    dialog.chmodRecursive = false;
    dialog.targetItems = std::move(items);
}

void CommanderEngine::triggerChecksums() {
    auto items = activePane().activeTab().selectedItems();
    std::vector<std::string> filePaths;
    for (const auto& it : items) {
        if (!it.isNavigableDirectory()) filePaths.push_back(it.path);
    }
    if (filePaths.empty()) {
        showToast("Zaznacz pliki do obliczenia sum kontrolnych (SHA-256 / MD5)");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::Checksums;
    dialog.isComputingChecksums = true;

    spawnWorker([this, filePaths = std::move(filePaths)]() {
        auto entries = ToolsService::computeChecksumsSync(filePaths);
        postMain([this, entries = std::move(entries)]() mutable {
            if (dialog.kind == DialogKind::Checksums) {
                dialog.checksumEntries = std::move(entries);
                dialog.isComputingChecksums = false;
            }
        });
    });
}

void CommanderEngine::triggerF1Help() {
    dialog = DialogState{};
    dialog.kind = DialogKind::Help;
}

void CommanderEngine::triggerF2UserMenu() {
    dialog = DialogState{};
    dialog.kind = DialogKind::UserMenu;
    const FileItem* item = activePane().activeTab().primaryTargetItem();
    if (item) dialog.targetItem = *item;
    dialog.userMenuItems = ToolsService::loadUserMenu();
}

void CommanderEngine::executeUserMenuItem(const UserMenuItem& menuItem) {
    std::string cmd = menuItem.command;
    if (cmd == "builtin:pack_7z") { triggerPackArchive(); return; }
    if (cmd == "builtin:unpack_7z") { triggerUnpackArchive(); return; }
    if (cmd == "builtin:diff") { triggerDiffFiles(); return; }
    if (cmd == "builtin:symlink") { triggerCreateLink(false, false); return; }
    if (cmd == "builtin:rel_symlink") { triggerCreateLink(false, true); return; }
    if (cmd == "builtin:hardlink") { triggerCreateLink(true, false); return; }
    if (cmd == "builtin:chmod") { triggerChmod(); return; }
    if (cmd == "builtin:checksums") { triggerChecksums(); return; }
    if (cmd == "builtin:rename") { triggerRename(); return; }

    auto quote = [](const std::string& s) -> std::string {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out.push_back(c);
        }
        out.push_back('\'');
        return out;
    };

    const FileItem* cur = activePane().activeTab().primaryTargetItem();
    std::string fileName = cur ? cur->name : "";
    std::string filePath = cur ? cur->path : "";
    std::string activeDir = activePane().activeTab().currentPath;
    std::string oppDir = inactivePane().activeTab().currentPath;

    auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replaceAll(cmd, "%f", quote(fileName));
    replaceAll(cmd, "%p", quote(filePath));
    replaceAll(cmd, "%d", quote(activeDir));
    replaceAll(cmd, "%D", quote(oppDir));

    dialog.kind = DialogKind::None;
    runQuickCommandInActiveDir(cmd, menuItem.label);
}

void CommanderEngine::triggerCreateLink(bool hardLink, bool relativeSymlink) {
    const FileItem* item = activePane().activeTab().primaryTargetItem();
    if (!item) {
        showToast("Wybierz plik lub katalog źródłowy");
        return;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::CreateLink;
    dialog.targetItem = *item;
    dialog.inputBuffer = item->path; // source
    dialog.destinationPath = (fs::path(inactivePane().activeTab().currentPath) / item->name).string();
    dialog.linkIsHard = hardLink;
    dialog.linkIsRelative = relativeSymlink;
}

void CommanderEngine::triggerDiffFiles() {
    const FileItem* leftItem = leftPane.activeTab().primaryTargetItem();
    const FileItem* rightItem = rightPane.activeTab().primaryTargetItem();

    std::string lPath;
    std::string rPath;

    if (activeSide == PaneSide::Left && leftItem && !leftItem->isNavigableDirectory()) {
        lPath = leftItem->path;
        // Look for same name in right pane first, fallback to right cursor item
        for (const auto& it : rightPane.activeTab().displayedItems) {
            if (!it.isNavigableDirectory() && it.name == leftItem->name) {
                rPath = it.path;
                break;
            }
        }
        if (rPath.empty() && rightItem && !rightItem->isNavigableDirectory()) {
            rPath = rightItem->path;
        }
    } else if (activeSide == PaneSide::Right && rightItem && !rightItem->isNavigableDirectory()) {
        rPath = rightItem->path;
        for (const auto& it : leftPane.activeTab().displayedItems) {
            if (!it.isNavigableDirectory() && it.name == rightItem->name) {
                lPath = it.path;
                break;
            }
        }
        if (lPath.empty() && leftItem && !leftItem->isNavigableDirectory()) {
            lPath = leftItem->path;
        }
    }

    if (lPath.empty() || rPath.empty()) {
        showToast("Wybierz po jednym pliku w lewym i prawym panelu do porównania (diff)");
        return;
    }

    dialog = DialogState{};
    dialog.kind = DialogKind::DiffViewer;
    dialog.diffLeftPath = lPath;
    dialog.diffRightPath = rPath;
    dialog.diffOutput = "Porównywanie plików…";
    spawnWorker([this, lPath, rPath]() {
        std::string out = ToolsService::diffFilesSync(lPath, rPath);
        postMain([this, lPath, rPath, out = std::move(out)]() mutable {
            if (dialog.kind == DialogKind::DiffViewer && dialog.diffLeftPath == lPath && dialog.diffRightPath == rPath) {
                dialog.diffOutput = std::move(out);
            }
        });
    });
}

// MARK: - File Operations

void CommanderEngine::createFolder(const std::string& name, const std::string& parentPath, PaneSide side) {
    auto& tab = pane(side).activeTab();
    if (tab.isRemote()) {
        RemoteConnection conn = *tab.remote;
        dialog.kind = DialogKind::None;
        spawnWorker([this, conn, name, side, tabID = tab.id]() {
            std::string err;
            bool ok = ToolsService::createRemoteDirectorySync(conn, name, err);
            postMain([this, ok, err = std::move(err), name, side, tabID, conn]() {
                if (ok) {
                    showToast("Utworzono zdalny katalog: " + name);
                    loadRemoteDirectory(side, tabID, conn);
                } else {
                    errorAlertMessage = err;
                }
            });
        });
        return;
    }

    std::string createdPath, err;
    if (FileSystemService::createDirectorySync(name, parentPath, createdPath, err)) {
        dialog.kind = DialogKind::None;
        showToast("Utworzono katalog: " + fs::path(createdPath).filename().string());
        loadDirectory(side, pane(side).activeTab().id, parentPath, true, createdPath);
        refreshOppositeIfSameFolder(parentPath, side);
    } else {
        errorAlertMessage = err;
    }
}

void CommanderEngine::renameItem(const FileItem& item, const std::string& newName, PaneSide side) {
    auto& tab = pane(side).activeTab();
    if (tab.isRemote()) {
        RemoteConnection conn = *tab.remote;
        dialog.kind = DialogKind::None;
        spawnWorker([this, conn, oldPath = item.path, newName, side, tabID = tab.id]() {
            std::string err;
            bool ok = ToolsService::renameRemoteItemSync(conn, oldPath, newName, err);
            postMain([this, ok, err = std::move(err), newName, side, tabID, conn]() {
                if (ok) {
                    showToast("Zmieniono nazwę na: " + newName);
                    loadRemoteDirectory(side, tabID, conn);
                } else {
                    errorAlertMessage = err;
                }
            });
        });
        return;
    }

    std::string destPath, err;
    if (FileSystemService::renameItemSync(item.path, newName, destPath, err)) {
        dialog.kind = DialogKind::None;
        showToast("Zmieniono nazwę na: " + fs::path(destPath).filename().string());
        loadDirectory(side, pane(side).activeTab().id, pane(side).activeTab().currentPath, true, destPath);
        refreshOppositeIfSameFolder(fs::path(item.path).parent_path().string(), side);
    } else {
        errorAlertMessage = err;
    }
}

void CommanderEngine::createLink() {
    std::string err;
    if (FileSystemService::createLinkSync(
            dialog.inputBuffer,
            dialog.destinationPath,
            dialog.linkIsHard,
            dialog.linkIsRelative,
            err)) {
        dialog.kind = DialogKind::None;
        showToast(dialog.linkIsHard ? "Utworzono dowiązanie twarde (hardlink)" : "Utworzono dowiązanie symboliczne (symlink)");
        refreshAllVisibleTabs();
    } else {
        errorAlertMessage = err;
    }
}

void CommanderEngine::runQuickCommandInActiveDir(const std::string& cmd, const std::string& title) {
    dialog.kind = DialogKind::None;
    std::string cwd = activePane().activeTab().currentPath;
    isRunningCommand = true;
    isCommandOutputVisible = true;
    showToast(title);

    spawnWorker([this, cmd, cwd]() {
        auto result = ToolsService::runShellCommandSync(cmd, cwd);
        postMain([this, result = std::move(result)]() mutable {
            lastCommandResult = std::move(result);
            isRunningCommand = false;
            refreshAllVisibleTabs();
        });
    });
}

void CommanderEngine::deleteItems(const std::vector<FileItem>& items, bool permanently, PaneSide side) {
    auto& tab = pane(side).activeTab();
    if (tab.isRemote()) {
        std::vector<std::string> remotePaths;
        std::vector<bool> dirFlags;
        for (const auto& it : items) {
            if (!it.isParentEntry) {
                remotePaths.push_back(it.path);
                dirFlags.push_back(it.isNavigableDirectory());
            }
        }
        RemoteConnection conn = *tab.remote;
        dialog.kind = DialogKind::None;
        spawnWorker([this, conn, remotePaths = std::move(remotePaths), dirFlags = std::move(dirFlags), side, tabID = tab.id]() {
            std::string err;
            bool ok = ToolsService::deleteRemoteItemsSync(conn, remotePaths, dirFlags, err);
            postMain([this, ok, err = std::move(err), count = remotePaths.size(), side, tabID, conn]() {
                if (ok) {
                    showToast("Usunięto ze zdalnego serwera: " + std::to_string(count) + " el.");
                    loadRemoteDirectory(side, tabID, conn);
                } else {
                    errorAlertMessage = err;
                }
            });
        });
        return;
    }

    std::vector<std::string> paths;
    paths.reserve(items.size());
    for (const auto& it : items) {
        if (!it.isParentEntry) paths.push_back(it.path);
    }
    dialog.kind = DialogKind::None;

    spawnWorker([this, paths = std::move(paths), permanently, side]() {
        std::string err;
        bool ok = permanently
            ? FileSystemService::permanentlyDeleteSync(paths, err)
            : FileSystemService::trashItemsSync(paths, err);
        postMain([this, ok, err, permanently, count = paths.size(), side]() {
            if (ok) {
                showToast(permanently
                    ? ("Trwale usunięto " + std::to_string(count) + " el.")
                    : ("Przeniesiono do Kosza: " + std::to_string(count) + " el."));
                refreshPane(side);
                refreshOppositeIfSameFolder(pane(side).activeTab().currentPath, side);
            } else {
                errorAlertMessage = err;
            }
        });
    });
}

void CommanderEngine::startTransfer(
    const std::vector<std::string>& sourcePaths,
    const std::string& destDir,
    bool isMove,
    ConflictResolutionPolicy policy,
    bool inBackground
) {
    if (transferCancelFlag_) transferCancelFlag_->store(true);
    transferCancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = transferCancelFlag_;

    dialog.kind = DialogKind::None;
    operationProgress = FileOperationProgress{
        isMove ? "Przenoszenie…" : "Kopiowanie…",
        "",
        0,
        static_cast<int>(sourcePaths.size()),
        0,
        0,
        inBackground
    };

    spawnWorker([this, sourcePaths, destDir, isMove, policy, inBackground, cancel]() {
        auto outcome = FileSystemService::transferItemsSync(
            sourcePaths, destDir, isMove, policy, *cancel,
            [this, inBackground](const FileOperationProgress& prog) {
                postMain([this, prog, inBackground]() {
                    bool bg = operationProgress.has_value() ? operationProgress->inBackground : inBackground;
                    operationProgress = prog;
                    operationProgress->inBackground = bg;
                });
            }
        );

        postMain([this, outcome = std::move(outcome), destDir, isMove, totalCount = sourcePaths.size()]() mutable {
            operationProgress.reset();
            switch (outcome.status) {
                case TransferStatus::Success:
                    showToast((isMove ? "Przeniesiono " : "Skopiowano ") + std::to_string(totalCount) + " el.");
                    activePane().activeTab().deselectAll();
                    refreshAllVisibleTabs();
                    break;
                case TransferStatus::Cancelled:
                    showToast("Operacja anulowana");
                    refreshAllVisibleTabs();
                    break;
                case TransferStatus::ConflictNeedsDecision:
                    dialog = DialogState{};
                    dialog.kind = DialogKind::Conflict;
                    dialog.conflictSourcePath = std::move(outcome.conflictSourcePath);
                    dialog.conflictDestPath = std::move(outcome.conflictDestPath);
                    dialog.conflictRemainingPaths = std::move(outcome.remainingSourcePaths);
                    dialog.destinationPath = destDir;
                    dialog.isMove = isMove;
                    break;
                case TransferStatus::Error:
                    errorAlertMessage = outcome.errorMessage;
                    refreshAllVisibleTabs();
                    break;
            }
        });
    });
}

void CommanderEngine::moveTransferToBackground() {
    if (operationProgress.has_value()) {
        operationProgress->inBackground = true;
        showToast("Operacja przeniesiona do tła");
    }
}

void CommanderEngine::startArchiveExtract(const std::string& archivePath, const std::vector<std::string>& internalPaths, const std::string& destDir, bool inBackground) {
    dialog.kind = DialogKind::None;
    operationProgress = FileOperationProgress{"Rozpakowywanie archiwum…", fs::path(archivePath).filename().string(), 0, static_cast<int>(internalPaths.size()), 0, 0, inBackground};
    spawnWorker([this, archivePath, internalPaths, destDir]() {
        std::string err;
        bool ok = ToolsService::extractArchiveEntriesSync(archivePath, internalPaths, destDir, "", err);
        postMain([this, ok, err = std::move(err), cnt = internalPaths.size()]() {
            operationProgress.reset();
            if (ok) {
                showToast("Rozpakowano " + std::to_string(cnt) + " el.");
                refreshAllVisibleTabs();
            } else {
                errorAlertMessage = err.empty() ? "Błąd podczas rozpakowywania z archiwum" : err;
            }
        });
    });
}

void CommanderEngine::startRemoteTransfer(
    const std::optional<RemoteConnection>& fromRemote,
    const std::optional<RemoteConnection>& toRemote,
    const std::vector<FileItem>& items,
    const std::string& localDestDir,
    bool inBackground
) {
    dialog.kind = DialogKind::None;
    if (fromRemote.has_value() && toRemote.has_value()) {
        showToast("Bezpośrednie kopiowanie SFTP -> SFTP nie jest jeszcze obsługiwane — pobierz lokalnie na Maca");
        return;
    }
    if (items.empty()) return;

    bool isDownload = fromRemote.has_value();
    std::string title = isDownload ? "Pobieranie SFTP…" : "Wysyłanie SFTP…";
    operationProgress = FileOperationProgress{title, items.front().name, 0, static_cast<int>(items.size()), 0, 0, inBackground};

    std::vector<std::string> paths;
    std::vector<bool> dirFlags;
    for (const auto& it : items) {
        if (!it.isParentEntry) {
            paths.push_back(it.path);
            dirFlags.push_back(it.isNavigableDirectory());
        }
    }

    spawnWorker([this, isDownload, fromRemote, toRemote, paths = std::move(paths), dirFlags = std::move(dirFlags), localDestDir]() {
        std::string err;
        bool ok = isDownload
            ? ToolsService::downloadFromRemoteSync(*fromRemote, paths, dirFlags, localDestDir, err)
            : ToolsService::uploadToRemoteSync(*toRemote, paths, toRemote->remotePath, err);

        postMain([this, ok, err = std::move(err), isDownload, count = paths.size()]() {
            operationProgress.reset();
            if (ok) {
                showToast((isDownload ? "Pobrano SFTP: " : "Wysłano SFTP: ") + std::to_string(count) + " el.");
                activePane().activeTab().deselectAll();
                refreshAllVisibleTabs();
            } else {
                errorAlertMessage = err;
            }
        });
    });
}

void CommanderEngine::handleDroppedFiles(const std::vector<std::string>& paths, PaneSide targetSide, bool isMove) {
    if (paths.empty()) return;
    activeSide = targetSide;
    auto& tab = pane(targetSide).activeTab();
    if (tab.isInsideArchive()) {
        showToast("Nie można upuszczać plików bezpośrednio do archiwum");
        return;
    }
    startTransfer(paths, tab.currentPath, isMove, ConflictResolutionPolicy::Ask, false);
}

void CommanderEngine::resolveConflict(ConflictResolutionPolicy policy) {
    std::string src = dialog.conflictSourcePath;
    std::string destDir = dialog.destinationPath;
    auto remaining = dialog.conflictRemainingPaths;
    bool isMove = dialog.isMove;
    dialog.kind = DialogKind::None;

    if (policy == ConflictResolutionPolicy::Skip) {
        if (!remaining.empty()) {
            startTransfer(remaining, destDir, isMove, ConflictResolutionPolicy::Ask);
        } else {
            refreshAllVisibleTabs();
        }
        return;
    }

    std::vector<std::string> batch;
    batch.push_back(src);
    batch.insert(batch.end(), remaining.begin(), remaining.end());
    startTransfer(batch, destDir, isMove, policy);
}

void CommanderEngine::cancelActiveTransfer() {
    if (transferCancelFlag_) {
        transferCancelFlag_->store(true);
    }
    operationProgress.reset();
    showToast("Anulowano operację");
}

void CommanderEngine::applyPatternSelection(const std::string& pattern, bool isSelecting) {
    int count = activePane().activeTab().selectByPattern(pattern, isSelecting);
    dialog.kind = DialogKind::None;
    showToast(isSelecting
        ? ("Zaznaczono " + std::to_string(count) + " el. dla „" + pattern + "”")
        : ("Odznaczono " + std::to_string(count) + " el."));
}

void CommanderEngine::updateMultiRenamePreview() {
    dialog.multiRenamePreview = ToolsService::previewMultiRename(dialog.targetItems, dialog.multiRenameConfig);
}

void CommanderEngine::applyMultiRename() {
    std::string err;
    int count = ToolsService::applyMultiRename(dialog.multiRenamePreview, err);
    if (!err.empty()) {
        errorAlertMessage = err;
    } else {
        dialog.kind = DialogKind::None;
        showToast("Zmieniono nazwy " + std::to_string(count) + " plików");
        refreshPane(activeSide);
    }
}

void CommanderEngine::executeSyncDirs() {
    std::string leftDir = dialog.syncLeftDir;
    std::string rightDir = dialog.syncRightDir;
    auto rows = dialog.syncRows;
    dialog.kind = DialogKind::None;

    spawnWorker([this, leftDir, rightDir, rows = std::move(rows)]() {
        int copied = 0;
        std::error_code ec;
        for (const auto& row : rows) {
            if (!row.selected || row.direction == 0) continue;
            if (row.direction > 0 && !row.leftPath.empty()) {
                fs::path dst = fs::path(rightDir) / row.relativeName;
                fs::copy_file(row.leftPath, dst, fs::copy_options::overwrite_existing, ec);
                if (!ec) ++copied;
            } else if (row.direction < 0 && !row.rightPath.empty()) {
                fs::path dst = fs::path(leftDir) / row.relativeName;
                fs::copy_file(row.rightPath, dst, fs::copy_options::overwrite_existing, ec);
                if (!ec) ++copied;
            }
        }
        postMain([this, copied]() {
            showToast("Zsynchronizowano " + std::to_string(copied) + " plików");
            refreshAllVisibleTabs();
        });
    });
}

void CommanderEngine::executePackArchive() {
    if (!dialog.packConfig.password.empty() &&
        dialog.packConfig.password != dialog.packConfig.passwordConfirm) {
        errorAlertMessage = "Podane hasła do archiwum nie są identyczne.";
        return;
    }
    std::vector<std::string> sources;
    for (const auto& it : dialog.targetItems) {
        if (!it.isParentEntry) sources.push_back(it.path);
    }
    std::string outPath = dialog.destinationPath;
    PackArchiveConfig cfg = dialog.packConfig;
    dialog.kind = DialogKind::None;
    showToast("Pakowanie archiwum…");

    spawnWorker([this, sources = std::move(sources), outPath, cfg]() {
        std::string err;
        bool ok = ToolsService::packArchiveSync(sources, outPath, cfg, err);
        postMain([this, ok, err, outPath]() {
            if (ok) {
                showToast("Utworzono archiwum: " + fs::path(outPath).filename().string());
                refreshAllVisibleTabs();
            } else {
                errorAlertMessage = err;
            }
        });
    });
}

void CommanderEngine::executeUnpackArchive() {
    std::string archPath = dialog.targetItem.path;
    std::string destDir = dialog.destinationPath;
    std::string pwd = dialog.unpackPassword;
    FileItem targetCopy = dialog.targetItem;
    dialog.kind = DialogKind::None;
    showToast("Rozpakowywanie archiwum…");

    spawnWorker([this, archPath, destDir, pwd, targetCopy]() {
        std::string err;
        bool ok = ToolsService::unpackArchiveSync(archPath, destDir, pwd, err);
        postMain([this, ok, err, destDir, targetCopy]() {
            if (ok) {
                showToast("Rozpakowano archiwum");
                refreshAllVisibleTabs();
            } else {
                // Re-open unpack dialog if password was needed or wrong
                dialog = DialogState{};
                dialog.kind = DialogKind::UnpackArchive;
                dialog.targetItem = targetCopy;
                dialog.destinationPath = destDir;
                errorAlertMessage = err;
            }
        });
    });
}

void CommanderEngine::executeChmod() {
    std::vector<std::string> paths;
    for (const auto& it : dialog.targetItems) {
        if (!it.isParentEntry) paths.push_back(it.path);
    }
    uint16_t mode = dialog.chmodMode;
    bool rec = dialog.chmodRecursive;
    dialog.kind = DialogKind::None;

    std::string err;
    if (ToolsService::changePermissionsSync(paths, mode, rec, err)) {
        showToast("Zmieniono atrybuty POSIX");
        refreshAllVisibleTabs();
    } else {
        errorAlertMessage = err;
    }
}

void CommanderEngine::startSearch() {
    stopSearch();
    dialog.searchResults.clear();
    dialog.isSearching = true;
    dialog.searchCancelFlag = std::make_shared<std::atomic<bool>>(false);
    auto cancel = dialog.searchCancelFlag;

    std::string root = dialog.searchRootPath;
    std::string pattern = dialog.searchPattern;
    std::string content = dialog.searchContent;
    bool hidden = dialog.searchIncludeHidden;

    spawnWorker([this, root, pattern, content, hidden, cancel]() {
        ToolsService::searchFilesSync(root, pattern, content, hidden, *cancel, [this, cancel](SearchResultItem item) {
            if (cancel->load(std::memory_order_relaxed)) return;
            postMain([this, item = std::move(item), cancel]() mutable {
                if (dialog.kind == DialogKind::SearchFiles && dialog.searchCancelFlag == cancel) {
                    dialog.searchResults.push_back(std::move(item));
                }
            });
        });
        postMain([this, cancel]() {
            if (dialog.kind == DialogKind::SearchFiles && dialog.searchCancelFlag == cancel) {
                dialog.isSearching = false;
            }
        });
    });
}

void CommanderEngine::stopSearch() {
    if (dialog.searchCancelFlag) {
        dialog.searchCancelFlag->store(true);
    }
    dialog.isSearching = false;
}

void CommanderEngine::navigateToSearchResult(const SearchResultItem& result) {
    stopSearch();
    dialog.kind = DialogKind::None;
    if (result.isDirectory) {
        navigate(result.path);
    } else {
        std::string folder = fs::path(result.path).parent_path().string();
        auto& tab = activePane().activeTab();
        tab.historyBack.push_back(tab.currentPath);
        tab.historyForward.clear();
        tab.currentPath = folder;
        tab.filterText.clear();
        loadDirectory(activeSide, tab.id, folder, false, result.path);
    }
}

void CommanderEngine::openViewer(const FileItem& item) {
    if (dialog.viewerImageTexture) {
        platform::releaseMetalTexture(dialog.viewerImageTexture);
        dialog.viewerImageTexture = nullptr;
    }
    dialog = DialogState{};
    dialog.kind = DialogKind::Viewer;
    dialog.targetItem = item;
    dialog.viewerHexMode = false;

    if (item.isArchive()) {
        dialog.viewerText = ToolsService::listArchiveContentsSync(item.path);
        dialog.viewerHexText = dialog.viewerText;
        return;
    }

    if (isImageExtension(item.name) && mtlDevice != nullptr) {
        int w = 0, h = 0;
        void* tex = platform::loadMetalTexture(item.path, mtlDevice, w, h);
        if (tex) {
            dialog.viewerIsImage = true;
            dialog.viewerImageTexture = tex;
            dialog.viewerImageWidth = w;
            dialog.viewerImageHeight = h;
            return;
        }
    }

    std::ifstream in(item.path, std::ios::binary);
    if (!in) {
        dialog.viewerText = "Nie udało się otworzyć pliku.";
        return;
    }

    constexpr size_t maxRead = 1'048'576;
    std::vector<uint8_t> bytes(maxRead);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(maxRead));
    bytes.resize(static_cast<size_t>(in.gcount()));

    dialog.viewerHexText = ToolsService::formatHexDump(bytes);

    bool looksBinary = false;
    size_t checkLen = std::min<size_t>(bytes.size(), 4096);
    for (size_t i = 0; i < checkLen; ++i) {
        if (bytes[i] == 0) {
            looksBinary = true;
            break;
        }
    }

    if (looksBinary) {
        dialog.viewerHexMode = true;
        dialog.viewerText = "(Plik binarny — przełączono na tryb HEX)";
    } else {
        dialog.viewerText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
}

void CommanderEngine::openEditor(const FileItem& item) {
    dialog = DialogState{};
    dialog.kind = DialogKind::Editor;
    dialog.targetItem = item;
    dialog.editorDirty = false;

    std::ifstream in(item.path, std::ios::binary);
    if (!in) {
        errorAlertMessage = "Nie udało się wczytać pliku do edycji.";
        dialog.kind = DialogKind::None;
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    dialog.editorBuffer = ss.str();
}

void CommanderEngine::saveEditor() {
    std::ofstream out(dialog.targetItem.path, std::ios::binary | std::ios::trunc);
    if (!out) {
        errorAlertMessage = "Nie udało się zapisać pliku.";
        return;
    }
    out.write(dialog.editorBuffer.data(), static_cast<std::streamsize>(dialog.editorBuffer.size()));
    dialog.editorDirty = false;
    showToast("Zapisano: " + dialog.targetItem.name);
    refreshPane(activeSide);
}

void CommanderEngine::insertCursorNameToCommandLine(bool fullPath) {
    const FileItem* cur = activePane().activeTab().cursorItem();
    if (!cur || cur->isParentEntry) return;
    std::string token = fullPath ? cur->path : cur->name;
    if (token.find(' ') != std::string::npos) {
        token = "\"" + token + "\"";
    }
    if (!commandInput.empty() && commandInput.back() != ' ') {
        commandInput.push_back(' ');
    }
    commandInput += token + " ";
    requestCommandFocus = true;
}

void CommanderEngine::runCommandInput() {
    std::string cmd = commandInput;
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.front()))) cmd.erase(cmd.begin());
    while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.back()))) cmd.pop_back();
    if (cmd.empty()) return;

    commandHistory.push_back(cmd);
    commandHistoryPos = -1;

    if (cmd == "cd" || cmd.rfind("cd ", 0) == 0) {
        std::string arg = cmd.size() > 3 ? cmd.substr(3) : platform::homeDirectory();
        while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front()))) arg.erase(arg.begin());
        if (arg == "~") arg = platform::homeDirectory();
        else if (arg.rfind("~/", 0) == 0) arg = platform::homeDirectory() + arg.substr(1);
        else if (arg.rfind("sh://", 0) == 0 || arg.rfind("ssh://", 0) == 0 || arg.rfind("sftp://", 0) == 0) {
            // Keep remote URI untouched
        }
        else if (!arg.empty() && arg.front() != '/') {
            arg = (fs::path(activePane().activeTab().currentPath) / arg).string();
        }
        commandInput.clear();
        navigate(arg);
        return;
    }

    std::string cwd = activePane().activeTab().currentPath;
    commandInput.clear();
    isRunningCommand = true;
    isCommandOutputVisible = true;

    spawnWorker([this, cmd, cwd]() {
        auto result = ToolsService::runShellCommandSync(cmd, cwd);
        postMain([this, result = std::move(result)]() mutable {
            lastCommandResult = std::move(result);
            isRunningCommand = false;
            refreshAllVisibleTabs();
        });
    });
}

void CommanderEngine::loadFavorites() {
    favorites = FileSystemService::defaultFavorites();
    auto custom = platform::loadCustomFavorites();
    favorites.insert(favorites.end(), custom.begin(), custom.end());
}

void CommanderEngine::pinFolderToFavorites(const std::string& path) {
    for (const auto& f : favorites) {
        if (f.path == path) {
            showToast("Katalog jest już na liście (⌘D)");
            return;
        }
    }
    FavoriteLocation loc;
    loc.id = "custom-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    loc.title = fs::path(path).filename().string();
    if (loc.title.empty()) loc.title = path;
    loc.path = path;
    loc.isSystemDefault = false;
    favorites.push_back(loc);

    std::vector<FavoriteLocation> custom;
    for (const auto& f : favorites) {
        if (!f.isSystemDefault) custom.push_back(f);
    }
    platform::saveCustomFavorites(custom);
    showToast("Dodano „" + loc.title + "” do Hotlisty (⌘D)");
}

void CommanderEngine::removeFavorite(const std::string& id) {
    std::erase_if(favorites, [&](const FavoriteLocation& f) {
        return !f.isSystemDefault && f.id == id;
    });
    std::vector<FavoriteLocation> custom;
    for (const auto& f : favorites) {
        if (!f.isSystemDefault) custom.push_back(f);
    }
    platform::saveCustomFavorites(custom);
}

void CommanderEngine::refreshVolumes() {
    volumes = platform::mountedVolumes();
}

void CommanderEngine::ejectVolume(const MountedVolume& volume) {
    std::string err;
    if (platform::ejectVolume(volume.path, err)) {
        showToast("Wysunięto dysk: " + volume.name);
        refreshVolumes();
    } else {
        errorAlertMessage = err;
    }
}

void CommanderEngine::copySelectedPathsToClipboard() {
    auto items = activePane().activeTab().selectedItems();
    if (items.empty()) {
        std::string current = activePane().activeTab().currentPath;
        platform::copyToClipboard(current);
        showToast("Skopiowano ścieżkę katalogu");
        return;
    }
    std::string joined;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) joined.push_back('\n');
        joined += items[i].path;
    }
    platform::copyToClipboard(joined);
    showToast(items.size() == 1 ? "Skopiowano ścieżkę" : ("Skopiowano " + std::to_string(items.size()) + " ścieżek"));
}

void CommanderEngine::showToast(const std::string& message) {
    toastMessage = message;
    toastExpiry_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(2600);
}

void CommanderEngine::refreshOppositeIfSameFolder(const std::string& folderPath, PaneSide sourceSide) {
    PaneSide opp = oppositeSide(sourceSide);
    if (normalizeDirPath(pane(opp).activeTab().currentPath) == normalizeDirPath(folderPath)) {
        refreshPane(opp);
    }
}

} // namespace mc
