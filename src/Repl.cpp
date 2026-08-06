// The Raku++ REPL.
//
// The session is ONE Interpreter that never sees run(): every line goes through
// evalString into the same global scope, so `my $x` on line 1 is still there on
// line 9, and a `sub`, `class` or `sub infix:<…>` declared at the prompt is
// visible — and parseable — on every later line. That behaviour was already in
// place for EVAL; the REPL is the loop around it.
//
// Everything below is terminal handling plus that loop. Nothing here is reached
// by a script run, and the file is not part of the runtime library, so `--exe`
// output does not carry any of it.

#include "Repl.h"
#include "Interpreter.h"
#include "Runtime.h"
#include "Highlight.h"
#include "Parser.h"
#include "Lexer.h"
#include "Ast.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace rakupp {
namespace {

// ---------------------------------------------------------------- UTF-8 ----
// The buffer is bytes, but the cursor moves over CHARACTERS: Raku source is full
// of `»`, `∈`, `…`, and a cursor that steps by bytes would land inside one.

size_t cpLenAt(const std::string& s, size_t i) {
    if (i >= s.size()) return 0;
    unsigned char c = (unsigned char)s[i];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    return std::min(n, s.size() - i);
}

size_t nextCp(const std::string& s, size_t i) { return std::min(s.size(), i + cpLenAt(s, i)); }

size_t prevCp(const std::string& s, size_t i) {
    if (i == 0) return 0;
    size_t j = i - 1;
    while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) j--; // skip continuation bytes
    return j;
}

char32_t decodeCp(const std::string& s, size_t i) {
    size_t n = cpLenAt(s, i);
    unsigned char c = (unsigned char)s[i];
    if (n == 1) return c;
    char32_t v = c & (0xFF >> (n + 1));
    for (size_t k = 1; k < n; k++) v = (v << 6) | ((unsigned char)s[i + k] & 0x3F);
    return v;
}

// Columns a codepoint occupies. Approximate but covers what shows up in Raku
// source: combining marks take none, CJK and emoji take two.
int cpWidth(char32_t c) {
    if (c < 32 || (c >= 0x7F && c < 0xA0)) return 0;
    if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF) ||
        (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F)) return 0;
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) ||
        (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF60) ||
        (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) ||
        (c >= 0x1F900 && c <= 0x1F9FF) || (c >= 0x20000 && c <= 0x3FFFD)) return 2;
    return 1;
}

int strWidth(const std::string& s, size_t from = 0, size_t to = std::string::npos) {
    to = std::min(to, s.size());
    int w = 0;
    for (size_t i = from; i < to; i = nextCp(s, i)) w += cpWidth(decodeCp(s, i));
    return w;
}

// ------------------------------------------------------------- prompts -----
const char* kPrompt     = "\x1b[1;32m>\x1b[0m ";  // primary
const char* kPromptCont = "\x1b[1;33m*\x1b[0m ";  // continuation: input is unfinished
const int   kPromptCols = 2;                      // both are "X " once rendered

// Names worth completing that are not in any scope table — the core types and
// the routines people actually reach for at a prompt.
const char* const kCoreNames[] = {
    "Any", "Array", "Bag", "Baggy", "Blob", "Bool", "Buf", "Callable", "Capture",
    "Channel", "Code", "Complex", "Cool", "Date", "DateTime", "Duration", "Failure",
    "Hash", "IO", "Instant", "Int", "IntStr", "Iterable", "Iterator", "Junction",
    "Label", "List", "Map", "Match", "Method", "Mix", "Mu", "Nil", "Num", "Numeric",
    "Pair", "Parcel", "Positional", "Promise", "Proc", "Range", "Rat", "Real",
    "Regex", "Routine", "Scalar", "Selector", "Seq", "Set", "SetHash", "Signature",
    "Slip", "Stash", "Str", "Stringy", "Sub", "Submethod", "Supply", "Tap", "Thread",
    "Version", "Whatever", "WhateverCode",
    "say", "put", "print", "note", "dd", "exit", "die", "warn", "sleep", "now",
    "map", "grep", "sort", "first", "reduce", "zip", "flat", "lines", "slurp",
    "spurt", "shell", "run", "elems", "keys", "values", "pairs", "join", "split",
    "chomp", "chop", "trim", "uc", "lc", "tc", "tclc", "chars", "codes", "index",
    "substr", "sprintf", "printf", "abs", "ceiling", "floor", "round", "sqrt",
    "min", "max", "sum", "unique", "repeated", "squish", "roll", "pick", "eager",
};

