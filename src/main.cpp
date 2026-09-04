#include "AsciiCtype.h"
#include "BuildInfo.h"
#include "Runtime.h"
#include "Profiler.h"
#include <cstdint>
#include <cstdio>
#include "Codegen.h"
#include "codegen/Js.h"
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include "Lexer.h"
#include "Parser.h"
#include "AstSerial.h"
#include "SlimScan.h"
#include "Interpreter.h"
#include "DeclCheck.h"
#include "Lint.h"
#include "Ffi.h"
#include "Highlight.h"
#include "Lsp.h"
#include "JupyterKernel.h"
#include "McpServer.h"
#include "Repl.h"
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "Platform.h"
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

using namespace rakupp;

// ---- helpers for the compile modes (--bundle / --aot / --exe) ----------

#ifdef _WIN32
// UTF-8 <-> UTF-16 for the Win32 wide APIs: the narrow (ANSI) functions mangle
// any path character outside the system codepage — e.g. an install under a
// non-Latin user profile — which made the runtime-library probes miss and the
// compile command fail. All path traffic in the compile modes goes wide.
static std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

// Does a file exist, with the path treated as UTF-8 (wide probe on Windows)?
static bool fileExists(const std::string& path) {
#ifdef _WIN32
    DWORD a = ::GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    return std::ifstream(path).good();
#endif
}

// Run a compile command with the string treated as UTF-8 end to end.
static int runCommand(const std::string& cmd) {
#ifdef _WIN32
    return ::_wsystem(widen(cmd).c_str());
#else
    return std::system(cmd.c_str());
#endif
}

// Open a file for writing / remove a file, path treated as UTF-8.
static std::ofstream openOut(const std::string& path) {
#ifdef _WIN32
    return std::ofstream(std::filesystem::path(widen(path)));
#else
    return std::ofstream(path);
#endif
}
static void removeFile(const std::string& path) {
#ifdef _WIN32
    ::_wremove(widen(path).c_str());
#else
    std::remove(path.c_str());
#endif
}

// Wrap a string for the shell — single quotes for POSIX shells, double quotes
// for cmd.exe (which has no single-quote syntax).
static std::string shq(const std::string& s) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    out += "'";
    return out;
#endif
}

// Does this compiler use MSVC-style options? (cl or clang-cl, by basename)
static bool msvcStyle(const std::string& cxx) {
    std::string b = cxx;
    size_t sl = b.find_last_of("/\\");
    if (sl != std::string::npos) b = b.substr(sl + 1);
    for (auto& ch : b) ch = (char)ascii::tolower((unsigned char)ch);
    if (b.size() > 4 && b.compare(b.size() - 4, 4, ".exe") == 0) b.resize(b.size() - 4);
    return b == "cl" || b == "clang-cl";
}

#ifdef _WIN32
static bool onPathW(const wchar_t* name) {
    wchar_t buf[4096];
    return ::SearchPathW(nullptr, name, L".exe", 4096, buf, nullptr) != 0;
}
// A compile failed on Windows: point at the likely toolchain mismatch. The
// runtime archive is toolchain-specific, so the MinGW build needs g++ and the
// MSVC build needs cl — using the wrong build in the wrong shell is the usual
// cause (a MinGW .a handed to cl, or vice versa).
static void winCompilerHint(const std::string& lib) {
    bool gnuArchive = lib.size() >= 2 && lib.compare(lib.size() - 2, 2, ".a") == 0;
    if (gnuArchive)
        std::cerr << "(this is the MinGW build: its --exe needs g++ (MSYS2/MinGW-w64) on PATH. "
                     "In a Visual Studio / cl prompt, use the MSVC build instead — or set CXX.)\n";
    else
        std::cerr << "(no working MSVC compiler: open a Developer Command Prompt or install the "
                     "VS Build Tools; or use the MinGW build with g++ on PATH — or set CXX.)\n";
}
#endif

// The native compiler for --exe/--bundle: $CXX wins; otherwise the compiler must
// match the runtime archive we ship, since a static archive is toolchain-specific:
//   rakupp_rt.lib  (MSVC build)  -> cl / clang-cl
//   librakupp_rt.a (MinGW build) -> g++ / clang++   (cl CANNOT link a GNU .a —
//                                   that was the "unrecognized source file type
//                                   …librakupp_rt.a" + LNK2019 failure when --exe
//                                   ran from a VS prompt with cl on PATH.)
// `lib` is the archive path found by findRuntime; its extension picks the family.
// Elsewhere it's always `c++`.
static std::string nativeCxx(const std::string& lib = "") {
    const char* e = std::getenv("CXX");
    if (e && *e) return e;                        // explicit user choice always wins
#ifdef _WIN32
    bool gnuArchive = lib.size() >= 2 && lib.compare(lib.size() - 2, 2, ".a") == 0;
    if (gnuArchive) {                             // MinGW runtime: only a GNU compiler links it
        if (onPathW(L"g++")) return "g++";
        if (onPathW(L"clang++")) return "clang++";
        return "g++";                             // best guess; a clear error if absent
    }
    // MSVC runtime (rakupp_rt.lib), or no archive info yet: prefer cl / clang-cl.
    if (onPathW(L"cl")) return "cl";
    if (onPathW(L"clang-cl")) return "clang-cl";
    if (onPathW(L"g++")) return "g++";            // last resort if no MSVC compiler present
    if (onPathW(L"clang++")) return "clang++";
    return "cl";                                  // vcvars bootstrap below may still find it
#else
    (void)lib; return "c++";
#endif
}

#ifdef _WIN32
// `cl` requested but not on PATH (a plain cmd/PowerShell rather than a
// Developer Command Prompt): locate Visual Studio through vswhere and prefix
// the compile command with vcvars64.bat, so --exe works out of the box on any
// machine with VS or the Build Tools installed.
static std::string msvcEnvPrefix() {
    if (onPathW(L"cl")) return "";
    const char* pf = std::getenv("ProgramFiles(x86)");
    std::string vswhere = std::string(pf ? pf : "C:\\Program Files (x86)") +
                          "\\Microsoft Visual Studio\\Installer\\vswhere.exe";
    if (!fileExists(vswhere)) return "";
    std::string q = shq(vswhere) +
        " -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
        " -property installationPath";
    FILE* p = ::_wpopen(widen(q).c_str(), L"r");
    if (!p) return "";
    char line[1024]; std::string vs;
    if (fgets(line, sizeof line, p)) {
        vs = line;
        while (!vs.empty() && (vs.back() == '\n' || vs.back() == '\r')) vs.pop_back();
    }
    ::_pclose(p);
    if (vs.empty()) return "";
    std::string vcvars = vs + "\\VC\\Auxiliary\\Build\\vcvars64.bat";
    if (!fileExists(vcvars)) return "";
    return "call " + shq(vcvars) + " >nul 2>&1 && ";
}
#endif

// CMake defines this from project(VERSION …); the fallback keeps ad-hoc
// compiles of this file honest rather than lying with a real-looking number.
#ifndef RAKUPP_VERSION
#define RAKUPP_VERSION "0.0.0"
#endif

// --slim: how much of itself a compiled binary keeps (docs/dev/plans/SLIM-PLAN.md).
//
// P0 ships the LEVELS, and `safe` becomes the default with no flag: the
// linker drops unreferenced sections and local symbols leave the symbol table.
// That removes no Raku feature and runs no analysis — there is nothing for a
// user to weigh, which is the bar for being a default. Measured on `say
// "Hello"`: 9.86 MB → 8.09 MB. The escapes are `--slim=none` (the old output,
// bit for bit) and `--slim=+symbols` (dead-strip but keep the symbol table,
// for a crash report worth reading).
//
// P3 shipped the EXPLICIT half of the feature grammar: `-feature` swaps that
// feature's real archive for its stub in librakupp_stubs.a, so the tables (or
// the parser) stay out of the binary and the operations that needed them
// throw X::Feature::NotBuilt. `+feature` re-keeps one; `all` is the four as a
// group, `unicode` the three Unicode features, and a named feature beats a
// group — `-all,+eval` is "cut everything except eval".
//
// P4 ships the SCAN and the levels above `safe`: `auto` (what bare `--slim`
// means, and what any level-less SPEC implies) cuts only what the scan PROVES
// no site can reach, and keeps everything — saying so on stderr — when the
// program contains a construct that can run code the scan never saw. `max`
// cuts by the same static evidence but ignores those triggers: unsound by
// design, and a wrong cut throws X::Feature::NotBuilt at run time rather
// than crash or lie. Explicit ±feature beats either level's conclusion.
// Every conflict is an error: two levels, the same name with both signs,
// `none` with any override — SLIM-PLAN's rule is that a wrong ask is loud,
// never last-wins.
enum class SlimLevel { None, Safe, Auto, Max };
// P5: the introspection directives. At most one per SPEC; `help` must stand
// alone. `list`/`why:` analyse without compiling; `verify` compiles twice.
enum class SlimDirective { None, Help, List, Why, Verify };
struct SlimSpec {
    SlimLevel level = SlimLevel::Safe;
    bool deadStrip = true;   // level ≥ safe: unreferenced sections dropped at link
    bool stripSyms = true;   // …and local symbols left out of the symbol table
    int  featSign[4] = {0, 0, 0, 0};             // explicit ± per feature (0 = unsaid)
    bool cut[4] = {false, false, false, false};  // the decision; the scan adds to it
    SlimDirective directive = SlimDirective::None;
    std::string whyFeat;                         // why:FEAT — which feature to explain
};
static SlimSpec g_slim;                 // default = level `safe`, nothing cut
static bool g_standalone = false;       // --standalone: an unembeddable module is a build ERROR (MODULES-PLAN B2)
// -q / --quiet — ONE option, accepted by every mode (issue #50). It drops the
// lines a mode prints about its own progress or success: `Compiled …`,
// `Syntax OK`, the lint summary, the REPL banner, the installer's `already
// installed:`. It never drops a mode's PRODUCT (a program's output, lint
// findings, the AST, `install --list`), a warning, or an error — a quiet run
// that succeeds prints nothing, one that fails prints what it always did.
// A mode with nothing informational to say accepts the flag and changes
// nothing, the way -l does.
static bool g_quiet = false;

// MODULES-PLAN B1: every compile mode says what it embedded and — one line
// each, with the reason — what it could NOT. B2: under --standalone the
// skips are fatal. B5: the native libraries the binary will still dlopen
// are named; they cannot be embedded and must not be hidden.
// Returns false when --standalone must fail the build.
// -q keeps the skips and the natives: both say the binary will need the
// disk, which is a caveat about the product, not narration.
static bool reportModuleEmbedding(const char* mode,
                                  const std::vector<rakupp::BundledModule>& mods,
                                  const std::vector<rakupp::ModuleSkip>& skips,
                                  const std::set<std::string>& natives) {
    if (!mods.empty() && !g_quiet) {
        std::cerr << mode << ": embedded " << mods.size() << " module"
                  << (mods.size() == 1 ? "" : "s") << ":";
        for (auto& m : mods) std::cerr << " " << m.name;
        std::cerr << "\n";
    }
    for (auto& s : skips)
        std::cerr << mode << ": not embedded: " << s.name << " — " << s.reason
                  << (g_standalone ? "" : " (the binary will need the disk at run time)") << "\n";
    if (!natives.empty()) {
        std::cerr << mode << ": native libraries the binary will dlopen at run time:";
        for (auto& n : natives) std::cerr << " " << n;
        std::cerr << "\n";
    }
    if (g_standalone && !skips.empty()) {
        std::cerr << mode << ": --standalone: the modules above cannot be embedded — refusing to build a binary that needs the disk\n";
        return false;
    }
    return true;
}
static bool     g_slimExplicit = false; // a --slim flag was given (mode validation)

// The cuttable features and their archives, index-aligned with SlimSpec::cut.
// The names are the user-facing grammar; the archives are what findRuntimeSet
// swaps. `eval` is index 3 — compileToExe checks it by name kSlimEval.
static const char* kSlimFeatures[4] = {"unicode-names", "unicode-collation",
                                       "unicode-props", "eval"};
static const char* kSlimArchives[4] = {"ucd_names", "ucd_coll", "ucd_props", "parse"};
static const int   kSlimEval = 3;
static const char* kSlimValid =
    "none, safe, auto, max, +symbols, and ±feature of: unicode-names, "
    "unicode-collation, unicode-props, eval, unicode, all";

static bool slimScans() {
    return g_slim.level == SlimLevel::Auto || g_slim.level == SlimLevel::Max;
}

// Parse a --slim SPEC into g_slim. Returns "" on success, else the error text.
// Grammar: at most one level (`none` | `safe` | `auto` | `max`), `±symbols`,
// `±feature` with the groups `unicode` and `all`. A SPEC that names no level
// means `auto` — so bare `--slim` is the button, and `--slim=+eval` is
// "automatic pruning, but keep eval". Conflicts are errors that name the
// valid alternatives.
static std::string parseSlimSpec(const std::string& spec) {
    SlimSpec out;
    bool haveLevel = false, levelNone = false, anyOverride = false;
    int symSign = 0, allSign = 0, uniSign = 0;   // ±symbols / ±all / ±unicode
    int featSign[4] = {0, 0, 0, 0};              // ±feature, by kSlimFeatures index
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t comma = spec.find(',', pos);
        std::string tok = spec.substr(pos, comma == std::string::npos ? std::string::npos
                                                                      : comma - pos);
        pos = comma == std::string::npos ? spec.size() + 1 : comma + 1;
        if (tok.empty()) continue;
        if (tok == "help" || tok == "list" || tok == "verify" || tok.rfind("why:", 0) == 0) {
            if (out.directive != SlimDirective::None)
                return "--slim takes one directive at a time (got '" + tok + "' after another)";
            if (tok == "help") out.directive = SlimDirective::Help;
            else if (tok == "list") out.directive = SlimDirective::List;
            else if (tok == "verify") out.directive = SlimDirective::Verify;
            else {
                out.directive = SlimDirective::Why;
                out.whyFeat = tok.substr(4);
                bool known = false;
                for (int i = 0; i < 4; i++) if (out.whyFeat == kSlimFeatures[i]) known = true;
                if (!known)
                    return "--slim=why: takes a feature name (unicode-names, "
                           "unicode-collation, unicode-props, eval); got '" + out.whyFeat + "'";
            }
            continue;
        }
        if (tok == "none" || tok == "safe" || tok == "auto" || tok == "max") {
            if (haveLevel) return "--slim takes at most one level (got a second: '" + tok + "')";
            haveLevel = true;
            if (tok == "none") { levelNone = true; out.level = SlimLevel::None;
                                 out.deadStrip = out.stripSyms = false; }
            else if (tok == "safe") out.level = SlimLevel::Safe;
            else if (tok == "auto") out.level = SlimLevel::Auto;
            else out.level = SlimLevel::Max;
            continue;
        }
        if (tok[0] == '+' || tok[0] == '-') {
            int sign = tok[0] == '+' ? 1 : -1;
            std::string name = tok.substr(1);
            int* slot = nullptr;
            if (name == "symbols") slot = &symSign;
            else if (name == "all") slot = &allSign;
            else if (name == "unicode") slot = &uniSign;
            else
                for (int i = 0; i < 4; i++)
                    if (name == kSlimFeatures[i]) { slot = &featSign[i]; break; }
            if (!slot)
                return "Unknown --slim feature '" + name + "' (available: " + kSlimValid + ")";
            if (*slot && *slot != sign)
                return "--slim got both +" + name + " and -" + name + " — pick one";
            *slot = sign;
            anyOverride = true;
            continue;
        }
        return "Unknown --slim token '" + tok + "' (available: " + std::string(kSlimValid) + ")";
    }
    if (levelNone && anyOverride)
        return "--slim=none already keeps everything; combining it with a ±symbols "
               "or ±feature override is a conflict, not a refinement";
    if (out.directive == SlimDirective::Help && (haveLevel || anyOverride))
        return "--slim=help stands alone (it documents the whole grammar)";
    // A SPEC that names no level means `auto` — including the empty SPEC
    // (bare `--slim`). `safe` with overrides stays explicit-only, no scan.
    if (!haveLevel) out.level = SlimLevel::Auto;
    if (symSign) out.stripSyms = symSign < 0;
    // Group resolution, most specific wins: a named feature beats `unicode`
    // beats `all` (so `-all,+eval` cuts three, and `-all,+unicode` cuts
    // exactly eval). The resolved sign is remembered: under auto/max an
    // explicit ± also beats whatever the scan concludes.
    for (int i = 0; i < 4; i++) {
        int g = (i < 3 && uniSign) ? uniSign : allSign;
        out.featSign[i] = featSign[i] ? featSign[i] : g;
        out.cut[i] = out.featSign[i] < 0;
    }
    g_slim = out;
    return "";
}

