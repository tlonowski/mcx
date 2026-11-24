#import "MacBridge.hpp"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace mc::platform {

static NSURL* urlFromPath(const std::string& path) {
    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    if (!nsPath) return nil;
    return [NSURL fileURLWithPath:nsPath];
}

std::string homeDirectory() {
    NSString* home = NSHomeDirectory();
    return home ? std::string([home UTF8String]) : "/Users";
}

std::vector<std::string> localeSearchDirectories() {
    std::vector<std::string> dirs;
    // 1. User custom locales override
    std::string userLocales = homeDirectory() + "/Library/Application Support/mcx/Locales";
    dirs.push_back(userLocales);
    // 2. App bundle Resources/Locales and Resources/
    NSString* resPath = [[NSBundle mainBundle] resourcePath];
    if (resPath) {
        std::string res([resPath UTF8String]);
        dirs.push_back(res + "/Locales");
        dirs.push_back(res);
    }
    return dirs;
}

std::string bundleResourceFile(const std::string& filename) {
    NSString* resPath = [[NSBundle mainBundle] resourcePath];
    if (!resPath) return "";
    NSString* nsFile = [NSString stringWithUTF8String:filename.c_str()];
    if (!nsFile) return "";
    NSString* direct = [resPath stringByAppendingPathComponent:nsFile];
    if ([[NSFileManager defaultManager] fileExistsAtPath:direct]) {
        return std::string([direct UTF8String]);
    }
    NSString* inFonts = [[resPath stringByAppendingPathComponent:@"Fonts"] stringByAppendingPathComponent:nsFile];
    if ([[NSFileManager defaultManager] fileExistsAtPath:inFonts]) {
        return std::string([inFonts UTF8String]);
    }
    return "";
}

bool isPackagePath(const std::string& path) {
    NSURL* url = urlFromPath(path);
    if (!url) return false;
    NSNumber* isPkg = nil;
    [url getResourceValue:&isPkg forKey:NSURLIsPackageKey error:nil];
    return isPkg.boolValue;
}

std::string localizedTypeDescription(const std::string& path, bool isDirectory) {
    NSURL* url = urlFromPath(path);
    if (!url) return isDirectory ? "Folder" : "Dokument";
    NSString* desc = nil;
    [url getResourceValue:&desc forKey:NSURLLocalizedTypeDescriptionKey error:nil];
    if (desc && desc.length > 0) {
        return std::string([desc UTF8String]);
    }
    return isDirectory ? "Folder" : "Dokument";
}

std::vector<MountedVolume> mountedVolumes() {
    std::vector<MountedVolume> result;
    NSArray<NSURLResourceKey>* keys = @[
        NSURLVolumeNameKey,
        NSURLVolumeAvailableCapacityForImportantUsageKey,
        NSURLVolumeTotalCapacityKey,
        NSURLVolumeIsRemovableKey,
        NSURLVolumeIsInternalKey
    ];
    NSArray<NSURL*>* urls = [[NSFileManager defaultManager]
        mountedVolumeURLsIncludingResourceValuesForKeys:keys
                                               options:NSVolumeEnumerationSkipHiddenVolumes];
    if (!urls) return result;

    for (NSURL* url in urls) {
        NSDictionary<NSURLResourceKey, id>* vals = [url resourceValuesForKeys:keys error:nil];
        NSString* name = vals[NSURLVolumeNameKey];
        NSNumber* avail = vals[NSURLVolumeAvailableCapacityForImportantUsageKey];
        NSNumber* total = vals[NSURLVolumeTotalCapacityKey];
        NSNumber* removable = vals[NSURLVolumeIsRemovableKey];
        NSNumber* internal = vals[NSURLVolumeIsInternalKey];

        MountedVolume vol;
        vol.path = url.path ? std::string([url.path UTF8String]) : "/";
        vol.name = (name && name.length > 0) ? std::string([name UTF8String]) : "Macintosh HD";
        if (avail) vol.availableCapacity = avail.longLongValue;
        if (total) vol.totalCapacity = total.longLongValue;
        vol.isRemovable = removable ? removable.boolValue : false;
        vol.isInternal = internal ? internal.boolValue : true;
        result.push_back(std::move(vol));
    }
    return result;
}

