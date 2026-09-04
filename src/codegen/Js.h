#pragma once
// The JavaScript backend (`rakupp --target=js`, TRANSPILE-PLAN.md): a second
// transpiler next to Codegen.cpp, emitting a JavaScript program over the
// runtime in src/js-rt/ (baked into the binary as jsRuntimeSource()).
#include "../Ast.h"
#include "../Codegen.h"   // CodegenError: the same refusal type main.cpp already catches
#include <set>
#include <string>

namespace rakupp {

struct JsOptions {
    std::string srcPath;                  // for $?FILE and the header
    std::string srcText;                  // the unit's source (for the manifest hash)
    std::string version;                  // RAKUPP_VERSION
    bool standalone = false;              // inline the runtime into the one file
    std::string rtPath = "./rakupp-rt.js"; // the sidecar's import path otherwise
    std::set<std::string> moduleExports;  // `is export` subs of `use`d modules (unused until modules land)
    bool module = false;
    std::string mapUrl;                   // when set: the `//# sourceMappingURL=` the program ends with (main writes the map beside it)                  // --module: an ES module exporting the program's subs, classes and MAIN instead of running it
};

// Transpile a whole program into a JavaScript program. Throws CodegenError on any
// construct outside the JS core; the message carries the source line.
std::string transpileToJs(Program& prog, const JsOptions& opt, std::string* dts = nullptr, std::string* map = nullptr);   // dts: the TypeScript declarations of a --module; map: the source map JSON

// The runtime, assembled from src/js-rt/*.js (generated: src/JsRuntimeSrc.cpp).
std::string jsRuntimeSource();
// The runtime as an ES module (the sidecar `-o` writes next to the program).
std::string jsRuntimeModule();

// The WASM-wrapper tier (`--fallback=wasm`): a program that loads rakujs.js and
// runs the embedded source through the interpreter compiled to WebAssembly.
std::string jsWasmWrapper(const std::string& src, const JsOptions& opt);

// The one-line manifest comment every output carries (readable by --exe-info).
std::string jsManifestLine(const JsOptions& opt, const char* mode);

}
