// CSOPESY Semi-Major Output 1 - ASCII Marquee Console (Windows)
//
// Design:
//   * Main thread   : keyboard polling + command interpreter (never blocks on animation).
//   * Worker thread : renders one marquee frame every <speed> ms.
//   * All drawing uses WriteConsoleOutputCharacterA, which writes at explicit
//     coordinates WITHOUT moving the cursor, so the animation never disturbs
//     what the user is typing and nothing has to be cleared (no flicker).
//
// Build (MinGW): g++ -std=c++17 -O2 -pthread marquee.cpp -o marquee.exe
// Build (MSVC) : cl /EHsc /std:c++17 marquee.cpp

#define NOMINMAX
#include <windows.h>
#include <conio.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Small string helpers
// ---------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

static std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Parses a whole string as a long. Returns false for "abc", "12x", "", overflow.
static bool parseLong(const std::string& s, long& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

// ---------------------------------------------------------------------------
// Configuration (config.txt)
// ---------------------------------------------------------------------------
static const int MIN_SPEED_MS = 1;
static const int MAX_SPEED_MS = 60000;

struct Config {
    int         consoleWidth  = 80;
    int         consoleHeight = 25;
    int         asciiHeight   = 5;
    int         defaultSpeed  = 500;
    std::string defaultText   = "Hello world!";
    int         spacing       = 5;
    int         step          = 2;
    int         pollMs        = 5;
};

// Reads an integer config value, keeping the default if invalid/out of range.
static void applyInt(int& target, const std::string& value, int lo, int hi) {
    long v;
    if (parseLong(value, v) && v >= lo && v <= hi) target = static_cast<int>(v);
}

static Config loadConfig(const std::string& path) {
    Config cfg;
    std::ifstream in(path);
    if (!in) return cfg;  // missing file -> safe defaults

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = toLower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));

        if      (key == "console_width")  applyInt(cfg.consoleWidth,  val, 40, 300);
        else if (key == "console_height") applyInt(cfg.consoleHeight, val, 15, 100);
        else if (key == "ascii_height")   applyInt(cfg.asciiHeight,   val, 5, 20);
        else if (key == "default_speed")  applyInt(cfg.defaultSpeed,  val, MIN_SPEED_MS, MAX_SPEED_MS);
        else if (key == "spacing")        applyInt(cfg.spacing,       val, 0, 100);
        else if (key == "step")           applyInt(cfg.step,          val, 1, 20);
        else if (key == "poll_ms")        applyInt(cfg.pollMs,        val, 1, 100);
        else if (key == "default_text")   { if (!val.empty()) cfg.defaultText = val.substr(0, 200); }
    }
    return cfg;
}

// ---------------------------------------------------------------------------
// ASCII font: every glyph is 5 rows tall; all rows of a glyph share one width.
// Lowercase letters reuse the uppercase glyphs.
// ---------------------------------------------------------------------------
static const int GLYPH_H = 5;

struct Glyph { const char* r[GLYPH_H]; };

