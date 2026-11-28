#include "CommanderUI.hpp"
#include "../Core/Localization.hpp"
#include "../Core/Version.hpp"
#include "../Platform/MacBridge.hpp"
#include "../Vendor/imgui/imgui.h"
#include "../Vendor/imgui/imgui_internal.h"
#include "../Vendor/imgui/misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace mc::ui {

namespace {

bool boxButton(const char *label, bool active = false,
               const ImVec4 &accent = AntigravityColors::accent,
               ImVec2 size = ImVec2(0, 0)) {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
  if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(accent.x, accent.y, accent.z, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(accent.x, accent.y, accent.z, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_Border, accent);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, AntigravityColors::bgElevated);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AntigravityColors::bgHover);
    ImGui::PushStyleColor(ImGuiCol_Border, AntigravityColors::borderSubtle);
    ImGui::PushStyleColor(ImGuiCol_Text, AntigravityColors::textPrimary);
  }
  bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  return clicked;
}

void drawRowPrefixIcon(const FileItem &item, ImVec2 p) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  // Center 16x14 icon inside 24px row
  float x = std::floor(p.x + 2.0f);
  float y = std::floor(p.y + 4.0f);

  const std::string ext = toLowerASCII(item.fileExtension);

  if (item.isParentEntry) {
    // Muted turquoise return arrow on blue panel
    ImU32 arrowCol = IM_COL32(112, 194, 230, 240);
    dl->AddLine(ImVec2(x + 11, y + 12), ImVec2(x + 11, y + 5), arrowCol, 1.6f);
    dl->AddLine(ImVec2(x + 11, y + 5), ImVec2(x + 5, y + 5), arrowCol, 1.6f);
    dl->AddTriangleFilled(ImVec2(x + 2, y + 5), ImVec2(x + 7, y + 1.5f),
                          ImVec2(x + 7, y + 8.5f), arrowCol);
  } else if (item.isNavigableDirectory()) {
    // Classic Total Commander warm muted amber-gold folder on the blue panel
    // background
    ImU32 tabCol = item.isHidden ? IM_COL32(138, 120, 78, 145)
                                 : IM_COL32(184, 138, 56, 235);
    ImU32 bodyCol = item.isHidden ? IM_COL32(158, 138, 92, 155)
                                  : IM_COL32(218, 174, 86, 240);
    ImU32 topLine = item.isHidden ? IM_COL32(180, 162, 115, 145)
                                  : IM_COL32(238, 206, 136, 225);

    dl->AddRectFilled(ImVec2(x + 1, y + 2), ImVec2(x + 7, y + 5), tabCol, 1.5f);
    dl->AddRectFilled(ImVec2(x + 1, y + 4), ImVec2(x + 15, y + 13), bodyCol,
                      1.5f);
    dl->AddLine(ImVec2(x + 2, y + 5), ImVec2(x + 14, y + 5), topLine, 1.0f);
  } else if (item.isArchive()) {
    // Muted ochre archive icon
    ImU32 boxCol = IM_COL32(186, 138, 82, 220);
    ImU32 zipperBg = IM_COL32(36, 44, 56, 230);
    ImU32 zipperCol = IM_COL32(198, 206, 216, 230);

    dl->AddRectFilled(ImVec2(x + 1, y + 2), ImVec2(x + 15, y + 13), boxCol,
                      2.0f);
    dl->AddRectFilled(ImVec2(x + 6.5f, y + 2), ImVec2(x + 9.5f, y + 13),
                      zipperBg);
    for (int zy = 3; zy <= 10; zy += 2) {
      dl->AddRectFilled(ImVec2(x + 7.2f, y + static_cast<float>(zy)),
                        ImVec2(x + 8.8f, y + static_cast<float>(zy + 1)),
                        zipperCol);
    }
  } else if (item.isPackage || item.isExecutable || ext == "app" ||
             ext == "sh" || ext == "zsh" || ext == "bash") {
    // Muted terminal/app badge
    ImU32 winBg = IM_COL32(38, 46, 58, 240);
    ImU32 titleBar = item.isPackage ? IM_COL32(132, 114, 178, 230)
                                    : IM_COL32(88, 144, 184, 230);
    ImU32 glyphCol = IM_COL32(98, 176, 142, 235);

    dl->AddRectFilled(ImVec2(x + 1, y + 2), ImVec2(x + 15, y + 13), winBg,
                      2.0f);
    dl->AddRectFilled(ImVec2(x + 1, y + 2), ImVec2(x + 15, y + 5), titleBar,
                      2.0f);
    dl->AddLine(ImVec2(x + 4, y + 7), ImVec2(x + 6.5f, y + 9), glyphCol, 1.4f);
    dl->AddLine(ImVec2(x + 6.5f, y + 9), ImVec2(x + 4, y + 11), glyphCol, 1.4f);
    dl->AddLine(ImVec2(x + 8, y + 11), ImVec2(x + 11.5f, y + 11), glyphCol,
                1.4f);
  } else {
    // Muted document sheet with folded corner
    bool isCode = (ext == "cpp" || ext == "hpp" || ext == "c" || ext == "h" ||
                   ext == "mm" || ext == "swift" || ext == "py" ||
                   ext == "js" || ext == "ts" || ext == "rs" || ext == "go");
    bool isImage =
        (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
         ext == "webp" || ext == "svg" || ext == "icns" || ext == "bmp");
    bool isMedia = (ext == "mp4" || ext == "mov" || ext == "mkv" ||
                    ext == "mp3" || ext == "wav" || ext == "flac");
    bool isPdf = (ext == "pdf");

    ImU32 pageBg = item.isHidden ? IM_COL32(96, 108, 124, 120)
                                 : IM_COL32(158, 170, 184, 215);
    ImU32 foldBg = IM_COL32(96, 108, 124, 220);

    ImVec2 poly[5] = {ImVec2(x + 2.5f, y + 1), ImVec2(x + 10, y + 1),
                      ImVec2(x + 13.5f, y + 4.5f), ImVec2(x + 13.5f, y + 13.5f),
                      ImVec2(x + 2.5f, y + 13.5f)};
    dl->AddConvexPolyFilled(poly, 5, pageBg);
    dl->AddTriangleFilled(ImVec2(x + 10, y + 1), ImVec2(x + 13.5f, y + 4.5f),
                          ImVec2(x + 10, y + 4.5f), foldBg);

    if (isPdf) {
      dl->AddRectFilled(ImVec2(x + 4, y + 6.5f), ImVec2(x + 12, y + 11.5f),
                        IM_COL32(188, 86, 86, 235), 1.0f);
    } else if (isImage) {
      dl->AddRectFilled(ImVec2(x + 4, y + 6), ImVec2(x + 12, y + 11.5f),
                        IM_COL32(82, 138, 178, 220), 1.0f);
      dl->AddTriangleFilled(ImVec2(x + 4, y + 11.5f), ImVec2(x + 8, y + 7.5f),
                            ImVec2(x + 12, y + 11.5f),
                            IM_COL32(84, 152, 124, 235));
    } else if (isMedia) {
      dl->AddTriangleFilled(ImVec2(x + 6, y + 6), ImVec2(x + 11.5f, y + 9),
                            ImVec2(x + 6, y + 12),
                            IM_COL32(128, 106, 176, 235));
    } else if (isCode) {
      dl->AddLine(ImVec2(x + 4.5f, y + 7), ImVec2(x + 9.5f, y + 7),
                  IM_COL32(58, 118, 162, 235), 1.4f);
      dl->AddLine(ImVec2(x + 5.5f, y + 9), ImVec2(x + 11.5f, y + 9),
                  IM_COL32(54, 128, 102, 235), 1.4f);
      dl->AddLine(ImVec2(x + 4.5f, y + 11), ImVec2(x + 10.0f, y + 11),
                  IM_COL32(94, 88, 156, 235), 1.4f);
  } else {
      ImU32 lineCol = IM_COL32(68, 78, 92, 210);
      dl->AddLine(ImVec2(x + 4.5f, y + 7), ImVec2(x + 11, y + 7), lineCol,
                  1.2f);
      dl->AddLine(ImVec2(x + 4.5f, y + 9), ImVec2(x + 11.5f, y + 9), lineCol,
                  1.2f);
      dl->AddLine(ImVec2(x + 4.5f, y + 11), ImVec2(x + 9, y + 11), lineCol,
                  1.2f);
    }
  }

  if (item.isSymbolicLink) {
    dl->AddRectFilled(ImVec2(x + 1, y + 8.5f), ImVec2(x + 7, y + 13.5f),
                      IM_COL32(24, 30, 40, 230), 1.0f);
    dl->AddLine(ImVec2(x + 2, y + 12.5f), ImVec2(x + 6, y + 9.5f),
                IM_COL32(112, 162, 201, 235), 1.3f);
  }
}

} // namespace

CommanderUI::CommanderUI(CommanderEngine &engine, ThemeFonts fonts)
    : engine_(engine), fonts_(fonts), splitRatio_(engine.splitRatio) {}

void CommanderUI::render() {
  engine_.tick();
  ImGui::GetStyle().FontScaleMain = engine_.fontScale;
  handleGlobalShortcuts();

  if (engine_.requestCommandFocus) {
    focusCommandNextFrame_ = true;
    engine_.requestCommandFocus = false;
  }
  if (engine_.requestFilterFocus) {
    focusFilterNextFrame_ = true;
    engine_.requestFilterFocus = false;
  }

  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);

  ImGuiWindowFlags rootFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##MCommanderCleanBoxRoot", nullptr, rootFlags);
  ImGui::PopStyleVar(3);

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

  // Row 1: Top TC Button Bar
  renderTopToolbar();

  // Calculate dual-pane geometry
  float totalWidth = ImGui::GetContentRegionAvail().x;
  float inspectorWidth = engine_.showInspector ? 280.0f : 0.0f;
  float panesTotalWidth = totalWidth - inspectorWidth;
  float splitterW = 4.0f;
  float leftW =
      std::floor(std::clamp((panesTotalWidth - splitterW) * splitRatio_, 260.0f,
                            panesTotalWidth - 260.0f - splitterW));
  float rightW = panesTotalWidth - leftW - splitterW;

  // Row 2: Dual Drive Bars (Left & Right, just like Total Commander)
  renderDualDriveBars(leftW, rightW, splitterW);

  // Row 3: Dual File Panes (or Midnight Commander Subshell Ctrl+O / Quick View
  // / Info Panel)
  float bottomReserve = 58.0f; // Command bar (28) + F-Key bar (30)
  if (!engine_.subshellFullScreen && engine_.isCommandOutputVisible &&
      engine_.lastCommandResult.has_value()) {
    bottomReserve += 130.0f;
  }
  float paneHeight =
      std::max(180.0f, ImGui::GetContentRegionAvail().y - bottomReserve);

  if (engine_.subshellFullScreen) {
    renderSubshellView(totalWidth, paneHeight);
  } else {
    if (engine_.activeSide == PaneSide::Right && engine_.quickView.active) {
      renderQuickViewPane(PaneSide::Left, leftW, paneHeight);
    } else if (engine_.activeSide == PaneSide::Right &&
               engine_.infoPanelActive) {
      renderInfoPane(PaneSide::Left, leftW, paneHeight);
  } else {
      renderFilePane(engine_.leftPane, leftW, paneHeight);
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, AntigravityColors::bgHeader);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AntigravityColors::accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AntigravityColors::accent);
    ImGui::Button("##SplitterBar", ImVec2(splitterW, paneHeight));
    if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive() && panesTotalWidth > 100.0f) {
      splitRatio_ = std::clamp(splitRatio_ + ImGui::GetIO().MouseDelta.x /
                                                 panesTotalWidth,
                               0.22f, 0.78f);
      engine_.splitRatio = splitRatio_;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      engine_.saveSettings();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.0f, 0.0f);
    if (engine_.activeSide == PaneSide::Left && engine_.quickView.active) {
      renderQuickViewPane(PaneSide::Right, rightW, paneHeight);
    } else if (engine_.activeSide == PaneSide::Left &&
               engine_.infoPanelActive) {
      renderInfoPane(PaneSide::Right, rightW, paneHeight);
  } else {
      renderFilePane(engine_.rightPane, rightW, paneHeight);
    }

    if (engine_.showInspector) {
      ImGui::SameLine(0.0f, 0.0f);
      renderInspector(inspectorWidth, paneHeight);
    }
  }

  // Row 4: Command Line Bar (+ optional output drawer)
  renderCommandBar();

  // Row 5: 7-Cell Total Commander Function Key Bar
  renderFunctionKeyBar();

  ImGui::PopStyleVar(); // ItemSpacing(0,0)

  renderHotlistAndHistoryPopups();
  renderDialogs();
  renderToastAndProgress();

  ImGui::End();
}