bool trashItem(const std::string& path, std::string& errorOut) {
    NSURL* url = urlFromPath(path);
    if (!url) {
        errorOut = "Nieprawidłowa ścieżka: " + path;
        return false;
    }
    NSError* err = nil;
    BOOL ok = [[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:&err];
    if (!ok) {
        errorOut = err ? std::string([[err localizedDescription] UTF8String]) : "Nie udało się przenieść do kosza.";
        return false;
    }
    return true;
}

void openItem(const std::string& path) {
    NSURL* url = urlFromPath(path);
    if (url) {
        [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

void revealInFinder(const std::vector<std::string>& paths) {
    NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:paths.size()];
    for (const auto& p : paths) {
        if (NSURL* u = urlFromPath(p)) {
            [urls addObject:u];
        }
    }
    if (urls.count > 0) {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:urls];
    }
}

void openInTerminal(const std::string& directoryPath) {
    NSURL* dirURL = urlFromPath(directoryPath);
    NSURL* termURL = [NSURL fileURLWithPath:@"/System/Applications/Utilities/Terminal.app"];
    if (!dirURL) return;
    NSWorkspaceOpenConfiguration* config = [NSWorkspaceOpenConfiguration configuration];
    [[NSWorkspace sharedWorkspace] openURLs:@[dirURL]
                       withApplicationAtURL:termURL
                              configuration:config
                          completionHandler:nil];
}

bool ejectVolume(const std::string& path, std::string& errorOut) {
    NSURL* url = urlFromPath(path);
    if (!url) {
        errorOut = "Nieprawidłowa ścieżka woluminu.";
        return false;
    }
    NSError* err = nil;
    BOOL ok = [[NSWorkspace sharedWorkspace] unmountAndEjectDeviceAtURL:url error:&err];
    if (!ok) {
        errorOut = err ? std::string([[err localizedDescription] UTF8String]) : "Nie udało się wysunąć dysku.";
        return false;
    }
    return true;
}

void copyToClipboard(const std::string& text) {
    NSString* ns = [NSString stringWithUTF8String:text.c_str()];
    if (!ns) return;
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:ns forType:NSPasteboardTypeString];
}

void showQuickLook(const std::string& path) {
    if (path.empty()) return;
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/qlmanage"];
    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    if (!nsPath) return;
    task.arguments = @[@"-p", nsPath];
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    [task launchAndReturnError:nil];
}

static NSString* const kCustomFavoritesKey = @"MCommander.CustomFavorites.v2";

std::vector<FavoriteLocation> loadCustomFavorites() {
    std::vector<FavoriteLocation> out;
    NSArray<NSDictionary*>* arr = [[NSUserDefaults standardUserDefaults] arrayForKey:kCustomFavoritesKey];
    if (![arr isKindOfClass:[NSArray class]]) return out;
    for (NSDictionary* dict in arr) {
        if (![dict isKindOfClass:[NSDictionary class]]) continue;
        NSString* title = dict[@"title"];
        NSString* path = dict[@"path"];
        NSString* fid = dict[@"id"];
        if (title && path) {
            FavoriteLocation loc;
            loc.id = fid ? std::string([fid UTF8String]) : std::string([path UTF8String]);
            loc.title = std::string([title UTF8String]);
            loc.path = std::string([path UTF8String]);
            loc.isSystemDefault = false;
            out.push_back(std::move(loc));
        }
    }
    return out;
}

void saveCustomFavorites(const std::vector<FavoriteLocation>& custom) {
    NSMutableArray<NSDictionary*>* arr = [NSMutableArray arrayWithCapacity:custom.size()];
    for (const auto& loc : custom) {
        NSString* title = [NSString stringWithUTF8String:loc.title.c_str()];
        NSString* path = [NSString stringWithUTF8String:loc.path.c_str()];
        NSString* fid = [NSString stringWithUTF8String:loc.id.c_str()];
        if (title && path) {
            [arr addObject:@{
                @"id": fid ?: path,
                @"title": title,
                @"path": path
            }];
        }
    }
    [[NSUserDefaults standardUserDefaults] setObject:arr forKey:kCustomFavoritesKey];
}

static NSString* const kAppSettingsKey = @"MCommander.AppSettings.v1";

static NSDictionary* encodePaneSettings(const PaneSavedSettings& p) {
    NSMutableArray<NSString*>* tabs = [NSMutableArray arrayWithCapacity:p.tabPaths.size()];
    for (const auto& path : p.tabPaths) {
        NSString* s = [NSString stringWithUTF8String:path.c_str()];
        if (s) [tabs addObject:s];
    }
    return @{
        @"tabPaths": tabs,
        @"activeTabIndex": @(p.activeTabIndex),
        @"sortField": @(static_cast<int>(p.sortField)),
        @"sortAscending": @(p.sortAscending),
        @"briefMode": @(p.briefMode)
    };
}

static PaneSavedSettings decodePaneSettings(NSDictionary* dict) {
    PaneSavedSettings out;
    if (![dict isKindOfClass:[NSDictionary class]]) return out;
    NSArray* tabs = dict[@"tabPaths"];
    if ([tabs isKindOfClass:[NSArray class]]) {
        for (id obj in tabs) {
            if ([obj isKindOfClass:[NSString class]]) {
                out.tabPaths.emplace_back([(NSString*)obj UTF8String]);
            }
        }
    }
    if (NSNumber* idx = dict[@"activeTabIndex"]) out.activeTabIndex = idx.intValue;
    if (NSNumber* sf = dict[@"sortField"]) {
        int v = sf.intValue;
        if (v >= 0 && v <= 4) out.sortField = static_cast<FileSortField>(v);
    }
    if (NSNumber* asc = dict[@"sortAscending"]) out.sortAscending = asc.boolValue;
    if (NSNumber* brief = dict[@"briefMode"]) out.briefMode = brief.boolValue;
    return out;
}

AppSettings loadAppSettings() {
    AppSettings s;
    NSDictionary* dict = [[NSUserDefaults standardUserDefaults] dictionaryForKey:kAppSettingsKey];
    if (![dict isKindOfClass:[NSDictionary class]]) return s;

    s.leftPane = decodePaneSettings(dict[@"leftPane"]);
    s.rightPane = decodePaneSettings(dict[@"rightPane"]);
    if (NSNumber* side = dict[@"activeSide"]) {
        s.activeSide = (side.intValue == 1) ? PaneSide::Right : PaneSide::Left;
    }
    if (NSNumber* ratio = dict[@"splitRatio"]) {
        float r = ratio.floatValue;
        if (r >= 0.2f && r <= 0.8f) s.splitRatio = r;
    }
    if (NSNumber* hid = dict[@"showHiddenFiles"]) s.showHiddenFiles = hid.boolValue;
    if (NSNumber* insp = dict[@"showInspector"]) s.showInspector = insp.boolValue;
    if (NSString* lang = dict[@"language"]) {
        if ([lang isKindOfClass:[NSString class]] && lang.length > 0) {
            s.language = std::string([lang UTF8String]);
        }
    }
    return s;
}

void saveAppSettings(const AppSettings& settings) {
    NSString* lang = [NSString stringWithUTF8String:settings.language.c_str()] ?: @"pl";
    NSDictionary* dict = @{
        @"leftPane": encodePaneSettings(settings.leftPane),
        @"rightPane": encodePaneSettings(settings.rightPane),
        @"activeSide": @(settings.activeSide == PaneSide::Right ? 1 : 0),
        @"splitRatio": @(settings.splitRatio),
        @"showHiddenFiles": @(settings.showHiddenFiles),
        @"showInspector": @(settings.showInspector),
        @"language": lang
    };
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:dict forKey:kAppSettingsKey];
    [defaults synchronize];
}

} // namespace mc::platform
