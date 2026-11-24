#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

struct LocaleInfo {
    std::string code;        // e.g. "pl", "en", "de"
    std::string displayName; // e.g. "Polski", "English", "Deutsch"
    std::string filePath;
};

class Localization {
public:
    static Localization& instance();

    void initialize(const std::vector<std::string>& searchDirs, const std::string& preferredCode);
    bool setLanguage(const std::string& code);

    const std::string& currentLanguage() const { return currentCode_; }
    const std::vector<LocaleInfo>& availableLocales() const { return locales_; }

    // Returns stable C-string pointer for ImGui (zero allocation per frame).
    const char* tr(const char* key) const;
    std::string trStr(const char* key) const;

    static bool parseFlatJsonFile(const std::string& filePath, std::unordered_map<std::string, std::string>& outMap);

private:
    Localization() = default;

    std::vector<LocaleInfo> locales_;
    std::string currentCode_ = "pl";
    std::unordered_map<std::string, std::string> activeMap_;
    std::unordered_map<std::string, std::string> fallbackMap_;
};

inline const char* tr(const char* key) {
    return Localization::instance().tr(key);
}

} // namespace mc