// ------------------------------------------------------------- terminal ----
#if !defined(_WIN32)

int termCols() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

// All REPL output goes through here, std::cout included, so editor redraws and
// evaluation results cannot interleave out of order. In raw mode line endings are
// written explicitly as \r\n, since ONLCR is off.
void out(const std::string& s) { std::cout << s << std::flush; }

// Raw mode for the duration of one readLine, restored on every exit path —
// including an exception out of the completion callback.
struct RawMode {
    termios saved{};
    bool on = false;
    RawMode() {
        if (tcgetattr(STDIN_FILENO, &saved) == -1) return;
        termios raw = saved;
        raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(unsigned long)(OPOST);
        raw.c_cflag |= (unsigned long)(CS8);
        raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        // TCSADRAIN, not TCSAFLUSH: the terminal is in cooked mode while an
        // evaluation runs, so anything typed ahead during a slow one is sitting in
        // the input queue. TCSAFLUSH would discard it — type-ahead has to survive.
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) == -1) return;
        on = true;
        // Bracketed paste: a pasted block arrives wrapped in markers, so it can be
        // taken literally instead of each newline acting as Enter mid-paste.
        out("\x1b[?2004h");
    }
    ~RawMode() {
        if (!on) return;
        out("\x1b[?2004l");
        // TCSADRAIN here too. Leaving raw mode happens after EVERY line, so a
        // TCSAFLUSH would throw away whatever was typed while the previous line
        // was still being evaluated — the fast typist would lose keystrokes.
        tcsetattr(STDIN_FILENO, TCSADRAIN, &saved);
    }
};

int readByte() {
    char c;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) return -1;
    return (unsigned char)c;
}

// ---------------------------------------------------------- line editor ----
class LineEditor {
public:
    explicit LineEditor(const Interpreter& interp) : interp_(interp) { loadHistory(); }

    // Reads one logical line. False means end of input (^D on an empty buffer, or
    // a closed stdin). `\x03` in the result signals ^C — abandon what is buffered.
    bool readLine(const std::string& prompt, int promptCols, std::string& result) {
        // A paste (or a multi-line recall) arrives as several lines at once; hand
        // them back one at a time so the caller's accumulate loop sees them just
        // as if they had been typed.
        if (!pending_.empty()) {
            size_t nl = pending_.find('\n');
            result = pending_.substr(0, nl); // npos = whole string
            pending_ = nl == std::string::npos ? "" : pending_.substr(nl + 1);
            out(prompt + highlightOf(result) + "\r\n");
            return true;
        }
        RawMode raw;
        if (!raw.on) { // not a usable terminal after all — fall back to cooked reads
            out(prompt);
            return (bool)std::getline(std::cin, result);
        }
        buf_.clear();
        pos_ = 0;
        prompt_ = prompt;
        promptCols_ = promptCols;
        histIdx_ = history_.size();
        refresh();
        for (;;) {
            int c = readByte();
            if (c < 0) { result.clear(); return false; }            // stdin closed
            switch (c) {
                case 13: case 10:                                    // Enter
                    out("\r\n");
                    result = buf_;
                    if (!buf_.empty()) pushHistory(buf_);
                    return true;
                case 3:                                              // ^C
                    out("\r\n");
                    result = "\x03";
                    return true;
                case 4:                                              // ^D
                    if (buf_.empty()) { out("\r\n"); result.clear(); return false; }
                    if (pos_ < buf_.size()) { buf_.erase(pos_, cpLenAt(buf_, pos_)); refresh(); }
                    break;
                case 127: case 8:                                    // Backspace
                    if (pos_ > 0) { size_t p = prevCp(buf_, pos_); buf_.erase(p, pos_ - p); pos_ = p; refresh(); }
                    break;
                case 1: pos_ = 0; refresh(); break;                  // ^A
                case 5: pos_ = buf_.size(); refresh(); break;        // ^E
                case 2: pos_ = prevCp(buf_, pos_); refresh(); break; // ^B
                case 6: pos_ = nextCp(buf_, pos_); refresh(); break; // ^F
                case 11: buf_.erase(pos_); refresh(); break;         // ^K
                case 21: buf_.erase(0, pos_); pos_ = 0; refresh(); break; // ^U
                case 23: killWordBack(); break;                      // ^W
                case 12: out("\x1b[H\x1b[2J"); refresh(); break;     // ^L
                case 16: historyMove(-1); break;                     // ^P
                case 14: historyMove(+1); break;                     // ^N
                case 18: reverseSearch(); break;                     // ^R
                case 9: complete(); break;                           // Tab
                case 27: escape(); break;                            // ESC …
                default:
                    if (c < 32) break;                               // ignore other controls
                    insertByte((char)c);
                    break;
            }
        }
    }

