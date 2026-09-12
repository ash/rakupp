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

#include "AsciiCtype.h"
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
    // The line's CODE skeleton: the characters that came from plain spans, in
    // order. Brackets are counted from this and nowhere else — a `{` inside a
    // string or a comment is not a block, and counting it would indent the rest
    // of the file by one level.
    std::string code;
    // One flag per byte of `text`: does a `,` or `=>` TOKEN start here? R5
    // moves nothing that is not marked, which is why it runs early — R1 and R4
    // rewrite the line's leading bytes and the marks stop lining up.
    std::string anchor;
    // …and one per byte: is it from a plain span? R2 needs it, because a line
    // can BEGIN in code and END inside a literal: `token TOP { ` has a trailing
    // space that belongs to the rule body, and trimming it changed the pattern.
    std::string plainMask;
};

// Byte offsets in `src` at which a real `,` or `=>` TOKEN begins.
//
// R5 used to find them by reading characters, and `<=>` cost a day: it
// CONTAINS the bytes `=>`, so the character scanner spaced `1 <=> 2` out into
// `1 < => 2` — a different program. `==>` the same. The lexer already knows
// which of those bytes are an operator and which are three; ask it, and let R5
// touch nothing else.
//
// A fat arrow GLUED to a zip/cross metaoperator (`1,2 X=> 3,4`) is left exactly
// as written: there the space is the difference between a metaop and a syntax
// error.
//
// The position comes from `Token::off`, the byte just PAST the token, so the
// start is `off - text.size()`. `Token::col` is NOT usable for this — it drifts
// several columns ahead of the token it belongs to. And the arithmetic checks
// itself: a mark is laid down only when those bytes really do spell the token,
// so a wrong offset makes R5 go quiet rather than wrong.
std::string spacingAnchors(const std::string& src) {
    std::string mark(src.size(), '0');
    std::vector<Token> toks;
    try { Lexer lexer(src); toks = lexer.tokenize(); }
    catch (...) { return mark; }
    for (size_t t = 0; t < toks.size(); t++) {
        const Token& tk = toks[t];
        const char* what;
        if (tk.kind == Tok::Comma) what = ",";
        else if (tk.kind == Tok::FatArrow) what = "=>";
        else continue;
        if (tk.kind == Tok::FatArrow && !tk.spaceBefore && t > 0) {
            const std::string& prev = toks[t - 1].text;
            bool metaop = !prev.empty() && prev.size() <= 3;
            for (const char c : prev)
                if (c != 'Z' && c != 'X' && c != 'R' && c != 'S') metaop = false;
            if (metaop) continue;
        }
        const size_t n = std::char_traits<char>::length(what);
        if (tk.off < n || tk.off > src.size()) continue;
        const size_t start = tk.off - n;
        if (src.compare(start, n, what) != 0) continue;
        for (size_t i = 0; i < n; i++) mark[start + i] = '1';
    }
    return mark;
}

std::vector<Line> toLines(const std::vector<Span>& spans, const std::string& anchors) {
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
    size_t off = 0;   // the span stream is lossless, so this is the byte in `src`
    for (const Span& sp : spans) {
        const bool protectedSpan = sp.cls[0] != '\0' &&
                                   sp.text.find('\n') != std::string::npos;
        const bool plain = sp.cls[0] == '\0';
        for (const char c : sp.text) {
            // Both witnesses have to agree: the lexer says a `,`/`=>` token
            // starts here, and the scanner says this byte is code. The lexer
            // alone is not enough — it tokenizes the inside of a word quote,
            // so `<vp 1,2,3 hi>` came back with a comma token in it and R5
            // rewrote a string literal.
            const bool marked = plain && off < anchors.size() && anchors[off] == '1';
            off++;
            if (!decided) { out.back().editable = !protectedSpan; decided = true; }
            if (c == '\n') { out.push_back({}); decided = false; continue; }
            out.back().text += c;
            out.back().anchor += marked ? '1' : '0';
            out.back().plainMask += plain ? '1' : '0';
            if (plain) out.back().code += c;
        }
    }
    if (!decided) out.back().editable = true;   // nothing followed the last newline
    // a trailing newline leaves an empty final line; drop it and remember
    if (out.size() > 1 && out.back().text.empty()) out.pop_back();
    else out.back().hadNewline = false;
    return out;
}