static std::string cppstr(const std::string& s);  // defined below (C string literal)

// The marker in front of the embedded manifest. Built by concatenation at run
// time so the contiguous byte sequence exists in COMPILED programs but never
// in rakupp's own binary — otherwise `--exe-info rakupp` would find this very
// string constant and report the scanner's scaffolding as a manifest.
static std::string slimMarker() { return std::string("RAKUPP-EXE-") + "MANIFEST "; }

// The build manifest (SLIM-PLAN P3): appended to every generated translation
// unit, so each compiled binary carries one greppable line saying what it is
// and what was cut — readable with `rakupp --exe-info BIN` (or strings|grep).
// A dynamic initializer keeps it alive: initializer arrays are dead-strip
// roots on every linker we drive (.init_array is KEEP in ELF scripts,
// mod_init_func on ld64, CRT$XCU on MSVC), where plain unreferenced data
// would be exactly what --slim's own dead-strip removes. The initializer
// hands the pointer to an rt function rather than touching a volatile local:
// clang at -O2 elides an unescaped volatile local, initializer and all —
// measured, not hypothetical — but a call into another TU with the address
// as argument is beyond any optimizer's reach.
static std::string slimManifestTU(const char* how) {
    std::string cut;
    for (int i = 0; i < 4; i++)
        if (g_slim.cut[i]) cut += std::string(cut.empty() ? "" : ",") +
                                  "\"" + kSlimFeatures[i] + "\"";
    const char* lvl = g_slim.level == SlimLevel::None ? "none"
                    : g_slim.level == SlimLevel::Safe ? "safe"
                    : g_slim.level == SlimLevel::Auto ? "auto" : "max";
    std::string json = std::string("{\"rakupp\":\"") + RAKUPP_VERSION +
        "\",\"mode\":\"" + how +
        "\",\"slim\":\"" + lvl +
        "\",\"symbols\":\"" + (g_slim.stripSyms ? "stripped" : "kept") +
        "\",\"cut\":[" + cut + "]}";
    return "\nextern \"C\" const char rakupp_exe_manifest[] = " +
           cppstr(slimMarker() + json) + ";\n"
           "namespace rakupp { void rakuppKeepManifest(const char*); }\n"
           "namespace { struct RkKeepManifest { RkKeepManifest() {\n"
           "    rakupp::rakuppKeepManifest(rakupp_exe_manifest);\n"
           "} } rk_keep_manifest; }\n";
}

// The ONE place a feature's fate is decided from (spec, scan) — the compile
// path (applySlimScan) and the `list`/`why:` directives both read it, so what
// `list` prints is what a compile does, by construction.
struct SlimDecision {
    bool cut;
    std::string why;        // human-readable reason, in list's column style
};
static void slimDecide(const SlimScanResult& sr, SlimDecision d[4]) {
    bool scans = slimScans();
    bool full  = g_slim.level == SlimLevel::Auto && !sr.triggers.empty();
    for (int i = 0; i < 4; i++) {
        if (g_slim.featSign[i] > 0) { d[i] = {false, std::string("kept by +") + kSlimFeatures[i]}; continue; }
        if (g_slim.featSign[i] < 0) { d[i] = {true,  std::string("cut by -") + kSlimFeatures[i]}; continue; }
        // first recorded use, for the reason column
        std::string firstUse;
        for (const auto& site : sr.sites)
            if (site.feat == i) {
                firstUse = "used: " + site.what;
                if (!site.where.empty()) firstUse += " (module " + site.where + ")";
                else if (site.line)      firstUse += " (line " + std::to_string(site.line) + ")";
                break;
            }
        if (!scans) {
            const char* lvl = g_slim.level == SlimLevel::None ? "none" : "safe";
            d[i] = {false, std::string("level ") + lvl + " keeps everything" +
                           (sr.used[i] ? "; also " + firstUse
                                       : std::string("; bare --slim would cut it"))};
        }
        else if (full)
            d[i] = {false, sr.used[i] ? firstUse
                                      : "kept: a dynamic construct keeps everything (see below)"};
        else if (sr.used[i])
            d[i] = {false, firstUse};
        else
            d[i] = {true, g_slim.level == SlimLevel::Max && !sr.triggers.empty()
                              ? "proven unused (dynamic constructs ignored)"
                              : "proven unused"};
    }
}

// Fold the scan's verdict into g_slim (SLIM-PLAN P4). Under `auto` a trigger
// keeps everything and says so — the program can run code the scan never saw,
// and the default answer to uncertainty is "keep". Under `max` the triggers
// are ignored by design (a wrong cut throws X::Feature::NotBuilt at run
// time), but they are still WORTH a line: the user should know the binary is
// trusting static evidence alone. An explicit ±feature beats both.
static void applySlimScan(const SlimScanResult& sr) {
    bool full = g_slim.level == SlimLevel::Auto && !sr.triggers.empty();
    if (full) {
        std::cerr << "--slim: this program can run code the scan cannot see — keeping every feature.\n";
        size_t show = sr.triggers.size() < 4 ? sr.triggers.size() : 4;
        for (size_t i = 0; i < show; i++)
            std::cerr << "        " << sr.triggers[i] << "\n";
        if (sr.triggers.size() > show)
            std::cerr << "        … and " << (sr.triggers.size() - show) << " more\n";
        std::cerr << "        (--slim=max cuts on static evidence anyway; ±feature overrides either)\n";
    }
    else if (g_slim.level == SlimLevel::Max && !sr.triggers.empty()) {
        std::cerr << "--slim=max: ignoring " << sr.triggers.size()
                  << " dynamic construct(s) the scan cannot see — a feature they need "
                     "at run time will throw X::Feature::NotBuilt.\n";
    }
    SlimDecision d[4];
    slimDecide(sr, d);
    for (int i = 0; i < 4; i++)
        if (d[i].cut) g_slim.cut[i] = true;
}

// --exe-info FILE: print the embedded manifest of a rakupp-compiled binary.
// A byte scan, on purpose: it needs no symbol table (--slim strips those), no
// object-format parsing, and works on a binary for another platform.
static int exeInfo(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Cannot read " << path << "\n"; return 5; }
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string marker = slimMarker();
    size_t at = bytes.find(marker);
    if (at == std::string::npos) {
        std::cerr << path << ": no build manifest — not a rakupp-compiled "
                  "executable (or one from before v3.14)\n";
        return 1;
    }
    size_t end = bytes.find_first_of(std::string("\0\n", 2), at);   // a .js carries it in a comment line
    std::cout << bytes.substr(at, end == std::string::npos ? std::string::npos : end - at)
              << "\n";
    return 0;
}

// Build the compile-and-link command for a generated source + the runtime
// archive, in the dialect of the chosen compiler. `opt` is the Unix-style
// optimization flag ("-O2", "-O0", …); it is translated for cl.
static std::string compileCmd(const std::string& cxx, const std::string& opt,
                              const std::string& inc, const std::string& in,
                              const std::vector<std::string>& libs, const std::string& out) {
    if (msvcStyle(cxx)) {
        std::string o = opt == "-O0" ? "/Od" : opt == "-O1" ? "/O1" : "/O2";
        // /MT: static CRT, matching the /MT-built runtime archive (mixing
        // /MD stub objects with an /MT library is a link error)
        std::string c = cxx + " /nologo /std:c++17 /EHsc /MT /w " + o;
        if (!inc.empty()) c += " /I " + shq(inc);
        c += " " + shq(in);
        // link.exe resolves symbols across libraries iteratively, so the
        // rt<->parse cycle needs no grouping here.
        for (const auto& l : libs) c += " " + shq(l);
        c += " /Fe:" + shq(out) + " ws2_32.lib";
        // 256 MiB main-thread stack: Windows defaults to 1 MB, which is under
        // the recursion guard's 2 MiB headroom reserve — the first guarded
        // call in a natively-compiled program threw X::Recursion immediately
        c += " /link /STACK:268435456";
        // --slim ≥ safe: /OPT:REF drops unreferenced COMDATs. It is link.exe's
        // default without /DEBUG, so this is mostly documentation-by-command —
        // but it stays explicit so the two toolchains read the same. Symbol
        // stripping has no MSVC arm: symbols live in a PDB we never emit.
        if (g_slim.deadStrip) c += " /OPT:REF";
#ifdef _WIN32
        c = msvcEnvPrefix() + c; // bootstrap vcvars when cl isn't in this shell
#endif
        return c;
    }
    std::string c = cxx + " -std=c++17 " + (opt.empty() ? "-O2" : opt) + " -w -pthread -Wl,-w";
    // --slim ≥ safe (the default): let the linker see section granularity in
    // the one TU we compile here, and drop what nothing references. The big
    // win is symbol stripping — the runtime archive's reachability is real
    // (SLIM-PLAN measured dead-strip alone at −2%, symbols at −16%) — but
    // dead-strip is free and it is what the later phases' stubs lean on.
    if (g_slim.deadStrip) c += " -ffunction-sections -fdata-sections";
    if (!inc.empty()) c += " -I " + shq(inc);
    c += " " + shq(in);
    // rt and parse reference each other (the runtime drives the Parser; the
    // Parser leans on runtime helpers), which a single-pass GNU ld only
    // resolves inside a group. ld64 iterates archives on its own and has no
    // --start-group at all, so macOS takes the plain list.
#ifdef __APPLE__
    for (const auto& l : libs) c += " " + shq(l);
#else
    c += " -Wl,--start-group";
    for (const auto& l : libs) c += " " + shq(l);
    c += " -Wl,--end-group";
#endif
    c += " -o " + shq(out);
#ifdef _WIN32
    c += " -lws2_32";                 // MinGW: the runtime's sockets need Winsock
    c += " -Wl,--stack,268435456";    // and the same 256 MiB main stack as MSVC
    if (g_slim.deadStrip) c += " -Wl,--gc-sections";
    if (g_slim.stripSyms) c += " -s"; // GNU ld on PE: strip at link
#elif defined(__APPLE__)
    // The generated main() runs on the process main thread, whose default 8 MiB
    // stack gives natively-compiled recursion a far smaller budget than the
    // interpreter's 1 GiB big-stack thread. 512 MiB is the arm64 ld cap; the
    // recursion guard reads it via pthread_get_stacksize_np automatically.
    c += " -Wl,-stack_size,0x20000000";
    if (g_slim.deadStrip) c += " -Wl,-dead_strip";
    // ld64's -x keeps local symbols out of the output — the same table
    // `strip -x` would remove, without a second process.
    if (g_slim.stripSyms) c += " -Wl,-x";
#else
    if (g_slim.deadStrip) c += " -Wl,--gc-sections";
    if (g_slim.stripSyms) c += " -Wl,-s"; // ELF: no symbol table in the output
#endif
    return c;
}

// On Windows the produced binary must carry .exe (cl's /Fe would add it anyway,
// leaving our messages and default-output logic out of sync).
static void ensureExeSuffix(std::string& outPath) {
#ifdef _WIN32
    if (outPath.size() < 4 || outPath.compare(outPath.size() - 4, 4, ".exe") != 0)
        outPath += ".exe";
#endif
    (void)outPath;
}

// An output path naming an existing directory (say `-o t` beside the t/ test
// tree) can never take the binary; caught here it earns a plain message where
// the linker would only say "Is a directory".
static bool outIsDirectory(const std::string& outPath) {
    struct stat st;
    return stat(outPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Emit a C++ string literal for `s` (used for the embedded program name).
static std::string cppstr(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') { out += '\\'; out += (char)c; }
        else if (c == '\n') out += "\\n";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\x%02x", c); out += b; }
        else out += (char)c;
    }
    out += "\"";
    return out;
}

static std::string dirOf(const std::string& path) {
    auto p = path.find_last_of("/\\"); // Windows binaries report backslashed paths
    return p == std::string::npos ? "." : path.substr(0, p);
}
static std::string baseOf(const std::string& path) {
    auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// The absolute path of *this* rakupp binary. argv[0] is unreliable — when rakupp
// is on $PATH it is just "rakupp", so the compile modes couldn't find their
// runtime library. Resolve the real executable: OS-specific first, then argv[0]
// (as a path, or searched on $PATH), so `--exe` works from any directory.
static std::string selfExePath(const char* argv0) {
    char buf[4096], rp[4096];
#if defined(_WIN32)
    wchar_t wbuf[4096];
    DWORD wn = ::GetModuleFileNameW(nullptr, wbuf, 4096); // wide: survives non-ANSI install paths
    if (wn > 0 && wn < 4096) return narrow(wbuf);
    (void)buf;
#elif defined(__APPLE__)
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0 && realpath(buf, rp)) return rp;
#else
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return buf; }
#endif
    if (argv0 && *argv0) {
        if (std::strchr(argv0, '/')) { if (realpath(argv0, rp)) return rp; return argv0; }
        if (const char* path = std::getenv("PATH")) {           // bare name: search $PATH
            std::string p(path);
            for (size_t s = 0; s <= p.size(); ) {
                size_t e = p.find(':', s);
                std::string d = p.substr(s, e == std::string::npos ? std::string::npos : e - s);
                if (!d.empty()) {
                    std::string cand = d + "/" + argv0;
                    if (::access(cand.c_str(), X_OK) == 0) { if (realpath(cand.c_str(), rp)) return rp; return cand; }
                }
                if (e == std::string::npos) break;
                s = e + 1;
            }
        }
    }
    return argv0 && *argv0 ? argv0 : "rakupp";
}

