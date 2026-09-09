#pragma once
// The Raku programs `rakupp install` and `rakupp doc` dispatch to, carried
// inside the CLI instead of beside it. A binary on its own — copied into a
// container, unpacked as a bare rakupp.exe, installed by a route that dropped
// libexec/ — runs the same tools as a checkout.
//
// The two blobs are maintained differently, and the difference is deliberate:
// InstallerSrc.cpp IS the installer's source, hand-edited, with no .raku file
// anywhere; DocToolSrc.cpp is generated, because its blob carries
// docs/guide/{REFERENCE,FEATURES}.md, which are living documents that would
// drift from a hand-kept copy.
//
// CLI-only: main.cpp is the only caller, and CMakeLists.txt keeps both
// translation units out of rakupp_rt so no `--exe` binary carries the blobs.
#include <string>

namespace rakupp {

std::string installerSource();  // src/InstallerSrc.cpp (hand-maintained)
std::string docToolSource();    // tools/doc.raku + the two guides, generated

} // namespace rakupp
