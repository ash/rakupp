#pragma once
// The Raku programs `rakupp install` and `rakupp doc` dispatch to, carried
// inside the CLI instead of beside it. A binary on its own — copied into a
// container, unpacked as a bare rakupp.exe, installed by a route that dropped
// libexec/ — runs the same tools as a checkout.
//
// Both are defined in a translation unit cmake/EmbedTools.cmake writes into
// the BUILD tree at build time, from the ordinary files in tools/ — nothing
// generated is checked in, and those files stay the only copy of their own
// contents.
//
// CLI-only: main.cpp is the only caller, and CMakeLists.txt keeps the
// translation unit out of rakupp_rt so no `--exe` binary carries the blobs.
#include <string>

namespace rakupp {

std::string installerSource();  // tools/install.raku
std::string docToolSource();    // tools/doc.raku, with the two guides spliced in

} // namespace rakupp