// Locate the runtime static library and its headers relative to the binary,
// trying: $RAKUPP_HOME, the build tree (lib beside the binary, headers in
// ../src), and an installed prefix (bin/ + lib/ + include/rakupp). Returns the
// first that has the library; `lib` is set to the best guess for error messages.
static bool findRuntime(const std::string& selfExe, std::string& lib, std::string& inc) {
    std::string d = dirOf(selfExe);
    std::vector<std::pair<std::string, std::string>> dirs;
    if (const char* home = std::getenv("RAKUPP_HOME"))
        dirs.push_back({std::string(home) + "/lib", std::string(home) + "/include/rakupp"});
    dirs.push_back({d, d + "/../src"});                    // build tree (MSVC: Release/ beside the exe)
    dirs.push_back({d + "/../lib", d + "/../include/rakupp"}); // installed prefix (bin/ + lib/)
    dirs.push_back({d + "/lib", d + "/include/rakupp"});   // exe at the package ROOT, lib/ beside it
    // the archive is librakupp_rt.a (Unix toolchains) or rakupp_rt.lib (MSVC) —
    // accept whichever is present (github issue #1)
    for (auto& c : dirs)
        for (const char* nm : {"/librakupp_rt.a", "/rakupp_rt.lib"})
            if (fileExists(c.first + nm)) {
                lib = c.first + nm;
                // validate the header guess — multi-config generators (MSVC)
                // put the exe in build\Release\, one level deeper than the
                // single-config layouts the pairs above assume
                inc = c.second;
                if (!fileExists(inc + "/Interpreter.h"))
                    for (const std::string cand : {d + "/../../src", d + "/../include/rakupp",
                                                   d + "/include/rakupp", d + "/../src"})
                        if (fileExists(cand + "/Interpreter.h")) { inc = cand; break; }
                return true;
            }
    // not found: report EVERY probed path, so a failure diagnoses itself
    lib.clear();
    for (auto& c : dirs)
        for (const char* nm : {"/librakupp_rt.a", "/rakupp_rt.lib"})
            lib += (lib.empty() ? "" : "\n                 ") + c.first + nm;
    return false;
}

// The runtime is a SET of archives since the SLIM split (SLIM-PLAN P2): the
// core plus the four feature groups. `rtLib` is the core archive findRuntime()
// located; the rest ship beside it with the same prefix/extension family.
// On a missing member this fails with the member's expected path, in
// findRuntime()'s own error style, so a half-copied installation diagnoses
// itself rather than surfacing as an undefined ucd:: symbol from the linker.
//
// P3: a feature cut by --slim drops its real archive from the line, and the
// stub archive is appended once, LAST — every linker we drive resolves in
// scan order (ld64 and link.exe lazily, GNU ld within the group), so a real
// archive always beats its stub, and only the cut features' symbols fall
// through to the throwing doubles. Only what THIS link needs is checked: a
// missing archive the link would not touch is a fact, not an error.
static bool findRuntimeSet(const std::string& rtLib, std::vector<std::string>& libs,
                           std::string& missing) {
    libs.clear();
    bool msvcFamily = rtLib.size() >= 4 &&
                      rtLib.compare(rtLib.size() - 4, 4, ".lib") == 0;
    std::string dir = dirOf(rtLib);
    auto member = [&](const std::string& f) {
        return dir + (msvcFamily ? "/rakupp_" + f + ".lib" : "/librakupp_" + f + ".a");
    };
    libs.push_back(rtLib);
    bool anyCut = false;
    for (int i = 0; i < 4; i++) {
        if (g_slim.cut[i]) { anyCut = true; continue; }
        std::string p = member(kSlimArchives[i]);
        if (!fileExists(p)) { missing = p; return false; }
        libs.push_back(p);
    }
    if (anyCut) {
        std::string p = member("stubs");
        if (!fileExists(p)) { missing = p; return false; }
        libs.push_back(p);
    }
    return true;
}

// Default output path for a compiled program: the source name minus a Raku
// extension, or `a.out` for `-e` code.
static std::string defaultOut(const std::string& srcName) {
    if (srcName == "-e") return "a.out";
    for (const char* ext : {".rakumod", ".raku", ".p6", ".pl6"}) {
        size_t n = std::string(ext).size();
        if (srcName.size() > n && srcName.compare(srcName.size() - n, n, ext) == 0)
            return srcName.substr(0, srcName.size() - n);
    }
    return srcName + ".out";
}

// Bundle a Raku program into a standalone native executable: generate a
// small C++ stub that embeds the program source and calls the runtime, then
// link it against librakupp_rt.a (statically, so the result needs no rakupp).
static int compileToExe(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe,
                        const std::vector<std::string>& libPaths = {}) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);
    if (outIsDirectory(outPath)) { std::cerr << "Cannot write " << outPath << ": is a directory\n"; return 5; }

    // A bundled binary embeds its SOURCE and parses it at run time — cut the
    // parser and its first act would be X::Feature::NotBuilt. Native and AOT
    // binaries carry the program pre-compiled, so `-eval` is fine THERE; this
    // is the one mode the cut contradicts outright, and it is refused loudly
    // (SLIM-PLAN's rule) instead of producing a binary that cannot run.
    // Reached directly via --bundle, or as the fallback when a program is
    // outside the natively-compilable subset.
    if (g_slim.cut[kSlimEval]) {
        std::cerr << "--slim=-eval is incompatible with bundling: a bundled binary parses its\n"
                     "embedded source at run time, which IS the eval feature. Natively compiled\n"
                     "programs (--exe, when the program stays in the compilable subset) can cut\n"
                     "eval; this one cannot. Drop -eval, or drop --bundle.\n";
        return 5;
    }
    // The scan never applies here: a bundled binary embeds SOURCE and parses
    // it at run time, so nothing can be proven unused. Loud, per SLIM-PLAN
    // trigger 5 — whether --bundle was asked for or codegen fell back to it.
    if (slimScans())
        std::cerr << "--slim: a bundled binary parses its source at run time — nothing can be "
                     "proven unused; keeping every feature (explicit -feature still applies).\n";

    // The runtime static library sits next to this rakupp binary.
    std::string lib, inc;
    if (!findRuntime(selfExe, lib, inc)) {
        std::cerr << "Cannot find runtime library. Looked for:\n                 " << lib << "\n"
                  << "(build rakupp first: cmake --build build; or set RAKUPP_HOME)\n";
        return 5;
    }

    // Generate the stub. The program source is embedded as a raw byte array so
    // that any content (quotes, delimiters, binary) round-trips exactly.
    std::string stubPath = outPath + ".rakupp.stub.cpp";
    {
        std::ofstream stub = openOut(stubPath);
        if (!stub) { std::cerr << "Cannot write " << stubPath << "\n"; return 5; }
        stub << "// Generated by `rakupp --bundle`. Embeds a Raku program and runs it\n"
                "// via the linked-in Raku++ runtime.\n"
                "#include <string>\n#include <vector>\n#include <cstdlib>\n"
                "#ifdef _WIN32\n"
                "#define RAKUPP_REALPATH(p, r) _fullpath((r), (p), 4096)\n"
                "#else\n"
                "#define RAKUPP_REALPATH(p, r) realpath((p), (r))\n"
                "#endif\n"
                "namespace rakupp { int rakuppRunBigStack(const std::string&, std::vector<std::string>,"
                " const std::string&, const std::string&, const std::vector<std::string>&); void setConsoleUtf8();"
                " int rakuppRefuseInterpreterEval(int, char**); }\n";
        stub << "static const unsigned char SRC[] = {";
        for (size_t i = 0; i < src.size(); i++) { if (i) stub << ","; stub << (int)(unsigned char)src[i]; }
        if (src.empty()) stub << "0"; // avoid zero-size array; length tracked separately
        stub << "};\n";
        stub << "static const unsigned long SRC_LEN = " << src.size() << "UL;\n";
        // …and the modules it uses, as parsed ASTs, so a bundled binary is as
        // self-sufficient as an --exe or --aot one. Bundling only the mainline
        // left it needing the module tree at run time, which defeats the point of
        // a single-file deliverable. A program whose source will not even parse
        // still bundles (that is this mode's job — it parses at run time), and
        // then simply carries no modules.
        std::string bundleModuleCalls; // filled while emitting the stub
        {
            std::vector<BundledModule> mods;
            try {
                Lexer lx(src);
                Parser ps(lx.tokenize());
                ps.libPaths_ = effectiveSearchPath(libPaths); // find a `use`d module's operators
                Program pr = ps.parseProgram();
                std::vector<ModuleSkip> skips;
                std::set<std::string> natives;
                mods = collectModuleGraph(pr, effectiveSearchPath(libPaths), nullptr,
                                          &skips, &natives);
                if (!reportModuleEmbedding("--bundle", mods, skips, natives)) return 4;
            } catch (const ParseError&) {}
            std::ostringstream decls, calls;
            emitModuleTable(mods, decls, calls, /*withSources=*/true);
            stub << decls.str();
            bundleModuleCalls = calls.str();
        }
        stub << "namespace rakupp { void rakuppRegisterModule(const std::string&, const char*, unsigned long, const std::string&);\n"
                "                  void rakuppRegisterModuleSource(const std::string&, const char*, unsigned long); }\n";
        stub << "int main(int argc, char** argv) {\n"
             // a bundled binary embeds ONE program: `-e` has nothing to eval here
             << "  if (int rc = rakupp::rakuppRefuseInterpreterEval(argc, argv)) return rc;\n"
             << bundleModuleCalls
             << "  rakupp::setConsoleUtf8();\n"
                "  std::string src(reinterpret_cast<const char*>(SRC), SRC_LEN);\n"
                "  std::vector<std::string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);\n"
                "  std::string exe = argc > 0 ? argv[0] : \"program\";\n"
                "  char rp[4096]; if (RAKUPP_REALPATH(exe.c_str(), rp)) exe = rp;\n"
                "  return rakupp::rakuppRunBigStack(src, args, " << cppstr(baseOf(srcName)) << ", exe, {});\n"
                "}\n";
        stub << slimManifestTU("bundle");
    }

    std::vector<std::string> rtLibs; std::string missingLib;
    if (!findRuntimeSet(lib, rtLibs, missingLib)) {
        std::cerr << "Runtime archive set is incomplete. Missing:\n                 " << missingLib
                  << "\n(the archives ship together — rebuild rakupp: cmake --build build; or reinstall)\n";
        return 5;
    }
    std::string cmd = compileCmd(nativeCxx(lib), "-O2", "", stubPath, rtLibs, outPath);
    int rc = runCommand(cmd);
    removeFile(stubPath);
    if (rc != 0) {
        std::cerr << "Compilation failed (compiler exit " << rc << ")\n";
#ifdef _WIN32
        winCompilerHint(lib);
#endif
        return 5;
    }
    if (!g_quiet) std::cerr << "Compiled " << srcName << " -> " << outPath << "\n";
    return 0;
}

// Fully compile a Raku program to a native executable by transpiling its AST to
// C++ (no interpreter embedded) and linking against the runtime library. Falls
static std::string absPath(const std::string& p) {
    char rp[4096];
    if (!p.empty() && realpath(p.c_str(), rp)) return rp;
    return p;
}

// Splice the embedded-module table into codegen's output: the byte arrays go
// just above `int main`, the registration calls just inside it, so every module
// is in place before the program body starts. Codegen owns the shape of that
// function, so this keys on it rather than on a marker Codegen would have to
// remember to emit; if it ever stops looking like this, the modules are simply
// not embedded and the binary loads them from disk.
static std::string injectModuleTable(const std::string& cpp, const std::string& decls,
                                     const std::string& calls) {
    const std::string mainSig = "int main(int argc, char** argv) {";
    auto at = cpp.find(mainSig);
    if (at == std::string::npos) return cpp;
    return cpp.substr(0, at) + decls + "\n" + mainSig + "\n" + calls +
           cpp.substr(at + mainSig.size());
}

// The undeclared-variable gate (DeclCheck.h) for the modes that parse the whole
// unit before producing anything: --cpp and the three compile modes. `--exe`
// used to hand this straight to the C++ compiler, which reported it as
// `use of undeclared identifier 'v_sy'` against generated code the author never
// wrote (issue #32). Answers -1 when the program may proceed; a parse error is
// left for the caller, which reports it in its own shape.
static int declCheckGate(const std::string& src, const std::string& fileName,
                         const std::vector<std::string>& searchPath) {
    if (!declCheckEnabled()) return -1;
    Program prog;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        parser.libPaths_ = searchPath;
        parser.srcFile_ = fileName;
        prog = parser.parseProgram();
    } catch (const ParseError&) { return -1; }
    auto us = findUndeclaredVars(prog, src, searchPath);
    return us.empty() ? -1 : reportUndeclaredVars(us, fileName, src);
}

// ---- --target=js ------------------------------------------------------------
static bool g_jsVerify = false;
static bool g_jsRuntimeOnly = false;
static std::string g_jsFallback;

static std::string jsHostCommand() {
    if (const char* h = std::getenv("RAKUPP_JS")) if (*h) return h;
    for (const char* c : { "node", "bun", "deno" }) {
        std::string probe = std::string("command -v ") + c + " >/dev/null 2>&1";
#ifdef _WIN32
        probe = std::string("where ") + c + " >NUL 2>&1";
#endif
        if (runCommand(probe) == 0) return c == std::string("deno") ? "deno run -A" : c;
    }
    return "";
}

