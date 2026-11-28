#include "PtyTerminal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <libproc.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

namespace mc {

namespace {

constexpr uint32_t packCol32(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(r);
}

constexpr uint32_t kDefaultFg = packCol32(0xF8, 0xF8, 0xF2, 0xFF);

uint32_t ansi16Color(int idx) {
    // Monokai-inspired 16-color palette
    static constexpr uint32_t kPalette[16] = {
        packCol32(0x27, 0x28, 0x22), // 0 black
        packCol32(0xF9, 0x26, 0x72), // 1 red
        packCol32(0xA6, 0xE2, 0x2E), // 2 green
        packCol32(0xE6, 0xDB, 0x74), // 3 yellow
        packCol32(0x66, 0xD9, 0xEF), // 4 blue/cyan
        packCol32(0xAE, 0x81, 0xFF), // 5 magenta
        packCol32(0x66, 0xD9, 0xEF), // 6 cyan
        packCol32(0xF8, 0xF8, 0xF2), // 7 white
        packCol32(0x75, 0x71, 0x5E), // 8 bright black
        packCol32(0xFF, 0x59, 0x95), // 9 bright red
        packCol32(0xB6, 0xF2, 0x3E), // 10 bright green
        packCol32(0xFE, 0xF0, 0x8A), // 11 bright yellow
        packCol32(0x88, 0xE8, 0xFF), // 12 bright blue
        packCol32(0xC4, 0x9F, 0xFF), // 13 bright magenta
        packCol32(0x9E, 0xF0, 0xFF), // 14 bright cyan
        packCol32(0xFF, 0xFF, 0xFF), // 15 bright white
    };
    return kPalette[idx & 15];
}

uint32_t xterm256Color(int n) {
    if (n < 0) return kDefaultFg;
    if (n < 16) return ansi16Color(n);
    if (n < 232) {
        int c = n - 16;
        int b = c % 6;
        int g = (c / 6) % 6;
        int r = c / 36;
        auto scale = [](int v) -> uint8_t { return v == 0 ? 0 : static_cast<uint8_t>(55 + v * 40); };
        return packCol32(scale(r), scale(g), scale(b));
    }
    uint8_t gray = static_cast<uint8_t>(8 + (n - 232) * 10);
    return packCol32(gray, gray, gray);
}

std::string shellSingleQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

} // namespace

PtyTerminal::PtyTerminal() {
    screen_.assign(static_cast<size_t>(rows_), std::vector<TerminalCell>(static_cast<size_t>(cols_)));
}

PtyTerminal::~PtyTerminal() {
    stop();
}

bool PtyTerminal::start(const std::string& initialDir) {
    if (running_.load()) return true;

    struct winsize ws{};
    ws.ws_row = static_cast<unsigned short>(rows_);
    ws.ws_col = static_cast<unsigned short>(cols_);

    int master = -1;
    pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        if (!initialDir.empty()) {
            chdir(initialDir.c_str());
        }
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        const char* shell = getenv("SHELL");
        if (!shell || !shell[0]) shell = "/bin/zsh";
        execlp(shell, shell, "-i", nullptr);
        _exit(127);
    }

    int flags = fcntl(master, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(master, F_SETFL, flags | O_NONBLOCK);
    }

    masterFd_ = master;
    childPid_ = pid;
    running_.store(true);
    readerThread_ = std::thread(&PtyTerminal::readerLoop, this);
    return true;
}

void PtyTerminal::stop() {
    bool wasRunning = running_.exchange(false);
    int fd = masterFd_;
    int pid = childPid_;
    masterFd_ = -1;
    childPid_ = -1;

    if (fd >= 0) {
        close(fd);
    }
    if (wasRunning && pid > 0) {
        kill(pid, SIGHUP);
        int status = 0;
        waitpid(pid, &status, WNOHANG);
    }
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
}