    void saveHistoryLine(const std::string& line) {
        if (histPath_.empty() || line.empty()) return;
        std::ofstream f(histPath_, std::ios::app);
        if (f) f << line << "\n";
    }

private:
    const Interpreter& interp_;
    std::string buf_, prompt_, pending_, histPath_;
    size_t pos_ = 0;
    int promptCols_ = 2;
    std::vector<std::string> history_;
    size_t histIdx_ = 0;

    // ---- rendering ----
    // Highlighting rewrites the buffer with SGR codes, which destroys the
    // byte↔column mapping a scroll window needs. So: colour when the line fits on
    // screen (nearly always), and render plain when it has to scroll.
    std::string highlightOf(const std::string& s) const {
        if (s.size() > 4096) return s;
        try { return highlight(s, "ansi") + "\x1b[0m"; }
        catch (...) { return s; }   // a half-typed literal may not tokenize yet
    }

    void refresh() {
        int cols = termCols();
        int avail = std::max(8, cols - promptCols_ - 1);
        std::string line;
        int cursorCol;
        if (strWidth(buf_) <= avail) {
            line = highlightOf(buf_);
            cursorCol = promptCols_ + strWidth(buf_, 0, pos_);
        } else {
            // Scroll horizontally: keep the cursor on screen, drop what does not fit.
            size_t start = 0;
            while (strWidth(buf_, start, pos_) > avail - 1) start = nextCp(buf_, start);
            size_t end = pos_;
            while (end < buf_.size() && strWidth(buf_, start, nextCp(buf_, end)) <= avail) end = nextCp(buf_, end);
            line = buf_.substr(start, end - start);
            cursorCol = promptCols_ + strWidth(buf_, start, pos_);
        }
        out("\r" + prompt_ + line + "\x1b[0K\r\x1b[" + std::to_string(cursorCol) + "C");
    }

    void insertByte(char c) {
        buf_.insert(pos_, 1, c);
        pos_++;
        // Redraw only once the trailing codepoint is COMPLETE. A `∀` arrives as
        // three separate reads, and measuring the buffer between them would both
        // mis-count the width and emit a partial UTF-8 sequence to the terminal.
        size_t start = prevCp(buf_, pos_);
        if (start + cpLenAt(buf_, start) > pos_) return; // more bytes still coming
        refresh();
    }

    void insertText(const std::string& s) {
        buf_.insert(pos_, s);
        pos_ += s.size();
        refresh();
    }