static const std::map<char, Glyph>& fontTable() {
    static const std::map<char, Glyph> table = {
        {'A', {{" ### ", "#   #", "#####", "#   #", "#   #"}}},
        {'B', {{"#### ", "#   #", "#### ", "#   #", "#### "}}},
        {'C', {{" ####", "#    ", "#    ", "#    ", " ####"}}},
        {'D', {{"#### ", "#   #", "#   #", "#   #", "#### "}}},
        {'E', {{"#####", "#    ", "#### ", "#    ", "#####"}}},
        {'F', {{"#####", "#    ", "#### ", "#    ", "#    "}}},
        {'G', {{" ####", "#    ", "#  ##", "#   #", " ####"}}},
        {'H', {{"#   #", "#   #", "#####", "#   #", "#   #"}}},
        {'I', {{"###", " # ", " # ", " # ", "###"}}},
        {'J', {{"  ###", "    #", "    #", "#   #", " ### "}}},
        {'K', {{"#   #", "#  # ", "###  ", "#  # ", "#   #"}}},
        {'L', {{"#    ", "#    ", "#    ", "#    ", "#####"}}},
        {'M', {{"#   #", "## ##", "# # #", "#   #", "#   #"}}},
        {'N', {{"#   #", "##  #", "# # #", "#  ##", "#   #"}}},
        {'O', {{" ### ", "#   #", "#   #", "#   #", " ### "}}},
        {'P', {{"#### ", "#   #", "#### ", "#    ", "#    "}}},
        {'Q', {{" ### ", "#   #", "# # #", "#  # ", " ## #"}}},
        {'R', {{"#### ", "#   #", "#### ", "#  # ", "#   #"}}},
        {'S', {{" ####", "#    ", " ### ", "    #", "#### "}}},
        {'T', {{"#####", "  #  ", "  #  ", "  #  ", "  #  "}}},
        {'U', {{"#   #", "#   #", "#   #", "#   #", " ### "}}},
        {'V', {{"#   #", "#   #", "#   #", " # # ", "  #  "}}},
        {'W', {{"#   #", "#   #", "# # #", "## ##", "#   #"}}},
        {'X', {{"#   #", " # # ", "  #  ", " # # ", "#   #"}}},
        {'Y', {{"#   #", " # # ", "  #  ", "  #  ", "  #  "}}},
        {'Z', {{"#####", "   # ", "  #  ", " #   ", "#####"}}},

        {'0', {{" ### ", "#  ##", "# # #", "##  #", " ### "}}},
        {'1', {{"  #  ", " ##  ", "  #  ", "  #  ", " ### "}}},
        {'2', {{" ### ", "#   #", "  ## ", " #   ", "#####"}}},
        {'3', {{"#### ", "    #", " ### ", "    #", "#### "}}},
        {'4', {{"#   #", "#   #", "#####", "    #", "    #"}}},
        {'5', {{"#####", "#    ", "#### ", "    #", "#### "}}},
        {'6', {{" ### ", "#    ", "#### ", "#   #", " ### "}}},
        {'7', {{"#####", "    #", "   # ", "  #  ", "  #  "}}},
        {'8', {{" ### ", "#   #", " ### ", "#   #", " ### "}}},
        {'9', {{" ### ", "#   #", " ####", "    #", " ### "}}},

        {' ',  {{"   ", "   ", "   ", "   ", "   "}}},
        {'!',  {{"#", "#", "#", " ", "#"}}},
        {'?',  {{" ### ", "#   #", "  ## ", "     ", "  #  "}}},
        {'.',  {{" ", " ", " ", " ", "#"}}},
        {',',  {{"  ", "  ", "  ", " #", "# "}}},
        {'-',  {{"    ", "    ", "####", "    ", "    "}}},
        {'_',  {{"     ", "     ", "     ", "     ", "#####"}}},
        {':',  {{" ", "#", " ", "#", " "}}},
        {';',  {{"  ", " #", "  ", " #", "# "}}},
        {'\'', {{"#", "#", " ", " ", " "}}},
    };
    return table;
}

// Placeholder used for characters that are not in the font (a hollow box).
static const Glyph PLACEHOLDER = {{"#####", "#   #", "# # #", "#   #", "#####"}};

// Converts text into GLYPH_H rows of ASCII art (one column of gap between letters).
static std::vector<std::string> renderAscii(const std::string& text) {
    std::vector<std::string> rows(GLYPH_H);
    const auto& font = fontTable();
    for (char c : text) {
        char key = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto it = font.find(key);
        const Glyph& g = (it != font.end()) ? it->second : PLACEHOLDER;
        for (int r = 0; r < GLYPH_H; ++r) {
            rows[r] += g.r[r];
            rows[r] += ' ';
        }
    }
    return rows;
}

// ---------------------------------------------------------------------------
// Console output (thread-safe, cursor-less drawing)
// ---------------------------------------------------------------------------
class ConsoleScreen {
public:
    void init(int wantW, int wantH) {
        out_ = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        int winW = 80, winH = 25;
        top_ = 0;
        left_ = 0;
        if (GetConsoleScreenBufferInfo(out_, &csbi)) {
            winW  = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            winH  = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            top_  = csbi.srWindow.Top;
            left_ = csbi.srWindow.Left;
        }
        width_  = std::max(40, std::min(wantW, winW));
        height_ = std::max(12, std::min(wantH, std::max(winH, 12)));

        // Clear the visible window once at start-up.
        DWORD written = 0;
        DWORD cells = static_cast<DWORD>(winW) * static_cast<DWORD>(winH);
        COORD origin = {left_, top_};
        FillConsoleOutputCharacterA(out_, ' ', cells, origin, &written);
        SetConsoleCursorPosition(out_, origin);
    }

