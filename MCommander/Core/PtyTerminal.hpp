#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mc {

struct TerminalCell {
    uint32_t cp = ' ';
    uint32_t fgRgba = 0xFFF8F8F2; // Default Monokai light text (ABGR/RGBA packed as 0xAABBGGRR for ImU32)
    uint32_t bgRgba = 0x00000000; // 0 = transparent (default canvas)
    bool bold = false;
};

struct TerminalSnapshot {
    int rows = 24;
    int cols = 80;
    int cursorRow = 0;
    int cursorCol = 0;
    bool cursorVisible = true;
    std::vector<std::vector<TerminalCell>> scrollback;
    std::vector<std::vector<TerminalCell>> screen;
};

class PtyTerminal {
public:
    PtyTerminal();
    ~PtyTerminal();

    bool isRunning() const { return running_.load(); }
    bool start(const std::string& initialDir);
    void stop();

    void resize(int rows, int cols);
    void writeBytes(const char* data, size_t len);
    void writeString(const std::string& s) { writeBytes(s.data(), s.size()); }
    void changeDirectory(const std::string& path);

    std::string currentWorkingDirectory() const;
    TerminalSnapshot snapshot() const;

private:
    enum class ParseState {
        Normal,
        Esc,
        Csi,
        Osc,
        OscEsc
    };

    int masterFd_ = -1;
    int childPid_ = -1;
    std::atomic<bool> running_{false};
    std::thread readerThread_;
    mutable std::mutex mutex_;

    int rows_ = 24;
    int cols_ = 80;
    int cursorRow_ = 0;
    int cursorCol_ = 0;
    int savedRow_ = 0;
    int savedCol_ = 0;
    int scrollTop_ = 0;
    int scrollBottom_ = 23;
    bool cursorVisible_ = true;
    bool altScreen_ = false;

    uint32_t curFg_ = 0xFFF2F8F8; // ImGui IM_COL32(0xF8, 0xF8, 0xF2, 0xFF)
    uint32_t curBg_ = 0x00000000;
    bool curBold_ = false;

    std::vector<std::vector<TerminalCell>> screen_;
    std::vector<std::vector<TerminalCell>> altSavedScreen_;
    std::deque<std::vector<TerminalCell>> scrollback_;

    ParseState parseState_ = ParseState::Normal;
    std::string csiBuf_;
    uint32_t utf8Codepoint_ = 0;
    int utf8Remaining_ = 0;

    void readerLoop();
    void feedBytes(const char* data, size_t len);
    void processByte(uint8_t b);
    void putCodepoint(uint32_t cp);
    void handleCsi(char finalChar);
    void handleSgr(const std::vector<int>& params);
    void newLine();
    void scrollUp(int top, int bottom, int count);
    void scrollDown(int top, int bottom, int count);
    void clearRange(int r1, int c1, int r2, int c2);
    void ensureValidCursor();
};

} // namespace mc