void CommanderUI::handleGlobalShortcuts() {
  ImGuiIO &io = ImGui::GetIO();
  bool cmd = io.KeySuper; // Cmd on macOS
  bool ctrl = io.KeyCtrl;
  bool shift = io.KeyShift;
  bool alt = io.KeyAlt;

  if (engine_.dialog.kind != DialogKind::None) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      if (engine_.dialog.kind == DialogKind::SearchFiles) {
        engine_.stopSearch();
      }
      engine_.dialog.kind = DialogKind::None;
    }
    return;
  }

  // Total Commander Directory Hotlist (Ctrl+D / Cmd+D) & History (Alt+Down)
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_D)) {
    hotlistSide_ = engine_.activeSide;
    engine_.requestHotlistPopup = true;
    return;
  }
  if (alt && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    historySide_ = engine_.activeSide;
    engine_.requestHistoryPopup = true;
    return;
  }

  // Midnight Commander Subshell (Ctrl+O)
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
    engine_.toggleSubshell();
    return;
  }

  if (engine_.subshellFullScreen) {
    handleTerminalInput();
    return;
  }

  // Midnight Commander Alt+O: Open highlighted directory in opposite panel
  if (alt && ImGui::IsKeyPressed(ImGuiKey_O)) {
    engine_.openCursorDirInOppositePane();
    return;
  }

  // Midnight Commander Alt+. : Toggle hidden dotfiles
  if (alt && ImGui::IsKeyPressed(ImGuiKey_Period)) {
    engine_.toggleHiddenFiles();
    return;
  }

  // Midnight Commander Alt+T : Cycle panel view mode (Full / Brief)
  if (alt && ImGui::IsKeyPressed(ImGuiKey_T)) {
    auto &t = engine_.activePane().activeTab();
    t.briefMode = !t.briefMode;
    return;
  }

  // Midnight Commander Alt+Enter : Insert filename into command line
  if (alt && !shift && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    engine_.insertCursorNameToCommandLine(false);
    return;
  }

  // Disk Usage / ncdu mode (Ctrl+Space)
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Space)) {
    engine_.toggleDiskUsageMode();
    return;
  }

  // Quick View Panel (Ctrl+Q) & Info Panel (Ctrl+I)
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Q)) {
    engine_.toggleQuickView();
    return;
  }
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I) && !io.WantTextInput) {
    engine_.toggleInfoPanel();
    return;
  }

  // Brief / Full view modes (Cmd+1 / Cmd+2 or Ctrl+F1 / Ctrl+F2)
  if ((cmd && ImGui::IsKeyPressed(ImGuiKey_1)) ||
      (ctrl && ImGui::IsKeyPressed(ImGuiKey_F1))) {
    engine_.activePane().activeTab().briefMode = true;
    return;
  }
  if ((cmd && ImGui::IsKeyPressed(ImGuiKey_2)) ||
      (ctrl && ImGui::IsKeyPressed(ImGuiKey_F2))) {
    engine_.activePane().activeTab().briefMode = false;
    return;
  }

  // Sort shortcuts (Cmd+F3..F6 or Ctrl+F3..F6)
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_F3)) {
    engine_.activePane().activeTab().toggleSort(FileSortField::Name);
    return;
  }
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_F4)) {
    engine_.activePane().activeTab().toggleSort(FileSortField::Extension);
    return;
  }
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_F5)) {
    engine_.activePane().activeTab().toggleSort(
        FileSortField::ModificationDate);
    return;
  }
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_F6)) {
    engine_.activePane().activeTab().toggleSort(FileSortField::Size);
    return;
  }

  // Insert filename / path into command line (Ctrl+Enter / Cmd+Enter)
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    engine_.insertCursorNameToCommandLine(shift);
    return;
  }

  // Calculate all directory sizes (Alt+Shift+Enter)
  if (alt && shift && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
    engine_.calculateAllDirectorySizes();
    return;
  }

  // Tabs & Panels
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_T)) {
    engine_.addTab();
    return;
  }
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_W)) {
    engine_.closeTab(engine_.activePane().activeTab().id, engine_.activeSide);
    return;
  }
  if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
    engine_.activePane().selectAdjacentTab(shift ? -1 : 1);
    return;
  }
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_I)) {
    engine_.showInspector = !engine_.showInspector;
    return;
  }
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_U)) {
    engine_.swapPanes();
    return;
  }
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_M)) {
    engine_.triggerMultiRename();
    return;
  }
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_L)) {
    engine_.triggerGoToPath();
    return;
  }
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_K)) {
    engine_.triggerConnectRemote();
    return;
  }
  if (cmd && ImGui::IsKeyPressed(ImGuiKey_Y)) {
    engine_.triggerQuickLook();
    return;
  }
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_R)) {
    engine_.refreshAllVisibleTabs();
    return;
  }
  if (cmd && shift && ImGui::IsKeyPressed(ImGuiKey_C)) {
    engine_.compareDirectories();
    return;
  }
  if (cmd && shift && ImGui::IsKeyPressed(ImGuiKey_F)) {
    engine_.triggerFindFiles();
    return;
  }
  if ((cmd && ImGui::IsKeyPressed(ImGuiKey_F)) ||
      (ctrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
    focusFilterNextFrame_ = true;
    return;
  }
  if (cmd && shift && ImGui::IsKeyPressed(ImGuiKey_Period)) {
    engine_.toggleHiddenFiles();
    return;
  }
  if (cmd && alt && ImGui::IsKeyPressed(ImGuiKey_Equal)) {
    engine_.syncInactivePaneToActive();
    return;
  }
  if (cmd && alt && ImGui::IsKeyPressed(ImGuiKey_C)) {
    engine_.copySelectedPathsToClipboard();
    return;
  }
  if ((cmd || ctrl) && shift && ImGui::IsKeyPressed(ImGuiKey_A)) {
    engine_.activePane().activeTab().deselectAll();
    return;
  }
  if ((cmd || ctrl) && ImGui::IsKeyPressed(ImGuiKey_A) && !io.WantTextInput) {
    engine_.activePane().activeTab().selectAll();
    return;
  }

  // Select same extension (Alt + Num+)
  if (alt && (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
              ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))) {
    engine_.activePane().activeTab().selectSameExtension(!shift);
    return;
  }

  if (cmd && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    engine_.goUp();
    return;
  }
  if ((cmd || alt) && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
    engine_.goBack();
    return;
  }
  if ((cmd || alt) && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
    engine_.goForward();
    return;
  }

  // Function keys (TC + Midnight Commander F1..F10 standard)
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
    engine_.triggerF1Help();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
    if (shift)
      engine_.triggerRename();
    else
      engine_.triggerF2UserMenu();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
    engine_.triggerF3View();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F4)) {
    if (shift)
      engine_.triggerNewFile();
    else
      engine_.triggerF4Edit();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
    if (alt)
      engine_.triggerPackArchive();
    else
      engine_.triggerF5Copy();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
    if (shift)
      engine_.triggerRename();
    else
      engine_.triggerF6Move();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F7)) {
    if (alt)
      engine_.triggerFindFiles();
    else
      engine_.triggerF7NewFolder();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F8) ||
      (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete))) {
    engine_.triggerF8Delete();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F9)) {
    if (alt)
      engine_.triggerUnpackArchive();
    else
      openTopMenuNextFrame_ = true;
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
    std::exit(0);
  }

  if (io.WantTextInput)
    return;

  auto &tab = engine_.activePane().activeTab();
  bool &scrollFlag = (engine_.activeSide == PaneSide::Left)
                         ? engine_.requestScrollToCursorLeft
                         : engine_.requestScrollToCursorRight;

  if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
    engine_.switchPane();
  } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    tab.moveCursor(-1, shift);
    scrollFlag = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    tab.moveCursor(1, shift);
    scrollFlag = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
    tab.moveCursor(-16, shift);
    scrollFlag = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
    tab.moveCursor(16, shift);
    scrollFlag = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
    tab.moveCursorToEdge(true, shift);
    scrollFlag = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
    tab.moveCursorToEdge(false, shift);
    scrollFlag = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_Insert)) {
    engine_.toggleSpaceSelection(true);
    scrollFlag = true;
  } else if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
    engine_.toggleSpaceSelection(false);
  } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
    engine_.openSelected();
  } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
    engine_.goUp();
  } else if (ImGui::IsKeyPressed(ImGuiKey_Backslash)) {
    engine_.goToRoot();
  } else if (shift && (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))) {
    engine_.fontScale = std::clamp(engine_.fontScale + 0.05f, 0.75f, 1.60f);
    engine_.saveSettings();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Czcionka: %.0f%%", engine_.fontScale * 100.0f);
    engine_.showToast(buf);
  } else if (shift && (ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))) {
    engine_.fontScale = std::clamp(engine_.fontScale - 0.05f, 0.75f, 1.60f);
    engine_.saveSettings();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Czcionka: %.0f%%", engine_.fontScale * 100.0f);
    engine_.showToast(buf);
  } else if (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
             ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
    engine_.triggerSelectByPattern(true);
  } else if (ImGui::IsKeyPressed(ImGuiKey_Minus) ||
             ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
    engine_.triggerSelectByPattern(false);
  } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply) ||
             (shift && ImGui::IsKeyPressed(ImGuiKey_8))) {
    tab.invertSelection();
  }
}

void CommanderUI::renderTopToolbar() {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgHeader);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
  ImGui::BeginChild("##TCTopButtonBar", ImVec2(0, 30), ImGuiChildFlags_Borders);

  ImGui::SetCursorPosY(3.0f);
  if (openTopMenuNextFrame_) {
    ImGui::OpenPopup("##MCTopMenuFile");
    openTopMenuNextFrame_ = false;
  }

  auto beginDropdownPopup = [](const char *popupId, ImVec2 anchorMin,
                               ImVec2 anchorMax) -> bool {
    ImGui::SetNextWindowPos(ImVec2(anchorMin.x, anchorMax.y + 2.0f),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, 0.0f),
                                        ImVec2(420.0f, 600.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, AntigravityColors::bgPopup);
    ImGui::PushStyleColor(ImGuiCol_Border, AntigravityColors::borderPopup);
    if (ImGui::BeginPopup(popupId)) {
      return true;
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return false;
  };

  auto endDropdownPopup = []() {
    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
  };

  // Classic Midnight Commander 5 top pull-down menus (Lewy | Plik | Polecenie |
  // Opcje | Prawy)
  if (boxButton(tr("menu.left")))
    ImGui::OpenPopup("##MCTopMenuLeft");
  ImVec2 bMin = ImGui::GetItemRectMin(), bMax = ImGui::GetItemRectMax();
  if (beginDropdownPopup("##MCTopMenuLeft", bMin, bMax)) {
    auto &lTab = engine_.leftPane.activeTab();
    if (ImGui::MenuItem(tr("menu.view_full"), "⌘2", !lTab.briefMode))
      lTab.briefMode = false;
    if (ImGui::MenuItem(tr("menu.view_brief"), "⌘1", lTab.briefMode))
      lTab.briefMode = true;
    if (ImGui::MenuItem(tr("menu.info_panel"), "Ctrl+I",
                        engine_.infoPanelActive))
      engine_.toggleInfoPanel();
    if (ImGui::MenuItem(tr("menu.quick_view"), "Ctrl+Q",
                        engine_.quickView.active))
      engine_.toggleQuickView();
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.sort_name"), "Ctrl+F3",
                        lTab.sortField == FileSortField::Name))
      lTab.toggleSort(FileSortField::Name);
    if (ImGui::MenuItem(tr("menu.sort_ext"), "Ctrl+F4",
                        lTab.sortField == FileSortField::Extension))
      lTab.toggleSort(FileSortField::Extension);
    if (ImGui::MenuItem(tr("menu.sort_date"), "Ctrl+F5",
                        lTab.sortField == FileSortField::ModificationDate))
      lTab.toggleSort(FileSortField::ModificationDate);
    if (ImGui::MenuItem(tr("menu.sort_size"), "Ctrl+F6",
                        lTab.sortField == FileSortField::Size))
      lTab.toggleSort(FileSortField::Size);
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.refresh_left"), "⌘R"))
      engine_.refreshPane(PaneSide::Left);
    endDropdownPopup();
  }
  ImGui::SameLine();

  if (boxButton(tr("menu.file")))
    ImGui::OpenPopup("##MCTopMenuFile");
  bMin = ImGui::GetItemRectMin();
  bMax = ImGui::GetItemRectMax();
  if (beginDropdownPopup("##MCTopMenuFile", bMin, bMax)) {
    if (ImGui::MenuItem(tr("menu.f3_view"), "F3"))
      engine_.triggerF3View();
    if (ImGui::MenuItem(tr("menu.f4_edit"), "F4"))
      engine_.triggerF4Edit();
    if (ImGui::MenuItem(tr("menu.f5_copy"), "F5"))
      engine_.triggerF5Copy();
    if (ImGui::MenuItem(tr("menu.f6_move"), "F6"))
      engine_.triggerF6Move();
    if (ImGui::MenuItem(tr("menu.rename"), "Shift+F6"))
      engine_.triggerRename();
    if (ImGui::MenuItem(tr("menu.f7_mkdir"), "F7"))
      engine_.triggerF7NewFolder();
    if (ImGui::MenuItem(tr("menu.f8_delete"), "F8"))
      engine_.triggerF8Delete();
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.symlink")))
      engine_.triggerCreateLink(false, false);
    if (ImGui::MenuItem(tr("menu.rel_symlink")))
      engine_.triggerCreateLink(false, true);
    if (ImGui::MenuItem(tr("menu.hardlink")))
      engine_.triggerCreateLink(true, false);
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.chmod")))
      engine_.triggerChmod();
    if (ImGui::MenuItem(tr("menu.multirename"), "⌘M"))
      engine_.triggerMultiRename();
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.sel_group"), "+"))
      engine_.triggerSelectByPattern(true);
    if (ImGui::MenuItem(tr("menu.unsel_group"), "-"))
      engine_.triggerSelectByPattern(false);
    if (ImGui::MenuItem(tr("menu.invert_sel"), "*"))
      engine_.activePane().activeTab().invertSelection();
    endDropdownPopup();
  }
  ImGui::SameLine();

  if (boxButton(tr("menu.command")))
    ImGui::OpenPopup("##MCTopMenuCmd");
  bMin = ImGui::GetItemRectMin();
  bMax = ImGui::GetItemRectMax();
  if (beginDropdownPopup("##MCTopMenuCmd", bMin, bMax)) {
    if (ImGui::MenuItem(tr("menu.user_menu"), "F2"))
      engine_.triggerF2UserMenu();
    if (ImGui::MenuItem(tr("menu.find_files"), "Alt+F7"))
      engine_.triggerFindFiles();
    if (ImGui::MenuItem(tr("menu.multirename"), "⌘M"))
      engine_.triggerMultiRename();
    if (ImGui::MenuItem(tr("menu.compare_dirs"), "⇧⌘C"))
      engine_.compareDirectories();
    if (ImGui::MenuItem(tr("menu.diff_files")))
      engine_.triggerDiffFiles();
    if (ImGui::MenuItem(tr("menu.sync_dirs")))
      engine_.triggerSyncDirs();
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.pack_archive"), "Alt+F5"))
      engine_.triggerPackArchive();
    if (ImGui::MenuItem(tr("menu.unpack_archive"), "Alt+F9"))
      engine_.triggerUnpackArchive();
    if (ImGui::MenuItem(tr("menu.checksums")))
      engine_.triggerChecksums();
    if (ImGui::MenuItem(tr("menu.dir_sizes"), "Alt+Shift+Enter"))
      engine_.calculateAllDirectorySizes();
    if (ImGui::MenuItem(tr("menu.ncdu_mode"), "Ctrl+Space",
                        engine_.activePane().activeTab().diskUsageMode))
      engine_.toggleDiskUsageMode();
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.info_panel_opp"), "Ctrl+I",
                        engine_.infoPanelActive))
      engine_.toggleInfoPanel();
    if (ImGui::MenuItem(tr("menu.quick_view_opp"), "Ctrl+Q",
                        engine_.quickView.active))
      engine_.toggleQuickView();
    if (ImGui::MenuItem(tr("menu.subshell"), "Ctrl+O",
                        engine_.subshellFullScreen))
      engine_.toggleSubshell();
    if (ImGui::MenuItem(tr("menu.open_terminal"))) {
      platform::openInTerminal(engine_.activePane().activeTab().currentPath);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.hotlist"), "⌘D")) {
      hotlistSide_ = engine_.activeSide;
      engine_.requestHotlistPopup = true;
    }
    if (ImGui::MenuItem(tr("menu.connect_remote"), "⌘K"))
      engine_.triggerConnectRemote();
    if (ImGui::MenuItem(tr("menu.swap_panes"), "Ctrl+U"))
      engine_.swapPanes();
    if (ImGui::MenuItem(tr("menu.sync_panes"), "⌥⌘="))
      engine_.syncInactivePaneToActive();
    if (ImGui::MenuItem(tr("menu.open_in_opp"), "Alt+O"))
      engine_.openCursorDirInOppositePane();
    endDropdownPopup();
  }
  ImGui::SameLine();

  if (boxButton(tr("menu.options")))
    ImGui::OpenPopup("##MCTopMenuOpts");
  bMin = ImGui::GetItemRectMin();
  bMax = ImGui::GetItemRectMax();
  if (beginDropdownPopup("##MCTopMenuOpts", bMin, bMax)) {
    if (ImGui::MenuItem(tr("menu.show_hidden"), "Alt+.",
                        engine_.showHiddenFiles))
      engine_.toggleHiddenFiles();
    if (ImGui::MenuItem(tr("menu.inspector"), "⌘I", engine_.showInspector))
      engine_.showInspector = !engine_.showInspector;
    if (ImGui::MenuItem(tr("menu.info_panel_opp"), "Ctrl+I",
                        engine_.infoPanelActive))
      engine_.toggleInfoPanel();
    if (ImGui::MenuItem(tr("menu.quick_view_opp"), "Ctrl+Q",
                        engine_.quickView.active))
      engine_.toggleQuickView();
    ImGui::Separator();
    if (ImGui::BeginMenu(tr("menu.language"))) {
      auto &loc = Localization::instance();
      for (const auto &info : loc.availableLocales()) {
        bool selected = (info.code == loc.currentLanguage());
        std::string itemText = info.displayName + " (" + info.code + ")";
        if (ImGui::MenuItem(itemText.c_str(), nullptr, selected)) {
          loc.setLanguage(info.code);
          engine_.saveSettings();
        }
      }
      ImGui::EndMenu();
    }
    endDropdownPopup();
  }
  ImGui::SameLine();

  if (boxButton(tr("menu.right")))
    ImGui::OpenPopup("##MCTopMenuRight");
  bMin = ImGui::GetItemRectMin();
  bMax = ImGui::GetItemRectMax();
  if (beginDropdownPopup("##MCTopMenuRight", bMin, bMax)) {
    auto &rTab = engine_.rightPane.activeTab();
    if (ImGui::MenuItem(tr("menu.view_full"), nullptr, !rTab.briefMode))
      rTab.briefMode = false;
    if (ImGui::MenuItem(tr("menu.view_brief"), nullptr, rTab.briefMode))
      rTab.briefMode = true;
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.sort_name"), nullptr,
                        rTab.sortField == FileSortField::Name))
      rTab.toggleSort(FileSortField::Name);
    if (ImGui::MenuItem(tr("menu.sort_ext"), nullptr,
                        rTab.sortField == FileSortField::Extension))
      rTab.toggleSort(FileSortField::Extension);
    if (ImGui::MenuItem(tr("menu.sort_date"), nullptr,
                        rTab.sortField == FileSortField::ModificationDate))
      rTab.toggleSort(FileSortField::ModificationDate);
    if (ImGui::MenuItem(tr("menu.sort_size"), nullptr,
                        rTab.sortField == FileSortField::Size))
      rTab.toggleSort(FileSortField::Size);
    ImGui::Separator();
    if (ImGui::MenuItem(tr("menu.refresh_right")))
      engine_.refreshPane(PaneSide::Right);
    endDropdownPopup();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void CommanderUI::renderDualDriveBars(float leftW, float rightW,
                                      float splitterW) {
  renderSingleDriveBar(engine_.leftPane, leftW);
  ImGui::SameLine(0.0f, 0.0f);
  ImGui::Dummy(ImVec2(splitterW, 26.0f));
  ImGui::SameLine(0.0f, 0.0f);
  renderSingleDriveBar(engine_.rightPane, rightW);
  if (engine_.showInspector) {
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgCanvas);
    ImGui::BeginChild("##DriveBarInspectorFiller", ImVec2(0, 26),
                      ImGuiChildFlags_Borders);
    ImGui::SetCursorPos(ImVec2(8.0f, 4.0f));
    ImGui::TextColored(AntigravityColors::accent, "INSPEKTOR PLIKU");
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }
}