// Run the program under the interpreter and under the JS host; true when
// stdout, stderr and the exit status agree byte for byte (the --slim=verify
// protocol). `jsPath` is the emitted program; it must be runnable as is.
static bool jsVerify(const std::string& src, const std::string& srcName, const std::string& jsPath,
                     const std::string& selfExe, const std::vector<std::string>& libPaths, std::string& why) {
    std::string host = jsHostCommand();
    if (host.empty()) { why = "no JavaScript host found (set RAKUPP_JS, or put node/bun on PATH)"; return false; }
    std::string base = jsPath + ".verify";
    std::string rakuFile = srcName == "-e" ? base + ".raku" : srcName;
    if (srcName == "-e") { std::ofstream f = openOut(rakuFile); f << src; }
    std::string io = base + ".i-out", ie = base + ".i-err", jo = base + ".j-out", je = base + ".j-err";
    std::string incs; for (auto& p : libPaths) incs += " -I " + shq(p);
    int xi = runCommand(shq(selfExe) + incs + " " + shq(rakuFile) + " < /dev/null > " + shq(io) + " 2> " + shq(ie));
    int xj = runCommand(host + " " + shq(jsPath) + " < /dev/null > " + shq(jo) + " 2> " + shq(je));
    auto slurp = [](const std::string& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
    std::string so = slurp(io), sj = slurp(jo), eo = slurp(ie), ej = slurp(je);
#ifndef _WIN32
    if (xi > 255) xi >>= 8;
    if (xj > 255) xj >>= 8;
#endif
    bool agree = xi == xj && so == sj && eo == ej;
    if (!agree) {
        why = "exit " + std::to_string(xi) + " vs " + std::to_string(xj);
        if (so != sj) why += "; stdout differs (" + std::to_string(so.size()) + " vs " + std::to_string(sj.size()) + " bytes)";
        if (eo != ej) why += "; stderr differs (" + std::to_string(eo.size()) + " vs " + std::to_string(ej.size()) + " bytes)";
        if (!ej.empty() && eo != ej) why += "\n  js stderr: " + ej.substr(0, 300);
    }
    for (auto& p : { io, ie, jo, je }) removeFile(p);
    if (srcName == "-e") removeFile(rakuFile);
    return agree;
}

// The JS backend's driver: parse, transpile (or wrap for the WASM tier), verify
// when asked, then write the program — to stdout without -o, else to OUT.js plus
// the runtime sidecar rakupp-rt.js (unless --standalone inlined it).
static int compileJs(const std::string& src, const std::string& srcName, std::string outPath,
                     const std::string& selfExe, const std::vector<std::string>& libPaths) {
    if (!outPath.empty() && outIsDirectory(outPath)) { std::cerr << "Cannot write " << outPath << ": is a directory\n"; return 5; }
    if (int rc = declCheckGate(src, srcName, effectiveSearchPath(libPaths)); rc >= 0) return rc;
    JsOptions jo;
    jo.srcPath = srcName == "-e" ? "-e" : absPath(srcName);
    jo.srcText = src;
    jo.version = RAKUPP_VERSION;
    jo.standalone = g_standalone;
    std::string js;
    bool wasm = false;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        parser.libPaths_ = effectiveSearchPath(libPaths);
        parser.srcFile_ = srcName;
        Program prog = parser.parseProgram();
        std::vector<ModuleSkip> skips; std::set<std::string> natives;
        auto mods = collectModuleGraph(prog, effectiveSearchPath(libPaths), &jo.moduleExports, &skips, &natives);
        for (auto& m : mods) if (m.name != "JS") throw CodegenError{"a `use`d module (" + m.name + ")"};   // JS is the interop stub the emitter knows
        js = transpileToJs(prog, jo);
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        return 2;
    } catch (const CodegenError& e) {
        if (g_jsFallback != "wasm") {
            std::cerr << "note: " << e.msg << " — outside the JavaScript core; --fallback=wasm runs it on the WebAssembly engine instead\n";
            return 5;
        }
        if (!g_quiet) std::cerr << "note: " << e.msg << " — outside the JavaScript core; emitting the WebAssembly-engine wrapper\n";
        js = jsWasmWrapper(src, jo);
        wasm = true;
    }
    if (g_jsVerify) {
        // verify runs a file: the standalone form, which every host runs as a plain
        // script (an ES module needs the host's module detection or a package.json)
        std::string vpath = (outPath.empty() ? std::string(".rakupp-verify-") + std::to_string((long long)getpid()) : outPath) + ".verify.js";
        std::string runnable = js;
        if (!wasm && !g_standalone) { JsOptions so = jo; so.standalone = true;
            Lexer lexer(src); Parser parser(lexer.tokenize()); parser.libPaths_ = effectiveSearchPath(libPaths); parser.srcFile_ = srcName;
            Program prog = parser.parseProgram(); runnable = transpileToJs(prog, so); }
        { std::ofstream f = openOut(vpath); if (!f) { std::cerr << "Cannot write " << vpath << "\n"; return 5; } f << runnable; }
        std::string why;
        bool ok = jsVerify(src, srcName, vpath, selfExe, libPaths, why);
        removeFile(vpath);
        if (!ok) {
            std::cerr << "--verify: the interpreter and the JavaScript program DISAGREE — nothing emitted.\n(" << why << "; if the program is nondeterministic, verify cannot judge it.)\n";
            return 6;
        }
        if (!g_quiet) std::cerr << "verified: interpreter and JavaScript agree" << (outPath.empty() ? "" : " — emitting " + outPath) << "\n";
    }
    if (outPath.empty()) { std::cout << js; return 0; }
    { std::ofstream f = openOut(outPath); if (!f) { std::cerr << "Cannot write " << outPath << "\n"; return 5; } f << js; }
    if (!wasm && !g_standalone) {
        std::string dir = outPath; size_t sl = dir.find_last_of("/\\"); dir = sl == std::string::npos ? "" : dir.substr(0, sl + 1);
        std::string rtPath = dir + "rakupp-rt.js";
        std::ofstream f = openOut(rtPath); if (!f) { std::cerr << "Cannot write " << rtPath << "\n"; return 5; }
        f << jsRuntimeModule();
    }
    if (!g_quiet) std::cerr << "Compiled (js" << (wasm ? ", wasm wrapper" : "") << ") " << srcName << " -> " << outPath << "\n";
    return 0;
}

// back with a clear message if the program uses an unsupported construct.
static int compileNative(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe, bool optimize = false, const std::string& ccOpt = "-O2",
                         const std::vector<std::string>& libPaths = {}) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);
    if (outIsDirectory(outPath)) { std::cerr << "Cannot write " << outPath << ": is a directory\n"; return 5; }

    std::string cpp;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        parser.libPaths_ = effectiveSearchPath(libPaths); // find a `use`d module's operators
        parser.srcFile_ = srcName;
        Program prog = parser.parseProgram();
        // The program is compiled; its MODULES ride along as parsed ASTs, so the
        // binary is self-sufficient. (Natively compiling module code is a separate
        // step — most module files declare a package, which codegen does not take.)
        // The graph is walked BEFORE transpiling because codegen needs the names
        // those modules export: it resolves calls by name, so an exported sub that
        // collides with a built-in has to be known to reach it at all.
        std::set<std::string> moduleExports;
        std::vector<ModuleSkip> skips;
        std::set<std::string> natives;
        auto mods = collectModuleGraph(prog, effectiveSearchPath(libPaths), &moduleExports,
                                       &skips, &natives);
        if (!reportModuleEmbedding("--exe", mods, skips, natives)) return 4;
        cpp = transpileToCpp(prog, optimize, absPath(srcName), moduleExports, src);
        if (!mods.empty()) {
            std::ostringstream decls, calls;
            emitModuleTable(mods, decls, calls);
            cpp = injectModuleTable(cpp, decls.str(), calls.str());
        }
        // The scan (SLIM-PLAN P4), over the same Program and the same module
        // graph codegen just compiled — and only AFTER transpile succeeded:
        // the CodegenError fallback above bundles an interpreter, where
        // nothing can be proven unused (compileToExe says so itself).
        if (slimScans()) applySlimScan(slimScan(prog, mods));
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        return 2;
    } catch (const CodegenError& e) {
        // Any construct outside the native subset: fall back to AOT bundling so
        // `--exe` still produces a correct binary for the full language.
        std::cerr << "note: " << e.msg << " — not yet natively compiled; "
                     "bundling the whole program with the interpreter instead.\n";
        return compileToExe(src, srcName, outPath, selfExe, libPaths); // keep -I modules bundled
    }

    std::string lib, inc;
    if (!findRuntime(selfExe, lib, inc)) {
        std::cerr << "Cannot find runtime library. Looked for:\n                 " << lib << "\n"
                  << "(build rakupp first: cmake --build build; or set RAKUPP_HOME)\n";
        return 5;
    }

    std::string genPath = outPath + ".rakupp.gen.cpp";
    cpp += slimManifestTU("native");
    { std::ofstream g = openOut(genPath); if (!g) { std::cerr << "Cannot write " << genPath << "\n"; return 5; } g << cpp; }

    std::vector<std::string> rtLibs; std::string missingLib;
    if (!findRuntimeSet(lib, rtLibs, missingLib)) {
        std::cerr << "Runtime archive set is incomplete. Missing:\n                 " << missingLib
                  << "\n(the archives ship together — rebuild rakupp: cmake --build build; or reinstall)\n";
        return 5;
    }
    std::string cmd = compileCmd(nativeCxx(lib), ccOpt, inc, genPath, rtLibs, outPath);
    int rc = runCommand(cmd);
    if (!std::getenv("RAKUPP_KEEPGEN")) removeFile(genPath);
    if (rc != 0) {
        std::cerr << "Compilation failed (compiler exit " << rc << ")\n";
#ifdef _WIN32
        winCompilerHint(lib);
#endif
        return 5;
    }
    if (!g_quiet) std::cerr << "Compiled (native) " << srcName << " -> " << outPath << "\n";
    return 0;
}

// Real AOT: parse ahead of time, then emit C++ that rebuilds the AST at startup
// and interprets it (no lexing/parsing in the produced binary). Falls back to
// source bundling for any construct the AST emitter can't reconstruct.
static int compileAotAst(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe,
                         const std::vector<std::string>& libPaths = {}) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);
    if (outIsDirectory(outPath)) { std::cerr << "Cannot write " << outPath << ": is a directory\n"; return 5; }
    // The scan is `--exe`-only until SLIM-PLAN P7 (an AOT binary interprets a
    // rebuilt AST; the same scan APPLIES in principle, it just is not wired).
    // Loud rather than quietly weaker; explicit ±feature still works here.
    if (slimScans())
        std::cerr << "--slim: the feature scan covers --exe only (SLIM-PLAN P7); --aot keeps "
                     "every feature (explicit ±feature still applies).\n";
    std::string cpp, finish;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        parser.libPaths_ = effectiveSearchPath(libPaths); // find a `use`d module's operators
        parser.srcFile_ = srcName;
        finish = lexer.finishData();
        Program prog = parser.parseProgram();
        // Everything the program `use`s, resolved and parsed NOW, so the binary
        // carries it. Anything unresolvable is simply absent and falls back to a
        // run-time load — see collectModuleGraph.
        std::vector<ModuleSkip> skips;
        std::set<std::string> natives;
        auto mods = collectModuleGraph(prog, effectiveSearchPath(libPaths), nullptr,
                                       &skips, &natives);
        if (!reportModuleEmbedding("--aot", mods, skips, natives)) return 4;
        std::ostringstream ss;
        emitAstProgram(prog, ss, baseOf(srcName), finish, mods);
        cpp = ss.str();
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        return 2;
    } catch (const AstEmitError& e) {
        std::cerr << "note: " << e.msg << " — falling back to source bundling.\n";
        return compileToExe(src, srcName, outPath, selfExe, libPaths); // keep -I modules bundled
    }

    std::string lib, inc;
    if (!findRuntime(selfExe, lib, inc)) {
        std::cerr << "Cannot find runtime library. Looked for:\n                 " << lib << "\n"
                  << "(build rakupp first: cmake --build build; or set RAKUPP_HOME)\n";
        return 5;
    }
    std::string genPath = outPath + ".rakupp.ast.cpp";
    cpp += slimManifestTU("aot");
    { std::ofstream g = openOut(genPath); if (!g) { std::cerr << "Cannot write " << genPath << "\n"; return 5; } g << cpp; }
    std::vector<std::string> rtLibs; std::string missingLib;
    if (!findRuntimeSet(lib, rtLibs, missingLib)) {
        std::cerr << "Runtime archive set is incomplete. Missing:\n                 " << missingLib
                  << "\n(the archives ship together — rebuild rakupp: cmake --build build; or reinstall)\n";
        return 5;
    }
    std::string cmd = compileCmd(nativeCxx(lib), "-O2", inc, genPath, rtLibs, outPath);
    int rc = runCommand(cmd);
    if (!std::getenv("RAKUPP_KEEPGEN")) removeFile(genPath);
    if (rc != 0) {
        std::cerr << "Compilation failed (compiler exit " << rc << ")\n";
#ifdef _WIN32
        winCompilerHint(lib);
#endif
        return 5;
    }
    if (!g_quiet) std::cerr << "Compiled (AOT) " << srcName << " -> " << outPath << "\n";
    return 0;
}

// ---- the --slim directives (SLIM-PLAN P5) ---------------------------------

static long long slimFileBytes(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    return f ? (long long)f.tellg() : -1;
}
static std::string slimHuman(long long b) {
    if (b < 0) return "?";
    char buf[32];
    if (b >= 1024 * 1024) snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    else                  snprintf(buf, sizeof buf, "%lld KB", b / 1024);
    return buf;
}
// The four feature archives' on-disk sizes — the honest proxy for what a cut
// saves (the tables dominate; the exact in-binary delta depends on the link).
static void slimArchiveBytes(const std::string& selfExe, long long out[4]) {
    std::string lib, inc;
    for (int i = 0; i < 4; i++) out[i] = -1;
    if (!findRuntime(selfExe, lib, inc)) return;
    bool msvcFamily = lib.size() >= 4 && lib.compare(lib.size() - 4, 4, ".lib") == 0;
    std::string dir = dirOf(lib);
    for (int i = 0; i < 4; i++)
        out[i] = slimFileBytes(dir + (msvcFamily ? "/rakupp_" + std::string(kSlimArchives[i]) + ".lib"
                                                 : "/librakupp_" + std::string(kSlimArchives[i]) + ".a"));
}

// --slim=help: the key documents itself — grammar, feature table with the
// REAL archive sizes beside this rakupp, directives. Needs no source file.
static int slimHelp(const std::string& selfExe) {
    long long b[4];
    slimArchiveBytes(selfExe, b);
    std::cout <<
        "--slim[=SPEC] — how much of itself a compiled binary keeps (docs/guide/CLI.md)\n"
        "\n"
        "SPEC is comma-separated: one LEVEL, ±symbols, ±FEATURE, or a DIRECTIVE.\n"
        "\n"
        "levels (at most one; no --slim flag at all = safe):\n"
        "  none      nothing at all: no dead-strip, symbols kept (for debugging)\n"
        "  safe      dead-strip + strip symbols; no feature removed, no analysis\n"
        "  auto      what bare --slim means: safe, plus cut every feature the scan\n"
        "            PROVES unused; any dynamic construct (EVAL, ::($name), <$re>, …)\n"
        "            keeps everything, and stderr says which construct\n"
        "  max       auto, but ignoring those constructs — a wrong cut throws\n"
        "            X::Feature::NotBuilt at run time, never crashes or lies\n"
        "\n"
        "features (+keep / -cut, overriding the level; a named feature beats a group):\n";
    const char* what[4] = {"uniname/uniparse/unival",
                           "unicmp, coll, .collate (DUCET)",
                           "uniprop Script/Block/Bidi_Class",
                           "EVAL, require, regex code blocks"};
    for (int i = 0; i < 4; i++) {
        char line[128];
        snprintf(line, sizeof line, "  %-18s %-33s %s\n",
                 kSlimFeatures[i], what[i], slimHuman(b[i]).c_str());
        std::cout << line;
    }
    std::cout <<
        "  unicode            the three Unicode features as a group\n"
        "  all                all four\n"
        "  symbols            the symbol table (+symbols: readable crash reports)\n"
        "\n"
        "directives (one per SPEC; help stands alone):\n"
        "  help      this text\n"
        "  list      what this compile would keep and cut, and why (does not compile)\n"
        "  why:FEAT  every site that forces FEAT to be kept\n"
        "  verify    build slim AND full, run both, emit only if outputs agree\n"
        "\n"
        "Examples:\n"
        "  rakupp --exe prog.raku --slim                  # sound automatic pruning\n"
        "  rakupp --exe prog.raku --slim=max,+unicode     # smallest, Unicode intact\n"
        "  rakupp --exe prog.raku --slim=safe,-eval       # one deliberate cut, no scan\n"
        "  rakupp --exe prog.raku --slim=list             # what would happen, and why\n"
        "\n"
        "rakupp --exe-info BIN prints a compiled binary's embedded build manifest.\n";
    return 0;
}