    int width() const  { return width_; }
    int height() const { return height_; }

    // Writes one full row (padded/truncated to the console width).
    void putLine(int row, std::string s) {
        s.resize(static_cast<size_t>(width_), ' ');
        COORD pos = {left_, static_cast<SHORT>(top_ + row)};
        DWORD written = 0;
        std::lock_guard<std::mutex> lk(mtx_);
        WriteConsoleOutputCharacterA(out_, s.c_str(), static_cast<DWORD>(s.size()), pos, &written);
    }

    // Draws the prompt row and puts the real cursor at the end of the typed text.
    void putPrompt(const std::string& buffer) {
        std::string line = "Command> " + buffer;
        int cursorX = std::min(static_cast<int>(line.size()), width_ - 1);
        line.resize(static_cast<size_t>(width_), ' ');
        COORD pos = {left_, static_cast<SHORT>(top_ + height_ - 1)};
        DWORD written = 0;
        std::lock_guard<std::mutex> lk(mtx_);
        WriteConsoleOutputCharacterA(out_, line.c_str(), static_cast<DWORD>(line.size()), pos, &written);
        COORD cur = {static_cast<SHORT>(left_ + cursorX), pos.Y};
        SetConsoleCursorPosition(out_, cur);
    }

    // Moves the cursor below the UI so the shell prompt is clean after exit.
    void moveBelow() {
        std::lock_guard<std::mutex> lk(mtx_);
        COORD pos = {left_, static_cast<SHORT>(top_ + height_)};
        SetConsoleCursorPosition(out_, pos);
    }

private:
    HANDLE     out_ = nullptr;
    SHORT      top_ = 0, left_ = 0;
    int        width_ = 80, height_ = 25;
    std::mutex mtx_;
};

// ---------------------------------------------------------------------------
// Shared marquee state (protected by g.m)
// ---------------------------------------------------------------------------
struct SharedState {
    std::mutex              m;
    std::condition_variable cv;
    bool                    running = false;  // animation active?
    bool                    quit    = false;  // program is exiting
    bool                    dirty   = false;  // text/speed changed -> redraw now
    int                     speed   = 500;    // ms between frames
    std::string             text;
    std::vector<std::string> rows;            // current ASCII graphic
    int                     pos     = 0;      // left offset of graphic
};

static SharedState  g;
static ConsoleScreen screen;
static Config        cfg;

// Layout (rows relative to top of window)
static int marqueeTop()  { return 4; }
static int marqueeRows() { return cfg.asciiHeight; }
static int logTop()      { return marqueeTop() + marqueeRows() + 1; }
static int logRows()     { return screen.height() - 1 - logTop(); }

// Builds the visible frame from the current state. Caller must hold g.m.
static std::vector<std::string> makeFrameLocked() {
    const int W = screen.width();
    const int A = marqueeRows();
    std::vector<std::string> frame(static_cast<size_t>(A), std::string(static_cast<size_t>(W), ' '));

    const int G = g.rows.empty() ? 0 : static_cast<int>(g.rows[0].size());
    if (G == 0) return frame;
    const int period = G + cfg.spacing;
    const int offsetRow = (A - GLYPH_H) / 2;  // center graphic vertically

    for (int r = 0; r < GLYPH_H; ++r) {
        std::string& line = frame[static_cast<size_t>(offsetRow + r)];
        for (int c = 0; c < W; ++c) {
            // Graphic moves left -> right; modulo makes it wrap around smoothly.
            int idx = ((c - g.pos) % period + period) % period;
            if (idx < G) line[static_cast<size_t>(c)] = g.rows[static_cast<size_t>(r)][static_cast<size_t>(idx)];
        }
    }
    return frame;
}

