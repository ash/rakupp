#pragma once
// `rakupp --doc-links`: every construct of a program, linked to the page on
// docs.raku.org that documents it — the keywords, the routines and methods it
// calls, the operators, the types it names, the special variables, the
// literals and quoting forms.
//
// Two layers decide a link. The TOKEN stream (Highlight.h's lossless spans)
// says where each construct is and what kind of word it is; the AST says what
// it means where a word cannot — that `-` is prefix or infix, that `.uc` is
// called on a Str, that `Point` is the program's own class. A program that does
// not parse still gets the token layer, with `parsed` false.
//
// Nothing here runs the program, and nothing touches the network: names are
// resolved against kIndex, a copy of the docs site's own search index
// committed as src/raku_docs_index_gen.cpp (tools/gen-raku-docs-index.raku),
// so every URL printed is one the site itself links to.
#include <cstddef>
#include <string>
#include <vector>

namespace rakupp {
namespace doclinks {

// One row of the docs site's index; see tools/gen-raku-docs-index.raku for
// what each kind holds.
struct IndexRow {
    char kind;          // T type, M member, R routine, S syntax, O operator, H heading, L page
    const char* key;
    const char* url;    // relative to https://docs.raku.org
    const char* info;
};
extern const IndexRow kIndex[];
extern const size_t kIndexSize;
extern const char kIndexDate[];

// One linked occurrence in the source.
struct Ref {
    size_t start = 0, end = 0;   // byte offsets
    size_t ustart = 0, uend = 0; // the same span in UTF-16 code units, for JavaScript hosts
    int line = 0, col = 0;       // 1-based; col counts code points
    std::string text;            // the source text linked
    std::string group;           // syntax | type | routine | operator | variable | local
    std::string label;           // "Str.uc", "infix ~", "for" — what the page is about
    std::string url;             // absolute; "" for a `local` ref
    std::string note;            // "invocant not known: Str, Cool, Allomorph" …
    bool exact = true;           // false: the page lists every type the name could be
    int declLine = 0;            // group == "local": the line that declares it
};

struct Result {
    std::vector<Ref> refs;       // in source order, non-overlapping
    bool parsed = true;          // false: the AST layer was missing (parse error)
    int errorLine = 0;
    std::string error;
    std::vector<std::string> unknown; // built-in names with no page (Raku++ extensions)
};

Result analyze(const std::string& source);

// Renderers. `file` is the name shown in headers ("-e" for code given inline).
std::string renderText(const Result& r, const std::string& file);
std::string renderJson(const Result& r, const std::string& file);
std::string renderHtml(const Result& r, const std::string& source, const std::string& file);
std::string renderAnsi(const Result& r, const std::string& source, const std::string& file);

} // namespace doclinks

// The CLI surface: `fmt` is "text", "json", "html" or "ansi". Returns the
// process exit code.
int runDocLinks(const std::string& source, const std::string& file, const std::string& fmt);

} // namespace rakupp