// --slim=list / --slim=why:FEAT — analyse exactly what the compile would do
// (same parse, same module graph, same scan, same slimDecide) and stop.
static int slimExplain(const std::string& src, const std::string& srcName,
                       const std::string& selfExe, const std::vector<std::string>& libPaths,
                       const char* modeNote) {
    Program prog;
    std::vector<BundledModule> mods;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        parser.libPaths_ = effectiveSearchPath(libPaths);
        parser.srcFile_ = srcName;
        prog = parser.parseProgram();
        mods = collectModuleGraph(prog, effectiveSearchPath(libPaths));
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        return 2;
    }
    SlimScanResult sr = slimScan(prog, mods);
    SlimDecision d[4];
    slimDecide(sr, d);
    const char* lvl = g_slim.level == SlimLevel::None ? "none"
                    : g_slim.level == SlimLevel::Safe ? "safe"
                    : g_slim.level == SlimLevel::Auto ? "auto" : "max";

    if (g_slim.directive == SlimDirective::List) {
        long long b[4];
        slimArchiveBytes(selfExe, b);
        std::cout << "--slim=" << lvl << " for " << srcName << " (" << modeNote << "):\n";
        long long cutBytes = 0, cuttable = 0;
        for (int i = 0; i < 4; i++) {
            if (b[i] > 0) cuttable += b[i];
            if (d[i].cut && b[i] > 0) cutBytes += b[i];
            char line[160];
            snprintf(line, sizeof line, "  %-18s %-5s %8s   ",
                     kSlimFeatures[i], d[i].cut ? "cut" : "keep", slimHuman(b[i]).c_str());
            std::cout << line << d[i].why << "\n";
        }
        if (g_slim.level == SlimLevel::Auto && !sr.triggers.empty()) {
            std::cout << "dynamic constructs keeping everything (--slim=max cuts anyway):\n";
            for (const auto& t : sr.triggers) std::cout << "  " << t << "\n";
        }
        else if (g_slim.level == SlimLevel::Max && !sr.triggers.empty()) {
            std::cout << "dynamic constructs IGNORED by max (a wrong cut throws at run time):\n";
            for (const auto& t : sr.triggers) std::cout << "  " << t << "\n";
        }
        std::cout << "would cut " << slimHuman(cutBytes) << " of " << slimHuman(cuttable)
                  << " cuttable (archive sizes)\n";
        return 0;
    }

    // why:FEAT
    int fi = 0;
    for (int i = 0; i < 4; i++) if (g_slim.whyFeat == kSlimFeatures[i]) fi = i;
    std::cout << (d[fi].cut ? "cut" : "kept") << ": " << g_slim.whyFeat
              << " for " << srcName << " under --slim=" << lvl
              << " — " << d[fi].why << "\n";
    bool any = false;
    for (const auto& site : sr.sites) {
        if (site.feat != fi) continue;
        any = true;
        char line[160];
        snprintf(line, sizeof line, "  %-36s %s%s\n", site.what.c_str(),
                 site.where.empty() ? "the program" : ("module " + site.where).c_str(),
                 site.line ? (", line " + std::to_string(site.line)).c_str() : "");
        std::cout << line;
    }
    if (!any && !d[fi].cut && g_slim.level == SlimLevel::Auto && !sr.triggers.empty()) {
        std::cout << "  no direct use — a dynamic construct keeps every feature:\n";
        for (const auto& t : sr.triggers) std::cout << "    " << t << "\n";
    }
    else if (!any)
        std::cout << "  no use anywhere in the program or its modules\n";
    return 0;
}