    void killWordBack() {
        size_t p = pos_;
        while (p > 0 && std::isspace((unsigned char)buf_[prevCp(buf_, p)])) p = prevCp(buf_, p);
        while (p > 0 && !std::isspace((unsigned char)buf_[prevCp(buf_, p)])) p = prevCp(buf_, p);
        buf_.erase(p, pos_ - p);
        pos_ = p;
        refresh();
    }

    // ---- escape sequences ----
    void escape() {
        int a = readByte();
        if (a < 0) return;
        if (a != '[' && a != 'O') return;
        int b = readByte();
        if (b < 0) return;
        if (b >= '0' && b <= '9') {                     // \e[<n>~ and \e[200~ paste
            std::string num(1, (char)b);
            int t;
            while ((t = readByte()) >= 0 && t >= '0' && t <= '9') num += (char)t;
            if (t != '~') return;
            if (num == "3") { if (pos_ < buf_.size()) { buf_.erase(pos_, cpLenAt(buf_, pos_)); refresh(); } }
            else if (num == "1" || num == "7") { pos_ = 0; refresh(); }
            else if (num == "4" || num == "8") { pos_ = buf_.size(); refresh(); }
            else if (num == "200") bracketedPaste();
            return;
        }
        switch (b) {
            case 'A': historyMove(-1); break;
            case 'B': historyMove(+1); break;
            case 'C': pos_ = nextCp(buf_, pos_); refresh(); break;
            case 'D': pos_ = prevCp(buf_, pos_); refresh(); break;
            case 'H': pos_ = 0; refresh(); break;
            case 'F': pos_ = buf_.size(); refresh(); break;
            default: break;
        }
    }

    // Everything up to \e[201~ is literal text — no key in it is interpreted, so
    // pasting an indented block cannot be mangled by editing keys or auto-indent.
    void bracketedPaste() {
        std::string text;
        for (;;) {
            int c = readByte();
            if (c < 0) break;
            if (c == 27) {  // possible terminator
                std::string seq;
                int t;
                while ((t = readByte()) >= 0) { seq += (char)t; if (t == '~' || seq.size() > 8) break; }
                if (seq == "[201~") break;
                text += (char)27;
                text += seq;
                continue;
            }
            if (c == 13) c = 10;   // terminals send CR; the accumulator wants LF
            text += (char)c;
        }
        size_t nl = text.find('\n');
        if (nl == std::string::npos) { insertText(text); return; }
        // Multi-line paste: the first line finishes the buffer, the rest queue up
        // for the following readLine calls.
        insertText(text.substr(0, nl));
        pending_ = text.substr(nl + 1);
        if (!pending_.empty() && pending_.back() == '\n') pending_.pop_back();
    }

    // ---- history ----
    void pushHistory(const std::string& line) {
        if (!history_.empty() && history_.back() == line) return;
        history_.push_back(line);
        saveHistoryLine(line);
    }

    void historyMove(int dir) {
        if (history_.empty()) return;
        if (dir < 0 && histIdx_ == 0) return;
        if (dir > 0 && histIdx_ >= history_.size()) return;
        histIdx_ = (size_t)((long long)histIdx_ + dir);
        buf_ = histIdx_ < history_.size() ? history_[histIdx_] : std::string();
        pos_ = buf_.size();
        refresh();
    }

    void reverseSearch() {
        std::string needle;
        size_t found = history_.size();
        for (;;) {
            std::string shown = found < history_.size() ? history_[found] : std::string();
            out("\r\x1b[0K(reverse-i-search)`" + needle + "': " + shown);
            int c = readByte();
            if (c < 0 || c == 7 || c == 3) break;                      // ^G / ^C: cancel
            if (c == 13 || c == 10 || c == 27) {                       // accept
                if (found < history_.size()) { buf_ = history_[found]; pos_ = buf_.size(); }
                break;
            }
            if (c == 127 || c == 8) { if (!needle.empty()) needle.erase(prevCp(needle, needle.size())); }
            else if (c == 18) { if (found > 0) { size_t f = searchBack(needle, found - 1); if (f != history_.size()) found = f; continue; } }
            else if (c < 32) continue;
            else needle += (char)c;
            found = searchBack(needle, history_.size() ? history_.size() - 1 : 0);
        }
        out("\r\x1b[0K");
        refresh();
    }

