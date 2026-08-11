// The exception behind the SLIM stubs (SLIM-PLAN P3). A binary compiled with
// `--slim=-feature` links librakupp_stubs.a in place of that feature's real
// archive; every stubbed entry point lands here. The throw is a plain typed
// Raku exception — catchable with `when X::Feature::NotBuilt`, printed with
// the message below when nothing catches it — because a cut feature is a
// BUILD decision the program ran into, not an internal error.
//
// This lives in its own rt translation unit (not in a stub) so that both
// sides of the seam can reach it: the stubs need it by definition, and rt
// itself may some day want to gate something the linker can't.

#include "ucd_seam.h"
#include "Interpreter.h"

namespace rakupp {

// The keep-alive half of the embedded manifest (main.cpp, slimManifestTU):
// every compiled binary's pre-main initializer parks the manifest pointer
// here. The call is what anchors the string against dead-strip — an escaped
// address into another TU is beyond any optimizer's reach — and the global
// means a future rt could self-report what it was built as.
static const char* g_exeManifest = nullptr;
void rakuppKeepManifest(const char* m) { g_exeManifest = m; }

[[noreturn]] void featureMissing(const char* feature, const char* neededFor) {
    throw RakuError{Value::typeObj("X::Feature::NotBuilt"),
        std::string(neededFor) + " needs the '" + feature +
        "' feature, and this binary was compiled without it (--slim=-" + feature +
        "). Recompile without that cut to include it; `rakupp --exe-info` on "
        "this binary shows the full build manifest."};
}

}
