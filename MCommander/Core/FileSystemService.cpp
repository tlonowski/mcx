#include "FileSystemService.hpp"
#include "../Platform/MacBridge.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fts.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <unordered_set>

namespace fs = std::filesystem;

namespace mc {

namespace {

std::string formatPosixPerms(mode_t mode, bool isDirectory, bool isSymlink) {
    char buf[11];
    buf[0] = isSymlink ? 'l' : (isDirectory ? 'd' : '-');
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? 'x' : '-';
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? 'x' : '-';
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';
    return std::string(buf);
}

bool isKnownPackageExtension(const std::string& extLower) {
    static const std::unordered_set<std::string> pkgExts = {
        "app", "bundle", "framework", "xcodeproj", "xcworkspace",
        "playground", "pkg", "kext", "plugin", "appex", "xpc",
        "rtfd", "pages", "numbers", "key", "photoslibrary"
    };
    return pkgExts.contains(extLower);
}

std::string uniqueDestinationPath(const fs::path& target) {
    fs::path dir = target.parent_path();
    std::string stem = target.stem().string();
    std::string ext = target.extension().string(); // includes leading dot
    std::error_code ec;
    int counter = 2;
    while (true) {
        std::string candidateName = stem + " " + std::to_string(counter) + ext;
        fs::path candidate = dir / candidateName;
        if (!fs::exists(candidate, ec)) {
            return candidate.string();
        }
        ++counter;
    }
}

} // namespace

std::vector<FileItem> FileSystemService::listDirectorySync(
    const std::string& directoryPath,
    bool showHidden,
    std::string& errorOut
) {
    std::vector<FileItem> items;
    DIR* dir = opendir(directoryPath.c_str());
    if (!dir) {
        errorOut = std::strerror(errno);
        return items;
    }

    std::string base = directoryPath;
    if (!base.empty() && base.back() != '/') {
        base.push_back('/');
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const char* dname = entry->d_name;
        if (std::strcmp(dname, ".") == 0 || std::strcmp(dname, "..") == 0) {
            continue;
        }
        bool hidden = (dname[0] == '.');
        if (!showHidden && hidden) {
            continue;
        }

        std::string fullPath = base + dname;
        struct stat lst{};
        if (lstat(fullPath.c_str(), &lst) != 0) {
            continue;
        }

        bool isSymlink = S_ISLNK(lst.st_mode);
        std::string linkTarget;
        if (isSymlink) {
            char lbuf[PATH_MAX];
            ssize_t len = readlink(fullPath.c_str(), lbuf, sizeof(lbuf) - 1);
            if (len > 0) {
                lbuf[len] = '\0';
                linkTarget.assign(lbuf, static_cast<size_t>(len));
            }
        }
        struct stat st = lst;
        if (isSymlink) {
            struct stat targetSt{};
            if (stat(fullPath.c_str(), &targetSt) == 0) {
                st = targetSt;
            }
        }

        bool isDir = S_ISDIR(st.st_mode);
        fs::path p(fullPath);
        std::string ext = p.extension().string();
        if (!ext.empty() && ext.front() == '.') {
            ext.erase(ext.begin());
        }

        bool isPkg = isDir && isKnownPackageExtension(toLowerASCII(ext));
        bool isNavDir = isDir && !isPkg;

        FileItem item;
        item.path = std::move(fullPath);
        item.name = dname;
        if (isNavDir || ext.empty() || item.name == ("." + ext)) {
            item.displayStem = item.name;
            item.fileExtension = isNavDir ? "" : ext;
        } else {
            item.displayStem = item.name.substr(0, item.name.size() - ext.size() - 1);
            item.fileExtension = std::move(ext);
        }
        item.isDirectory = isDir;
        item.isPackage = isPkg;
        item.isHidden = hidden;
        item.isSymbolicLink = isSymlink;
        item.symlinkTarget = std::move(linkTarget);
        item.inode = static_cast<uint64_t>(lst.st_ino);
        item.isExecutable = !isDir && ((lst.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0);
        item.posixMode = static_cast<uint16_t>(lst.st_mode & 0777);
        item.posixPermissions = formatPosixPerms(lst.st_mode, isDir, isSymlink);

        const struct passwd* pw = getpwuid(lst.st_uid);
        const struct group* gr = getgrgid(lst.st_gid);
        std::string uname = (pw && pw->pw_name) ? pw->pw_name : std::to_string(lst.st_uid);
        std::string gname = (gr && gr->gr_name) ? gr->gr_name : std::to_string(lst.st_gid);
        item.ownerGroup = uname + ":" + gname;
        item.typeDescription = isPkg ? "Pakiet" : (isDir ? "Folder" : (item.fileExtension.empty() ? "Dokument" : (toLowerASCII(item.fileExtension) + " plik")));
        if (!isNavDir) {
            item.size = static_cast<int64_t>(st.st_size);
        }
        item.modificationTime = st.st_mtime;
        item.creationTime = st.st_birthtimespec.tv_sec;
        items.push_back(std::move(item));
    }

    closedir(dir);
    return items;
}

std::optional<int64_t> FileSystemService::availableCapacity(const std::string& directoryPath) {
    struct statvfs vfs{};
    if (statvfs(directoryPath.c_str(), &vfs) != 0) {
        return std::nullopt;
    }
    return static_cast<int64_t>(vfs.f_bavail) * static_cast<int64_t>(vfs.f_frsize);
}

int64_t FileSystemService::calculateDirectorySizeSync(const std::string& directoryPath) {
    if (directoryPath.empty()) return 0;
    char* paths[2] = {const_cast<char*>(directoryPath.c_str()), nullptr};
    FTS* fts = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, nullptr);
    if (!fts) return 0;

    int64_t total = 0;
    while (FTSENT* ent = fts_read(fts)) {
        if (ent->fts_info == FTS_F && ent->fts_statp != nullptr) {
            total += static_cast<int64_t>(ent->fts_statp->st_size);
        }
    }
    fts_close(fts);
    return total;
}

std::vector<FavoriteLocation> FileSystemService::defaultFavorites() {
    std::string home = platform::homeDirectory();
    struct Candidate {
        const char* id;
        const char* title;
        std::string path;
    };
    std::vector<Candidate> candidates = {
        {"def-home", "Katalog domowy", home},
        {"def-desktop", "Biurko", home + "/Desktop"},
        {"def-docs", "Dokumenty", home + "/Documents"},
        {"def-downloads", "Pobrane", home + "/Downloads"},
        {"def-apps", "Aplikacje", "/Applications"},
        {"def-root", "Dysk główny (/)", "/"}
    };

    std::vector<FavoriteLocation> out;
    std::error_code ec;
    for (auto& c : candidates) {
        if (fs::exists(c.path, ec)) {
            out.push_back(FavoriteLocation{c.id, c.title, std::move(c.path), true});
        }
    }
    return out;
}

bool FileSystemService::createDirectorySync(
    const std::string& name,
    const std::string& parentPath,
    std::string& createdPathOut,
    std::string& errorOut
) {
    std::string trimmed = name;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    if (trimmed.empty()) {
        errorOut = "Nazwa nie może być pusta.";
        return false;
    }

    fs::path target = fs::path(parentPath) / trimmed;
    std::error_code ec;
    if (!fs::create_directory(target, ec)) {
        errorOut = ec ? ec.message() : "Nie udało się utworzyć folderu.";
        return false;
    }
    createdPathOut = target.string();
    return true;
}

bool FileSystemService::renameItemSync(
    const std::string& sourcePath,
    const std::string& newName,
    std::string& destPathOut,
    std::string& errorOut
) {
    std::string trimmed = newName;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
    if (trimmed.empty()) {
        errorOut = "Nazwa nie może być pusta.";
        return false;
    }

    fs::path src(sourcePath);
    fs::path dst = src.parent_path() / trimmed;
    std::error_code ec;
    fs::rename(src, dst, ec);
    if (ec) {
        errorOut = ec.message();
        return false;
    }
    destPathOut = dst.string();
    return true;
}

bool FileSystemService::createLinkSync(
    const std::string& targetPath,
    const std::string& linkPath,
    bool hardLink,
    bool relativeSymlink,
    std::string& errorOut
) {
    if (targetPath.empty() || linkPath.empty()) {
        errorOut = "Ścieżka docelowa i nazwa dowiązania nie mogą być puste.";
        return false;
    }
    if (hardLink) {
        if (link(targetPath.c_str(), linkPath.c_str()) != 0) {
            errorOut = std::strerror(errno);
            return false;
        }
        return true;
    }

    std::string effectiveTarget = targetPath;
    if (relativeSymlink) {
        std::error_code ec;
        fs::path rel = fs::relative(fs::path(targetPath), fs::path(linkPath).parent_path(), ec);
        if (!ec && !rel.empty()) {
            effectiveTarget = rel.string();
        }
    }
    if (symlink(effectiveTarget.c_str(), linkPath.c_str()) != 0) {
        errorOut = std::strerror(errno);
        return false;
    }
    return true;
}

bool FileSystemService::permanentlyDeleteSync(const std::vector<std::string>& paths, std::string& errorOut) {
    std::error_code ec;
    for (const auto& p : paths) {
        fs::remove_all(fs::path(p), ec);
        if (ec) {
            errorOut = ec.message();
            return false;
        }
    }
    return true;
}

bool FileSystemService::trashItemsSync(const std::vector<std::string>& paths, std::string& errorOut) {
    for (const auto& p : paths) {
        if (!platform::trashItem(p, errorOut)) {
            return false;
        }
    }
    return true;
}

TransferOutcome FileSystemService::transferItemsSync(
    const std::vector<std::string>& sourcePaths,
    const std::string& destinationDirectory,
    bool isMove,
    ConflictResolutionPolicy policy,
    const std::atomic<bool>& cancelFlag,
    const std::function<void(const FileOperationProgress&)>& onProgress
) {
    TransferOutcome outcome;
    int total = static_cast<int>(sourcePaths.size());
    std::string title = isMove ? "Przenoszenie…" : "Kopiowanie…";

    std::error_code ec;
    fs::path destDirCanon = fs::weakly_canonical(fs::path(destinationDirectory), ec);
    if (ec) destDirCanon = fs::path(destinationDirectory);

    for (size_t i = 0; i < sourcePaths.size(); ++i) {
        if (cancelFlag.load(std::memory_order_relaxed)) {
            outcome.status = TransferStatus::Cancelled;
            return outcome;
        }

        fs::path src(sourcePaths[i]);
        std::string itemName = src.filename().string();

        if (onProgress) {
            onProgress(FileOperationProgress{title, itemName, static_cast<int>(i), total});
        }

        fs::path srcCanon = fs::weakly_canonical(src, ec);
        if (ec) srcCanon = src;

        std::string srcStr = srcCanon.string();
        std::string destDirStr = destDirCanon.string();
        if (destDirStr == srcStr ||
            (destDirStr.size() > srcStr.size() &&
             destDirStr.compare(0, srcStr.size(), srcStr) == 0 &&
             destDirStr[srcStr.size()] == '/')) {
            outcome.status = TransferStatus::Error;
            outcome.errorMessage = "Nie można skopiować ani przenieść folderu „" + itemName + "” do jego własnego podfolderu.";
            return outcome;
        }

        fs::path target = fs::path(destinationDirectory) / itemName;
        fs::path targetCanon = fs::weakly_canonical(target, ec);
        if (ec) targetCanon = target;

        if (srcCanon == targetCanon) {
            if (policy == ConflictResolutionPolicy::KeepBoth) {
                target = uniqueDestinationPath(target);
            } else {
                continue;
            }
        }

        if (fs::exists(target, ec)) {
            switch (policy) {
                case ConflictResolutionPolicy::Ask: {
                    outcome.status = TransferStatus::ConflictNeedsDecision;
                    outcome.conflictSourcePath = sourcePaths[i];
                    outcome.conflictDestPath = target.string();
                    for (size_t j = i + 1; j < sourcePaths.size(); ++j) {
                        outcome.remainingSourcePaths.push_back(sourcePaths[j]);
                    }
                    return outcome;
                }
                case ConflictResolutionPolicy::Skip:
                    continue;
                case ConflictResolutionPolicy::Overwrite:
                    fs::remove_all(target, ec);
                    if (ec) {
                        outcome.status = TransferStatus::Error;
                        outcome.errorMessage = ec.message();
                        return outcome;
                    }
                    break;
                case ConflictResolutionPolicy::KeepBoth:
                    target = uniqueDestinationPath(target);
                    break;
            }
        }

        if (isMove) {
            fs::rename(src, target, ec);
            if (ec) {
                // Cross-device move fallback: copy then remove
                fs::copy(src, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
                if (ec) {
                    outcome.status = TransferStatus::Error;
                    outcome.errorMessage = ec.message();
                    return outcome;
                }
                fs::remove_all(src, ec);
                if (ec) {
                    outcome.status = TransferStatus::Error;
                    outcome.errorMessage = ec.message();
                    return outcome;
                }
            }
        } else {
            fs::copy(src, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            if (ec) {
                outcome.status = TransferStatus::Error;
                outcome.errorMessage = ec.message();
                return outcome;
            }
        }
    }

    if (onProgress) {
        onProgress(FileOperationProgress{title, "", total, total});
    }
    return outcome;
}

} // namespace mc