    size_t searchBack(const std::string& needle, size_t from) const {
        if (history_.empty() || needle.empty()) return history_.size();
        for (size_t i = from + 1; i-- > 0;)
            if (history_[i].find(needle) != std::string::npos) return i;
        return history_.size();
    }

    void loadHistory() {
        const char* env = std::getenv("RAKUPP_HISTORY");
        if (env && !*env) return;                 // RAKUPP_HISTORY= (empty) disables it
        if (env) histPath_ = env;
        else {
            const char* home = std::getenv("HOME");
            if (!home) return;
            histPath_ = std::string(home) + "/.rakupp_history";
        }
        std::ifstream f(histPath_);
        std::string line;
        while (std::getline(f, line)) if (!line.empty()) history_.push_back(line);
        const size_t kCap = 5000;
        if (history_.size() > kCap) history_.erase(history_.begin(), history_.end() - kCap);
    }

    // ---- completion ----
    // Candidates come from the live scope chain, so a sub or class declared three
    // lines ago completes; the sigil the user typed decides what is offered.
    void complete() {
        size_t start = pos_;
        auto wordChar = [](char ch) {
            return std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == ':';
        };
        while (start > 0 && wordChar(buf_[start - 1])) start--;
        bool method = start > 0 && buf_[start - 1] == '.';
        if (!method && start > 0 && std::strchr("$@%&", buf_[start - 1])) start--;
        std::string word = buf_.substr(start, pos_ - start);
        if (word.empty() && !method) return;

        std::vector<std::string> hits;
        auto consider = [&](const std::string& name) {
            std::string cand = name;
            if (!word.empty() && !std::strchr("$@%&", word[0]) && !cand.empty() && cand[0] == '&')
                cand = cand.substr(1);           // `&say` completes a bare `sa`
            if (cand.rfind(word, 0) == 0 && cand != word) hits.push_back(cand);
        };
        if (method) {
            for (auto& m : methodNames()) if (m.rfind(word, 0) == 0 && m != word) hits.push_back(m);
        } else {
            for (auto& n : interp_.replNames()) consider(n);
            for (auto* n : kCoreNames) consider(n);
        }
        std::sort(hits.begin(), hits.end());
        hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
        if (hits.empty()) return;

        // Extend to the longest common prefix first — one Tab always makes progress.
        std::string common = hits.front();
        for (auto& h : hits)
            while (common.size() && h.rfind(common, 0) != 0) common.pop_back();
        if (common.size() > word.size()) {
            buf_.replace(start, pos_ - start, common);
            pos_ = start + common.size();
            refresh();
            if (hits.size() == 1) return;
        }
        if (hits.size() == 1) return;
        out("\r\n");
        int cols = termCols();
        size_t widest = 0;
        for (auto& h : hits) widest = std::max(widest, h.size());
        size_t perRow = std::max<size_t>(1, (size_t)cols / (widest + 2));
        std::string listing;
        for (size_t i = 0; i < hits.size(); i++) {
            listing += hits[i];
            listing += std::string(widest + 2 - hits[i].size(), ' ');
            if ((i + 1) % perRow == 0) listing += "\r\n";
        }
        if (hits.size() % perRow) listing += "\r\n";
        out(listing);
        refresh();
    }