void PtyTerminal::resize(int rows, int cols) {
    rows = std::clamp(rows, 4, 200);
    cols = std::clamp(cols, 10, 400);

    std::lock_guard<std::mutex> lock(mutex_);
    if (rows == rows_ && cols == cols_) return;

    std::vector<std::vector<TerminalCell>> next(static_cast<size_t>(rows), std::vector<TerminalCell>(static_cast<size_t>(cols)));
    int copyRows = std::min(rows_, rows);
    int copyCols = std::min(cols_, cols);
    for (int r = 0; r < copyRows; ++r) {
        for (int c = 0; c < copyCols; ++c) {
            next[static_cast<size_t>(r)][static_cast<size_t>(c)] = screen_[static_cast<size_t>(r)][static_cast<size_t>(c)];
        }
    }
    screen_ = std::move(next);
    rows_ = rows;
    cols_ = cols;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    ensureValidCursor();

    if (masterFd_ >= 0) {
        struct winsize ws{};
        ws.ws_row = static_cast<unsigned short>(rows_);
        ws.ws_col = static_cast<unsigned short>(cols_);
        ioctl(masterFd_, TIOCSWINSZ, &ws);
    }
}

void PtyTerminal::writeBytes(const char* data, size_t len) {
    if (!running_.load() || masterFd_ < 0 || !data || len == 0) return;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(masterFd_, data + sent, len - sent);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            usleep(1000);
        } else {
            break;
        }
    }
}

void PtyTerminal::changeDirectory(const std::string& path) {
    if (!running_.load() || path.empty()) return;
    std::string cmd = " cd -q " + shellSingleQuote(path) + "\n";
    writeString(cmd);
}

std::string PtyTerminal::currentWorkingDirectory() const {
    if (childPid_ <= 0) return "";
    struct proc_vnodepathinfo vpi{};
    int ret = proc_pidinfo(childPid_, PROC_PIDVNODEPATHINFO, 0, &vpi, sizeof(vpi));
    if (ret <= 0) return "";
    if (vpi.pvi_cdir.vip_path[0] == '\0') return "";
    return std::string(vpi.pvi_cdir.vip_path);
}

TerminalSnapshot PtyTerminal::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    TerminalSnapshot snap;
    snap.rows = rows_;
    snap.cols = cols_;
    snap.cursorRow = cursorRow_;
    snap.cursorCol = cursorCol_;
    snap.cursorVisible = cursorVisible_;
    snap.screen = screen_;
    snap.scrollback.assign(scrollback_.begin(), scrollback_.end());
    return snap;
}

void PtyTerminal::readerLoop() {
    char buf[8192];
    while (running_.load()) {
        int fd = masterFd_;
        if (fd < 0) break;

        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, 50);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;

        if (pfd.revents & (POLLIN)) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                feedBytes(buf, static_cast<size_t>(n));
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                break;
            }
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }
    }
    running_.store(false);
}

void PtyTerminal::feedBytes(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < len; ++i) {
        processByte(static_cast<uint8_t>(data[i]));
    }
}

