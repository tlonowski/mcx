#pragma once

#include "../Core/CommanderEngine.hpp"
#include "AntigravityTheme.hpp"

namespace mc::ui {

class CommanderUI {
public:
    CommanderUI(CommanderEngine& engine, ThemeFonts fonts);

    void render();

private:
    CommanderEngine& engine_;
    ThemeFonts fonts_;
    float splitRatio_ = 0.5f;
    bool focusFilterNextFrame_ = false;
    bool focusCommandNextFrame_ = false;
    bool openTopMenuNextFrame_ = false;
    PaneSide hotlistSide_ = PaneSide::Left;
    PaneSide historySide_ = PaneSide::Left;
    int terminalScrollOffset_ = 0;

    void handleGlobalShortcuts();
    void handleTerminalInput();
    void renderTopToolbar();
    void renderDualDriveBars(float leftW, float rightW, float splitterW);
    void renderSingleDriveBar(PaneState& pane, float width);
    void renderFilePane(PaneState& pane, float width, float height);
    void renderQuickViewPane(PaneSide side, float width, float height);
    void renderInfoPane(PaneSide side, float width, float height);
    void renderSubshellView(float width, float height);
    void renderInspector(float width, float height);
    void renderCommandBar();
    void renderFunctionKeyBar();
    void renderHotlistAndHistoryPopups();
    void renderDialogs();
    void renderToastAndProgress();
};

} // namespace mc::ui
