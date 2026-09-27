// The copy-and-patch stencil table for Raku.js: empty, as CMake writes it for
// any platform the extractor cannot serve (CMakeLists.txt, "the copy-and-patch
// stencil table"). WebAssembly cannot execute code it writes into linear
// memory, so there is nothing to extract; with kCount at 0, `--cnp` says it has
// no stencils and the program runs interpreted. build.sh links this file IN
// PLACE of CMake's generated CnpStencils.cpp, which this build never runs.
#include "cnp/CnpTable.h"
namespace rakupp { namespace cnp {
// One placeholder element, never read: kCount is 0 and CnpEmit bails on that
// before it indexes anything.
const Stencil     kStencils[]     = { { nullptr, nullptr, 0, 0 } };
const char* const kStencilNames[] = { nullptr };
const char* const kHelperNames[]  = { nullptr };
const unsigned    kCount          = 0;
const unsigned    kHelperCount    = 0;
const char* const kArch           = "none";
} }
