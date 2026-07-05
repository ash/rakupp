#pragma once
#include <string>

namespace rakupp {

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