// --slim=verify (SLIM-PLAN defence 7): build the slim binary AND a full
// reference (same strip settings, no cuts), run both bare, and emit the slim
// one only if stdout, stderr and exit status agree. A nondeterministic
// program cannot agree with anything — verify refuses it too, and says so.
static int slimVerify(char modeCh, const std::string& src, const std::string& srcName,
                      std::string outPath, const std::string& selfExe, bool optimize,
                      const std::string& ccOpt, const std::vector<std::string>& libPaths) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);
    if (outIsDirectory(outPath)) { std::cerr << "Cannot write " << outPath << ": is a directory\n"; return 5; }
    auto build = [&](const std::string& out) -> int {
        if (modeCh == 'x') return compileNative(src, srcName, out, selfExe, optimize, ccOpt, libPaths);
        if (modeCh == 'a') return compileAotAst(src, srcName, out, selfExe, libPaths);
        return compileToExe(src, srcName, out, selfExe, libPaths);
    };
    SlimSpec saved = g_slim;
    std::string fullBin = outPath + ".verify-full", slimBin = outPath + ".verify-slim";
    // the full reference: identical strip, no cuts, no scan
    g_slim = SlimSpec{};
    g_slim.deadStrip = saved.deadStrip;
    g_slim.stripSyms = saved.stripSyms;
    int rc = build(fullBin);
    if (rc == 0) {
        g_slim = saved;
        g_slim.directive = SlimDirective::None;
        rc = build(slimBin);
    }
    g_slim = saved;
    if (rc != 0) { removeFile(fullBin); removeFile(slimBin); return rc; }
    std::string of = outPath + ".vf-out", ef = outPath + ".vf-err";
    std::string os = outPath + ".vs-out", es = outPath + ".vs-err";
    int xf = runCommand(shq(fullBin) + " > " + shq(of) + " 2> " + shq(ef));
    int xs = runCommand(shq(slimBin) + " > " + shq(os) + " 2> " + shq(es));
    auto slurp = [](const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    bool agree = xf == xs && slurp(of) == slurp(os) && slurp(ef) == slurp(es);
    for (const auto& p : {of, ef, os, es}) removeFile(p);
    removeFile(fullBin);
    if (!agree) {
        removeFile(slimBin);
        // raw status compared above (finer: signals differ too); decoded here
        int df = xf, ds = xs;
#ifndef _WIN32
        if (df > 255) df >>= 8;
        if (ds > 255) ds >>= 8;
#endif
        std::cerr << "--slim=verify: the slim and full binaries DISAGREE — nothing emitted.\n"
                     "(exit " << df << " vs " << ds << "; run --slim=list to see the cuts. If the\n"
                     " program is nondeterministic, verify cannot judge it.)\n";
        return 6;
    }
    removeFile(outPath);                       // Windows rename() will not overwrite
    if (std::rename(slimBin.c_str(), outPath.c_str()) != 0) {
        std::cerr << "Cannot move " << slimBin << " to " << outPath << "\n";
        return 5;
    }
    if (!g_quiet) std::cerr << "verified: slim and full agree — emitted " << outPath << " (slim)\n";
    return 0;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    rakupp::setConsoleUtf8();  // Windows: render UTF-8 output instead of mojibake (no-op elsewhere)
    std::string exePath = selfExePath(argv[0]); // resolve the real binary (argv[0] may be a bare PATH name)
#ifndef _WIN32
    { char rp[4096]; if (realpath(exePath.c_str(), rp)) exePath = rp; }
#endif // Windows: GetModuleFileNameW is already absolute; _fullpath would ANSI-mangle the UTF-8

    // `rakupp install ...` — the module installer (MODULES-PLAN Part A): a
    // Raku program shipped BESIDE the binary, never inside it. Dispatch =
    // rewrite the command line to run that program; everything after
    // `install` is its arguments. Looked up relative to the real binary:
    // an installed layout's libexec/, or the checkout's tools/ from a build
    // directory.
    static std::vector<std::string> installArgs;
    static std::vector<char*> installArgv;
    // -q / --quiet may come before the command word (`rakupp -q install Foo`)
    // or anywhere after it (`rakupp install Foo -q`); either way it reaches
    // the installer as one leading --quiet and never as a module name. MAIN
    // would file a `-q` that follows a positional under the slurpy — Rakudo
    // does the same — so the lifting happens here, where the rest of the
    // option surface is position-independent too.
    auto isQuietTok = [](const char* a) {
        return std::strcmp(a, "-q") == 0 || std::strcmp(a, "--quiet") == 0;
    };
    int cmdAt = 1;
    while (cmdAt < argc && isQuietTok(argv[cmdAt])) cmdAt++;
    std::string cmdWord = cmdAt < argc ? argv[cmdAt] : "";
    bool isUninstall = cmdWord == "uninstall";
    bool isReinstall = cmdWord == "reinstall";
    bool isTestCmd   = cmdWord == "test";
    if (cmdWord == "install" || isUninstall || isReinstall || isTestCmd) {
        std::string exeDir = exePath.substr(0, exePath.find_last_of("/\\"));
        std::string script;
        for (const char* rel : {"/../libexec/rakupp/install.raku", "/../tools/install.raku"}) {
            std::string cand = exeDir + rel;
            if (std::ifstream(cand).good()) { script = cand; break; }
        }
        if (script.empty()) {
            std::cerr << "rakupp install: cannot find install.raku beside this binary\n"
                      << "  (expected in libexec/rakupp/ of an installed layout, or tools/ of a checkout)\n";
            return 4;
        }
        installArgs.push_back(argv[0]);
        installArgs.push_back(script);
        if (isUninstall) installArgs.push_back("--uninstall");
        if (isReinstall) installArgs.push_back("--reinstall");
        if (isTestCmd)   installArgs.push_back("--test-only");
        bool quietCmd = cmdAt > 1;
        std::vector<std::string> rest;
        for (int i = cmdAt + 1; i < argc; i++) {
            if (isQuietTok(argv[i])) { quietCmd = true; continue; }
            rest.push_back(argv[i]);
        }
        // --quiet goes BEFORE the module names: a named argument that follows a
        // positional is filed under MAIN's slurpy (measured: `Foo -q Bar` puts
        // -q in @modules, under this engine and under Rakudo alike)
        if (quietCmd) installArgs.push_back("--quiet");
        for (auto& r : rest) installArgs.push_back(r);
        for (auto& s : installArgs) installArgv.push_back(const_cast<char*>(s.c_str()));
        argv = installArgv.data();
        argc = (int)installArgv.size();
    }

    // A single-dash spelling of a known long option (`-exe`, `-cpp`, `-lint`, …)
    // is a common typo for the `--` form. These names are unambiguous — none is
    // a valid short-option cluster (`-exe` is NOT `-e xe`) — so accept them with
    // a gentle note. Real short options (-e, -c, -I, -o, -n, -p, -h, -V, -q)
    // are left exactly as they are.
    static std::string s_normArg1;
    if (argc >= 2 && argv[1][0] == '-' && argv[1][1] != '-' && argv[1][1] != '\0') {
        static const std::set<std::string> kLongNames = {
            "exe", "cpp", "emit-cpp", "bundle", "aot", "lint", "highlight",
            "ansi", "terminal", "ast", "dump-ast", "doc", "help", "version",
            "mcp", "lsp", "jupyter",
        };
        std::string bare = argv[1] + 1;
        if (kLongNames.count(bare)) {
            std::cerr << "note: treating '" << argv[1] << "' as '--" << bare << "'\n";
            s_normArg1 = "--" + bare;
            argv[1] = &s_normArg1[0];
        }
    }

    // ---- the option parser (v3 CLI plan, step 1) ---------------------------
    // One two-phase scan replaces the old argv[1]-only mode cascade:
    //   phase 1 (options) — flags in any order until the program token: a
    //     source FILE, `-e CODE`, or `-`. A bare `--` ends the phase early.
    //   phase 2 (program) — in run mode everything after the program token is
    //     the program's @*ARGS, completely untouched. Tool modes (--lint,
    //     --cpp, the compile modes, --highlight) keep scanning their own flags
    //     after the source, which is why `--exe src.raku -o out` works.
    // Mode flags are position-independent; combining two modes is an error.
    // Flags that only exist in one mode (-q, -o, -O, --html) are collected
    // wherever they appear and validated once the mode is known, so
    // `-o out --exe src` is as good as `--exe src -o out`.
    enum class Mode { Run, Help, Version, FfiInfo, Highlight, Ast, AstRoundtrip,
                      PrecompSetting, PrecompInfo, PrecompClean, Check, Lint,
                      Cpp, Bundle, Aot, Exe, Mcp, Lsp, Jupyter, JupyterInstall, Js };
    Mode mode = Mode::Run;
    std::string modeTok;                  // the spelling that selected the mode (for messages)
    std::vector<std::string> libPaths;    // -I, both spellings, any position
    std::vector<std::string> preloadModules; // -M/-m modules, in order
    std::vector<std::string> progArgs;    // run mode: the program's @*ARGS
    std::string src, fileName = "-e", outPath, ccOpt = "-O2";
    std::string hlFmt = "html", precompKey, precompVal;
    bool haveSrc = false, optionsDone = false;
    bool optN = false, optP = false;      // the perl line-loop family
    bool optA = false;                    // -a: autosplit into @F (implies -n)
    bool optL = false;                    // -l: accepted no-op (lines() already
                                          // chomps and -p prints with .say)
    bool optI = false;                    // -i[.ext()]: edit the argument files in place
    std::string backupExt;                // -i.bak — glued only, as in perl
    std::string profileDest;              // --profile[-=FILE]: "-" = stderr table
    std::string fieldSep;                 // -F: separator (implies -a)
    bool haveF = false, fieldSepRegex = false;
    long recMode = -1;                    // -0[octal]: 0 = NUL records, 0777 = slurp
    bool quiet = false;                   // -q / --quiet: any mode (see g_quiet)
    bool optimize = false;                // -O (compile modes and --cpp)
    bool sawHtml = false;                 // --html is only legal under --highlight
    long mcpTimeout = -1;                 // --timeout=SECS, only legal under --mcp
    std::string jupyterConn;              // --jupyter FILE: Jupyter's connection file
    std::string jupyterName = "raku";     // --jupyter-install --name=NAME
    std::string jupyterPrefix;            // --jupyter-install --prefix=DIR
    bool sawJupyterOpt = false;           // --name/--prefix seen (only --jupyter-install wants them)

    // Rakudo prints this banner for an unknown option and exits 0; we stay
    // bug-compatible (the exit code included — it is pinned by the suite).
    auto illegalOpt = [&](const std::string& a) -> int {
        std::cerr << "Illegal option " << a.substr(0, a.find('=')) << "\n";
        std::cerr << "  [switches] [--] [programfile] [arguments]\n";
        return 0;
    };
    auto setMode = [&](Mode m, const std::string& tok) -> bool {
        if (mode != Mode::Run && mode != m) {
            std::cerr << "Cannot combine " << modeTok << " with " << tok << "\n";
            return false;
        }
        mode = m; modeTok = tok; return true;
    };
    auto isCompileMode = [&](Mode m) {
        return m == Mode::Bundle || m == Mode::Aot || m == Mode::Exe;
    };

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        // phase 2: a running program's arguments are never ours to interpret
        if (haveSrc && mode == Mode::Run) { progArgs.push_back(a); continue; }
        if (!optionsDone && a == "--") { optionsDone = true; continue; }
        bool isOpt = !optionsDone && a.size() > 1 && a[0] == '-';
        if (isOpt) {
            // the information modes win outright, from any position
            if (a == "--help" || a == "-h")  { mode = Mode::Help; break; }
            if (a == "--version" || a == "-V" || a == "-v") { mode = Mode::Version; break; }
            if (a == "--ffi-info")           { mode = Mode::FfiInfo; break; }
            if (a == "--doc") { rakupp::rakuppSetDocMode(true); continue; }
            if (a == "-I") { if (i + 1 < argc) libPaths.push_back(argv[++i]); continue; }
            if (a.rfind("-I", 0) == 0 && a.size() > 2) { libPaths.push_back(a.substr(2)); continue; }
            // -M <module> loads a module before the program runs (Rakudo/Perl;
            // repeatable, glued -MName works too). -m is the Perl-muscle-memory
            // alias — Rakudo itself rejects -m, a deliberate borrow.
            if (a == "-M" || a == "-m") { if (i + 1 < argc) preloadModules.push_back(argv[++i]); continue; }
            if ((a.rfind("-M", 0) == 0 || a.rfind("-m", 0) == 0) && a.size() > 2) {
                preloadModules.push_back(a.substr(2)); continue;
            }
            // mode selectors
            if (a == "--highlight") { if (!setMode(Mode::Highlight, a)) return 4; continue; }
            if (a == "--mcp") { if (!setMode(Mode::Mcp, a)) return 4; continue; }
            if (a == "--lsp") { if (!setMode(Mode::Lsp, a)) return 4; continue; }
            // --jupyter FILE: Jupyter launches the kernel with the connection
            // file as its own argv token, so the flag EATS the next argument —
            // it is not the program to run, and the two-phase scan would take
            // it for one.
            if (a == "--jupyter-install") { if (!setMode(Mode::JupyterInstall, a)) return 4; continue; }
            if (a == "--jupyter" || a.rfind("--jupyter=", 0) == 0) {
                if (!setMode(Mode::Jupyter, "--jupyter")) return 4;
                if (a.size() > 9 && a[9] == '=') jupyterConn = a.substr(10);
                else if (i + 1 < argc) jupyterConn = argv[++i];
                continue;
            }
            if (a.rfind("--name=", 0) == 0) { jupyterName = a.substr(7); sawJupyterOpt = true; continue; }
            if (a.rfind("--prefix=", 0) == 0) { jupyterPrefix = a.substr(9); sawJupyterOpt = true; continue; }
            if (a == "--timeout" || a.rfind("--timeout=", 0) == 0) {
                std::string v = a.size() > 9 && a[9] == '=' ? a.substr(10)
                              : i + 1 < argc ? std::string(argv[++i]) : std::string();
                char* rest = nullptr;
                mcpTimeout = std::strtol(v.c_str(), &rest, 10);
                if (v.empty() || !rest || *rest || mcpTimeout < 0) {
                    std::cerr << "--timeout wants a whole number of seconds (0 = no limit)\n";
                    return 4;
                }
                continue;
            }
            if (a == "--ansi" || a == "--terminal") {
                // a bare --ansi implies --highlight (terminal output)
                if (mode != Mode::Run && mode != Mode::Highlight) return illegalOpt(a);
                if (!setMode(Mode::Highlight, "--highlight")) return 4;
                hlFmt = "ansi"; continue;
            }
            if (a == "--html") { sawHtml = true; hlFmt = "html"; continue; }
            if (a == "--ast" || a == "--dump-ast") { if (!setMode(Mode::Ast, "--ast")) return 4; continue; }
            if (a == "--ast-roundtrip") { if (!setMode(Mode::AstRoundtrip, a)) return 4; continue; }
            if (a.rfind("--precomp-modules=", 0) == 0 || a.rfind("--precomp-files=", 0) == 0) {
                if (!setMode(Mode::PrecompSetting, a.substr(0, a.find('=')))) return 4;
                auto eq = a.find('=');
                precompKey = a.substr(2, eq - 2); precompVal = a.substr(eq + 1);
                continue;
            }
            if (a == "--precomp-info")  { if (!setMode(Mode::PrecompInfo, a)) return 4; continue; }
            if (a == "--precomp-clean") { if (!setMode(Mode::PrecompClean, a)) return 4; continue; }
            if (a == "-c")     { if (!setMode(Mode::Check, a)) return 4; continue; }
            if (a == "--lint") { if (!setMode(Mode::Lint, a)) return 4; continue; }
            if (a == "--cpp" || a == "--emit-cpp") { if (!setMode(Mode::Cpp, "--cpp")) return 4; continue; }
            if (a == "--bundle") { if (!setMode(Mode::Bundle, a)) return 4; continue; }
            if (a == "--aot")    { if (!setMode(Mode::Aot, a)) return 4; continue; }
            if (a == "--exe")    { if (!setMode(Mode::Exe, a)) return 4; continue; }
            if (a.rfind("--target=", 0) == 0) {  // Rakudo muscle memory
                std::string t = a.substr(9);
                if (t == "parse") { if (!setMode(Mode::Check, a)) return 4; }
                else if (t == "ast") { if (!setMode(Mode::Ast, a)) return 4; }
                else if (t == "js") { if (!setMode(Mode::Js, a)) return 4; }
                else { std::cerr << "Unknown --target '" << t << "' (supported: parse, ast, js)\n"; return 4; }
                continue;
            }
            // --target=js companions (TRANSPILE-PLAN): --verify runs the program under
            // the interpreter and the JS host and emits only on agreement;
            // --fallback=wasm opts in to the WebAssembly-wrapper tier.
            if (a == "--verify") { g_jsVerify = true; continue; }
            if (a == "--runtime") { g_jsRuntimeOnly = true; continue; }
            if (a.rfind("--fallback=", 0) == 0) {
                std::string f = a.substr(11);
                if (f != "wasm") { std::cerr << "Unknown --fallback '" << f << "' (supported: wasm)\n"; return 4; }
                g_jsFallback = f; continue;
            }
            if (a == "--profile") { profileDest = "-"; continue; }
            if (a.rfind("--profile=", 0) == 0) {
                profileDest = a.substr(10);
                if (profileDest.empty()) { std::cerr << "Usage: rakupp --profile[=FILE] PROGRAM\n"; return 4; }
                continue;
            }
            if (a == "--quiet" || a == "-q") { quiet = g_quiet = true; continue; }
            if (a == "-o") { if (i + 1 < argc) outPath = argv[++i]; continue; }
            if (a.rfind("-o", 0) == 0 && a.size() > 2) { outPath = a.substr(2); continue; }
            // any -O… turns on the codegen optimizer; a suffix (-O3/-Os/…)
            // is forwarded to the C++ compiler for the generated binary.
            if (a.rfind("-O", 0) == 0) { optimize = true; if (a.size() > 2) ccOpt = a; continue; }
            // --exe-info FILE — print a compiled binary's embedded build
            // manifest and stop. A diagnostic mode: it takes the file
            // directly (the argument is a binary, not Raku source, so it
            // must not fall through to the program-token phase).
            if (a == "--exe-info") {
                if (i + 1 >= argc) { std::cerr << "Usage: rakupp --exe-info BINARY\n"; return 4; }
                return exeInfo(argv[i + 1]);
            }
            // --slim[=SPEC] — how much of itself a compiled binary keeps
            // (SLIM-PLAN). Level `safe` is the no-flag default; the SPEC's
            // errors name what exists, so a wrong ask teaches the grammar.
            if (a == "--slim" || a.rfind("--slim=", 0) == 0) {
                std::string err = parseSlimSpec(a.size() > 6 ? a.substr(7) : "");
                if (!err.empty()) { std::cerr << err << "\n"; return 4; }
                if (g_slim.directive == SlimDirective::Help) return slimHelp(exePath);
                g_slimExplicit = true; continue;
            }
            // --standalone (MODULES-PLAN B2): a module the compile modes
            // cannot embed becomes a BUILD ERROR instead of a silent
            // load-from-disk-at-run-time fallback.
            if (a == "--standalone") { g_standalone = true; continue; }
            if (a == "-e" || (a.rfind("-e", 0) == 0 && a.size() > 2)) {
                if (!haveSrc) {
                    if (a == "-e") {
                        if (i + 1 >= argc) { std::cerr << "No code given for -e\n"; return 4; }
                        src = argv[++i];
                    }
                    else { src = a.substr(2); } // attached form: -e"say 123"
                    haveSrc = true; fileName = "-e";
                }
                continue;
            }
            // -F<sep>: the autosplit separator (implies -a, which implies -n).
            // A literal string with \t-style escapes by default — perl regex
            // syntax is not Raku regex syntax, so the regex form is the
            // explicit /…/ spelling. Glued (perl's only form) or separate.
            if (a == "-F") { if (i + 1 < argc) { fieldSep = argv[++i]; haveF = true; } continue; }
            if (a.rfind("-F", 0) == 0 && a.size() > 2) { fieldSep = a.substr(2); haveF = true; continue; }
            // perl-style line-loop clusters: -n / -p / -a / -l / -0[octal],
            // combinable in one token (-lane, -0777pe); 'e' ENDS the cluster
            // with everything after it glued as the code (the shell strips
            // the quotes).
            if (mode == Mode::Run && !haveSrc) {
                size_t j = 1;
                bool sawN = false, sawP = false, sawE = false, sawA = false, sawL = false, ok = true;
                long saw0 = -1;
                while (j < a.size()) {
                    char c = a[j];
                    if (c == 'n') { sawN = true; j++; }
                    else if (c == 'p') { sawP = true; j++; }
                    else if (c == 'a') { sawA = true; j++; }
                    else if (c == 'l') { sawL = true; j++; }
                    else if (c == '0') { // -0[octal]: value in octal, like perl
                        j++; long v = 0;
                        while (j < a.size() && a[j] >= '0' && a[j] <= '7') { v = v * 8 + (a[j] - '0'); j++; }
                        saw0 = v;
                    }
                    else if (c == 'i') { // -i[.ext()]: the REST of the token is the
                        // backup extension, exactly as in perl — which is also
                        // perl's famous -pie trap: the 'e' becomes the extension
                        optI = true; backupExt = a.substr(j + 1); j = a.size();
                        break;
                    }
                    else if (c == 'e') { sawE = true; j++; break; }
                    else { ok = false; break; } // -nfoo is not a flag cluster at all
                }
                if (ok && (sawN || sawP || sawA || sawL || sawE || saw0 >= 0 || optI)) {
                    optN |= sawN; optP |= sawP; optA |= sawA; optL |= sawL;
                    if (saw0 >= 0) recMode = saw0;
                    if (sawE) {
                        if (j < a.size()) { src = a.substr(j); }
                        else {
                            if (i + 1 >= argc) { std::cerr << "No code given for -e\n"; return 4; }
                            src = argv[++i];
                        }
                        haveSrc = true; fileName = "-e";
                    }
                    continue;
                }
            }
            return illegalOpt(a);
        }
        // a non-option token: the source program (or the tool's input)
        if (a == "-" && !haveSrc) { // read it from stdin, explicitly
            std::ostringstream ss; ss << std::cin.rdbuf(); src = ss.str();
            haveSrc = true; fileName = "-";
            continue;
        }
        if (!haveSrc) {
            if (mode == Mode::Run) {
                struct stat st;
                if (stat(a.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    std::cerr << "Can not run directory " << a << "\n"; return 1;
                }
            }
            std::ifstream in(a);
            if (!in) {
                if (mode == Mode::Run) { std::cerr << "Could not open " << a << "\n"; return 1; }
                std::cerr << "Cannot open file: " << a << "\n"; return 4;
            }
            std::ostringstream ss; ss << in.rdbuf(); src = ss.str(); fileName = a;
            haveSrc = true;
            continue;
        }
        // tool modes ignore stray extra non-options (as their old scans did)
    }
    // flags collected above that the final mode has no use for are illegal —
    // same banner the flag would have earned in run mode all along
    if (mode != Mode::Help && mode != Mode::Version && mode != Mode::FfiInfo) {
        if (sawHtml && mode != Mode::Highlight) return illegalOpt("--html");
        // (-q is deliberately absent here: every mode takes it — see g_quiet)
        if (mcpTimeout >= 0 && mode != Mode::Mcp) return illegalOpt("--timeout");
        if (mode == Mode::Lsp && haveSrc) {
            // Same shape as --mcp: the server IS the program, stdio is the
            // protocol, and the editor sends documents over it — not argv.
            std::cerr << "--lsp serves diagnostics over stdio and takes no program\n";
            return 4;
        }
        if (mode == Mode::Mcp) {
            // The server IS the program; stdio is the protocol. A source file
            // or -e here is a misunderstanding worth a sentence, not a guess.
            if (haveSrc) {
                std::cerr << "--mcp serves the interpreter over stdio and takes no program\n";
                return 4;
            }
            if (!libPaths.empty()) {
                std::cerr << "--mcp: the embedded session reads RAKULIB=dir1,dir2 — use that instead of -I\n";
                return 4;
            }
        }
        if (sawJupyterOpt && mode != Mode::JupyterInstall) {
            return illegalOpt(jupyterPrefix.empty() ? "--name" : "--prefix");
        }
        if (mode == Mode::Jupyter) {
            // Same rule as --mcp: the kernel IS the program, and its input
            // arrives over five sockets rather than from a file.
            if (jupyterConn.empty()) {
                std::cerr << "--jupyter wants the connection file Jupyter passes as {connection_file}\n";
                return 4;
            }
            if (haveSrc) {
                std::cerr << "--jupyter serves a notebook frontend and takes no program\n";
                return 4;
            }
        }
        if (!outPath.empty() && !isCompileMode(mode) && mode != Mode::Js) return illegalOpt("-o");
        if ((g_jsVerify || g_jsRuntimeOnly || !g_jsFallback.empty()) && mode != Mode::Js) return illegalOpt(g_jsVerify ? "--verify" : g_jsRuntimeOnly ? "--runtime" : "--fallback");
        if (optimize && !isCompileMode(mode) && mode != Mode::Cpp) return illegalOpt("-O");
        // --slim shapes the LINK of a compiled binary, so it means nothing to
        // the interpreter or to --cpp (which emits source and never links).
        if (g_slimExplicit && !isCompileMode(mode)) return illegalOpt("--slim");
        if (g_standalone && !isCompileMode(mode) && mode != Mode::Js) return illegalOpt("--standalone");
        // -M applies where the program is checked, compiled or run; the pure
        // source tools see the file exactly as written
        if (!preloadModules.empty() &&
            (mode == Mode::Highlight || mode == Mode::Ast || mode == Mode::AstRoundtrip ||
             mode == Mode::PrecompSetting || mode == Mode::PrecompInfo || mode == Mode::PrecompClean))
            return illegalOpt("-M");
        if (haveF && mode != Mode::Run) return illegalOpt("-F");
        // --profile is for interpreted runs; a compiled binary has no
        // interpreter inside — use the OS profiler there (see CLI-PLAN.md)
        if (!profileDest.empty() && mode != Mode::Run) return illegalOpt("--profile");
    }
    if (!profileDest.empty()) rakupp::prof::setDest(profileDest);
    // the perl-family implications (perl 5.20+): -F implies -a, -a implies -n
    if (haveF) optA = true;
    if (optA && !optP) optN = true;
    if (recMode > 0 && recMode != 0777) { // 0777 is C++ octal — 511, perl's slurp marker
        std::cerr << "Only -0 (NUL records) and -0777 (slurp mode) are supported\n";
        return 4;
    }
    // -i refusals — each a DELIBERATE divergence from perl, which silently
    // no-ops -i without -n/-p and falls back to editing stdin(!) with no files
    if (optI && !(optN || optP)) {
        std::cerr << "-i is only meaningful together with -n or -p\n";
        return 4;
    }
    if (optI && recMode == 0) {
        std::cerr << "-0 (NUL records) does not combine with -i; use line mode or -0777\n";
        return 4;
    }
    if (haveF) { // -F/RE/ is a Raku regex; anything else is a literal separator
        if (fieldSep.size() >= 2 && fieldSep.front() == '/' && fieldSep.back() == '/') {
            fieldSep = fieldSep.substr(1, fieldSep.size() - 2);
            fieldSepRegex = true;
        }
        else { // C-style escapes in the literal form: -F'\t' means a TAB
            std::string t;
            for (size_t k = 0; k < fieldSep.size(); k++) {
                if (fieldSep[k] == '\\' && k + 1 < fieldSep.size()) {
                    char n = fieldSep[++k];
                    t += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n == '0' ? '\0' : n;
                }
                else { t += fieldSep[k]; }
            }
            fieldSep = t;
        }
    }
    // -M/-m: the program behaves as if it began with `use <module>; ` — joined
    // on the program's own first line, so error line numbers do not shift.
    // (Run mode applies it LAST, after the -n/-p wrap, so the `use` sits
    // outside the implicit line loop.)
    std::string usePrefix;
    for (auto& m : preloadModules) usePrefix += "use " + m + "; ";
    if (!usePrefix.empty() && haveSrc && mode != Mode::Run) src = usePrefix + src;

    if (mode == Mode::Help) {
        {
            std::cout <<
"Raku++ — a Raku interpreter and compiler in C++\n"
"\n"
"Usage:\n"
"  rakupp FILE [ARGS...]        Run a Raku program from a file\n"
"  rakupp -e 'CODE' [ARGS...]   Run a one-liner\n"
"  rakupp                       Start an interactive session (REPL)\n"
"  rakupp < FILE, ... | rakupp  Read a whole program from standard input\n"
"  rakupp - [ARGS...]           Same, explicitly — the spelling that lets a\n"
"                               stdin program take arguments (they land in @*ARGS)\n"
"\n"
"Options:\n"
"  -I <path>                    Add a directory to the module search path\n"
"                               (repeatable; -I<path> also works)\n"
"  -M <module>                  Load the module before running the program\n"
"                               (repeatable; -m and -M<module> also work)\n"
"  -n / -p                      Run the program once per input line ($_), from\n"
"                               files in the arguments or stdin; -p also prints\n"
"                               $_ after each line. Cluster like perl: -ne, -pe\n"
"  -a                           Autosplit each record into @F (implies -n);\n"
"                               -F<sep> sets the separator — a literal string\n"
"                               (\\t-style escapes) or a Raku regex as -F/…/\n"
"  -0777 / -0                   One record per FILE (slurp) / NUL-separated\n"
"                               records; -l is accepted (lines already chomp)\n"
"  -i[.ext]                     With -n/-p: edit the argument files in place\n"
"                               (extension glued, as in perl: -pi.bak keeps a\n"
"                               backup; the current file is $*ARGV)\n"
"  --profile[=FILE]             Routine-level wall-time profile after the run\n"
"                               (stderr by default; a .json FILE gets JSON).\n"
"                               Builtins are attributed to their caller\n"
"  -q, --quiet                  Drop the lines a mode prints about itself: `Syntax\n"
"                               OK`, the lint summary, `Compiled …`, the installer's\n"
"                               progress and `already installed:`, the REPL banner.\n"
"                               A mode's output, warnings and errors stay. Taken by\n"
"                               every mode, before or after its command\n"
"\n"
"Compile to a standalone binary (each takes FILE or -e CODE, plus -o OUT):\n"
"  rakupp --bundle SRC -o OUT   Embed source + interpreter (whole language)\n"
"  rakupp --aot    SRC -o OUT   Parse ahead of time, embed the AST\n"
"  rakupp --exe    SRC -o OUT   Native-compile to C++ (fastest; falls back to\n"
"                               bundling for constructs it can't compile yet).\n"
"                               -O optimizes for speed (the codegen passes); a level\n"
"                               (-O3/-Os/-Ofast/…) is forwarded to the C++ compiler.\n"
"                               Size: the linked runtime dominates the binary, so\n"
"                               -Os buys nothing — `strip OUT` (~15%) is the size tool\n"
"  --slim[=SPEC]                Cut unused runtime subsystems out of the compiled\n"
"                               binary (--slim=help explains the levels and cuts)\n"
"  --standalone                 A module the compile mode cannot embed becomes a\n"
"                               build ERROR instead of a run-time disk fallback\n"
"\n"
"Modules (the ecosystem installer; each command alone shows its full usage):\n"
"  rakupp install MODULE ...    Fetch, test and install into the CURI store\n"
"                               (~/.raku, shared with zef; --to=PATH for another).\n"
"                               Resolves from the zef index, then the REA archive\n"
"  rakupp uninstall MODULE ...  Remove distributions this installer put there\n"
"  rakupp reinstall MODULE ...  Uninstall and install fresh, as one command\n"
"  rakupp test MODULE ...       Build + run the dists' own suites against the\n"
"                               store; installs their deps, never the dists\n"
"  rakupp install --list        What is installed; --check: store integrity\n"
"                               report; --refresh: refetch the cached index(es)\n"
"  rakupp install -q MODULE     Only warnings and failures; nothing on success\n"
"                               (-q goes with every command here, in any position)\n"
"\n"
"Serve:\n"
"  rakupp --mcp                 Serve the interpreter over the Model Context\n"
"                               Protocol (JSON-RPC on stdio), so MCP clients —\n"
"                               Claude Code and friends — get two tools: raku (a\n"
"                               persistent session) and raku-parse (grammars).\n"
"                               --timeout=SECS answers a stuck call and exits, and\n"
"                               the client restarts fresh (default 120; 0 = never);\n"
"                               -M preloads modules into the session\n"
"  rakupp --jupyter FILE        Run as a Jupyter kernel against the connection\n"
"                               file the frontend passes; -M preloads modules\n"
"  rakupp --jupyter-install     Register this binary as the \"raku\" kernel, so\n"
"                               jupyter lab / jupyter console can launch it\n"
"                               (--name=NAME, --prefix=DIR for another location)\n"
"\n"
"Inspection:\n"
"  rakupp --lint SRC [-q]       Static-analyze without running: warn about unused\n"
"                               variables, unreachable code, redeclarations, etc.\n"
"                               (exit 1 if any warning; -q suppresses the summary)\n"
"  rakupp --ast SRC             Print the parsed AST as an indented tree\n"
"  rakupp --ast-roundtrip SRC   Check the AST survives the precomp cache format\n"
"  rakupp --precomp-info        Where the parsed-module cache is, and how big\n"
"  rakupp --precomp-clean       Empty it (entries are derived data — always safe)\n"
"  rakupp --precomp-modules=on|off   Cache the parse of `use`d modules (default off)\n"
"  rakupp --precomp-files=on|off     Cache the main program's own parse (default off)\n"
"  rakupp --cpp SRC [-O]        Print the C++ that --exe would transpile to\n"
"                               (add -O to print the optimized codegen instead)\n"
"  rakupp --target=js SRC       Transpile to JavaScript (to stdout; -o OUT.js writes\n"
"                               the program and its runtime rakupp-rt.js beside it;\n"
"                               --standalone inlines the runtime). --verify runs the\n"
"                               program under the interpreter and under node and\n"
"                               emits only if they agree; --fallback=wasm accepts a\n"
"                               program outside the JS core by wrapping the WASM engine;\n"
"                               --runtime writes just the runtime module\n"
"  rakupp --highlight [SRC]     Syntax-highlight Raku (--html [default] / --ansi;\n"
"                               reads stdin if no SRC), e.g. as a pygmentize drop-in.\n"
"                               Flags compose in any order; bare `rakupp --ansi SRC`\n"
"                               is a shorthand for `--highlight --ansi SRC`\n"
"  rakupp -c SRC                Compile-check only: parse, check every variable is\n"
"                               declared, report, don't run\n"
"  rakupp --doc SRC             Run, then render the program's Pod to stdout\n"
"  rakupp --exe-info BINARY     A compiled binary's embedded build manifest\n"
"                               (version, compile mode, --slim cuts)\n"
"  rakupp --target=parse|ast    Rakudo-compatible aliases of -c / --ast\n"
"  rakupp --lsp                 Run the Language Server (JSON-RPC on stdin/stdout)\n"
"                               for editor integration: live parse/lint diagnostics\n"
"  rakupp --help, -h            Show this help\n"
"  rakupp --version, -V, -v     Show the version\n"
"  rakupp --ffi-info            Show which FFI backend NativeCall will use\n"
"\n"
"Environment:\n"
"  RAKULIB=dir1,dir2            Extra module search dirs (like -I); ',' or ':'\n"
"  RAKUPP_GIL=1                 Coordinate threads under a global lock (the\n"
"                               pre-v3 default; start/worker threads run on\n"
"                               all cores otherwise)\n"
"  RAKUPP_DUMPTOKENS=1          Dump the lexer token stream before running\n"
"  RAKUPP_HISTORY=file          REPL history file (default ~/.rakupp_history);\n"
"                               set it empty to keep no history at all\n"
"  RAKUPP_REPL=1                Force a REPL session even when stdin is redirected\n"
"  RAKUPP_HOME=dir              Where --exe finds its runtime (dir/lib + dir/include/rakupp);\n"
"                               only needed if rakupp is moved away from its build/install tree\n"
"  RAKUPP_FFI=0 | /path/to/lib  Disable NativeCall's libffi backend, or point at a\n"
"                               specific libffi (default: found at runtime, see --ffi-info)\n"
"  RAKUPP_FFI_TRACE=1           Log every NativeCall crossing to stderr as it happens\n"
"\n"
"Run the spec-test harness (self-hosted, in Raku):\n"
"  ROAST=/path/to/roast rakupp tools/run-roast.raku [PATH-SUBSTRING]\n"
"\n"
"Docs, a tour of the language, and a browser playground: https://raku.online\n";
            return 0;
        }
    }
    // The first line keeps its shape: it is what a human greps for and what
    // t/run.raku asserts. Everything a bug report needs follows it — which
    // commit this binary came from, when, for what, and with which compiler.
    if (mode == Mode::Version) {
        std::cout << "Raku++ (rakupp) " RAKUPP_VERSION
                     " — a Raku interpreter and compiler in C++\n"
                     // 6.e is no longer "some features": it is implemented and
                     // gated, so a program gets 6.d unless it asks for 6.e. The
                     // exceptions are named on the support page rather than in a
                     // banner line nobody can fit them into.
                     "Implements Raku 6.d, and 6.e under `use v6.e.PREVIEW`.\n"
                  << "Build  " << rakupp::buildId() << " (" << rakupp::buildDate()
                  << "), " << rakupp::platform() << ", " << rakupp::compilerId() << "\n"
                  << "Home   https://raku.online — docs, a tour of the language, "
                     "and a browser playground\n";
        return 0;
    }
    // Which FFI backend NativeCall will use. The first question to ask of a
    // native-call bug report, and how the test suite tells its two CI legs
    // apart, so it is worth a flag of its own.
    if (mode == Mode::FfiInfo) { std::cout << ffi::describe() << "\n"; return 0; }

    if (mode == Mode::Jupyter) {
        rakupp::jupyter::Options jo;
        jo.connectionFile = jupyterConn;
        jo.preload = preloadModules;
        return rakupp::jupyter::runKernel(jo);
    }

    if (mode == Mode::JupyterInstall) {
        rakupp::jupyter::InstallOptions io;
        io.selfExe = exePath;
        io.name = jupyterName;
        io.prefix = jupyterPrefix;
        io.quiet = g_quiet;
        return rakupp::jupyter::installKernelspec(io);
    }

    // --lsp : the Language Server, JSON-RPC over stdin/stdout. Diagnostics only
    // (v1): it runs the SAME lex -> parse -> lintProgram pipeline as --lint, so an
    // editor's squiggles can never disagree with the CLI. Nothing in the engine is
    // reached — no interpreter, no codegen.
    if (mode == Mode::Lsp) return rakupp::runLsp();

    if (mode == Mode::Mcp) {
        rakupp::mcp::Options mo;
        if (mcpTimeout >= 0) mo.timeoutSecs = (int)mcpTimeout;
        mo.preload = preloadModules;
        mo.quiet = g_quiet;
        return rakupp::mcp::runServer(mo);
    }

    // --highlight [--html|--ansi] [FILE | -e CODE | -]  : syntax-highlight Raku
    // and exit. Default format is html (the course consumer); `-`/no file reads
    // stdin, so it drops in for `pygmentize -f html -l raku`. Flag composition
    // (`--ansi --highlight` == `--highlight --ansi`, bare `--ansi` implies
    // --highlight) is the option parser's doing now.
    if (mode == Mode::Highlight) {
        if (!haveSrc) { std::ostringstream ss; ss << std::cin.rdbuf(); src = ss.str(); }
        std::cout << highlight(src, hlFmt);
        return 0;
    }

    // --ast FILE | --ast -e CODE : print the parsed AST and exit
    // (--dump-ast and --target=ast are kept as compatible aliases)
    if (mode == Mode::Ast) {
        if (!haveSrc) { std::cerr << "Usage: rakupp --ast FILE | --ast -e CODE\n"; return 4; }
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            Program prog = parser.parseProgram();
            dumpAst(prog, std::cout);
        } catch (const ParseError& e) {
            std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
            return 2;
        }
        return 0;
    }

    // --ast-roundtrip FILE : parse, serialize, deserialize, and prove the tree
    // survived. Two independent checks, because they fail differently:
    //   * re-serializing the rebuilt tree must give BYTE-IDENTICAL output —
    //     catches a field the reader skips or misorders;
    //   * dumpAst of both trees must match — catches a field the WRITER never
    //     writes, which the byte check alone cannot see.
    // Neither is redundant. This is the tool to run over a corpus after touching
    // Ast.h; the precompiled-module cache is only as trustworthy as it.
    if (mode == Mode::AstRoundtrip) {
        if (!haveSrc) { std::cerr << "Usage: rakupp --ast-roundtrip FILE\n"; return 4; }
        Program prog;
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            prog = parser.parseProgram();
        } catch (const ParseError& e) {
            std::cerr << "PARSE " << fileName << ": " << e.what() << "\n";
            return 3; // not a serializer failure — the file does not parse at all
        }
        try {
            std::string blob = serializeAst(prog);
            Program back;
            deserializeAst(blob, back);
            std::string blob2 = serializeAst(back);
            if (blob != blob2) {
                std::cerr << "ROUNDTRIP-BYTES " << fileName << " (" << blob.size()
                          << " vs " << blob2.size() << ")\n";
                return 1;
            }
            std::ostringstream d1, d2;
            dumpAst(prog, d1); dumpAst(back, d2);
            if (d1.str() != d2.str()) { std::cerr << "ROUNDTRIP-DUMP " << fileName << "\n"; return 1; }
            if (!g_quiet) std::cout << "ok " << fileName << "  (" << blob.size() << " bytes)\n";
        } catch (const AstSerialError& e) {
            std::cerr << "SERIAL " << fileName << ": " << e.msg << "\n";
            return 1;
        }
        return 0;
    }

    // --precomp-info / --precomp-clean : the parsed-module cache. Entries are keyed
    // by SOURCE CONTENT, so a stale one can never be served — but an edited module
    // orphans its old entry, and those want a way out that is not "know the path".
    // --precomp-modules=on|off / --precomp-files=on|off : persist a switch.
    // Both default off: rakupp does not write to a user's disk unasked. They are
    // separate switches because they earn their keep very differently — caching a
    // module tree is a clear win, caching a small script's own parse is a wash —
    // so `modules` is the one likely to become a default later. See
    // docs/guide/CACHING.md for the measurements.
    if (mode == Mode::PrecompSetting) {
        const std::string& key = precompKey, & val = precompVal;
        bool on = (val == "on" || val == "1" || val == "true" || val == "yes");
        if (!on && !(val == "off" || val == "0" || val == "false" || val == "no")) {
            std::cerr << "Usage: rakupp --" << key << "=on|off\n";
            return 4;
        }
        if (!precompSetSetting(key, on)) {
            std::cerr << "Cannot write " << precompConfigPath() << "\n";
            return 5;
        }
        if (!g_quiet)
            std::cout << key << " = " << (on ? "on" : "off")
                      << "   (saved in " << precompConfigPath() << ")\n";
        return 0;
    }
    if (mode == Mode::PrecompInfo || mode == Mode::PrecompClean) {
        std::string dir = precompCacheDir();
        if (dir.empty()) {
            std::cout << "precompiled-parse caching unavailable: no HOME, so there is "
                         "nowhere to put a cache\n";
            return 0;
        }
        if (mode == Mode::PrecompClean) {
            auto [n, bytes] = precompCacheClear();
            if (!g_quiet)
                std::cout << "removed " << n << " entr" << (n == 1 ? "y" : "ies")
                          << " (" << (bytes + 1023) / 1024 << " KB) from " << dir << "\n";
            return 0;
        }
        auto entries = precompCacheList();
        unsigned long long bytes = 0, stale = 0, orphaned = 0;
        size_t nStale = 0, nOrphan = 0;
        for (auto& e : entries) {
            bytes += e.bytes;
            if (e.orphan)       { nOrphan++; orphaned += e.bytes; }
            else if (!e.usable) { nStale++;  stale    += e.bytes; }
        }
        std::cout << dir << "\n"
                  << "  modules: " << (precompModulesOn() ? "on " : "off")
                  << "  (" << precompModulesSource() << ")\n"
                  << "  files:   " << (precompFilesOn() ? "on " : "off")
                  << "  (" << precompFilesSource() << ")\n"
                  << "  config:  " << precompConfigPath() << "\n\n";
        if (entries.empty()) std::cout << "empty\n";
        for (auto& e : entries)
            std::cout << (e.orphan ? "  x " : e.usable ? "    " : "  ! ") << e.source
                      << "  (" << (e.bytes + 1023) / 1024 << " KB)\n";
        std::cout << "\n" << entries.size() << " entr" << (entries.size() == 1 ? "y" : "ies")
                  << ", " << (bytes + 1023) / 1024 << " KB\n";
        if (nStale)
            std::cout << nStale << " marked ! " << (nStale == 1 ? "is" : "are") << " stale ("
                      << (stale + 1023) / 1024 << " KB): built by another rakupp, or the source "
                         "has changed since. Each is rewritten in place on next use.\n";
        if (nOrphan)
            std::cout << nOrphan << " marked x " << (nOrphan == 1 ? "is" : "are") << " orphaned ("
                      << (orphaned + 1023) / 1024 << " KB): the source file is gone, so "
                      << (nOrphan == 1 ? "it is" : "they are")
                      << " never read or rewritten again. rakupp drops them as it goes; "
                         "--precomp-clean removes them now.\n";
        std::cout << "(one entry per source file; --precomp-clean empties it)\n";
        return 0;
    }

    // -c : syntax check only (like Rakudo's -c) — parse, report, never execute
    // (--target=parse is the Rakudo-compatible alias)
    if (mode == Mode::Check) {
        if (!haveSrc) { std::cerr << "Usage: rakupp -c (FILE | -e CODE)\n"; return 4; }
        Program prog;
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            parser.libPaths_ = effectiveSearchPath(libPaths);
            parser.srcFile_ = fileName;
            prog = parser.parseProgram();
        } catch (const ParseError& e) {
            std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
            return 2;
        }
        // Syntax is only half of "does this compile": an undeclared variable is
        // a compile error too, and -c reporting OK for a file that dies on its
        // third line was the other half of issue #32.
        if (declCheckEnabled()) {
            auto us = findUndeclaredVars(prog, src, effectiveSearchPath(libPaths));
            if (!us.empty()) return reportUndeclaredVars(us, fileName, src);
        }
        if (!g_quiet) std::cout << "Syntax OK\n";   // -q: the exit code is the verdict
        return 0;
    }

    // --lint : static analysis only — parse, analyse, report; never execute.
    // Prints `FILE:LINE: error|warning|note: message [rule]`. Exits 2 if the
    // file will not compile (a parse error, or an undeclared variable), 1 if
    // there were only warnings, 0 for notes or nothing.
    if (mode == Mode::Lint) {
        if (!haveSrc) { std::cerr << "Usage: rakupp --lint (FILE | -e CODE) [-q]\n"; return 4; }
        Program prog;
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            parser.libPaths_ = effectiveSearchPath(libPaths);
            parser.srcFile_ = fileName;
            prog = parser.parseProgram();
        } catch (const ParseError& e) {
            std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
            return 2;
        }
        auto findings = lintProgram(prog);
        // The linter only advises, so on its own it would report "no issues" for
        // a file the compiler refuses outright — which is worse than saying
        // nothing. The undeclared-variable check joins it as an ERROR, the one
        // severity here that means the program will not run.
        if (declCheckEnabled())
            for (auto& u : findUndeclaredVars(prog, src, effectiveSearchPath(libPaths)))
                findings.push_back({u.line, 'E', "undeclared-variable",
                                    "'" + u.name + "' is not declared"});
        std::stable_sort(findings.begin(), findings.end(),
                         [](const LintFinding& a, const LintFinding& b) {
                             if (a.line != b.line) return a.line < b.line;
                             return a.rule < b.rule;
                         });
        int errs = 0, warns = 0, notes = 0;
        for (auto& f : findings) {
            (f.severity == 'E' ? errs : f.severity == 'W' ? warns : notes)++;
            std::cout << fileName << ":" << f.line << ": "
                      << (f.severity == 'E' ? "error" : f.severity == 'W' ? "warning" : "note")
                      << ": " << f.message << " [" << f.rule << "]\n";
        }
        if (!quiet) {
            if (findings.empty()) std::cerr << "rakupp --lint: no issues found in " << fileName << "\n";
            else {
                std::cerr << "rakupp --lint: ";
                if (errs) std::cerr << errs << " error" << (errs == 1 ? "" : "s") << ", ";
                std::cerr << warns << " warning" << (warns == 1 ? "" : "s") << ", "
                          << notes << " note" << (notes == 1 ? "" : "s")
                          << " in " << fileName << "\n";
            }
        }
        return errs ? 2 : warns ? 1 : 0;
    }

    // --target=js : transpile to JavaScript (TRANSPILE-PLAN.md)
    if (mode == Mode::Js) {
        if (g_jsRuntimeOnly) {   // --target=js --runtime [-o rakupp-rt.js]: the sidecar alone, for a program written to stdout
            if (outPath.empty()) { std::cout << jsRuntimeModule(); return 0; }
            std::ofstream f = openOut(outPath); if (!f) { std::cerr << "Cannot write " << outPath << "\n"; return 5; }
            f << jsRuntimeModule();
            if (!g_quiet) std::cerr << "Wrote the JavaScript runtime -> " << outPath << "\n";
            return 0;
        }
        if (!haveSrc) { std::cerr << "Usage: rakupp --target=js (FILE | -e CODE) [-o OUT.js] [--standalone] [--verify] [--fallback=wasm]\n       rakupp --target=js --runtime [-o rakupp-rt.js]\n"; return 4; }
        return compileJs(src, fileName, outPath, exePath, libPaths);
    }

    // --cpp : print the C++ that `--exe` would transpile the program to (to stdout)
    if (mode == Mode::Cpp) {
        if (!haveSrc) { std::cerr << "Usage: rakupp --cpp (FILE | -e CODE) [-O]\n"; return 4; }
        if (int rc = declCheckGate(src, fileName, effectiveSearchPath(libPaths)); rc >= 0) return rc;
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            parser.libPaths_ = effectiveSearchPath(libPaths);
            parser.srcFile_ = fileName;
            Program prog = parser.parseProgram();
            // same module scan as --exe, so what this prints is what --exe compiles
            std::set<std::string> moduleExports;
            collectModuleGraph(prog, effectiveSearchPath(libPaths), &moduleExports);
            std::cout << transpileToCpp(prog, optimize, absPath(fileName), moduleExports, src);
        } catch (const ParseError& e) {
            std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
            return 2;
        } catch (const CodegenError& e) {
            // a construct outside the native subset: `--exe` would fall back to AOT
            std::cerr << "note: " << e.msg << " — not natively compilable; --exe would fall back to AOT (--aot)\n";
            return 5;
        }
        return 0;
    }

    // --bundle : embed program source + runtime interpreter (parses at run time)
    // --aot    : parse ahead of time, embed the AST, interpret it (no run-time parse)
    // --exe    : native transpilation to C++ (no interpreter inside)
    //   each: (FILE | -e CODE) [-o OUT] — and -I matters at COMPILE time here:
    //   it is how the bundler finds the modules to embed, so a binary built
    //   with -I needs nothing at run time.
    if (isCompileMode(mode)) {
        if (!haveSrc) { std::cerr << "Usage: rakupp " << modeTok << " (FILE | -e CODE) [-o OUT] [-I dir] [-O[level]]\n"; return 4; }
        // The P5 directives ride the compile modes: `list`/`why:` analyse and
        // stop (no compile); `verify` compiles twice and emits only on proof.
        if (g_slim.directive == SlimDirective::List || g_slim.directive == SlimDirective::Why) {
            const char* note = mode == Mode::Exe ? "--exe"
                : mode == Mode::Aot ? "--aot keeps every feature; shown as --exe would decide"
                : "--bundle keeps every feature; shown as --exe would decide";
            return slimExplain(src, fileName, exePath, libPaths, note);
        }
        if (g_slim.directive == SlimDirective::Verify)
            return slimVerify(mode == Mode::Exe ? 'x' : mode == Mode::Aot ? 'a' : 'b',
                              src, fileName, outPath, exePath, optimize, ccOpt, libPaths);
        if (int rc = declCheckGate(src, fileName, effectiveSearchPath(libPaths)); rc >= 0) return rc;
        if (mode == Mode::Exe) return compileNative(src, fileName, outPath, exePath, optimize, ccOpt, libPaths);
        if (mode == Mode::Aot) return compileAotAst(src, fileName, outPath, exePath, libPaths);
        return compileToExe(src, fileName, outPath, exePath, libPaths); // --bundle
    }

    // ---- run (the default mode) --------------------------------------------
    if (!haveSrc) {
        if (rakupp::stdinIsTerminal() || rakupp::replForced()) {
            // Bare `rakupp` at a terminal: an interactive session.
            return rakupp::rakuppRepl(exePath, libPaths, g_quiet);
        }
        // Bare `rakupp` with stdin redirected — `echo … | rakupp`, `rakupp < f.raku`
        // — is a whole program arriving on stdin, exactly as before.
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        src = ss.str();
    }
    if (optN || optP) { // wrap the program in a record loop (files in @*ARGS, else $*IN)
        // emit a string as a double-quoted Raku literal with per-char escaping
        // (control chars as \x[..] — a raw NUL would truncate the generated source)
        auto rakuDq = [](const std::string& s) {
            std::string lit;
            for (unsigned char c : s) {
                if (c == '"' || c == '\\' || c == '$' || c == '@' || c == '{' || c == '}') { lit += '\\'; lit += (char)c; }
                else if (c < 0x20) { char b[16]; snprintf(b, sizeof b, "\\x[%02x]", c); lit += b; }
                else lit += (char)c;
            }
            return lit;
        };
        // -a: @F, split by .words or the -F separator, declared before the body;
        // the /…/ separator form is spliced as Raku regex text.
        std::string pre;
        if (optA) {
            if (!haveF) pre = "my @F = .words; ";
            else if (fieldSepRegex) pre = "my @F = $_.split(/" + fieldSep + "/); ";
            else pre = "my @F = $_.split(\"" + rakuDq(fieldSep) + "\"); ";
        }
        if (optI) {
            // -i[.ext]: edit the argument files in place. Per file: run the body
            // with $*OUT rebound to a temp file in the same directory, preserve
            // the mode, rename the original to its backup (if an extension was
            // given), then the temp over the original. A file that cannot be
            // opened is reported and skipped, and the exit code says so —
            // unlike perl, which exits 0 (deliberate, per the plan).
            // ($__mode round-trips via parse-base(8): .IO.mode is an octal
            // STRING here, and chmod("0755") would read it as decimal.)
            if (progArgs.empty()) {
                std::cerr << "-i requires file arguments to edit in place\n";
                return 1;
            }
            std::string inner;
            if (recMode == 0777)
                inner = "my $_ = $__f.IO.slurp;\n" + pre + src + "\n" + (optP ? "print $_;\n" : "");
            else
                inner = "for $__f.IO.lines -> $_ is copy {\n" + pre + src + "\n"
                      + (optP ? "$_.say;\n" : "") + "}\n";
            std::string bak;
            if (!backupExt.empty())
                bak = "rename($__f, $__f ~ \"" + rakuDq(backupExt) + "\");\n";
            src = "my $__bad = 0;\n"
                  "for @*ARGS -> $__f {\n"
                  "if !$__f.IO.e { note \"Can't open $__f: No such file or directory.\"; $__bad = 1; next; }\n"
                  "if !$__f.IO.r { note \"Can't open $__f: Permission denied.\"; $__bad = 1; next; }\n"
                  "my $__mode = $__f.IO.mode;\n"
                  "my $__tmp = $__f ~ \".\" ~ $*PID ~ \".rakupp-tmp\";\n"
                  "my $__out = open($__tmp, :w);\n"
                  "my $*ARGV = $__f;\n"
                  "{\n"
                  "my $*OUT = $__out;\n"
                  + inner +
                  "}\n"
                  "$__out.close;\n"
                  "try $__tmp.IO.chmod($__mode.parse-base(8));\n"
                  + bak +
                  "rename($__tmp, $__f);\n"
                  "}\n"
                  "exit(1) if $__bad;\n";
        }
        else if (recMode == 0777) {
            // slurp mode: the body runs once per FILE with the whole file in $_;
            // -p prints the record raw (no appended newline), as perl does
            src = "my @__files = @*ARGS.elems ?? @*ARGS !! ['-'];\n"
                  "for @__files -> $__f {\n"
                  "my $_ = $__f eq '-' ?? $*IN.slurp !! $__f.IO.slurp;\n"
                  + pre + src + "\n" + (optP ? "print $_;\n" : "") + "}\n";
        }
        else if (recMode == 0) {
            // NUL-separated records (the `find -print0` partner). Records arrive
            // CHOMPED — the same Raku-side choice -n already makes for lines —
            // and -p re-appends the NUL so pipelines round-trip.
            src = "my $__all = @*ARGS.elems ?? @*ARGS.map(*.IO.slurp).join !! $*IN.slurp;\n"
                  "my @__recs = $__all.split(\"\\0\");\n"
                  "@__recs.pop if @__recs && @__recs[*-1] eq '';\n"
                  "for @__recs -> $_ is copy {\n"
                  + pre + src + "\n" + (optP ? "print $_ ~ \"\\0\";\n" : "") + "}\n";
        }
        else {
            src = "for lines() -> $_ is copy {\n"
                  + pre + src + "\n" + (optP ? "$_.say;\n" : "") + "}\n";
        }
    }
    if (!usePrefix.empty()) src = usePrefix + src; // -M: outside the -n/-p loop
    int rc = rakuppRunBigStack(src, std::move(progArgs), fileName, exePath, libPaths);
    rakupp::prof::report(); // no-op unless --profile was given
    return rc;
}