void CommanderUI::renderSingleDriveBar(PaneState &pane, float width) {
  auto &tab = pane.activeTab();
  ImGui::PushID(pane.side == PaneSide::Left ? "DriveBarL" : "DriveBarR");
  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgCanvas);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 2.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));

  ImGui::BeginChild("##DriveBox", ImVec2(width, 26), ImGuiChildFlags_Borders);
  ImGui::SetCursorPosY(2.0f);

  const MountedVolume *currentVol = nullptr;
  for (size_t i = 0; i < engine_.volumes.size(); ++i) {
    const auto &vol = engine_.volumes[i];
    bool isCurrent =
        (vol.path == "/" && tab.currentPath == "/") ||
        (vol.path != "/" && tab.currentPath.rfind(vol.path, 0) == 0);
    if (isCurrent)
      currentVol = &vol;

    ImGui::PushID(static_cast<int>(i));
    std::string btnLabel =
        "[" + (vol.path == "/" ? std::string("/") : vol.name) + "]";
    if (boxButton(btnLabel.c_str(), isCurrent)) {
      engine_.navigate(vol.path, pane.side);
    }
    if (vol.isRemovable) {
      ImGui::SameLine(0, 1);
      if (boxButton("⏏")) {
        engine_.ejectVolume(vol);
      }
    }
    ImGui::PopID();
    ImGui::SameLine();
  }

  if (!currentVol && !engine_.volumes.empty()) {
    currentVol = &engine_.volumes.front();
  }

  if (currentVol) {
    if (fonts_.mono)
      ImGui::PushFont(fonts_.mono);
    ImGui::TextColored(AntigravityColors::textSecondary, " [%s] %s",
                       currentVol->name.c_str(),
                       currentVol->capacitySummary().c_str());
    if (fonts_.mono)
      ImGui::PopFont();
  }

  // Right-aligned TC quick buttons: [*] (Hotlist), [↓] (History), [\] (Root),
  // [..] (Parent)
  float rightButtonsW = 114.0f;
  float availX = ImGui::GetWindowWidth() - rightButtonsW;
  if (availX > ImGui::GetCursorPosX()) {
    ImGui::SameLine(availX);
  } else {
    ImGui::SameLine();
  }

  if (boxButton("*##HotlistBtn")) {
    engine_.setActiveSide(pane.side);
    hotlistSide_ = pane.side;
    engine_.requestHotlistPopup = true;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Ulubione katalogi — Directory Hotlist (⌘D / Ctrl+D)");
  ImGui::SameLine();

  if (boxButton("▾##HistBtn")) {
    engine_.setActiveSide(pane.side);
    historySide_ = pane.side;
    engine_.requestHistoryPopup = true;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Historia katalogów (⌥↓)");
  ImGui::SameLine();

  if (boxButton("\\##RootBtn")) {
    engine_.goToRoot(pane.side);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Katalog główny / (\\)");
  ImGui::SameLine();

  if (boxButton("..##UpBtn")) {
    engine_.goUp(pane.side);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Katalog nadrzędny (Backspace)");

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  ImGui::PopID();
}

void CommanderUI::renderFilePane(PaneState &pane, float width, float height) {
  bool isActive = (engine_.activeSide == pane.side);
  auto &tab = pane.activeTab();

  ImGui::PushID(pane.side == PaneSide::Left ? "LeftPaneBox" : "RightPaneBox");
  ImGui::PushStyleColor(ImGuiCol_ChildBg, isActive
                                              ? AntigravityColors::bgPanel
                                              : AntigravityColors::bgPanelDim);
  ImGui::PushStyleColor(ImGuiCol_Border,
                        isActive ? AntigravityColors::borderActive
                                 : AntigravityColors::borderPanelInactive);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isActive ? 1.5f : 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

  ImGui::BeginChild("##PaneCleanBox", ImVec2(width, height),
                    ImGuiChildFlags_Borders);

  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
      (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
       ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
    engine_.setActiveSide(pane.side);
  }

  // 1. Folder Tabs Bar (TC Folder Tabs)
  {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgHeader);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    ImGui::BeginChild("##TabsStrip", ImVec2(width, 23),
                      ImGuiChildFlags_Borders);

    for (size_t i = 0; i < pane.tabs.size(); ++i) {
      const auto &t = pane.tabs[i];
      bool selected = (t.id == pane.activeTabID);
      ImGui::PushID(static_cast<int>(t.id));

      ImVec2 posBefore = ImGui::GetCursorScreenPos();
      if (boxButton(t.title().c_str(), selected && isActive)) {
        engine_.selectTab(t.id, pane.side);
      }
      if (selected) {
        ImVec2 minP = ImGui::GetItemRectMin();
        ImVec2 maxP = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(minP.x, posBefore.y), ImVec2(maxP.x, posBefore.y),
            ImGui::ColorConvertFloat4ToU32(isActive
                                               ? AntigravityColors::accent
                                               : AntigravityColors::textMuted),
            2.0f);
      }
      if (pane.tabs.size() > 1) {
        ImGui::SameLine(0, 1);
        if (boxButton("×")) {
          engine_.closeTab(t.id, pane.side);
        }
      }
      ImGui::PopID();
      ImGui::SameLine();
    }
    if (boxButton("+")) {
      engine_.addTab(pane.side);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
  }

  // 2. Active Path Header Bar (Klasyczny podświetlany pasek ścieżki Total
  // Commandera)
  {
    ImVec4 pathBg = isActive ? AntigravityColors::bgPathActive
                             : AntigravityColors::bgPathInactive;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pathBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
    ImGui::BeginChild("##PathHeaderBar", ImVec2(width, 24),
                      ImGuiChildFlags_Borders);

    if (isActive) {
      ImVec2 pMin = ImGui::GetWindowPos();
      ImVec2 pMax = ImVec2(pMin.x + 3.0f, pMin.y + ImGui::GetWindowHeight());
      ImGui::GetWindowDrawList()->AddRectFilled(
          pMin, pMax,
          ImGui::ColorConvertFloat4ToU32(AntigravityColors::accent));
    }

    float filterW = 130.0f;
    float pathBtnW = std::max(60.0f, width - filterW - 18.0f);

    std::string maskDisplay =
        tab.isRemote()
            ? tab.remote->uri()
            : (tab.isInsideArchive()
                   ? (tab.archivePath +
                      (tab.archiveSubdir.empty() ? ""
                                                 : ("/" + tab.archiveSubdir)))
                   : tab.currentPath);
    if (!maskDisplay.empty() && maskDisplay.back() != '/')
      maskDisplay += "/";
    maskDisplay +=
        tab.filterText.empty() ? "*.*" : ("*" + tab.filterText + "*");

    if (fonts_.mono)
      ImGui::PushFont(fonts_.mono);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          isActive ? AntigravityColors::textPrimary
                                   : AntigravityColors::textSecondary);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    if (ImGui::Button((maskDisplay + "##PathClick").c_str(),
                      ImVec2(pathBtnW, 19.0f))) {
      engine_.setActiveSide(pane.side);
      engine_.triggerGoToPath();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    if (isActive && focusFilterNextFrame_) {
      ImGui::SetKeyboardFocusHere();
      focusFilterNextFrame_ = false;
    }
    ImGui::SetNextItemWidth(filterW);
    if (ImGui::InputTextWithHint("##QuickFilter", tr("filter.hint"),
                                 &tab.filterText)) {
      tab.recomputeDisplayedItems();
    }
    if (fonts_.mono)
      ImGui::PopFont();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
  }

  // 3. File List Table (5-column Full mode or 2-column Brief mode)
  float footerH = 26.0f;
  float tableH = std::max(60.0f, height - 23.0f - 24.0f - footerH);

  if (!tab.errorMessage.empty()) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::BeginChild("##ErrorBox", ImVec2(width, tableH),
                      ImGuiChildFlags_Borders);
    ImGui::TextColored(AntigravityColors::dangerRed,
                       "Błąd dostępu do katalogu:");
    ImGui::TextWrapped("%s", tab.errorMessage.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar();
  } else {
    int colCount = tab.briefMode ? 2 : 5;
    ImGuiTableFlags tFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;

    ImVec4 panelBlue =
        isActive ? AntigravityColors::bgPanel : AntigravityColors::bgPanelDim;
    ImVec4 innerGrid = isActive ? AntigravityColors::borderPanel
                                : AntigravityColors::borderPanelInactive;
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, panelBlue);
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, innerGrid);
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, innerGrid);

    if (ImGui::BeginTable("##TCFileTable", colCount, tFlags,
                          ImVec2(width, tableH))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn(tr("col.name"),
                              ImGuiTableColumnFlags_WidthStretch |
                                  ImGuiTableColumnFlags_DefaultSort,
                              0.0f, 0);
      ImGui::TableSetupColumn(tr("col.ext"), ImGuiTableColumnFlags_WidthFixed,
                              64.0f, 1);
      if (!tab.briefMode) {
        ImGui::TableSetupColumn(tr("col.size"),
                                ImGuiTableColumnFlags_WidthFixed, 92.0f, 2);
        ImGui::TableSetupColumn(tr("col.date"),
                                ImGuiTableColumnFlags_WidthFixed, 118.0f, 3);
        ImGui::TableSetupColumn(tr("col.attr"),
                                ImGuiTableColumnFlags_WidthFixed, 82.0f, 4);
      }
      ImGui::PushStyleColor(ImGuiCol_Text,
                            isActive ? AntigravityColors::headerYellow
                                     : AntigravityColors::headerYellowDim);
      ImGui::TableHeadersRow();
      ImGui::PopStyleColor();

      if (ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs()) {
        if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
          const auto &spec = sortSpecs->Specs[0];
          FileSortField field = FileSortField::Name;
          if (spec.ColumnUserID == 1)
            field = FileSortField::Extension;
          else if (spec.ColumnUserID == 2)
            field = FileSortField::Size;
          else if (spec.ColumnUserID == 3)
            field = FileSortField::ModificationDate;
          tab.sortField = field;
          tab.sortAscending =
              (spec.SortDirection == ImGuiSortDirection_Ascending);
          tab.recomputeDisplayedItems();
          sortSpecs->SpecsDirty = false;
        }
      }

      bool &scrollReq = (pane.side == PaneSide::Left)
                            ? engine_.requestScrollToCursorLeft
                            : engine_.requestScrollToCursorRight;

      int64_t totalDiskBytes = 0;
      int64_t maxDiskBytes = 0;
      if (tab.diskUsageMode && !tab.briefMode) {
        for (const auto &it : tab.displayedItems) {
          if (it.isParentEntry)
            continue;
          int64_t sz = -1;
          if (it.isNavigableDirectory()) {
            auto f = tab.directorySizes.find(it.path);
            if (f != tab.directorySizes.end())
              sz = f->second;
          } else if (it.size.has_value()) {
            sz = *it.size;
          }
          if (sz > 0) {
            totalDiskBytes += sz;
            if (sz > maxDiskBytes)
              maxDiskBytes = sz;
          }
        }
      }

      const float rowHeight = std::round(25.0f * engine_.fontScale);
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(tab.displayedItems.size()), rowHeight);
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          const auto &item = tab.displayedItems[static_cast<size_t>(row)];
          bool isMarked =
              !item.isParentEntry && tab.selectedPaths.contains(item.path);
          bool isCursor = (tab.cursorPath == item.path);
          bool isDiff = tab.highlightedDiffPaths.contains(item.path);

          ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

          if (isCursor) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   ImGui::ColorConvertFloat4ToU32(
                                       isActive
                                           ? AntigravityColors::bgCursorActive
                                           : AntigravityColors::bgCursorDim));
          } else if (isMarked) {
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::ColorConvertFloat4ToU32(AntigravityColors::bgMarkedRow));
          } else if (isDiff) {
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                ImGui::ColorConvertFloat4ToU32(AntigravityColors::bgDiff));
          }

          // Column 0: Nazwa
          ImGui::TableSetColumnIndex(0);
          ImGui::PushID(row);

          ImGui::Dummy(ImVec2(4.0f, 1.0f));
          ImGui::SameLine(0, 0);
          ImVec2 iconPos = ImGui::GetCursorScreenPos();
          ImGui::Dummy(ImVec2(18.0f, rowHeight - 2.0f));
          ImGui::SameLine(0, 6);

          ImVec4 textCol = AntigravityColors::textPrimary;
          if (isMarked) {
            textCol = AntigravityColors::markedGold;
          } else if (item.isParentEntry) {
            textCol = AntigravityColors::accent;
          } else if (item.isNavigableDirectory()) {
            textCol = item.isHidden ? AntigravityColors::textSecondary
                                    : AntigravityColors::folderText;
          } else if (item.isArchive()) {
            textCol = AntigravityColors::archiveOrange;
          } else if (item.isExecutable) {
            textCol = AntigravityColors::successGreen;
          } else if (item.isHidden) {
            textCol = AntigravityColors::textMuted;
          }
          if (!isActive) {
            textCol.x *= 0.68f;
            textCol.y *= 0.68f;
            textCol.z *= 0.72f;
            textCol.w = 0.78f;
          }

          // Clean folder names without noisy [...] brackets (keep [..] only for
          // parent entry)
          std::string displayLabel =
              item.isParentEntry
                  ? ".."
                  : (item.isNavigableDirectory() ? item.name
                                                 : item.displayStem);

          ImGui::PushStyleColor(ImGuiCol_Text, textCol);
          ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                AntigravityColors::bgHover);
          ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                AntigravityColors::bgCursorActive);

          ImGuiSelectableFlags sFlags = ImGuiSelectableFlags_SpanAllColumns |
                                        ImGuiSelectableFlags_AllowDoubleClick;
          if (ImGui::Selectable(displayLabel.c_str(), isCursor, sFlags,
                                ImVec2(0, rowHeight - 2.0f))) {
            engine_.setActiveSide(pane.side);
            ImGuiIO &io = ImGui::GetIO();
            if (io.KeyAlt || io.KeySuper) {
              tab.toggleSelection(item.path);
            } else if (io.KeyShift) {
              tab.selectRange(item.path);
  } else {
              if (!tab.selectedPaths.contains(item.path)) {
                tab.deselectAll();
              }
              tab.setCursorOnly(item.path);
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
              engine_.openItem(item, pane.side);
            }
          }
          ImGui::PopStyleColor(4);

          drawRowPrefixIcon(item, iconPos);

          // Draw crisp 1px Total Commander cursor rectangle around the active
          // cursor row
          if (isCursor && isActive) {
            ImVec2 rMin = ImGui::GetItemRectMin();
            ImVec2 rMax = ImGui::GetItemRectMax();
            rMin.x = ImGui::GetWindowPos().x + 1.0f;
            rMax.x = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 2.0f;
            ImGui::GetWindowDrawList()->AddRect(
                rMin, rMax,
                ImGui::ColorConvertFloat4ToU32(AntigravityColors::borderActive),
                0.0f, 0, 1.0f);
          }

          if (isCursor && scrollReq) {
            ImGui::SetScrollHereY(0.5f);
            scrollReq = false;
          }

          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                              ImVec2(14.0f, 10.0f));
          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 8.0f));
          if (!item.isParentEntry &&
              ImGui::BeginPopupContextItem("##TCRowCtx")) {
            engine_.setActiveSide(pane.side);
            tab.setCursorOnly(item.path);
            if (ImGui::MenuItem("Otwórz", "Enter"))
              engine_.openItem(item, pane.side);
            if (ImGui::MenuItem("Zaznacz / Odznacz", "Spacja"))
              tab.toggleSelection(item.path);
            ImGui::Separator();
            if (ImGui::MenuItem("Podgląd Lister", "F3"))
              engine_.triggerF3View();
            if (ImGui::MenuItem("Szybki podgląd w panelu", "Ctrl+Q"))
              engine_.toggleQuickView();
            if (ImGui::MenuItem("Quick Look macOS", "⌘Y"))
              engine_.triggerQuickLook();
            if (!item.isNavigableDirectory() && ImGui::MenuItem("Edytuj", "F4"))
              engine_.triggerF4Edit();
            ImGui::Separator();
            if (ImGui::MenuItem("Kopiuj", "F5"))
              engine_.triggerF5Copy();
            if (ImGui::MenuItem("Przenieś", "F6"))
              engine_.triggerF6Move();
            if (ImGui::MenuItem("Zmień nazwę", "Shift+F6"))
              engine_.triggerRename();
            if (ImGui::MenuItem("Utwórz Symlink w 2. panelu…"))
              engine_.triggerCreateLink(false, false);
            if (ImGui::MenuItem("Zmień atrybuty POSIX…"))
              engine_.triggerChmod();
            if (ImGui::MenuItem("Pakuj do .7z / .zip…", "Alt+F5"))
              engine_.triggerPackArchive();
            if (item.isArchive() &&
                ImGui::MenuItem("Rozpakuj archiwum…", "Alt+F9"))
              engine_.triggerUnpackArchive();
            if (!item.isNavigableDirectory() &&
                ImGui::MenuItem("Sumy kontrolne SHA-256 / MD5…"))
              engine_.triggerChecksums();
            ImGui::Separator();
            if (ImGui::MenuItem("Pokaż w Finderze"))
              platform::revealInFinder({item.path});
            if (ImGui::MenuItem("Kopiuj ścieżkę"))
              engine_.copySelectedPathsToClipboard();
            ImGui::Separator();
            if (ImGui::MenuItem("Usuń", "F8"))
              engine_.triggerF8Delete();
            ImGui::EndPopup();
          }
          ImGui::PopStyleVar(2);

          // Column 1: Rozsz.
          ImGui::TableSetColumnIndex(1);
          auto dimIfInactive = [isActive](ImVec4 c) {
            if (!isActive) {
              c.x *= 0.65f;
              c.y *= 0.65f;
              c.z *= 0.70f;
              c.w = 0.72f;
            }
            return c;
          };
          if (fonts_.mono)
            ImGui::PushFont(fonts_.mono);
          if (!item.isNavigableDirectory() && !item.fileExtension.empty()) {
            ImGui::TextColored(
                dimIfInactive(isMarked ? AntigravityColors::markedGold
                                       : AntigravityColors::textSecondary),
                "%s", item.fileExtension.c_str());
          }

          if (!tab.briefMode) {
            // Column 2: Rozmiar
            ImGui::TableSetColumnIndex(2);
            std::string sizeStr = item.formattedSize();
            int64_t entryBytes = -1;
            if (!item.isParentEntry) {
              if (item.isNavigableDirectory()) {
                auto itSz = tab.directorySizes.find(item.path);
                if (itSz != tab.directorySizes.end()) {
                  entryBytes = itSz->second;
                  sizeStr = formatExactBytes(entryBytes);
                } else if (tab.diskUsageMode) {
                  sizeStr = "...";
                }
              } else if (item.size.has_value()) {
                entryBytes = *item.size;
              }
            }

            float colW = ImGui::GetColumnWidth();
            if (tab.diskUsageMode && !item.isParentEntry && entryBytes >= 0 &&
                maxDiskBytes > 0 && totalDiskBytes > 0) {
              double pct =
                  (static_cast<double>(entryBytes) * 100.0) /
                  static_cast<double>(totalDiskBytes);
              float barFrac = static_cast<float>(
                  static_cast<double>(entryBytes) /
                  static_cast<double>(maxDiskBytes));
              barFrac = std::clamp(barFrac, 0.0f, 1.0f);

              ImVec2 cellPos = ImGui::GetCursorScreenPos();
              ImU32 barCol = (pct >= 40.0)
                                 ? IM_COL32(249, 38, 114, 95)
                                 : (pct >= 15.0 ? IM_COL32(230, 219, 116, 85)
                                                : IM_COL32(102, 217, 239, 75));
              if (barFrac > 0.0f) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(cellPos.x, cellPos.y + 2.0f),
                    ImVec2(cellPos.x + std::max(3.0f, (colW - 4.0f) * barFrac),
                           cellPos.y + rowHeight - 2.0f),
                    barCol, 2.0f);
              }
              char pctBuf[48];
              std::snprintf(pctBuf, sizeof(pctBuf), "%s (%.1f%%)",
                            formatByteCount(entryBytes).c_str(), pct);
              sizeStr = pctBuf;
            }

            ImVec4 szCol = dimIfInactive(
                isMarked ? AntigravityColors::markedGold
                         : (item.isNavigableDirectory() && !tab.diskUsageMode
                                ? AntigravityColors::textMuted
                                : AntigravityColors::textSecondary));
            float textW = ImGui::CalcTextSize(sizeStr.c_str()).x;
            if (colW > textW + 6.0f) {
              ImGui::SetCursorPosX(ImGui::GetCursorPosX() + colW - textW -
                                   6.0f);
            }
            ImGui::TextColored(szCol, "%s", sizeStr.c_str());

            // Column 3: Data
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(
                dimIfInactive(isMarked ? AntigravityColors::markedGold
                                       : AntigravityColors::textMuted),
                "%s", item.formattedDate().c_str());

            // Column 4: Atryb.
            ImGui::TableSetColumnIndex(4);
            if (!item.isParentEntry) {
              ImGui::TextColored(dimIfInactive(AntigravityColors::textMuted),
                                 "%s", item.posixPermissions.c_str());
            }
          }
          if (fonts_.mono)
            ImGui::PopFont();

          ImGui::PopID();
        }
      }
      ImGui::EndTable();
      // Clicking empty space below rows inside the table clears selection
      ImGuiIO &io = ImGui::GetIO();
      if (ImGui::IsItemHovered() &&
          ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
          !io.KeySuper && !io.KeyShift) {
        tab.deselectAll();
      }
    }
    ImGui::PopStyleColor(3);
  }

  // 4. Total Commander Status Footer Box (inside Blue Panel)
  {
    ImVec4 panelBlue =
        isActive ? AntigravityColors::bgPanel : AntigravityColors::bgPanelDim;
    ImVec4 innerGrid = isActive ? AntigravityColors::borderPanel
                                : AntigravityColors::borderPanelInactive;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, panelBlue);
    ImGui::PushStyleColor(ImGuiCol_Border, innerGrid);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 3.0f));
    ImGui::BeginChild("##TCFooterBox", ImVec2(width, footerH),
                      ImGuiChildFlags_Borders);

    PaneFooterStats st = tab.computeFooterStats();
    const FileItem *cur = tab.cursorItem();
    if (fonts_.mono)
      ImGui::PushFont(fonts_.mono);
    if (cur && cur->isSymbolicLink && !cur->symlinkTarget.empty()) {
      ImGui::TextColored(AntigravityColors::accent, "-> %s   |   ",
                         cur->symlinkTarget.c_str());
      ImGui::SameLine(0, 0);
    }
    if (st.selectedFiles > 0 || st.selectedDirs > 0) {
      ImGui::TextColored(AntigravityColors::markedGold,
                         "%s z %s w %zu/%zu plik(ach) i %zu/%zu kat.",
                         formatByteCount(st.selectedBytes).c_str(),
                         formatByteCount(st.totalBytes).c_str(),
                         st.selectedFiles, st.totalFiles, st.selectedDirs,
                         st.totalDirs);
  } else {
      ImGui::TextColored(isActive ? AntigravityColors::textSecondary
                                  : AntigravityColors::textMuted,
                         "%s w %zu plik(ach), %zu kat.",
                         formatByteCount(st.totalBytes).c_str(), st.totalFiles,
                         st.totalDirs);
    }
    if (fonts_.mono)
      ImGui::PopFont();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(2);
  ImGui::PopID();
}

