#pragma once

#include "../Vendor/imgui/imgui.h"

namespace mc::ui {

struct AntigravityColors {
  // Monokai Chrome (surroundings, bars, popups, dialogs, command line)
  static constexpr ImVec4 bgCanvas =
      ImVec4(0.153f, 0.157f, 0.133f, 1.00f); // #272822 (Monokai main charcoal)
  static constexpr ImVec4 bgHeader =
      ImVec4(0.118f, 0.122f, 0.110f, 1.00f); // #1E1F1C (Monokai dark bar)
  static constexpr ImVec4 bgPopup =
      ImVec4(0.176f, 0.180f, 0.153f, 0.98f); // #2D2E27 (Monokai popup/modal)
  static constexpr ImVec4 bgElevated =
      ImVec4(0.220f, 0.220f, 0.188f, 1.00f); // #383830 (Monokai button/tab)
  static constexpr ImVec4 bgHover = ImVec4(
      0.286f, 0.282f, 0.243f, 1.00f); // #49483E (Monokai hover/selection)

    // Active Panel (High-contrast MC Blue) vs Inactive Panel (Low-contrast muted slate-navy)
    static constexpr ImVec4 bgPanel        = ImVec4(0.055f, 0.208f, 0.541f, 1.00f); // #0E358A (Vivid MC active blue)
    static constexpr ImVec4 bgPanelDim     = ImVec4(0.071f, 0.118f, 0.212f, 1.00f); // #121E36 (Muted low-contrast inactive panel)
    static constexpr ImVec4 bgPathActive   = ImVec4(0.133f, 0.573f, 0.588f, 1.00f); // #229296 (High-contrast MC teal path bar)
    static constexpr ImVec4 bgPathInactive = ImVec4(0.090f, 0.141f, 0.239f, 1.00f); // #17243D (Muted inactive path bar)

    // Cursor Bar inside Active vs Inactive Panel
    static constexpr ImVec4 bgCursorActive = ImVec4(0.133f, 0.596f, 0.612f, 0.96f); // #22989C (Crisp turquoise cursor bar)
    static constexpr ImVec4 bgCursorDim    = ImVec4(0.110f, 0.176f, 0.290f, 0.55f); // Subtle inactive cursor
    static constexpr ImVec4 bgMarkedRow    = ImVec4(0.310f, 0.290f, 0.100f, 0.42f);
    static constexpr ImVec4 bgDiff         = ImVec4(0.320f, 0.140f, 0.220f, 0.45f);

    // Borders: Active vs Inactive
    static constexpr ImVec4 borderSubtle        = ImVec4(0.243f, 0.239f, 0.196f, 1.00f); // #3E3D32 (Monokai border)
    static constexpr ImVec4 borderPanel         = ImVec4(0.420f, 0.612f, 0.886f, 0.88f); // #6B9CE2 (Crisp active inner grid)
    static constexpr ImVec4 borderPanelInactive = ImVec4(0.145f, 0.208f, 0.318f, 0.55f); // #253551 (Low-contrast inactive grid)
    static constexpr ImVec4 borderPopup         = ImVec4(0.322f, 0.318f, 0.267f, 1.00f); // #525144
    static constexpr ImVec4 borderActive        = ImVec4(0.400f, 0.851f, 0.937f, 0.98f); // #66D9EF (Monokai cyan active frame)

    // Monokai Accent Colors
    static constexpr ImVec4 accent             = ImVec4(0.400f, 0.851f, 0.937f, 1.00f); // #66D9EF (Monokai Cyan)
    static constexpr ImVec4 accentDim          = ImVec4(0.400f, 0.851f, 0.937f, 0.22f);
    static constexpr ImVec4 markedGold         = ImVec4(0.961f, 0.910f, 0.420f, 1.00f); // #F5E86B (Bright Monokai Yellow)
    static constexpr ImVec4 headerYellow       = ImVec4(0.961f, 0.910f, 0.420f, 1.00f); // #F5E86B (Active panel yellow headers)
    static constexpr ImVec4 headerYellowDim    = ImVec4(0.545f, 0.541f, 0.416f, 0.85f); // #8B8A6A (Muted inactive headers)
    static constexpr ImVec4 folderText         = ImVec4(1.000f, 1.000f, 0.988f, 1.00f); // #FFFFFC (Crisp white in active panel)
    static constexpr ImVec4 folderBlue         = ImVec4(0.400f, 0.851f, 0.937f, 1.00f); // #66D9EF
    static constexpr ImVec4 packagePurple      = ImVec4(0.682f, 0.506f, 1.000f, 1.00f); // #AE81FF (Monokai Purple)
    static constexpr ImVec4 archiveOrange      = ImVec4(0.992f, 0.592f, 0.122f, 1.00f); // #FD971F (Monokai Orange)
    static constexpr ImVec4 successGreen       = ImVec4(0.651f, 0.886f, 0.180f, 1.00f); // #A6E22E (Monokai Green)
    static constexpr ImVec4 warningAmber       = ImVec4(0.992f, 0.592f, 0.122f, 1.00f); // #FD971F (Monokai Orange)
    static constexpr ImVec4 dangerRed          = ImVec4(0.976f, 0.149f, 0.447f, 1.00f); // #F92672 (Monokai Pink/Red)

    static constexpr ImVec4 textPrimary        = ImVec4(0.910f, 0.941f, 0.980f, 1.00f); // #E8F0FA (High-contrast active file text)
    static constexpr ImVec4 textSecondary      = ImVec4(0.733f, 0.816f, 0.918f, 1.00f); // #BBD0EA
    static constexpr ImVec4 textMuted          = ImVec4(0.565f, 0.651f, 0.761f, 1.00f); // #90A6C2
};

struct ThemeFonts {
    ImFont* ui = nullptr;
    ImFont* uiBold = nullptr;
    ImFont* mono = nullptr;
};

ThemeFonts applyAntigravityTheme(float dpiScale = 1.0f);

} // namespace mc::ui