static void writeFrame(const std::vector<std::string>& frame) {
    for (size_t i = 0; i < frame.size(); ++i)
        screen.putLine(marqueeTop() + static_cast<int>(i), frame[i]);
}

// Redraws the marquee area with the current state without advancing it.
static void redrawMarquee() {
    std::vector<std::string> frame;
    {
        std::lock_guard<std::mutex> lk(g.m);
        frame = makeFrameLocked();
    }
    writeFrame(frame);
}

static void drawStatus() {
    std::string s;
    {
        std::lock_guard<std::mutex> lk(g.m);
        s = std::string("Status: ") + (g.running ? "RUNNING" : "STOPPED") +
            " | speed: " + std::to_string(g.speed) + " ms | text: " + g.text;
    }
    screen.putLine(3, s);
}

static void drawHeader() {
    std::string bar(static_cast<size_t>(screen.width()), '=');
    std::string title = "CSOPESY MARQUEE CONSOLE";
    int pad = std::max(0, (screen.width() - static_cast<int>(title.size())) / 2);
    screen.putLine(0, bar);
    screen.putLine(1, std::string(static_cast<size_t>(pad), ' ') + title);
    screen.putLine(2, bar);
}

// ---------------------------------------------------------------------------
// Message log (message area between marquee and prompt) - main thread only
// ---------------------------------------------------------------------------
static std::deque<std::string> logLines;

static void redrawLog() {
    const int rows = logRows();
    while (static_cast<int>(logLines.size()) > rows) logLines.pop_front();
    for (int i = 0; i < rows; ++i) {
        std::string s = (i < static_cast<int>(logLines.size())) ? logLines[static_cast<size_t>(i)] : "";
        screen.putLine(logTop() + i, s);
    }
}

static void addLog(const std::string& line) {
    logLines.push_back(line);
    redrawLog();
}

// ---------------------------------------------------------------------------
// Animation worker thread
// ---------------------------------------------------------------------------
static void animationWorker() {
    std::unique_lock<std::mutex> lk(g.m);
    while (!g.quit) {
        // Sleep until the marquee is started or the program quits.
        g.cv.wait(lk, [] { return g.quit || g.running; });
        if (g.quit) break;

        // Build the current frame, then move the graphic for the next one.
        std::vector<std::string> frame = makeFrameLocked();
        const int G = g.rows.empty() ? 0 : static_cast<int>(g.rows[0].size());
        const int period = std::max(1, G + cfg.spacing);
        g.pos = (g.pos + cfg.step) % period;

        lk.unlock();
        writeFrame(frame);  // draw without holding the state lock
        lk.lock();

        // Wait one refresh interval; wake early on stop / quit / text or speed change.
        g.cv.wait_for(lk, std::chrono::milliseconds(g.speed),
                      [] { return g.quit || !g.running || g.dirty; });
        g.dirty = false;
    }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
static void cmdHelp() {
    addLog("Available commands:");
    addLog("  help                  - show this list");
    addLog("  start_marquee         - start the marquee animation");
    addLog("  stop_marquee          - stop (pause) the marquee animation");
    addLog("  set_text <text>       - set the text shown as ASCII art");
    addLog("  set_speed <ms>        - set refresh interval in ms (" +
           std::to_string(MIN_SPEED_MS) + "-" + std::to_string(MAX_SPEED_MS) + ")");
    addLog("  exit                  - stop the marquee and quit");
}

static void cmdStart() {
    {
        std::lock_guard<std::mutex> lk(g.m);
        if (g.running) { addLog("Marquee is already running."); return; }
        g.running = true;
    }
    g.cv.notify_all();
    drawStatus();
    addLog("Marquee started.");
}

static void cmdStop() {
    {
        std::lock_guard<std::mutex> lk(g.m);
        if (!g.running) { addLog("Marquee is not running."); return; }
        g.running = false;
    }
    g.cv.notify_all();
    drawStatus();
    addLog("Marquee stopped.");
}

static void cmdSetText(const std::string& arg) {
    if (arg.empty()) { addLog("Usage: set_text <text>"); return; }
    std::string text = arg.substr(0, 200);
    std::vector<std::string> rows = renderAscii(text);  // built outside the lock
    bool running;
    {
        std::lock_guard<std::mutex> lk(g.m);
        g.text  = text;
        g.rows  = std::move(rows);
        g.pos   = 0;        // restart from the left edge
        g.dirty = true;     // make the worker redraw immediately
        running = g.running;
    }
    g.cv.notify_all();
    if (!running) redrawMarquee();  // show the new graphic even while stopped
    drawStatus();
    addLog("Text set to: " + text);
}

static void cmdSetSpeed(const std::string& arg) {
    if (arg.empty()) { addLog("Usage: set_speed <milliseconds>"); return; }
    long v;
    if (!parseLong(arg, v)) { addLog("Invalid speed '" + arg + "': please enter an integer."); return; }
    if (v < MIN_SPEED_MS || v > MAX_SPEED_MS) {
        addLog("Speed must be between " + std::to_string(MIN_SPEED_MS) + " and " +
               std::to_string(MAX_SPEED_MS) + " ms.");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g.m);
        g.speed = static_cast<int>(v);
        g.dirty = true;  // interrupt the current wait so the new speed applies now
    }
    g.cv.notify_all();
    drawStatus();
    addLog("Speed set to " + std::to_string(v) + " ms.");
}

