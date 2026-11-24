#pragma once

#include "../Core/Models.hpp"
#include <string>
#include <vector>

namespace mc::platform {

std::string homeDirectory();
std::vector<std::string> localeSearchDirectories();
std::string bundleResourceFile(const std::string& filename);
bool isPackagePath(const std::string& path);
std::string localizedTypeDescription(const std::string& path, bool isDirectory);
std::vector<MountedVolume> mountedVolumes();

bool trashItem(const std::string& path, std::string& errorOut);
void openItem(const std::string& path);
void revealInFinder(const std::vector<std::string>& paths);
void openInTerminal(const std::string& directoryPath);
bool ejectVolume(const std::string& path, std::string& errorOut);
void copyToClipboard(const std::string& text);
void showQuickLook(const std::string& path);

void* loadMetalTexture(const std::string& imagePath, void* mtlDevice, int& outWidth, int& outHeight);
void releaseMetalTexture(void* texturePtr);

std::vector<FavoriteLocation> loadCustomFavorites();
void saveCustomFavorites(const std::vector<FavoriteLocation>& custom);

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings& settings);

} // namespace mc::platform