void CommanderUI::renderQuickViewPane(PaneSide side, float width,
                                      float height) {
  ImGui::PushID(side == PaneSide::Left ? "QuickViewL" : "QuickViewR");
  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgPanel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  ImGui::BeginChild("##QuickViewCleanBox", ImVec2(width, height),
                    ImGuiChildFlags_Borders);

  const FileItem *item = engine_.activePane().activeTab().cursorItem();
  ImGui::TextColored(
      AntigravityColors::accent, "SZYBKI PODGLĄD (Ctrl+Q)  —  %s",
      (item && !item->isParentEntry) ? item->name.c_str() : "[..]");
  ImGui::SameLine(width - 150.0f);
  if (boxButton("Tekst", !engine_.quickView.hexMode))
    engine_.quickView.hexMode = false;
  ImGui::SameLine();
  if (boxButton("HEX", engine_.quickView.hexMode))
    engine_.quickView.hexMode = true;
  ImGui::SameLine();
  if (boxButton("×"))
    engine_.quickView.active = false;
  ImGui::Separator();

  if (!item || item->isParentEntry) {
    ImGui::Spacing();
    ImGui::TextColored(
        AntigravityColors::textMuted,
        "Najedź kursorem na plik lub katalog w aktywnym panelu.");
  } else if (engine_.quickView.isDirectory) {
    ImGui::Spacing();
    if (fonts_.mono)
      ImGui::PushFont(fonts_.mono);
    ImGui::TextColored(AntigravityColors::folderBlue, "Katalog: %s",
                       item->path.c_str());
    ImGui::Spacing();
    ImGui::Text("Bezpośrednie podkatalogi : %lld",
                static_cast<long long>(engine_.quickView.dirCount));
    ImGui::Text("Bezpośrednie pliki       : %lld",
                static_cast<long long>(engine_.quickView.fileCount));
    ImGui::Text("Rozmiar plików w kat.    : %s (%s B)",
                formatByteCount(engine_.quickView.totalBytes).c_str(),
                formatExactBytes(engine_.quickView.totalBytes).c_str());
    ImGui::Text("Uprawnienia POSIX        : %s",
                item->posixPermissions.c_str());
    ImGui::Text("Ostatnia modyfikacja     : %s", item->formattedDate().c_str());
    if (fonts_.mono)
      ImGui::PopFont();
  } else if (engine_.quickView.isImage &&
             engine_.quickView.imageTexture != nullptr) {
    ImGui::BeginChild("##QVImageScroll", ImVec2(0, 0));
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float iw = static_cast<float>(std::max(1, engine_.quickView.imageWidth));
    float ih = static_cast<float>(std::max(1, engine_.quickView.imageHeight));
    float maxH = std::max(40.0f, avail.y - 24.0f);
    float maxW = std::max(40.0f, avail.x - 8.0f);
    float scale = std::min(1.0f, std::min(maxW / iw, maxH / ih));
    float dw = iw * scale;
    float dh = ih * scale;
    if (dw < avail.x) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - dw) * 0.5f);
    }
    ImGui::Image((ImTextureID)(intptr_t)engine_.quickView.imageTexture,
                 ImVec2(dw, dh));
    ImGui::TextColored(AntigravityColors::textMuted, "%d x %d px",
                       engine_.quickView.imageWidth,
                       engine_.quickView.imageHeight);
    ImGui::EndChild();
  } else {
    if (fonts_.mono)
      ImGui::PushFont(fonts_.mono);
    const std::string &content = engine_.quickView.hexMode
                                     ? engine_.quickView.hexContent
                                     : engine_.quickView.textContent;
    ImGui::BeginChild("##QVScroll", ImVec2(0, 0));
    ImGui::TextUnformatted(content.c_str());
    ImGui::EndChild();
    if (fonts_.mono)
      ImGui::PopFont();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::PopID();
}

