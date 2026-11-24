#include "AntigravityTheme.hpp"

#include <filesystem>

namespace mc::ui {

ThemeFonts applyAntigravityTheme(float /*dpiScale*/) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding     = ImVec2(10.0f, 8.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.CellPadding       = ImVec2(8.0f, 3.0f);
    style.ItemSpacing       = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 8.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 1.0f;

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 3.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 0.0f;

    auto& c = style.Colors;
    c[ImGuiCol_Text]                  = AntigravityColors::textPrimary;
    c[ImGuiCol_TextDisabled]          = AntigravityColors::textMuted;
    c[ImGuiCol_WindowBg]              = AntigravityColors::bgCanvas;
    c[ImGuiCol_ChildBg]               = AntigravityColors::bgCanvas;
    c[ImGuiCol_PopupBg]               = AntigravityColors::bgPopup;
    c[ImGuiCol_Border]                = AntigravityColors::borderSubtle;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]               = AntigravityColors::bgHeader;
    c[ImGuiCol_FrameBgHovered]        = AntigravityColors::bgElevated;
    c[ImGuiCol_FrameBgActive]         = AntigravityColors::bgHover;

    c[ImGuiCol_TitleBg]               = AntigravityColors::bgHeader;
    c[ImGuiCol_TitleBgActive]         = AntigravityColors::bgPathActive;
    c[ImGuiCol_TitleBgCollapsed]      = AntigravityColors::bgPanelDim;

    c[ImGuiCol_MenuBarBg]             = AntigravityColors::bgHeader;
    c[ImGuiCol_ScrollbarBg]           = AntigravityColors::bgPanelDim;
    c[ImGuiCol_ScrollbarGrab]         = AntigravityColors::bgElevated;
    c[ImGuiCol_ScrollbarGrabHovered]  = AntigravityColors::textMuted;
    c[ImGuiCol_ScrollbarGrabActive]   = AntigravityColors::textSecondary;

    c[ImGuiCol_CheckMark]             = AntigravityColors::accent;
    c[ImGuiCol_SliderGrab]            = AntigravityColors::accent;
    c[ImGuiCol_SliderGrabActive]      = AntigravityColors::accent;

    c[ImGuiCol_Button]                = AntigravityColors::bgElevated;
    c[ImGuiCol_ButtonHovered]         = AntigravityColors::bgHover;
    c[ImGuiCol_ButtonActive]          = AntigravityColors::accentDim;

    c[ImGuiCol_Header]                = AntigravityColors::bgCursorActive;
    c[ImGuiCol_HeaderHovered]         = AntigravityColors::bgHover;
    c[ImGuiCol_HeaderActive]          = AntigravityColors::bgCursorActive;

    c[ImGuiCol_Separator]             = AntigravityColors::borderSubtle;
    c[ImGuiCol_SeparatorHovered]      = AntigravityColors::accent;
    c[ImGuiCol_SeparatorActive]       = AntigravityColors::accent;

    c[ImGuiCol_ResizeGrip]            = AntigravityColors::borderSubtle;
    c[ImGuiCol_ResizeGripHovered]     = AntigravityColors::accent;
    c[ImGuiCol_ResizeGripActive]      = AntigravityColors::accent;

    c[ImGuiCol_Tab]                   = AntigravityColors::bgPanelDim;
    c[ImGuiCol_TabHovered]            = AntigravityColors::bgHover;
    c[ImGuiCol_TabSelected]           = AntigravityColors::bgElevated;

    c[ImGuiCol_TableHeaderBg]         = AntigravityColors::bgHeader;
    c[ImGuiCol_TableBorderStrong]     = AntigravityColors::borderSubtle;
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.165f, 0.290f, 0.451f, 0.60f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.018f);

    c[ImGuiCol_TextSelectedBg]        = AntigravityColors::accentDim;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.02f, 0.03f, 0.05f, 0.76f);

    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar glyphRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1
        0x0100, 0x017F, // Latin Extended-A (Polish diacritics)
        0x2000, 0x206F, // General Punctuation
        0x2190, 0x21FF, // Arrows
        0x2300, 0x23FF, // Miscellaneous Technical
        0x2500, 0x25FF, // Box drawing & geometric shapes
        0x2600, 0x26FF, // Stars & misc symbols
        0x2700, 0x27BF, // Dingbats
        0,
    };

    ThemeFonts fonts;
    const char* sfuiPath = "/System/Library/Fonts/SFNS.ttf";
    const char* sfmonoPath = "/System/Library/Fonts/SFNSMono.ttf";
    const char* menloPath = "/System/Library/Fonts/Menlo.ttc";

    if (std::filesystem::exists(sfuiPath)) {
        fonts.ui = io.Fonts->AddFontFromFileTTF(sfuiPath, 16.0f, nullptr, glyphRanges);
        fonts.uiBold = io.Fonts->AddFontFromFileTTF(sfuiPath, 16.5f, nullptr, glyphRanges);
    }
    if (std::filesystem::exists(sfmonoPath)) {
        fonts.mono = io.Fonts->AddFontFromFileTTF(sfmonoPath, 15.0f, nullptr, glyphRanges);
    } else if (std::filesystem::exists(menloPath)) {
        fonts.mono = io.Fonts->AddFontFromFileTTF(menloPath, 15.0f, nullptr, glyphRanges);
    }

    if (!fonts.ui) fonts.ui = io.Fonts->AddFontDefault();
    if (!fonts.uiBold) fonts.uiBold = fonts.ui;
    if (!fonts.mono) fonts.mono = fonts.ui;

    return fonts;
}

} // namespace mc::ui
