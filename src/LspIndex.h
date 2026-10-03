#pragma once
// What `rakupp --lsp` knows about a document beyond its diagnostics: where its
// names are declared, what is visible at a position, and what REFERENCE.md says
// about the built-ins. Everything is computed from the TOKEN STREAM, not the
// AST — the lexer is tolerant and the parser is not, and hover and completion
// are asked for while the code is half typed. Nothing here runs the program.
//
// Positions inside this file are byte offsets into the document; Lsp.cpp
// converts to and from the protocol's (line, UTF-16 column) at the edge.
#include <string>
#include <vector>

namespace rakupp {
namespace lsp {

struct TextDoc {
    std::string text;
    std::vector<size_t> lineStarts; // byte offset of each line's first byte

    explicit TextDoc(std::string t);
    size_t offsetAt(int line0, int utf16Col) const;               // clamped
    void positionOf(size_t off, int& line0, int& utf16Col) const;
    int lineOf(size_t off) const;                                 // 0-based
    std::string lineText(int line0) const;                        // no newline
};

struct Span { size_t start = 0, end = 0; };

// The part of 1-based `line1` a diagnostic should underline: the token spelled
// `subject` on that line (a routine's `&f` also finds its bare `f`), else the
// subject's first appearance as text, else the line without its indentation.
Span locateOnLine(const TextDoc& doc, int line1, const std::string& subject);

// docs/guide/REFERENCE.md, indexed once. "" leaves built-ins undocumented.
void setReference(const std::string& markdown);

struct Hover {
    bool found = false;
    std::string plain, markdown; // the same text for either kind of client
    Span span;                   // the token hovered
};
Hover hoverAt(const TextDoc& doc, size_t off);

// LSP CompletionItemKind values used here.
enum class ItemKind { Method = 2, Function = 3, Field = 5, Variable = 6, Class = 7,
                      Module = 9, Keyword = 14, EnumMember = 20, Constant = 21 };

struct CompletionItem {
    std::string label;
    ItemKind kind = ItemKind::Variable;
    std::string detail;
    bool local = false;          // declared in this document: sorted first
    std::string docPlain, docMarkdown;
};
struct Completions {
    Span replace;                // the typed prefix the chosen item replaces
    std::vector<CompletionItem> items;
};
Completions completeAt(const TextDoc& doc, size_t off);

// The declaration of the name at `off`, in this document. False for built-ins
// and for names the index cannot see.
bool definitionAt(const TextDoc& doc, size_t off, Span& target);

} // namespace lsp
} // namespace rakupp