void CommanderUI::renderInfoPane(PaneSide side, float width, float height) {
  ImGui::PushID(side == PaneSide::Left ? "MCInfoL" : "MCInfoR");
  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgPanel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
  ImGui::BeginChild("##MCInfoCleanBox", ImVec2(width, height),
                    ImGuiChildFlags_Borders);

  ImGui::TextColored(AntigravityColors::accent,
                     "MIDNIGHT COMMANDER — PANEL INFO (Ctrl+I)");
  ImGui::SameLine(width - 42.0f);
  if (boxButton("×"))
    engine_.infoPanelActive = false;
  ImGui::Separator();

  const FileItem *item = engine_.activePane().activeTab().cursorItem();
  if (!item) {
    ImGui::TextColored(AntigravityColors::textMuted,
                       "Brak wybranego elementu.");
  } else if (fonts_.mono) {
    ImGui::PushFont(fonts_.mono);
    ImGui::Spacing();
    ImGui::TextColored(AntigravityColors::markedGold, "Plik           : %s",
                       item->name.c_str());
    ImGui::Text("Ścieżka        : %s", item->path.c_str());
    ImGui::Text("Typ            : %s", item->typeDescription.c_str());
    if (!item->isParentEntry) {
      char modeBuf[32];
      std::snprintf(modeBuf, sizeof(modeBuf), "%s (0%03o)",
                    item->posixPermissions.c_str(), item->posixMode & 0777);
      ImGui::Text("Uprawnienia    : %s", modeBuf);
      ImGui::Text("Właściciel     : %s", item->ownerGroup.c_str());
      ImGui::Text("Inode          : %llu",
                  static_cast<unsigned long long>(item->inode));
      ImGui::Text("Rozmiar        : %s (%s)", item->formattedSize().c_str(),
                  item->size.has_value() ? formatByteCount(*item->size).c_str()
                                         : "katalog");
      ImGui::Text("Modyfikacja    : %s", item->formattedDate().c_str());
      if (item->creationTime.has_value()) {
        ImGui::Text("Utworzono      : %s",
                    formatTimestamp(*item->creationTime).c_str());
      }
      if (item->isSymbolicLink) {
        ImGui::TextColored(AntigravityColors::accent, "Dowiązanie ->  : %s",
                           item->symlinkTarget.c_str());
      }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(AntigravityColors::accent,
                       "System plików (aktywny wolumin):");
    auto freeB = engine_.activePane().activeTab().availableSpaceBytes;
    if (freeB.has_value()) {
      ImGui::Text("Wolne miejsce  : %s (%s B)", formatByteCount(*freeB).c_str(),
                  formatExactBytes(*freeB).c_str());
    }
    ImGui::PopFont();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::PopID();
}

static void appendUtf8(std::string &out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

void CommanderUI::handleTerminalInput() {
  ImGuiIO &io = ImGui::GetIO();
  if (!engine_.ptyTerminal.isRunning() || io.WantTextInput)
    return;

  bool ctrl = io.KeyCtrl;
  bool cmd = io.KeySuper;

  if (cmd && ImGui::IsKeyPressed(ImGuiKey_V)) {
    if (const char *clip = ImGui::GetClipboardText()) {
      engine_.ptyTerminal.writeString(clip);
      terminalScrollOffset_ = 0;
    }
    return;
  }

  // Forward typed Unicode characters
  if (io.InputQueueCharacters.Size > 0 && !ctrl && !cmd) {
    std::string utf8;
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      ImWchar ch = io.InputQueueCharacters[i];
      if (ch >= 0x20 && ch != 0x7F) {
        appendUtf8(utf8, static_cast<uint32_t>(ch));
      }
    }
    if (!utf8.empty()) {
      engine_.ptyTerminal.writeString(utf8);
      terminalScrollOffset_ = 0;
    }
  }

  // Control keys A..Z (except Ctrl+O handled above)
  if (ctrl && !cmd) {
    for (int k = 0; k < 26; ++k) {
      ImGuiKey key = static_cast<ImGuiKey>(ImGuiKey_A + k);
      if (key == ImGuiKey_O)
        continue;
      if (ImGui::IsKeyPressed(key)) {
        char ch = static_cast<char>(1 + k);
        engine_.ptyTerminal.writeBytes(&ch, 1);
        terminalScrollOffset_ = 0;
      }
    }
    return;
  }

  struct KeySeq {
    ImGuiKey key;
    const char *seq;
  };
  static constexpr KeySeq kSpecials[] = {
      {ImGuiKey_Enter, "\r"},
      {ImGuiKey_KeypadEnter, "\r"},
      {ImGuiKey_Backspace, "\x7f"},
      {ImGuiKey_Tab, "\t"},
      {ImGuiKey_Escape, "\x1b"},
      {ImGuiKey_UpArrow, "\x1b[A"},
      {ImGuiKey_DownArrow, "\x1b[B"},
      {ImGuiKey_RightArrow, "\x1b[C"},
      {ImGuiKey_LeftArrow, "\x1b[D"},
      {ImGuiKey_Home, "\x1b[H"},
      {ImGuiKey_End, "\x1b[F"},
      {ImGuiKey_PageUp, "\x1b[5~"},
      {ImGuiKey_PageDown, "\x1b[6~"},
      {ImGuiKey_Delete, "\x1b[3~"},
  };
  for (const auto &ks : kSpecials) {
    if (ImGui::IsKeyPressed(ks.key)) {
      engine_.ptyTerminal.writeString(ks.seq);
      terminalScrollOffset_ = 0;
    }
  }
}

void CommanderUI::renderSubshellView(float width, float height) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgCanvas);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
  ImGui::BeginChild("##MCSubshellBox", ImVec2(width, height),
                    ImGuiChildFlags_Borders);

  std::string shellCwd = engine_.ptyTerminal.currentWorkingDirectory();
  if (shellCwd.empty())
    shellCwd = engine_.activePane().activeTab().currentPath;

  ImGui::TextColored(
      AntigravityColors::accent,
      "INTERAKTYWNA POWŁOKA (zsh / forkpty)   —   %s",
      shellCwd.c_str());
  ImGui::SameLine(std::max(240.0f, width - 310.0f));
  if (!engine_.ptyTerminal.isRunning()) {
    if (boxButton("Uruchom zsh ponownie", true)) {
      engine_.ptyTerminal.start(engine_.activePane().activeTab().currentPath);
    }
  } else {
    if (boxButton("Wyczyść (Ctrl+L)")) {
      engine_.ptyTerminal.writeBytes("\x0c", 1);
    }
  }
  ImGui::SameLine();
  if (boxButton("Wróć do paneli (Ctrl+O)", true)) {
    engine_.toggleSubshell();
  }
  ImGui::Separator();

  if (fonts_.mono)
    ImGui::PushFont(fonts_.mono);

  ImGui::BeginChild("##SubshellPtyArea", ImVec2(0, 0),
                    ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollWithMouse);

  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float cellW = ImGui::CalcTextSize("M").x;
  float cellH = ImGui::GetFontSize() * 1.25f;
  if (cellW < 4.0f)
    cellW = 8.0f;
  if (cellH < 8.0f)
    cellH = 16.0f;

  int cols = std::max(20, static_cast<int>(avail.x / cellW));
  int rows = std::max(5, static_cast<int>(avail.y / cellH));
  engine_.ptyTerminal.resize(rows, cols);

  TerminalSnapshot snap = engine_.ptyTerminal.snapshot();
  if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
    terminalScrollOffset_ += static_cast<int>(ImGui::GetIO().MouseWheel * 3.0f);
    terminalScrollOffset_ =
        std::clamp(terminalScrollOffset_, 0,
                   static_cast<int>(snap.scrollback.size()));
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  int sbSize = static_cast<int>(snap.scrollback.size());
  int startGlobalRow = std::max(0, sbSize - terminalScrollOffset_);

  auto drawRowCells = [&](int visualRow, const std::vector<TerminalCell> &row) {
    float y = origin.y + visualRow * cellH;
    int limit = std::min(cols, static_cast<int>(row.size()));
    std::string runUtf8;
    uint32_t runFg = 0;
    int runStartCol = 0;

    auto flushRun = [&]() {
      if (runUtf8.empty())
        return;
      dl->AddText(ImVec2(origin.x + runStartCol * cellW, y), runFg,
                  runUtf8.c_str());
      runUtf8.clear();
    };

    for (int c = 0; c < limit; ++c) {
      const auto &cell = row[static_cast<size_t>(c)];
      if (cell.bgRgba != 0) {
        flushRun();
        dl->AddRectFilled(
            ImVec2(origin.x + c * cellW, y),
            ImVec2(origin.x + (c + 1) * cellW, y + cellH), cell.bgRgba);
      }
      if (cell.cp == ' ' || cell.cp == 0) {
        flushRun();
        continue;
      }
      if (!runUtf8.empty() && cell.fgRgba != runFg) {
        flushRun();
      }
      if (runUtf8.empty()) {
        runStartCol = c;
        runFg = cell.fgRgba;
      }
      appendUtf8(runUtf8, cell.cp);
    }
    flushRun();
  };

  for (int r = 0; r < rows; ++r) {
    int gIdx = startGlobalRow + r;
    if (gIdx < sbSize) {
      drawRowCells(r, snap.scrollback[static_cast<size_t>(gIdx)]);
    } else {
      int scrIdx = gIdx - sbSize;
      if (scrIdx >= 0 && scrIdx < static_cast<int>(snap.screen.size())) {
        drawRowCells(r, snap.screen[static_cast<size_t>(scrIdx)]);
      }
    }
  }

  if (snap.cursorVisible && terminalScrollOffset_ == 0 &&
      engine_.ptyTerminal.isRunning()) {
    int curVisualRow = snap.cursorRow;
    if (curVisualRow >= 0 && curVisualRow < rows && snap.cursorCol >= 0 &&
        snap.cursorCol < cols) {
      float cx = origin.x + snap.cursorCol * cellW;
      float cy = origin.y + curVisualRow * cellH;
      dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cellW, cy + cellH),
                        IM_COL32(166, 226, 46, 150));
    }
  }

  ImGui::EndChild();
  if (fonts_.mono)
    ImGui::PopFont();

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void CommanderUI::renderInspector(float width, float height) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgPanel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  ImGui::BeginChild("##InspectorPane", ImVec2(width, height),
                    ImGuiChildFlags_Borders);

  const FileItem *item = engine_.activePane().activeTab().primaryTargetItem();
  if (!item) {
    ImGui::TextColored(AntigravityColors::textMuted,
                       "Brak wybranego elementu.");
  } else {
    ImGui::TextColored(AntigravityColors::accent, "%s", item->name.c_str());
    ImGui::TextColored(AntigravityColors::textSecondary, "%s",
                       item->typeDescription.c_str());
    ImGui::Separator();

    auto row = [&](const char *k, const std::string &v) {
      ImGui::TextColored(AntigravityColors::textMuted, "%s", k);
      if (fonts_.mono)
        ImGui::PushFont(fonts_.mono);
      ImGui::TextWrapped("%s", v.c_str());
      if (fonts_.mono)
        ImGui::PopFont();
      ImGui::Spacing();
    };

    row("Rozmiar:", item->formattedSize());
    row("Modyfikacja:", item->formattedDate());
    row("Uprawnienia POSIX:", item->posixPermissions);
    row("Ścieżka:", item->path);

    if (boxButton("Otwórz"))
      engine_.openItem(*item);
    ImGui::SameLine();
    if (boxButton("Quick Look"))
      platform::showQuickLook(item->path);
    ImGui::SameLine();
    if (boxButton("Finder"))
      platform::revealInFinder({item->path});
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void CommanderUI::renderCommandBar() {
  if (engine_.isCommandOutputVisible && engine_.lastCommandResult.has_value()) {
    const auto &res = *engine_.lastCommandResult;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgCanvas);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    ImGui::BeginChild("##CmdOutputDrawer", ImVec2(0, 130),
                      ImGuiChildFlags_Borders);
    ImGui::TextColored(res.exitCode == 0 ? AntigravityColors::successGreen
                                         : AntigravityColors::dangerRed,
                       "$ %s   [kod wyjścia: %d]", res.command.c_str(),
                       res.exitCode);
    ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f);
    if (boxButton("Zamknij"))
      engine_.isCommandOutputVisible = false;
    ImGui::Separator();
    if (fonts_.mono)
      ImGui::PushFont(fonts_.mono);
    ImGui::BeginChild("##CmdOutputScroll", ImVec2(0, 0));
    ImGui::TextUnformatted(res.output.c_str());
    ImGui::EndChild();
    if (fonts_.mono)
      ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgHeader);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 3.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
  ImGui::BeginChild("##TCCommandBar", ImVec2(0, 28), ImGuiChildFlags_Borders);

  std::string cwd = engine_.activePane().activeTab().currentPath;
  if (fonts_.mono)
    ImGui::PushFont(fonts_.mono);
  ImGui::SetCursorPosY(4.0f);
  ImGui::TextColored(AntigravityColors::textSecondary, "%s>", cwd.c_str());
  ImGui::SameLine();

  float btnW = 84.0f;
  ImGui::SetCursorPosY(2.0f);
  ImGui::SetNextItemWidth(
      std::max(100.0f, ImGui::GetContentRegionAvail().x - btnW - 8.0f));
  if (focusCommandNextFrame_) {
    ImGui::SetKeyboardFocusHere();
    focusCommandNextFrame_ = false;
  }
  bool enter = ImGui::InputTextWithHint("##TCCmdInput", tr("cmd.hint"),
                                        &engine_.commandInput,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  if (fonts_.mono)
    ImGui::PopFont();

  ImGui::SameLine();
  if ((enter || boxButton(engine_.isRunningCommand ? "…" : tr("cmd.run"), false,
                          AntigravityColors::accent, ImVec2(btnW, 21.0f))) &&
      !engine_.isRunningCommand) {
    engine_.runCommandInput();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void CommanderUI::renderFunctionKeyBar() {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, AntigravityColors::bgHeader);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  ImGui::BeginChild("##TCFunctionKeyBar", ImVec2(0, 30),
                    ImGuiChildFlags_Borders);

  struct FKeyCell {
    const char *key;
    const char *labelKey;
    int actionID;
  };
  static const FKeyCell cells[10] = {
      {"F1", "fkey.1", 1},   {"F2", "fkey.2", 2}, {"F3", "fkey.3", 3},
      {"F4", "fkey.4", 4},   {"F5", "fkey.5", 5}, {"F6", "fkey.6", 6},
      {"F7", "fkey.7", 7},   {"F8", "fkey.8", 8}, {"F9", "fkey.9", 9},
      {"F10", "fkey.10", 10}};

  float totalW = ImGui::GetWindowWidth();
  float cellW = std::floor(totalW / 10.0f);

  for (int i = 0; i < 10; ++i) {
    if (i > 0)
      ImGui::SameLine(0.0f, 0.0f);
    float w = (i == 9) ? (totalW - cellW * 9.0f) : cellW;
    ImGui::PushID(i);

    ImGui::PushStyleColor(ImGuiCol_Button, AntigravityColors::bgHeader);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AntigravityColors::bgHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          AntigravityColors::bgCursorActive);
    ImGui::PushStyleColor(ImGuiCol_Border, AntigravityColors::borderSubtle);

    std::string full = std::string(cells[i].key) + " " + tr(cells[i].labelKey);
    if (ImGui::Button(full.c_str(), ImVec2(w, 30.0f))) {
      switch (cells[i].actionID) {
      case 1:
        engine_.triggerF1Help();
        break;
      case 2:
        engine_.triggerF2UserMenu();
        break;
      case 3:
        engine_.triggerF3View();
        break;
      case 4:
        engine_.triggerF4Edit();
        break;
      case 5:
        engine_.triggerF5Copy();
        break;
      case 6:
        engine_.triggerF6Move();
        break;
      case 7:
        engine_.triggerF7NewFolder();
        break;
      case 8:
        engine_.triggerF8Delete();
        break;
      case 9:
        openTopMenuNextFrame_ = true;
        break;
      case 10:
        std::exit(0);
        break;
      }
    }
    ImGui::PopStyleColor(4);
    ImGui::PopID();
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void CommanderUI::renderHotlistAndHistoryPopups() {
  if (engine_.requestHotlistPopup) {
    ImGui::OpenPopup("##DirectoryHotlistPopup");
    engine_.requestHotlistPopup = false;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  if (ImGui::BeginPopup("##DirectoryHotlistPopup")) {
    ImGui::TextColored(AntigravityColors::accent, "DIRECTORY HOTLIST (⌘D)");
    ImGui::Separator();
    std::string curPath = engine_.pane(hotlistSide_).activeTab().currentPath;
    for (const auto &fav : engine_.favorites) {
      bool isCur = (fav.path == curPath);
      std::string itemLabel = fav.title + "   [" + fav.path + "]";
      if (ImGui::MenuItem(itemLabel.c_str(), nullptr, isCur)) {
        engine_.navigate(fav.path, hotlistSide_);
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("+ Dodaj bieżący katalog do Hotlisty")) {
      engine_.pinFolderToFavorites(curPath);
    }
    ImGui::EndPopup();
  }

  if (engine_.requestHistoryPopup) {
    ImGui::OpenPopup("##DirectoryHistoryPopup");
    engine_.requestHistoryPopup = false;
  }
  if (ImGui::BeginPopup("##DirectoryHistoryPopup")) {
    ImGui::TextColored(AntigravityColors::accent, "HISTORIA KATALOGÓW (⌥↓)");
    ImGui::Separator();
    auto &tab = engine_.pane(historySide_).activeTab();
    if (tab.historyBack.empty() && tab.historyForward.empty()) {
      ImGui::TextColored(AntigravityColors::textMuted,
                         "(Brak historii w tej karcie)");
  } else {
      for (auto it = tab.historyBack.rbegin(); it != tab.historyBack.rend();
           ++it) {
        if (ImGui::MenuItem(it->c_str())) {
          engine_.navigate(*it, historySide_);
        }
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
}

void CommanderUI::renderDialogs() {
  auto &d = engine_.dialog;
  if (d.kind == DialogKind::None && engine_.errorAlertMessage.empty())
    return;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

  if (!engine_.errorAlertMessage.empty()) {
    ImGui::OpenPopup("Błąd##Alert");
  }
  if (ImGui::BeginPopupModal("Błąd##Alert", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored(AntigravityColors::dangerRed, "Wystąpił błąd");
    ImGui::Separator();
    ImGui::TextWrapped("%s", engine_.errorAlertMessage.c_str());
    ImGui::Spacing();
    if (boxButton("OK", true)) {
      engine_.errorAlertMessage.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
    return;
  }

  auto centerNext = [](float w, float h) {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (w > 0 && h > 0)
      ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
  };

  switch (d.kind) {
  case DialogKind::NewFolder: {
    centerNext(420, 0);
    ImGui::OpenPopup("Nowy katalog (F7)");
    if (ImGui::BeginPopupModal("Nowy katalog (F7)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(AntigravityColors::textSecondary, "W katalogu: %s",
                         d.parentPath.c_str());
      ImGui::SetNextItemWidth(390);
      bool enter = ImGui::InputText("##NewFolderName", &d.inputBuffer,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Spacing();
      if (enter || boxButton("Utwórz", true)) {
        engine_.createFolder(d.inputBuffer, d.parentPath, d.side);
      }
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::Rename: {
    centerNext(420, 0);
    ImGui::OpenPopup("Zmień nazwę (Shift+F6)");
    if (ImGui::BeginPopupModal("Zmień nazwę (Shift+F6)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(AntigravityColors::textSecondary, "Element: %s",
                         d.targetItem.name.c_str());
      ImGui::SetNextItemWidth(390);
      bool enter = ImGui::InputText("##RenameInput", &d.inputBuffer,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Spacing();
      if (enter || boxButton("Zapisz", true)) {
        engine_.renameItem(d.targetItem, d.inputBuffer, d.side);
      }
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::TransferConfirm: {
    centerNext(540, 0);
    bool fromArchive = !d.fromArchivePath.empty();
    const char *title =
        fromArchive ? tr("dialog.extract.title")
                    : (d.isMove ? tr("dialog.move.title")
                                : tr("dialog.copy.title"));
    ImGui::OpenPopup(title);
    if (ImGui::BeginPopupModal(title, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::PushStyleColor(ImGuiCol_Text, AntigravityColors::accent);
      if (d.targetItems.size() == 1) {
        ImGui::Text("%s: %s",
                    fromArchive ? tr("dialog.extract.prompt")
                                : (d.isMove ? tr("dialog.move.prompt")
                                            : tr("dialog.copy.prompt")),
                    d.targetItems.front().name.c_str());
      } else {
        ImGui::Text("%s (%zu el.):",
                    fromArchive ? tr("dialog.extract.prompt")
                                : (d.isMove ? tr("dialog.move.prompt")
                                            : tr("dialog.copy.prompt")),
                    d.targetItems.size());
      }
      ImGui::PopStyleColor();
      ImGui::Spacing();
      ImGui::TextColored(AntigravityColors::textSecondary, "%s",
                         tr("dialog.transfer.dest"));
      if (fonts_.mono)
        ImGui::PushFont(fonts_.mono);
      ImGui::SetNextItemWidth(510);
      bool enter = ImGui::InputText("##DestPathInput", &d.destinationPath,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      if (fonts_.mono)
        ImGui::PopFont();

      if (!fromArchive) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(AntigravityColors::textSecondary, "%s",
                           tr("dialog.transfer.conflict"));
        int pol = static_cast<int>(d.conflictPolicy);
        ImGui::RadioButton(tr("dialog.transfer.ask"), &pol, 0);
        ImGui::SameLine();
        ImGui::RadioButton(tr("dialog.transfer.overwrite"), &pol, 1);
        ImGui::SameLine();
        ImGui::RadioButton(tr("dialog.transfer.skip"), &pol, 2);
        ImGui::SameLine();
        ImGui::RadioButton(tr("dialog.transfer.keep_both"), &pol, 3);
        d.conflictPolicy = static_cast<ConflictResolutionPolicy>(pol);
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      auto runTransfer = [&](bool bg) {
        if (d.fromRemote.has_value() || d.toRemote.has_value()) {
          engine_.startRemoteTransfer(d.fromRemote, d.toRemote, d.targetItems,
                                      d.destinationPath, bg);
        } else if (fromArchive) {
          std::vector<std::string> internalPaths;
          for (const auto &it : d.targetItems)
            internalPaths.push_back(it.path);
          engine_.startArchiveExtract(d.fromArchivePath, internalPaths,
                                      d.destinationPath);
        } else {
          std::vector<std::string> paths;
          for (const auto &it : d.targetItems)
            paths.push_back(it.path);
          engine_.startTransfer(paths, d.destinationPath, d.isMove,
                                d.conflictPolicy, bg);
        }
      };

      if (enter ||
          boxButton(fromArchive ? tr("dialog.extract.btn")
                                : (d.isMove ? tr("dialog.move.btn")
                                            : tr("dialog.copy.btn")),
                    true)) {
        runTransfer(false);
      }
      if (!fromArchive) {
        ImGui::SameLine();
        if (boxButton(tr("dialog.transfer.background"))) {
          runTransfer(true);
        }
      }
      ImGui::SameLine();
      if (boxButton(tr("common.cancel")))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::DeleteConfirm: {
    centerNext(440, 0);
    ImGui::OpenPopup("Usuń (F8)");
    if (ImGui::BeginPopupModal("Usuń (F8)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Czy usunąć %zu zaznaczonych pozycji?", d.targetItems.size());
      ImGui::Spacing();
      if (boxButton("Do Kosza", true)) {
        engine_.deleteItems(d.targetItems, false, d.side);
      }
      ImGui::SameLine();
      if (boxButton("Usuń trwale (Shift+F8)", false,
                    AntigravityColors::dangerRed)) {
        engine_.deleteItems(d.targetItems, true, d.side);
      }
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::Conflict: {
    centerNext(480, 0);
    ImGui::OpenPopup("Konflikt plików");
    if (ImGui::BeginPopupModal("Konflikt plików", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(AntigravityColors::warningAmber,
                         "Plik docelowy już istnieje:");
      ImGui::TextWrapped("%s", d.conflictDestPath.c_str());
      ImGui::Spacing();
      if (boxButton("Nadpisz", true))
        engine_.resolveConflict(ConflictResolutionPolicy::Overwrite);
      ImGui::SameLine();
      if (boxButton("Pomiń"))
        engine_.resolveConflict(ConflictResolutionPolicy::Skip);
      ImGui::SameLine();
      if (boxButton("Zachowaj oba"))
        engine_.resolveConflict(ConflictResolutionPolicy::KeepBoth);
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::GoToPath: {
    centerNext(480, 0);
    ImGui::OpenPopup("Przejdź do katalogu (⌘L)");
    if (ImGui::BeginPopupModal("Przejdź do katalogu (⌘L)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::SetNextItemWidth(450);
      bool enter = ImGui::InputText("##GoToPathInput", &d.inputBuffer,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Spacing();
      if (enter || boxButton("Przejdź", true)) {
        std::string p = d.inputBuffer;
        if (p == "~")
          p = platform::homeDirectory();
        else if (p.rfind("~/", 0) == 0)
          p = platform::homeDirectory() + p.substr(1);
        d.kind = DialogKind::None;
        engine_.navigate(p, d.side);
      }
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::SelectByPattern: {
    centerNext(400, 0);
    const char *title =
        d.isSelectingPattern ? "Zaznacz grupę (+)" : "Odznacz grupę (-)";
    ImGui::OpenPopup(title);
    if (ImGui::BeginPopupModal(title, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Maska plików (np. *.cpp;*.hpp):");
      ImGui::SetNextItemWidth(370);
      bool enter = ImGui::InputText("##PatternInput", &d.inputBuffer,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Spacing();
      if (enter || boxButton("OK", true)) {
        engine_.applyPatternSelection(d.inputBuffer, d.isSelectingPattern);
      }
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::MultiRename: {
    centerNext(780, 540);
    ImGui::OpenPopup("Narzędzie wielokrotnej zmiany nazw (⌘M)");
    if (ImGui::BeginPopupModal("Narzędzie wielokrotnej zmiany nazw (⌘M)")) {
      bool changed = false;
      ImGui::Text("Maska nazwy:");
      ImGui::SameLine(145);
      ImGui::SetNextItemWidth(220);
      changed |=
          ImGui::InputText("##MRNameMask", &d.multiRenameConfig.nameMask);
      ImGui::SameLine();
      if (boxButton("[N]")) {
        d.multiRenameConfig.nameMask += "[N]";
        changed = true;
      }
      ImGui::SameLine();
      if (boxButton("[C]")) {
        d.multiRenameConfig.nameMask += "[C]";
        changed = true;
      }
      ImGui::SameLine();
      if (boxButton("[YMD]")) {
        d.multiRenameConfig.nameMask += "[YMD]";
        changed = true;
      }
      ImGui::SameLine();
      if (boxButton("[hms]")) {
        d.multiRenameConfig.nameMask += "[hms]";
        changed = true;
      }

      ImGui::Text("Rozszerzenie:");
      ImGui::SameLine(145);
      ImGui::SetNextItemWidth(220);
      changed |=
          ImGui::InputText("##MRExtMask", &d.multiRenameConfig.extensionMask);

      ImGui::Text("Szukaj / Zamień:");
      ImGui::SameLine(145);
      ImGui::SetNextItemWidth(180);
      changed |= ImGui::InputTextWithHint("##MRSearch", "Szukaj…",
                                          &d.multiRenameConfig.searchText);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(180);
      changed |= ImGui::InputTextWithHint("##MRReplace", "Zamień na…",
                                          &d.multiRenameConfig.replaceText);
      ImGui::SameLine();
      changed |= ImGui::Checkbox("Regex", &d.multiRenameConfig.useRegex);

      ImGui::Text("Licznik (Start/Krok/Cyfry):");
      ImGui::SameLine(200);
      ImGui::SetNextItemWidth(60);
      changed |=
          ImGui::InputInt("##MRStart", &d.multiRenameConfig.counterStart, 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(60);
      changed |=
          ImGui::InputInt("##MRStep", &d.multiRenameConfig.counterStep, 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(60);
      changed |=
          ImGui::InputInt("##MRDigits", &d.multiRenameConfig.counterDigits, 0);

      if (changed)
        engine_.updateMultiRenamePreview();

      ImGui::Separator();
      float previewH = ImGui::GetContentRegionAvail().y - 36.0f;
      if (ImGui::BeginTable("##MRPreview", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_ScrollY,
                            ImVec2(0, previewH))) {
        ImGui::TableSetupColumn("Stara nazwa");
        ImGui::TableSetupColumn("Nowa nazwa");
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed,
                                90.0f);
        ImGui::TableHeadersRow();
        for (const auto &row : d.multiRenamePreview) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(row.originalName.c_str());
          ImGui::TableSetColumnIndex(1);
          ImGui::TextColored(AntigravityColors::accent, "%s",
                             row.proposedName.c_str());
          ImGui::TableSetColumnIndex(2);
          if (row.hasConflict)
            ImGui::TextColored(AntigravityColors::dangerRed, "Konflikt");
          else if (row.isChanged)
            ImGui::TextColored(AntigravityColors::successGreen, "OK");
          else
            ImGui::TextColored(AntigravityColors::textMuted, "Bez zmian");
        }
        ImGui::EndTable();
      }

      if (boxButton("Start!", true))
        engine_.applyMultiRename();
      ImGui::SameLine();
      if (boxButton("Zamknij"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::SearchFiles: {
    centerNext(780, 540);
    ImGui::OpenPopup("Szukaj plików (⌥F7)");
    if (ImGui::BeginPopupModal("Szukaj plików (⌥F7)")) {
      ImGui::Text("Szukaj w:");
      ImGui::SameLine(100);
      ImGui::SetNextItemWidth(460);
      ImGui::InputText("##SearchRoot", &d.searchRootPath);

      ImGui::Text("Szukaj plików:");
      ImGui::SameLine(100);
      ImGui::SetNextItemWidth(200);
      bool enter = ImGui::InputText("##SearchPat", &d.searchPattern,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      ImGui::Text("Tekst:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(200);
      enter |= ImGui::InputText("##SearchContent", &d.searchContent,
                                ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      ImGui::Checkbox("Ukryte", &d.searchIncludeHidden);

      if (d.isSearching) {
        if (boxButton("Zatrzymaj", false, AntigravityColors::dangerRed))
          engine_.stopSearch();
        ImGui::SameLine();
        ImGui::TextColored(AntigravityColors::accent, "Szukanie… (%zu trafień)",
                           d.searchResults.size());
  } else {
        if (enter || boxButton("Rozpocznij szukanie", true))
          engine_.startSearch();
        ImGui::SameLine();
        ImGui::TextColored(AntigravityColors::textSecondary, "Znaleziono: %zu",
                           d.searchResults.size());
      }

      ImGui::Separator();
      float resH = ImGui::GetContentRegionAvail().y - 34.0f;
      if (ImGui::BeginTable("##SearchResults", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_Resizable,
                            ImVec2(0, resH))) {
        ImGui::TableSetupColumn("Nazwa");
        ImGui::TableSetupColumn("Ścieżka względna");
        ImGui::TableSetupColumn("Rozmiar", ImGuiTableColumnFlags_WidthFixed,
                                90.0f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(d.searchResults.size()));
        while (clipper.Step()) {
          for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto &res = d.searchResults[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (ImGui::Selectable(res.name.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
              if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                engine_.navigateToSearchResult(res);
              }
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(AntigravityColors::textSecondary, "%s",
                               res.relativeFolder.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(AntigravityColors::textMuted, "%s",
                               res.isDirectory
                                   ? "<DIR>"
                                   : (res.size.has_value()
                                          ? formatExactBytes(*res.size).c_str()
                                          : "—"));
          }
        }
        ImGui::EndTable();
      }

      if (boxButton("Zamknij")) {
        engine_.stopSearch();
        d.kind = DialogKind::None;
      }
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::SyncDirs: {
    centerNext(820, 540);
    ImGui::OpenPopup("Synchronizuj katalogi");
    if (ImGui::BeginPopupModal("Synchronizuj katalogi")) {
      ImGui::TextColored(AntigravityColors::accent, "Lewy: %s",
                         d.syncLeftDir.c_str());
      ImGui::TextColored(AntigravityColors::accent, "Prawy: %s",
                         d.syncRightDir.c_str());
      ImGui::Checkbox("Pokaż [=>] Lewy do Prawego", &d.syncShowLeftToRight);
      ImGui::SameLine();
      ImGui::Checkbox("Pokaż [<=] Prawy do Lewego", &d.syncShowRightToLeft);
      ImGui::SameLine();
      ImGui::Checkbox("Pokaż [==] Identyczne", &d.syncShowEqual);
      ImGui::Separator();

      float tH = ImGui::GetContentRegionAvail().y - 36.0f;
      if (ImGui::BeginTable("##SyncTable", 5,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_ScrollY,
                            ImVec2(0, tH))) {
        ImGui::TableSetupColumn("Plik", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Lewy rozmiar",
                                ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Kierunek", ImGuiTableColumnFlags_WidthFixed,
                                80.0f);
        ImGui::TableSetupColumn("Prawy rozmiar",
                                ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Synchr.", ImGuiTableColumnFlags_WidthFixed,
                                60.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < d.syncRows.size(); ++i) {
          auto &row = d.syncRows[i];
          if (row.direction == 0 && !d.syncShowEqual)
            continue;
          if (row.direction > 0 && !d.syncShowLeftToRight)
            continue;
          if (row.direction < 0 && !d.syncShowRightToLeft)
            continue;

          ImGui::TableNextRow();
          ImGui::PushID(static_cast<int>(i));
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(row.relativeName.c_str());
          ImGui::TableSetColumnIndex(1);
          ImGui::TextColored(AntigravityColors::textSecondary, "%s",
                             row.leftSize.has_value()
                                 ? formatExactBytes(*row.leftSize).c_str()
                                 : "—");
          ImGui::TableSetColumnIndex(2);
          if (row.direction > 0)
            ImGui::TextColored(AntigravityColors::successGreen, "  ==>  ");
          else if (row.direction < 0)
            ImGui::TextColored(AntigravityColors::warningAmber, "  <==  ");
          else
            ImGui::TextColored(AntigravityColors::textMuted, "  ===  ");
          ImGui::TableSetColumnIndex(3);
          ImGui::TextColored(AntigravityColors::textSecondary, "%s",
                             row.rightSize.has_value()
                                 ? formatExactBytes(*row.rightSize).c_str()
                                 : "—");
          ImGui::TableSetColumnIndex(4);
          if (row.direction != 0) {
            ImGui::Checkbox("##SyncSel", &row.selected);
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      if (boxButton("Synchronizuj zaznaczone", true))
        engine_.executeSyncDirs();
      ImGui::SameLine();
      if (boxButton("Zamknij"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::PackArchive: {
    centerNext(520, 0);
    ImGui::OpenPopup("Pakuj pliki (⌥F5)");
    if (ImGui::BeginPopupModal("Pakuj pliki (⌥F5)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Spakuj %zu pozycji do archiwum:", d.targetItems.size());
      ImGui::SetNextItemWidth(490);
      ImGui::InputText("##PackOutPath", &d.destinationPath);

      ImGui::Spacing();
      ImGui::Text("Format archiwum:");
      int fmt = static_cast<int>(d.packConfig.format);
      int prevFmt = fmt;
      ImGui::RadioButton("7-Zip (.7z)", &fmt, 0);
      ImGui::SameLine();
      ImGui::RadioButton("ZIP (.zip)", &fmt, 1);
      ImGui::SameLine();
      ImGui::RadioButton("TAR.GZ (.tar.gz)", &fmt, 2);
      if (fmt != prevFmt) {
        d.packConfig.format = static_cast<ArchiveFormat>(fmt);
        auto replaceExt = [&](const std::string &newExt) {
          for (const char *oldExt : {".tar.gz", ".7z", ".zip"}) {
            std::string oe(oldExt);
            if (d.destinationPath.size() > oe.size() &&
                d.destinationPath.ends_with(oe)) {
              d.destinationPath = d.destinationPath.substr(
                                      0, d.destinationPath.size() - oe.size()) +
                                  newExt;
              return;
            }
          }
          d.destinationPath += newExt;
        };
        if (d.packConfig.format == ArchiveFormat::SevenZip)
          replaceExt(".7z");
        else if (d.packConfig.format == ArchiveFormat::Zip)
          replaceExt(".zip");
        else
          replaceExt(".tar.gz");
      }

      if (d.packConfig.format != ArchiveFormat::TarGz) {
        ImGui::Spacing();
        ImGui::Text(
            "Poziom kompresji (0 = bez kompresji, 5 = normalna, 9 = ultra):");
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("##CompLevel", &d.packConfig.compressionLevel, 0, 9);

        ImGui::Separator();
        ImGui::TextColored(AntigravityColors::accent,
                           "Szyfrowanie AES-256 (opcjonalne):");
        ImGuiInputTextFlags pwFlags =
            d.packConfig.showPassword ? 0 : ImGuiInputTextFlags_Password;
        ImGui::Text("Hasło:");
        ImGui::SameLine(130);
        ImGui::SetNextItemWidth(240);
        ImGui::InputTextWithHint("##PackPw1", "Pozostaw puste bez szyfrowania",
                                 &d.packConfig.password, pwFlags);
        ImGui::SameLine();
        ImGui::Checkbox("Pokaż", &d.packConfig.showPassword);

        ImGui::Text("Powtórz hasło:");
        ImGui::SameLine(130);
        ImGui::SetNextItemWidth(240);
        ImGui::InputText("##PackPw2", &d.packConfig.passwordConfirm, pwFlags);

        if (!d.packConfig.password.empty() &&
            d.packConfig.password != d.packConfig.passwordConfirm) {
          ImGui::TextColored(AntigravityColors::dangerRed,
                             "Hasła nie są zgodne!");
        }
        if (d.packConfig.format == ArchiveFormat::SevenZip) {
          ImGui::Checkbox(
              "Szyfruj również nazwy plików i strukturę katalogów (-mhe=on)",
              &d.packConfig.encryptHeaders);
        }
      }

      ImGui::Spacing();
      if (boxButton("Pakuj (Enter)", true))
        engine_.executePackArchive();
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::UnpackArchive: {
    centerNext(500, 0);
    ImGui::OpenPopup("Rozpakuj archiwum (⌥F9)");
    if (ImGui::BeginPopupModal("Rozpakuj archiwum (⌥F9)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(AntigravityColors::accent, "Archiwum: %s",
                         d.targetItem.name.c_str());
      ImGui::Text("Rozpakuj do katalogu:");
      ImGui::SetNextItemWidth(470);
      ImGui::InputText("##UnpackDest", &d.destinationPath);

      ImGui::Separator();
      ImGui::Text("Hasło (jeśli archiwum .7z / .zip jest zaszyfrowane):");
      ImGuiInputTextFlags pwFlags =
          d.unpackShowPassword ? 0 : ImGuiInputTextFlags_Password;
      ImGui::SetNextItemWidth(360);
      bool enter = ImGui::InputTextWithHint(
          "##UnpackPw", "Opcjonalne hasło…", &d.unpackPassword,
          pwFlags | ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      ImGui::Checkbox("Pokaż", &d.unpackShowPassword);

      ImGui::Spacing();
      if (enter || boxButton("Rozpakuj", true))
        engine_.executeUnpackArchive();
      ImGui::SameLine();
      if (boxButton("Podgląd zawartości (F3)")) {
        FileItem target = d.targetItem;
        engine_.openViewer(target);
      }
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::Chmod: {
    centerNext(380, 0);
    ImGui::OpenPopup("Zmień atrybuty POSIX (chmod)");
    if (ImGui::BeginPopupModal("Zmień atrybuty POSIX (chmod)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Zaznaczono: %zu el.", d.targetItems.size());
      ImGui::Separator();
      auto bitBox = [&](const char *label, uint16_t bit) {
        bool set = (d.chmodMode & bit) != 0;
        if (ImGui::Checkbox(label, &set)) {
          if (set)
            d.chmodMode |= bit;
          else
            d.chmodMode &= static_cast<uint16_t>(~bit);
        }
      };
      ImGui::Text("Właściciel:");
      ImGui::SameLine(110);
      bitBox("r##ur", 0400);
      ImGui::SameLine();
      bitBox("w##uw", 0200);
      ImGui::SameLine();
      bitBox("x##ux", 0100);
      ImGui::Text("Grupa:");
      ImGui::SameLine(110);
      bitBox("r##gr", 0040);
      ImGui::SameLine();
      bitBox("w##gw", 0020);
      ImGui::SameLine();
      bitBox("x##gx", 0010);
      ImGui::Text("Inni:");
      ImGui::SameLine(110);
      bitBox("r##or", 0004);
      ImGui::SameLine();
      bitBox("w##ow", 0002);
      ImGui::SameLine();
      bitBox("x##ox", 0001);

      char octBuf[16];
      std::snprintf(octBuf, sizeof(octBuf), "Ósemkowo: 0%03o",
                    d.chmodMode & 0777);
      ImGui::TextColored(AntigravityColors::accent, "%s", octBuf);
      ImGui::Checkbox("Rekurencyjnie dla podkatalogów", &d.chmodRecursive);
      ImGui::Spacing();
      if (boxButton("Zastosuj", true))
        engine_.executeChmod();
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::Checksums: {
    centerNext(760, 420);
    ImGui::OpenPopup("Sumy kontrolne SHA-256 / MD5");
    if (ImGui::BeginPopupModal("Sumy kontrolne SHA-256 / MD5")) {
      if (d.isComputingChecksums) {
        ImGui::TextColored(AntigravityColors::accent,
                           "Obliczanie sum kontrolnych…");
  } else {
        float tH = ImGui::GetContentRegionAvail().y - 36.0f;
        if (ImGui::BeginTable("##ChecksumTable", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable,
                              ImVec2(0, tH))) {
          ImGui::TableSetupColumn("Plik", ImGuiTableColumnFlags_WidthFixed,
                                  170.0f);
          ImGui::TableSetupColumn("SHA-256",
                                  ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn("MD5", ImGuiTableColumnFlags_WidthFixed,
                                  230.0f);
          ImGui::TableHeadersRow();
          if (fonts_.mono)
            ImGui::PushFont(fonts_.mono);
          for (const auto &c : d.checksumEntries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(c.fileName.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(AntigravityColors::accent, "%s",
                               c.sha256.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(AntigravityColors::textSecondary, "%s",
                               c.md5.c_str());
          }
          if (fonts_.mono)
            ImGui::PopFont();
          ImGui::EndTable();
        }
      }
      if (!d.isComputingChecksums &&
          boxButton("Kopiuj SHA-256 do schowka", true)) {
        std::string out;
        for (const auto &c : d.checksumEntries) {
          out += c.sha256 + "  " + c.fileName + "\n";
        }
        platform::copyToClipboard(out);
        engine_.showToast("Skopiowano sumy SHA-256");
      }
      ImGui::SameLine();
      if (boxButton("Zamknij"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::Viewer: {
    centerNext(840, 580);
    ImGui::OpenPopup("Lister (F3)");
    if (ImGui::BeginPopupModal("Lister (F3)")) {
      ImGui::TextColored(AntigravityColors::accent, "%s",
                         d.targetItem.name.c_str());
      if (d.viewerIsImage && d.viewerImageTexture != nullptr) {
        ImGui::SameLine(0, 16);
        ImGui::TextColored(AntigravityColors::textMuted, "%d x %d px",
                           d.viewerImageWidth, d.viewerImageHeight);
      } else {
        ImGui::SameLine(0, 16);
        if (boxButton("1: Tekst", !d.viewerHexMode))
          d.viewerHexMode = false;
        ImGui::SameLine();
        if (boxButton("3: HEX", d.viewerHexMode))
          d.viewerHexMode = true;
      }
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 75.0f);
      if (boxButton("Zamknij"))
        d.kind = DialogKind::None;
      ImGui::Separator();

      if (d.viewerIsImage && d.viewerImageTexture != nullptr) {
        ImGui::BeginChild("##ViewerImageScroll", ImVec2(0, 0),
                          ImGuiChildFlags_Borders);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float iw = static_cast<float>(std::max(1, d.viewerImageWidth));
        float ih = static_cast<float>(std::max(1, d.viewerImageHeight));
        float scale =
            std::min(1.0f, std::min(std::max(40.0f, avail.x - 16.0f) / iw,
                                    std::max(40.0f, avail.y - 16.0f) / ih));
        float dw = iw * scale;
        float dh = ih * scale;
        ImGui::SetCursorPos(ImVec2(
            ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - dw) * 0.5f),
            ImGui::GetCursorPosY() + std::max(0.0f, (avail.y - dh) * 0.5f)));
        ImGui::Image((ImTextureID)(intptr_t)d.viewerImageTexture,
                     ImVec2(dw, dh));
        ImGui::EndChild();
      } else {
        if (fonts_.mono)
          ImGui::PushFont(fonts_.mono);
        const std::string &content =
            d.viewerHexMode ? d.viewerHexText : d.viewerText;
        ImGui::BeginChild("##ViewerScroll", ImVec2(0, 0),
                          ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(content.c_str());
        ImGui::EndChild();
        if (fonts_.mono)
          ImGui::PopFont();
      }
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::Editor: {
    centerNext(860, 600);
    ImGui::OpenPopup("Edytor (F4)");
    if (ImGui::BeginPopupModal("Edytor (F4)")) {
      ImGui::TextColored(AntigravityColors::accent, "%s",
                         d.targetItem.name.c_str());
      ImGui::SameLine(0, 16);
      if (boxButton("Zapisz (F2)", true))
        engine_.saveEditor();
      ImGui::SameLine();
      if (boxButton("Zamknij (Esc)"))
        d.kind = DialogKind::None;
      ImGui::Separator();

      if (fonts_.mono)
        ImGui::PushFont(fonts_.mono);
      ImVec2 avail = ImGui::GetContentRegionAvail();
      if (ImGui::InputTextMultiline("##EditorBuf", &d.editorBuffer, avail)) {
        d.editorDirty = true;
      }
      if (fonts_.mono)
        ImGui::PopFont();
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::Help: {
    centerNext(720, 480);
    ImGui::OpenPopup(
        "Pomoc — Skróty Total Commander & Midnight Commander (F1)");
    if (ImGui::BeginPopupModal(
            "Pomoc — Skróty Total Commander & Midnight Commander (F1)")) {
      if (fonts_.mono)
        ImGui::PushFont(fonts_.mono);
      ImGui::BeginChild("##HelpScroll",
                        ImVec2(0, ImGui::GetContentRegionAvail().y - 36.0f),
                        ImGuiChildFlags_Borders);
      ImGui::TextColored(AntigravityColors::markedGold,
                         "%s v%s  —  C++20 / Metal", MCX_APP_NAME,
                         MCX_FULL_VERSION);
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::TextColored(AntigravityColors::accent,
                         "KLASYCZNE KLAWISZE FUNKCYJNE (F1..F10):");
      ImGui::Text("  F1            - Pomoc i lista skrótów");
      ImGui::Text("  F2            - Menu użytkownika Midnight Commander "
                  "(Shift+F2 / Shift+F6 = Zmień nazwę)");
      ImGui::Text("  F3            - Podgląd pliku Lister / lista archiwum .7z "
                  "/ rozmiar katalogu");
      ImGui::Text("  F4            - Wbudowany edytor pliku tekstowego");
      ImGui::Text("  F5 / Alt+F5   - Kopiuj do drugiego panelu / Pakuj do .7z "
                  "(z AES-256), .zip, .tar.gz");
      ImGui::Text("  F6 / Shift+F6 - Przenieś do drugiego panelu / Szybka "
                  "zmiana nazwy");
      ImGui::Text(
          "  F7 / Alt+F7   - Utwórz nowy katalog / Wyszukaj pliki i tekst");
      ImGui::Text(
          "  F8 / Delete   - Usuń zaznaczone pliki (do Kosza lub trwale)");
      ImGui::Text("  F9 / Alt+F9   - Otwórz górne menu / Rozpakuj archiwum "
                  "(.7z, .zip, .tar.gz)");
      ImGui::Text("  F10           - Zakończ program");
      ImGui::Spacing();
      ImGui::TextColored(AntigravityColors::accent,
                         "SKRÓTY MIDNIGHT COMMANDER (MC):");
      ImGui::Text("  Ctrl+O        - Przełącz pełnoekranowy podgląd powłoki "
                  "zsh (Subshell)");
      ImGui::Text("  Ctrl+I        - Panel Info MC w drugim oknie (inode, "
                  "właściciel, symlink, chmod)");
      ImGui::Text(
          "  Ctrl+Q        - Szybki podgląd pliku w drugim oknie (Quick View)");
      ImGui::Text(
          "  Alt+O         - Otwórz podświetlony katalog w drugim panelu");
      ImGui::Text("  Alt+.         - Pokaż / ukryj pliki zaczynające się od "
                  "kropki (dotfiles)");
      ImGui::Text(
          "  Alt+T         - Przełącz tryb listy: Pełny (5 kolumn) / Krótki");
      ImGui::Text("  Alt+Enter     - Wstaw nazwę pliku spod kursora do wiersza "
                  "poleceń");
      ImGui::Spacing();
      ImGui::TextColored(AntigravityColors::accent,
                         "ZAZNACZANIE I NAWIGACJA (TC / MC):");
      ImGui::Text("  Insert / Spacja - Zaznacz/odznacz pozycję pod kursorem "
                  "(Spacja liczy rozmiar kat.)");
      ImGui::Text("  + / - / *       - Zaznacz grupę maską / Odznacz grupę / "
                  "Odwróć zaznaczenie");
      ImGui::Text("  Ctrl+D / ⌘D     - Podręczna lista ulubionych katalogów "
                  "(Directory Hotlist)");
      ImGui::Text("  Ctrl+U / ⌘U     - Zamień miejscami lewy i prawy panel");
      ImGui::Text("  Alt+Shift+Enter - Oblicz rozmiary wszystkich podkatalogów "
                  "w panelu");
      ImGui::EndChild();
      if (fonts_.mono)
        ImGui::PopFont();
      if (boxButton("Zamknij (Esc)", true))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::UserMenu: {
    centerNext(540, 0);
    ImGui::OpenPopup("Menu Użytkownika Midnight Commander (F2)");
    if (ImGui::BeginPopupModal("Menu Użytkownika Midnight Commander (F2)",
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(
          AntigravityColors::accent,
          "Wybierz operację dla bieżącego zaznaczenia / katalogu:");
      ImGui::Separator();
      ImVec2 bSize(510.0f, 26.0f);
      for (int i = 0; i < static_cast<int>(d.userMenuItems.size()); ++i) {
        const auto &item = d.userMenuItems[static_cast<size_t>(i)];
        std::string label =
            (item.key.empty() ? "" : (item.key + ". ")) + item.label;
        ImGui::PushID(i);
        if (boxButton(label.c_str(), false, AntigravityColors::accent, bSize)) {
          engine_.executeUserMenuItem(item);
        }
        ImGui::PopID();
      }
      ImGui::Spacing();
      ImGui::TextColored(AntigravityColors::textMuted,
                         "Konfiguracja: ~/.config/mcx/menu.json");
      ImGui::Spacing();
      if (boxButton("Anuluj (Esc)"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::CreateLink: {
    centerNext(520, 0);
    const char *title =
        d.linkIsHard
            ? "Utwórz dowiązanie twarde (Hardlink)"
            : (d.linkIsRelative ? "Utwórz względny Symlink"
                                : "Utwórz dowiązanie symboliczne (Symlink)");
    ImGui::OpenPopup(title);
    if (ImGui::BeginPopupModal(title, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Obiekt docelowy ( na co wskazuje ):");
      ImGui::SetNextItemWidth(490);
      ImGui::InputText("##LinkTarget", &d.inputBuffer);
      ImGui::Text("Ścieżka nowego dowiązania:");
      ImGui::SetNextItemWidth(490);
      bool enter = ImGui::InputText("##LinkDest", &d.destinationPath,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      if (!d.linkIsHard) {
        ImGui::Checkbox("Użyj ścieżki względnej (Relative Symlink)",
                        &d.linkIsRelative);
      }
      ImGui::Spacing();
      if (enter || boxButton("Utwórz dowiązanie", true))
        engine_.createLink();
      ImGui::SameLine();
      if (boxButton("Anuluj"))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::DiffViewer: {
    centerNext(880, 600);
    ImGui::OpenPopup("Porównanie plików (diff -u)");
    if (ImGui::BeginPopupModal("Porównanie plików (diff -u)")) {
      ImGui::TextColored(AntigravityColors::dangerRed, "--- Lewy : %s",
                         d.diffLeftPath.c_str());
      ImGui::TextColored(AntigravityColors::successGreen, "+++ Prawy: %s",
                         d.diffRightPath.c_str());
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.0f);
      if (boxButton("Zamknij"))
        d.kind = DialogKind::None;
      ImGui::Separator();

      if (fonts_.mono)
        ImGui::PushFont(fonts_.mono);
      ImGui::BeginChild("##DiffScroll", ImVec2(0, 0), ImGuiChildFlags_Borders);
      std::istringstream iss(d.diffOutput);
      std::string line;
      while (std::getline(iss, line)) {
        ImVec4 col = AntigravityColors::textPrimary;
        if (line.rfind("@@", 0) == 0)
          col = AntigravityColors::accent;
        else if (line.rfind("+", 0) == 0)
          col = AntigravityColors::successGreen;
        else if (line.rfind("-", 0) == 0)
          col = AntigravityColors::dangerRed;
        ImGui::TextColored(col, "%s", line.c_str());
      }
      ImGui::EndChild();
      if (fonts_.mono)
        ImGui::PopFont();
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::CreateFile: {
    centerNext(460, 0);
    ImGui::OpenPopup(tr("dialog.create_file.title"));
    if (ImGui::BeginPopupModal(tr("dialog.create_file.title"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("%s", tr("dialog.create_file.prompt"));
      if (fonts_.mono)
        ImGui::PushFont(fonts_.mono);
      ImGui::SetNextItemWidth(430);
      bool enter = ImGui::InputText("##CreateFileName", &d.inputBuffer,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      if (fonts_.mono)
        ImGui::PopFont();
      ImGui::Spacing();
      if (enter || boxButton(tr("dialog.create_file.btn"), true)) {
        engine_.createEmptyFileAndEdit(d.inputBuffer, d.parentPath, d.side);
      }
      ImGui::SameLine();
      if (boxButton(tr("common.cancel")))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::ConnectRemote: {
    centerNext(520, 0);
    ImGui::OpenPopup(tr("dialog.connect_remote.title"));
    if (ImGui::BeginPopupModal(tr("dialog.connect_remote.title"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("%s", tr("dialog.connect_remote.prompt"));
      if (fonts_.mono)
        ImGui::PushFont(fonts_.mono);
      ImGui::SetNextItemWidth(490);
      bool enter = ImGui::InputText("##RemoteUriInput", &d.inputBuffer,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      if (fonts_.mono)
        ImGui::PopFont();
      ImGui::TextColored(AntigravityColors::textMuted, "%s",
                         tr("dialog.connect_remote.hint"));
      ImGui::Spacing();
      if (enter || boxButton(tr("dialog.connect_remote.btn"), true)) {
        engine_.connectRemote(d.inputBuffer, d.side);
      }
      ImGui::SameLine();
      if (boxButton(tr("common.cancel")))
        d.kind = DialogKind::None;
      ImGui::EndPopup();
    }
    break;
  }
  case DialogKind::None:
    break;
  }

  ImGui::PopStyleVar(2);
}

void CommanderUI::renderToastAndProgress() {
  const ImGuiViewport *vp = ImGui::GetMainViewport();

  if (engine_.operationProgress.has_value()) {
    const auto &prog = *engine_.operationProgress;
    if (prog.inBackground) {
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                     vp->WorkPos.y + vp->WorkSize.y - 44.0f),
                              ImGuiCond_Always, ImVec2(0.5f, 1.0f));
      ImGui::SetNextWindowSize(ImVec2(520, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, AntigravityColors::bgHeader);
      ImGui::PushStyleColor(ImGuiCol_Border, AntigravityColors::accent);
      ImGui::Begin("##ProgressBgOverlay", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::TextColored(AntigravityColors::accent, "[%s]",
                         tr("dialog.transfer.in_background"));
      ImGui::SameLine();
      ImGui::Text("%s (%d/%d) %s", prog.title.c_str(), prog.completedCount,
                  prog.totalCount, prog.currentItemName.c_str());
      ImGui::ProgressBar(prog.fraction(), ImVec2(410, 8));
      ImGui::SameLine();
      if (boxButton(tr("common.cancel")))
        engine_.cancelActiveTransfer();
      ImGui::End();
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar();
    } else {
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                     vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                              ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(500, 0));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
      ImGui::PushStyleColor(ImGuiCol_WindowBg, AntigravityColors::bgPanel);
      ImGui::PushStyleColor(ImGuiCol_Border, AntigravityColors::accent);
      ImGui::Begin("##ProgressModalOverlay", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::TextColored(AntigravityColors::accent, "%s (%d/%d)",
                         prog.title.c_str(), prog.completedCount,
                         prog.totalCount);
      ImGui::Separator();
      ImGui::TextWrapped("%s", prog.currentItemName.empty()
                                   ? "..."
                                   : prog.currentItemName.c_str());
      ImGui::Spacing();
      ImGui::ProgressBar(prog.fraction(), ImVec2(470, 14));
      if (prog.totalBytes > 0) {
        ImGui::TextColored(AntigravityColors::textSecondary, "%s / %s",
                           formatByteCount(prog.currentBytes).c_str(),
                           formatByteCount(prog.totalBytes).c_str());
      }
      ImGui::Spacing();
      if (boxButton(tr("dialog.transfer.background"), true))
        engine_.moveTransferToBackground();
      ImGui::SameLine();
      if (boxButton(tr("common.cancel")))
        engine_.cancelActiveTransfer();
      ImGui::End();
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar();
    }
  }

  if (!engine_.toastMessage.empty()) {
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y - 64.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, AntigravityColors::bgElevated);
    ImGui::PushStyleColor(ImGuiCol_Border, AntigravityColors::accent);
    ImGui::Begin("##ToastOverlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs);
    ImGui::TextColored(AntigravityColors::textPrimary, "%s",
                       engine_.toastMessage.c_str());
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
  }
}

} // namespace mc::ui