    // Methods of every class declared in this session. A REPL is mostly used on
    // things you just defined, which is exactly what this covers.
    std::vector<std::string> methodNames() const {
        std::vector<std::string> out;
        for (auto& kv : interp_.classes_)
            if (kv.second) for (auto& m : kv.second->methods) out.push_back(m.first);
        static const char* const kCommon[] = {
            "WHAT", "WHO", "WHY", "WHERE", "HOW", "raku", "gist", "perl", "Str", "Int",
            "Num", "Rat", "Bool", "List", "Array", "Hash", "Set", "Bag", "Mix", "Seq",
            "elems", "keys", "values", "kv", "pairs", "antipairs", "map", "grep",
            "first", "sort", "reverse", "unique", "squish", "join", "push", "pop",
            "shift", "unshift", "append", "prepend", "splice", "head", "tail", "chars",
            "codes", "lines", "words", "comb", "split", "subst", "trans", "trim",
            "trim-leading", "trim-trailing", "uc", "lc", "tc", "tclc", "flip", "chomp",
            "chop", "starts-with", "ends-with", "contains", "index", "rindex", "substr",
            "sum", "min", "max", "abs", "sqrt", "floor", "ceiling", "round", "defined",
        };
        for (auto* m : kCommon) out.push_back(m);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
};

#else // _WIN32 ---------------------------------------------------------------

// Windows console raw-mode editing is a separate job (ReadConsoleInputW, VT
// enabling, codepage handling). Until that is written the REPL still works here,
// just with the console's own line editing: history and completion are the
// console's, not ours.
int termCols() { return 80; }
void out(const std::string& s) { std::cout << s << std::flush; }

class LineEditor {
public:
    explicit LineEditor(const Interpreter&) {}
    bool readLine(const std::string& prompt, int, std::string& result) {
        out(prompt);
        return (bool)std::getline(std::cin, result);
    }
    void saveHistoryLine(const std::string&) {}
};

#endif

// ------------------------------------------------------------ meta commands -
// `\word` is a REPL command. A backslash CAN begin Raku (`\(1,2)` is a Capture),
// so only backslash-immediately-followed-by-a-letter counts, which no Raku
// statement starts with.
bool isMetaCommand(const std::string& line, std::string& cmd, std::string& rest) {
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '\\') return false;
    size_t j = i + 1;
    if (j >= line.size() || !std::isalpha((unsigned char)line[j])) return false;
    size_t k = j;
    while (k < line.size() && std::isalpha((unsigned char)line[k])) k++;
    cmd = line.substr(j, k - j);
    size_t r = line.find_first_not_of(" \t", k);
    rest = r == std::string::npos ? "" : line.substr(r);
    return true;
}

void printHelp() {
    std::cout <<
        "  \\h            this help\n"
        "  \\q            quit (also: ^D, or `exit`)\n"
        "  \\t EXPR       show the type of EXPR\n"
        "  \\a EXPR       dump the AST of EXPR\n"
        "  \\v            list the names in scope\n"
        "  \\l            clear the screen\n"
        "  \\r            reset the session (drops all declarations)\n"
        "\n"
        "  Tab completes, ^R searches history, ^A/^E/^K/^U/^W edit,\n"
        "  arrows move and recall. An unfinished line prompts with `*`.\n";
}

void printError(const std::string& msg) {
    std::string m = msg;
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) m.pop_back();
    std::cout << "\x1b[31m" << m << "\x1b[0m\n";
}

// Result echo is dimmed so it reads as the REPL talking, not as program output:
// `say 42` prints a bright 42 (the program) then a dim True (its return value).
//
// Rendering the value is itself Raku that can die — `1/0` returns a Rat whose
// failure only surfaces when something looks at it, and a user-defined `method
// gist` can throw outright. Either way the error is the answer, so it is
// reported rather than swallowed.
void printResult(Interpreter& interp, const Value& v) {
    std::string g;
    try { g = interp.gistOf(v); }
    catch (RakuError& e) { printError(e.message); return; }
    catch (std::exception& e) { printError(e.what()); return; }
    catch (...) { return; }
    std::cout << "\x1b[90m" << g << "\x1b[0m\n";
}

struct ReplCtx {
    std::string exePath;
    std::vector<std::string> libPaths;
};

