#pragma once
#include <string>
#include <vector>

namespace rakupp {

// A classified run of source text — the lossless span stream `--highlight`
// renders and `--fmt` edits between. `cls` is the Pygments short class, and
// **`""` means plain text**: operators, punctuation and whitespace, the only
// bytes a formatter is allowed to touch. Everything else — `s` string, `sr`
// regex, `c1`/`cm` comment and POD, `nv` variable, `n`/`nb` name, `k` keyword,
// `m*` number — is a span whose bytes are the author's.
//
// Concatenating every `text` reproduces the input byte for byte;
// `t/regression/highlight-lossless.raku` holds the scanner to that over the
// repo's corpora, because it is what the formatter stands on.
struct Span {
    std::string text;
    const char* cls;   // "" for plain text
};
std::vector<Span> scanSpans(const std::string& source);

// Syntax-highlight Raku source, emitting the same CSS token classes Pygments uses
// (k, nb, nv, n, mi, mf, mh, s, c1, sr, …) so existing Pygments stylesheets apply
// unchanged — but classified with rakupp's own knowledge of Raku, so e.g. a method
// call named like a keyword (`$obj.role`) is coloured as a method, not a keyword.
//
// `format` selects the renderer:
//   "html"  — <div class="highlight"><pre>…<span class="k">my</span>…</pre></div>
//   "ansi"  — the same tokens as terminal escape sequences (for console output)
std::string highlight(const std::string& source, const std::string& format);

} // namespace rakupp