void PtyTerminal::processByte(uint8_t b) {
    if (parseState_ == ParseState::Osc) {
        if (b == 0x07) {
            parseState_ = ParseState::Normal;
        } else if (b == 0x1B) {
            parseState_ = ParseState::OscEsc;
        }
        return;
    }
    if (parseState_ == ParseState::OscEsc) {
        parseState_ = (b == '\\') ? ParseState::Normal : ParseState::Osc;
        return;
    }
    if (parseState_ == ParseState::Esc) {
        if (b == '[') {
            parseState_ = ParseState::Csi;
            csiBuf_.clear();
        } else if (b == ']') {
            parseState_ = ParseState::Osc;
        } else if (b == '7') {
            savedRow_ = cursorRow_;
            savedCol_ = cursorCol_;
            parseState_ = ParseState::Normal;
        } else if (b == '8') {
            cursorRow_ = savedRow_;
            cursorCol_ = savedCol_;
            ensureValidCursor();
            parseState_ = ParseState::Normal;
        } else if (b == 'M') {
            if (cursorRow_ <= scrollTop_) scrollDown(scrollTop_, scrollBottom_, 1);
            else cursorRow_--;
            parseState_ = ParseState::Normal;
        } else if (b == '(' || b == ')' || b == '*' || b == '+') {
            // Skip charset designator next byte simplistically
            parseState_ = ParseState::Normal;
        } else {
            parseState_ = ParseState::Normal;
        }
        return;
    }
    if (parseState_ == ParseState::Csi) {
        if (b >= 0x40 && b <= 0x7E) {
            handleCsi(static_cast<char>(b));
            parseState_ = ParseState::Normal;
        } else {
            if (csiBuf_.size() < 64) csiBuf_.push_back(static_cast<char>(b));
        }
        return;
    }

    // Normal state
    if (b == 0x1B) {
        parseState_ = ParseState::Esc;
        utf8Remaining_ = 0;
        return;
    }
    if (b == '\r') {
        cursorCol_ = 0;
        return;
    }
    if (b == '\n') {
        newLine();
        return;
    }
    if (b == '\b') {
        if (cursorCol_ > 0) cursorCol_--;
        return;
    }
    if (b == '\t') {
        int nextTab = ((cursorCol_ / 8) + 1) * 8;
        cursorCol_ = std::min(cols_ - 1, nextTab);
        return;
    }
    if (b < 0x20) {
        return;
    }

    // UTF-8 decoding
    if ((b & 0x80) == 0) {
        utf8Remaining_ = 0;
        putCodepoint(b);
    } else if ((b & 0xE0) == 0xC0) {
        utf8Codepoint_ = b & 0x1F;
        utf8Remaining_ = 1;
    } else if ((b & 0xF0) == 0xE0) {
        utf8Codepoint_ = b & 0x0F;
        utf8Remaining_ = 2;
    } else if ((b & 0xF8) == 0xF0) {
        utf8Codepoint_ = b & 0x07;
        utf8Remaining_ = 3;
    } else if ((b & 0xC0) == 0x80 && utf8Remaining_ > 0) {
        utf8Codepoint_ = (utf8Codepoint_ << 6) | (b & 0x3F);
        utf8Remaining_--;
        if (utf8Remaining_ == 0) {
            putCodepoint(utf8Codepoint_);
        }
    } else {
        utf8Remaining_ = 0;
    }
}

void PtyTerminal::putCodepoint(uint32_t cp) {
    if (cursorCol_ >= cols_) {
        cursorCol_ = 0;
        newLine();
    }
    ensureValidCursor();
    auto& cell = screen_[static_cast<size_t>(cursorRow_)][static_cast<size_t>(cursorCol_)];
    cell.cp = cp;
    cell.fgRgba = curFg_;
    cell.bgRgba = curBg_;
    cell.bold = curBold_;
    cursorCol_++;
}

void PtyTerminal::newLine() {
    if (cursorRow_ >= scrollBottom_) {
        scrollUp(scrollTop_, scrollBottom_, 1);
    } else {
        cursorRow_++;
    }
}

void PtyTerminal::scrollUp(int top, int bottom, int count) {
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, top, rows_ - 1);
    count = std::max(1, count);
    for (int s = 0; s < count; ++s) {
        if (!altScreen_ && top == 0) {
            scrollback_.push_back(screen_[0]);
            if (scrollback_.size() > 1000) scrollback_.pop_front();
        }
        for (int r = top; r < bottom; ++r) {
            screen_[static_cast<size_t>(r)] = std::move(screen_[static_cast<size_t>(r + 1)]);
        }
        screen_[static_cast<size_t>(bottom)].assign(static_cast<size_t>(cols_), TerminalCell{' ', curFg_, 0, false});
    }
}