int replMain(ReplCtx& ctx) {
    setConsoleUtf8();
    std::cout << "Raku++ " << RAKUPP_VERSION << " — \\h for help, ^D to exit\n";

    auto fresh = [&]() {
        auto interp = std::make_unique<Interpreter>();
        interp->replStart({});
        interp->srcFile_ = "<repl>";
        interp->execPath_ = ctx.exePath;
        interp->libPaths_.insert(interp->libPaths_.begin(), ctx.libPaths.begin(), ctx.libPaths.end());
        return interp;
    };
    auto interp = fresh();
    LineEditor ed(*interp);

    std::string acc;          // the statement being accumulated across lines
    int exitCode = 0;
    for (;;) {
        std::string line;
        bool cont = !acc.empty();
        if (!ed.readLine(cont ? kPromptCont : kPrompt, kPromptCols, line)) break;
        if (line == "\x03") { acc.clear(); continue; }              // ^C abandons the buffer

        std::string cmd, rest;
        if (acc.empty() && isMetaCommand(line, cmd, rest)) {
            if (cmd == "q" || cmd == "quit" || cmd == "exit") break;
            else if (cmd == "h" || cmd == "help") printHelp();
            else if (cmd == "l" || cmd == "clear") std::cout << "\x1b[H\x1b[2J" << std::flush;
            else if (cmd == "r" || cmd == "reset") {
                interp->replFinish();
                interp = fresh();
                std::cout << "session reset\n";
            }
            else if (cmd == "v" || cmd == "vars") {
                for (auto& n : interp->replNames()) std::cout << "  " << n << "\n";
            }
            else if (cmd == "t" || cmd == "type") {
                if (rest.empty()) { printError("\\t needs an expression"); continue; }
                try { printResult(*interp, interp->evalString("(" + rest + ").WHAT", true)); }
                catch (RakuError& e) { printError(e.message); }
                catch (std::exception& e) { printError(e.what()); }
            }
            else if (cmd == "a" || cmd == "ast") {
                if (rest.empty()) { printError("\\a needs an expression"); continue; }
                try {
                    Lexer lx(rest);
                    Parser ps(lx.tokenize());
                    Program p = ps.parseProgram();
                    dumpAst(p, std::cout);
                } catch (ParseError& e) {
                    printError(std::string("Parse error: ") + e.what());
                }
            }
            else printError("unknown command \\" + cmd + " (try \\h)");
            continue;
        }

        acc += line;
        acc += "\n";
        if (acc.find_first_not_of(" \t\r\n") == std::string::npos) { acc.clear(); continue; }

        bool incomplete = false;
        try {
            Value v = interp->evalString(acc, /*mainlinePH=*/true, &incomplete);
            if (incomplete) continue;                    // nothing ran; ask for more
            printResult(*interp, v);
        } catch (ExitEx& e) {
            exitCode = e.code;
            acc.clear();
            break;
        } catch (RakuError& e) {
            printError(e.message);
        } catch (ParseError& e) {
            printError(std::string("Parse error: ") + e.what());
        } catch (std::exception& e) {
            printError(std::string("Internal error: ") + e.what());
        }
        acc.clear();
    }
    interp->replFinish();
    std::cout.flush();
    return exitCode;
}

} // namespace

bool stdinIsTerminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

// RAKUPP_REPL=1 forces a session even with stdin redirected — the only way to
// drive the loop from a test, since a terminal is otherwise required. Setting it
// to 0 (or leaving it unset) changes nothing.
bool replForced() {
    const char* e = std::getenv("RAKUPP_REPL");
    return e && *e && std::strcmp(e, "0") != 0;
}

int rakuppRepl(const std::string& exePath, const std::vector<std::string>& libPaths) {
    ReplCtx ctx{exePath, libPaths};
    // Same 1 GiB stack a script gets: recursion typed at the prompt should reach
    // as deep as recursion in a file.
    return rakuppMainOnBigStack([](void* p) { return replMain(*static_cast<ReplCtx*>(p)); }, &ctx);
}

} // namespace rakupp