// ---- the rules -----------------------------------------------------------
// Every one is whitespace-only, and every one asks `editable` first.
void applyRules(std::vector<Line>& lines) {
    // R2 — trailing whitespace, but only the whitespace that is CODE.
    //
    // `editable` is a per-line answer and this is the one place that is not
    // enough: a line can begin in code and end inside a literal. `token TOP { `
    // has a trailing space that belongs to the rule body, and trimming it
    // changed the pattern — a real grammar in the ecosystem, caught by the
    // semantic gate. So the trim stops at the first byte that is not plain.
    //
    // It also has to run BEFORE R5, which addresses bytes by position: taking
    // characters off the END leaves every earlier index alone, so R5's marks
    // still line up. Inserting first and trimming after would not.
    for (Line& l : lines) {
        if (!l.editable) continue;
        size_t e = l.text.size();
        while (e > 0) {
            const char c = l.text[e - 1];
            if (c != ' ' && c != '\t' && c != '\r') break;
            if (e - 1 < l.plainMask.size() && l.plainMask[e - 1] != '1') break;
            e--;
        }
        l.text.resize(e);
        if (l.anchor.size() > e) l.anchor.resize(e);
        if (l.plainMask.size() > e) l.plainMask.resize(e);
    }

    // R5 — MINIMUM spacing: at least one space after a `,` token and around a
    // `=>` token. *Minimum* is the load-bearing word — a run of spaces is never
    // shrunk, so a hand-aligned table of `=>` pairs or a lined-up argument list
    // comes through exactly as written.
    //
    // It runs early because it is the one rule that addresses bytes by
    // position: `anchor` is aligned with `text` as the scanner produced them,
    // and R1 and R4 both rewrite a line's leading bytes.
    for (Line& l : lines) {
        if (!l.editable || l.anchor.find('1') == std::string::npos) continue;
        std::string out;
        for (size_t k = 0; k < l.text.size(); k++) {
            const char c = l.text[k];
            const bool marked = k < l.anchor.size() && l.anchor[k] == '1';
            out += c;
            if (!marked) continue;
            const char next = k + 1 < l.text.size() ? l.text[k + 1] : '\0';
            if (c == ',') {
                // nothing is inserted before a closer: `f(1,)` stays
                if (next != '\0' && next != ' ' && next != '\t' &&
                    next != ')' && next != ']' && next != '}')
                    out += ' ';
                continue;
            }
            // `=>` — both bytes are marked; handle the pair at the `=`
            if (c != '=' || next != '>') continue;
            if (out.size() >= 2) {
                const char prev = out[out.size() - 2];
                if (prev != ' ' && prev != '\t') out.insert(out.size() - 1, 1, ' ');
            }
            out += '>';
            k++;
            const char after = k + 1 < l.text.size() ? l.text[k + 1] : '\0';
            if (after != '\0' && after != ' ' && after != '\t') out += ' ';
        }
        l.text.swap(out);
    }

    // R4 — `else` / `elsif` / `orwith` / `without` on their own line, at the
    // `if`'s indent: `} else {` becomes `}\nelse {`. House style, and the style
    // of every `else` in examples/. Only when the line's code STARTS with the
    // closing brace — `} else {` — so a trailing comment or anything else on
    // the line is left alone rather than guessed at.
    //
    // It runs BEFORE R1 so that R1 indents the two lines it makes. The other
    // way round they kept whatever column the joined line had, and the NEXT
    // run of the formatter put the `else` where it belonged — an oscillation
    // the idempotence gate caught on real modules, never a wrong file.
    {
        std::vector<Line> out;
        for (Line& l : lines) {
            static const char* kw[] = {"else", "elsif", "orwith", "without"};
            if (l.editable) {
                const size_t b = l.text.find_first_not_of(" \t");
                if (b != std::string::npos && l.text[b] == '}') {
                    size_t k = l.text.find_first_not_of(" \t", b + 1);
                    for (const char* w : kw) {
                        const size_t n = std::char_traits<char>::length(w);
                        if (k == std::string::npos || l.text.compare(k, n, w) != 0) continue;
                        const char after = k + n < l.text.size() ? l.text[k + n] : ' ';
                        if (ascii::isalnum((unsigned char)after) || after == '-' || after == '_') continue;
                        Line head = l;
                        head.text = l.text.substr(0, b + 1);   // `}` and its indent
                        head.code = "}";
                        // the `}` moves to the head, so it leaves the skeleton
                        // the rest of the line is indented from
                        const size_t cb = l.code.find('}');
                        if (cb != std::string::npos) l.code.erase(cb, 1);
                        l.text = l.text.substr(0, b) + l.text.substr(k);
                        out.push_back(head);
                        break;
                    }
                }
            }
            out.push_back(l);
        }
        lines.swap(out);
    }

    // R1 — indentation: a statement-start line is indented 4 x bracket depth.
    //
    // A CONTINUATION line — one whose statement began earlier, because the
    // previous code line did not end in `;`, `{`, `}`, `(` or `[` — is
    // deliberately NOT set to a computed column. It is shifted by the same
    // amount its statement's first line moved, which preserves whatever the
    // author lined up: chained-method ladders, aligned grammar rules,
    // multi-line argument lists. A formatter that recomputes those columns
    // destroys the alignment that made someone write them that way.
    {
        int depth = 0;
        int stmtShift = 0;          // how far this statement's first line moved
        bool inStatement = false;   // …and whether we are inside one
        for (Line& l : lines) {
            const size_t firstCode = l.text.find_first_not_of(" \t");
            const bool blank = firstCode == std::string::npos;
            // Leading closers belong to the enclosing level, not this one:
            // `}` lines up with the `{`'s statement.
            int lead = 0;
            for (char c : l.code) {
                if (c == '}' || c == ']' || c == ')') lead++;
                else if (c != ' ' && c != '\t') break;
            }
            int open = 0;
            for (char c : l.code) {
                if (c == '{' || c == '[' || c == '(') open++;
                else if (c == '}' || c == ']' || c == ')') open--;
            }
            if (!blank && l.editable) {
                const std::string cur = l.text.substr(0, firstCode);
                if (!inStatement) {
                    const int want = std::max(0, depth - lead) * 4;
                    const std::string ind(want, ' ');
                    stmtShift = (int)ind.size() - (int)cur.size();
                    l.text = ind + l.text.substr(firstCode);
                } else if (stmtShift != 0) {
                    // shift, never recompute — keep the author's relative column
                    if (stmtShift > 0) l.text = std::string(stmtShift, ' ') + l.text;
                    else {
                        const size_t drop = std::min((size_t)(-stmtShift), firstCode);
                        l.text = l.text.substr(drop);
                    }
                }
            }
            depth += open;
            if (!blank) {
                // The last significant CODE character decides whether the
                // statement ends here — a `;` inside a string does not count,
                // which is why this reads the skeleton and not the text.
                //
                // An OPENING bracket ends it too, the way `{` does: what
                // follows is a new level to be indented, not a continuation of
                // this line to be shifted. Without `(`/`[` in that list, an
                // `if` written inside a parenthesised expression kept the
                // author's column while its body was re-indented under it, and
                // the two passes disagreed about the closing `}` — 18 of the
                // 723 installed ecosystem modules were refused by the
                // idempotence gate on exactly that shape.
                size_t e = l.code.find_last_not_of(" \t\r");
                if (e != std::string::npos) {
                    const char last = l.code[e];
                    inStatement = !(last == ';' || last == '{' || last == '}' ||
                                    last == '(' || last == '[');
                }
                // …and a line with NO code characters — one that is entirely a
                // string, a regex or a comment — says nothing either way, so
                // the answer carries over. Reading it as "a statement ended
                // here" made the `}` after a block's final string expression a
                // continuation of it, and shifted the brace by the string's
                // own indent.
            }
        }
    }

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
    std::vector<Line> lines = toLines(scanSpans(src), spacingAnchors(src));
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
