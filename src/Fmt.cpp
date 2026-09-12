// `rakupp --fmt` — the source formatter (FMT-PLAN.md).
//
// The promise is gofmt's: a tool you run on every save without reading its
// output. For Raku that is a harder promise than for Go, because whitespace is
// SEMANTICALLY SIGNIFICANT — `say (1,2)` and `say(1,2)` are different programs,
// `%h<key>` and `%h < key` are different programs — so the architecture is not
// "reprint the tree" but "rewrite the whitespace BETWEEN classified spans":
//
//   * the source is scanned into a lossless span stream (Highlight.h), and the
//     formatter edits only spans whose class is `""` — plain text. The bytes of
//     a string, regex, comment, POD or heredoc body are never touched;
//   * every run is checked before a byte is emitted: the input must parse, the
//     formatted text must parse to the SAME program, and formatting it again
//     must change nothing.
//
// Reprinting the tree was considered and ruled out, and the ruling was re-tested
// after RakuAST landed: `.AST.DEPARSE` drops every comment, so a reprint would
// be a formatter that deletes them.
#include "Fmt.h"

#include "AstSerial.h"
#include "Highlight.h"
#include "Lexer.h"
#include "Parser.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rakupp {
namespace {

// ---- the line model ------------------------------------------------------
//
// A line is its text plus the one fact the rules need about it: whether each
// byte is EDITABLE (plain text outside any classified span) or the author's.
// Lines inside a heredoc, a multi-line string or POD are wholly uneditable, so
// a rule cannot reach in and reindent a string.
struct Line {
    std::string text;      // without the newline
    bool hadNewline = true;
    bool editable = true;  // false: inside a string/heredoc/POD — do not touch
};

std::vector<Line> toLines(const std::vector<Span>& spans) {
    std::vector<Line> out;
    out.push_back({});
    // A line is the author's when it STARTS inside a classified span — inside a
    // heredoc body, a multi-line string, a POD block. That is the test, and it
    // is not "the line contains a classified span": every line of code contains
    // several (a keyword, a variable, a number), and treating those as
    // untouchable makes the formatter a no-op on all real source. Conservative
    // at one edge: a line that starts inside a string and ends in code after it
    // closes is left alone, which loses nothing a rule here would have done.
    // Untouchable iff the line's FIRST BYTE comes from a span that is both
    // classified and MULTI-LINE — a heredoc body, a multi-line string, a POD
    // block. Both halves are load-bearing, and I got each of them wrong on the
    // way here: "the line contains a classified span" makes every line of code
    // untouchable (they all contain a keyword), and "the line's first span is
    // classified" does the same to every line that opens with one. A span that
    // spans lines is the thing whose interior a formatter must not reach into;
    // a `say` keyword is not.
    bool decided = false;
    for (const Span& sp : spans) {
        const bool protectedSpan = sp.cls[0] != '\0' &&
                                   sp.text.find('\n') != std::string::npos;
        for (const char c : sp.text) {
            if (!decided) { out.back().editable = !protectedSpan; decided = true; }
            if (c == '\n') { out.push_back({}); decided = false; continue; }
            out.back().text += c;
        }
    }
    if (!decided) out.back().editable = true;   // nothing followed the last newline
    // a trailing newline leaves an empty final line; drop it and remember
    if (out.size() > 1 && out.back().text.empty()) out.pop_back();
    else out.back().hadNewline = false;
    return out;
}

std::string rtrim(const std::string& s) {
    size_t e = s.find_last_not_of(" \t\r");
    return e == std::string::npos ? std::string() : s.substr(0, e + 1);
}

// ---- the rules -----------------------------------------------------------
// Every one is whitespace-only, and every one asks `editable` first.
void applyRules(std::vector<Line>& lines) {
    // R2 — trailing whitespace.
    for (Line& l : lines) if (l.editable) l.text = rtrim(l.text);
    // R6 — runs of three or more blank lines collapse to two. A single or
    // double blank is the author's paragraphing and stays.
    std::vector<Line> out;
    size_t blanks = 0;
    for (Line& l : lines) {
        const bool blank = l.editable && l.text.find_first_not_of(" \t\r") == std::string::npos;
        if (blank) { if (++blanks > 2) continue; }
        else blanks = 0;
        out.push_back(l);
    }
    lines.swap(out);
}

std::string render(const std::vector<Line>& lines) {
    std::string out;
    for (size_t k = 0; k < lines.size(); k++) {
        out += lines[k].text;
        out += '\n';        // R3 — exactly one newline at EOF, and one per line
    }
    return out;
}

// The canonical blob of a program's MEANING: parsed, serialized with the line
// numbers stripped, so two texts that differ only in layout compare equal.
// Throws whatever the parser throws.
std::string meaning(const std::string& src) {
    Lexer lexer(src);
    Parser parser(lexer.tokenize());
    Program prog = parser.parseProgram();
    return serializeAst(prog, /*stripLines=*/true);
}

std::string formatOnce(const std::string& src) {
    std::vector<Line> lines = toLines(scanSpans(src));
    applyRules(lines);
    return render(lines);
}

} // namespace