// Returns false when the program should exit.
static bool handleCommand(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) return true;

    addLog("Command> " + line);

    size_t sp = line.find_first_of(" \t");
    std::string cmd = toLower(line.substr(0, sp));
    std::string arg = (sp == std::string::npos) ? "" : trim(line.substr(sp + 1));

    if      (cmd == "help")          cmdHelp();
    else if (cmd == "start_marquee") cmdStart();
    else if (cmd == "stop_marquee")  cmdStop();
    else if (cmd == "set_text")      cmdSetText(arg);
    else if (cmd == "set_speed")     cmdSetSpeed(arg);
    else if (cmd == "exit")          return false;
    else addLog("Unknown command '" + cmd + "'. Type 'help' for the command list.");
    return true;
}

// ---------------------------------------------------------------------------
// main: keyboard polling loop + clean shutdown
// ---------------------------------------------------------------------------
int main() {
    cfg = loadConfig("config.txt");
    SetConsoleTitleA("CSOPESY Marquee Console");
    screen.init(cfg.consoleWidth, cfg.consoleHeight);

    // Initial state comes from config.txt; commands override it later.
    g.speed = cfg.defaultSpeed;
    g.text  = cfg.defaultText;
    g.rows  = renderAscii(g.text);

    drawHeader();
    drawStatus();
    redrawMarquee();
    addLog("Welcome to CSOPESY Marquee Console!");
    addLog("Group developer: (Simon Kabiling)");
    addLog("Version date: Sept 2026");
    addLog("Type 'help' for the list of commands.");

    std::thread worker(animationWorker);  // joined before returning from main

    std::string buffer;
    screen.putPrompt(buffer);
    const int maxInput = std::max(10, screen.width() - 12);
    bool alive = true;

    while (alive) {
        if (_kbhit()) {
            wint_t ch = _getwch();
            if (ch == 0 || ch == 0xE0) {          // arrow/function key: swallow 2nd code
                _getwch();
            } else if (ch == L'\r' || ch == L'\n') {
                std::string cmd = buffer;
                buffer.clear();
                alive = handleCommand(cmd);
            } else if (ch == L'\b') {
                if (!buffer.empty()) buffer.pop_back();
            } else if (ch >= 32 && ch < 127) {
                if (static_cast<int>(buffer.size()) < maxInput) buffer.push_back(static_cast<char>(ch));
            }
            if (alive) screen.putPrompt(buffer);
        } else {
            // Short sleep = "poll every N ms": keeps CPU low and typing responsive.
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.pollMs));
        }
    }

    // Graceful shutdown: signal the worker, wake it, and join it.
    {
        std::lock_guard<std::mutex> lk(g.m);
        g.running = false;
        g.quit = true;
    }
    g.cv.notify_all();
    worker.join();

    drawStatus();
    screen.moveBelow();
    std::cout << "Marquee stopped. Goodbye!" << std::endl;
    return 0;
}