void PtyTerminal::scrollDown(int top, int bottom, int count) {
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, top, rows_ - 1);
    count = std::max(1, count);
    for (int s = 0; s < count; ++s) {
        for (int r = bottom; r > top; --r) {
            screen_[static_cast<size_t>(r)] = std::move(screen_[static_cast<size_t>(r - 1)]);
        }
        screen_[static_cast<size_t>(top)].assign(static_cast<size_t>(cols_), TerminalCell{' ', curFg_, 0, false});
    }
}

void PtyTerminal::clearRange(int r1, int c1, int r2, int c2) {
    r1 = std::clamp(r1, 0, rows_ - 1);
    r2 = std::clamp(r2, 0, rows_ - 1);
    for (int r = r1; r <= r2; ++r) {
        int startC = (r == r1) ? std::clamp(c1, 0, cols_ - 1) : 0;
        int endC = (r == r2) ? std::clamp(c2, 0, cols_ - 1) : (cols_ - 1);
        for (int c = startC; c <= endC; ++c) {
            screen_[static_cast<size_t>(r)][static_cast<size_t>(c)] = TerminalCell{' ', kDefaultFg, 0, false};
        }
    }
}

void PtyTerminal::ensureValidCursor() {
    cursorRow_ = std::clamp(cursorRow_, 0, rows_ - 1);
    cursorCol_ = std::clamp(cursorCol_, 0, cols_ - 1);
}

void PtyTerminal::handleCsi(char finalChar) {
    bool isPrivate = !csiBuf_.empty() && csiBuf_.front() == '?';
    std::string raw = isPrivate ? csiBuf_.substr(1) : csiBuf_;

    std::vector<int> params;
    size_t i = 0;
    while (i < raw.size()) {
        if (std::isdigit(static_cast<unsigned char>(raw[i]))) {
            int val = 0;
            while (i < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i]))) {
                val = val * 10 + (raw[i] - '0');
                ++i;
            }
            params.push_back(val);
        } else {
            if (raw[i] == ';' && params.empty()) params.push_back(0);
            ++i;
        }
    }
    auto p = [&](size_t idx, int defVal) -> int {
        if (idx >= params.size() || params[idx] == 0) return defVal;
        return params[idx];
    };

    if (isPrivate) {
        int code = params.empty() ? 0 : params[0];
        if (finalChar == 'h' || finalChar == 'l') {
            bool set = (finalChar == 'h');
            if (code == 25) cursorVisible_ = set;
            else if (code == 1049 || code == 47) {
                if (set && !altScreen_) {
                    altSavedScreen_ = screen_;
                    screen_.assign(static_cast<size_t>(rows_), std::vector<TerminalCell>(static_cast<size_t>(cols_)));
                    altScreen_ = true;
                    cursorRow_ = 0;
                    cursorCol_ = 0;
                } else if (!set && altScreen_) {
                    if (!altSavedScreen_.empty()) {
                        screen_ = std::move(altSavedScreen_);
                        if (static_cast<int>(screen_.size()) != rows_) {
                            screen_.resize(static_cast<size_t>(rows_), std::vector<TerminalCell>(static_cast<size_t>(cols_)));
                        }
                    }
                    altScreen_ = false;
                }
            }
        }
        return;
    }

    switch (finalChar) {
        case 'A': cursorRow_ -= p(0, 1); ensureValidCursor(); break;
        case 'B': cursorRow_ += p(0, 1); ensureValidCursor(); break;
        case 'C': cursorCol_ += p(0, 1); ensureValidCursor(); break;
        case 'D': cursorCol_ -= p(0, 1); ensureValidCursor(); break;
        case 'G': cursorCol_ = p(0, 1) - 1; ensureValidCursor(); break;
        case 'd': cursorRow_ = p(0, 1) - 1; ensureValidCursor(); break;
        case 'H':
        case 'f':
            cursorRow_ = p(0, 1) - 1;
            cursorCol_ = p(1, 1) - 1;
            ensureValidCursor();
            break;
        case 'J': {
            int mode = params.empty() ? 0 : params[0];
            if (mode == 0) clearRange(cursorRow_, cursorCol_, rows_ - 1, cols_ - 1);
            else if (mode == 1) clearRange(0, 0, cursorRow_, cursorCol_);
            else if (mode == 2 || mode == 3) {
                clearRange(0, 0, rows_ - 1, cols_ - 1);
                if (mode == 3) scrollback_.clear();
            }
            break;
        }
        case 'K': {
            int mode = params.empty() ? 0 : params[0];
            if (mode == 0) clearRange(cursorRow_, cursorCol_, cursorRow_, cols_ - 1);
            else if (mode == 1) clearRange(cursorRow_, 0, cursorRow_, cursorCol_);
            else if (mode == 2) clearRange(cursorRow_, 0, cursorRow_, cols_ - 1);
            break;
        }
        case 'L': scrollDown(cursorRow_, scrollBottom_, p(0, 1)); break;
        case 'M': scrollUp(cursorRow_, scrollBottom_, p(0, 1)); break;
        case 'P': {
            int n = p(0, 1);
            ensureValidCursor();
            auto& row = screen_[static_cast<size_t>(cursorRow_)];
            for (int c = cursorCol_; c < cols_; ++c) {
                row[static_cast<size_t>(c)] = (c + n < cols_)
                    ? row[static_cast<size_t>(c + n)]
                    : TerminalCell{' ', kDefaultFg, 0, false};
            }
            break;
        }
        case 'r':
            scrollTop_ = std::clamp(p(0, 1) - 1, 0, rows_ - 1);
            scrollBottom_ = std::clamp(p(1, rows_) - 1, scrollTop_, rows_ - 1);
            break;
        case 's': savedRow_ = cursorRow_; savedCol_ = cursorCol_; break;
        case 'u': cursorRow_ = savedRow_; cursorCol_ = savedCol_; ensureValidCursor(); break;
        case 'm': handleSgr(params); break;
        default: break;
    }
}