FmtResult formatSource(const std::string& src) {
    FmtResult r;
    // GATE 1 — the input must parse. A file that does not parse is not
    // formatted: there is no best-effort tidying of broken code.
    std::string before;
    try { before = meaning(src); }
    catch (...) { r.status = FmtStatus::ParseError; return r; }

    std::string out = formatOnce(src);

    // GATE 2 — the formatted text must be the SAME PROGRAM. A backstop that
    // should never fire; its job is to make a formatting bug loud instead of
    // silent, the way `--slim=verify` does.
    try { if (meaning(out) != before) { r.status = FmtStatus::SemanticRefusal; return r; } }
    catch (...) { r.status = FmtStatus::SemanticRefusal; return r; }

    // GATE 3 — idempotence. A formatter that oscillates is worse than none,
    // and formatting the formatted text is cheap enough to check every run.
    if (formatOnce(out) != out) { r.status = FmtStatus::NotIdempotent; return r; }

    r.text = std::move(out);
    r.changed = r.text != src;
    return r;
}

// ---- the diff `--diff` prints -------------------------------------------
//
// Plain LCS over lines, after trimming the common head and tail — which is what
// makes it cheap here: formatting changes a handful of lines in a file of
// thousands, so the quadratic part runs over the handful.
std::string fmtUnifiedDiff(const std::string& name,
                           const std::string& before, const std::string& after) {
    auto split = [](const std::string& s) {
        std::vector<std::string> v;
        size_t i = 0;
        while (i <= s.size()) {
            size_t nl = s.find('\n', i);
            if (nl == std::string::npos) { if (i < s.size()) v.push_back(s.substr(i)); break; }
            v.push_back(s.substr(i, nl - i));
            i = nl + 1;
        }
        return v;
    };
    std::vector<std::string> a = split(before), b = split(after);
    size_t head = 0;
    while (head < a.size() && head < b.size() && a[head] == b[head]) head++;
    size_t tail = 0;
    while (tail < a.size() - head && tail < b.size() - head &&
           a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) tail++;
    const size_t an = a.size() - head - tail, bn = b.size() - head - tail;
    if (an == 0 && bn == 0) return std::string();

    // LCS over the changed middle
    std::vector<std::vector<size_t>> L(an + 1, std::vector<size_t>(bn + 1, 0));
    for (size_t i = an; i-- > 0; )
        for (size_t j = bn; j-- > 0; )
            L[i][j] = a[head + i] == b[head + j] ? L[i + 1][j + 1] + 1
                                                 : std::max(L[i + 1][j], L[i][j + 1]);
    struct Edit { char op; const std::string* line; };
    std::vector<Edit> edits;
    { size_t i = 0, j = 0;
      while (i < an && j < bn) {
          if (a[head + i] == b[head + j]) { edits.push_back({' ', &a[head + i]}); i++; j++; }
          else if (L[i + 1][j] >= L[i][j + 1]) { edits.push_back({'-', &a[head + i]}); i++; }
          else { edits.push_back({'+', &b[head + j]}); j++; }
      }
      for (; i < an; i++) edits.push_back({'-', &a[head + i]});
      for (; j < bn; j++) edits.push_back({'+', &b[head + j]});
    }
    // …with three lines of context on each side, taken from the trimmed head
    // and tail so the hunk header's line numbers are the real ones.
    const size_t ctx = 3;
    const size_t preN = std::min(ctx, head);
    const size_t postN = std::min(ctx, tail);
    std::string out = "--- " + name + "\n+++ " + name + " (formatted)\n";
    size_t aCount = an + preN + postN, bCount = bn + preN + postN;
    out += "@@ -" + std::to_string(head - preN + 1) + "," + std::to_string(aCount) +
           " +" + std::to_string(head - preN + 1) + "," + std::to_string(bCount) + " @@\n";
    for (size_t k = head - preN; k < head; k++) out += " " + a[k] + "\n";
    for (const Edit& e : edits) { out += e.op; out += *e.line; out += "\n"; }
    for (size_t k = 0; k < postN; k++) out += " " + a[a.size() - tail + k] + "\n";
    return out;
}

} // namespace rakupp