void PtyTerminal::handleSgr(const std::vector<int>& params) {
    if (params.empty()) {
        curFg_ = kDefaultFg;
        curBg_ = 0;
        curBold_ = false;
        return;
    }
    for (size_t i = 0; i < params.size(); ++i) {
        int code = params[i];
        if (code == 0) {
            curFg_ = kDefaultFg;
            curBg_ = 0;
            curBold_ = false;
        } else if (code == 1) {
            curBold_ = true;
        } else if (code == 22) {
            curBold_ = false;
        } else if (code >= 30 && code <= 37) {
            curFg_ = ansi16Color((code - 30) + (curBold_ ? 8 : 0));
        } else if (code == 39) {
            curFg_ = kDefaultFg;
        } else if (code >= 40 && code <= 47) {
            curBg_ = ansi16Color(code - 40);
        } else if (code == 49) {
            curBg_ = 0;
        } else if (code >= 90 && code <= 97) {
            curFg_ = ansi16Color((code - 90) + 8);
        } else if (code >= 100 && code <= 107) {
            curBg_ = ansi16Color((code - 100) + 8);
        } else if ((code == 38 || code == 48) && i + 1 < params.size()) {
            bool isFg = (code == 38);
            int mode = params[++i];
            if (mode == 5 && i + 1 < params.size()) {
                uint32_t col = xterm256Color(params[++i]);
                if (isFg) curFg_ = col; else curBg_ = col;
            } else if (mode == 2 && i + 3 < params.size()) {
                uint8_t r = static_cast<uint8_t>(std::clamp(params[++i], 0, 255));
                uint8_t g = static_cast<uint8_t>(std::clamp(params[++i], 0, 255));
                uint8_t b = static_cast<uint8_t>(std::clamp(params[++i], 0, 255));
                uint32_t col = packCol32(r, g, b);
                if (isFg) curFg_ = col; else curBg_ = col;
            }
        }
    }
}

} // namespace mc
