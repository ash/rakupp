#include "Runtime.h"
#include <cstdint>
#include <cstdio>
#include "Codegen.h"
#include "Lexer.h"
#include "Parser.h"
#include "AstSerial.h"
#include "Interpreter.h"
#include "Lint.h"
#include "Ffi.h"
#include "Highlight.h"
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
    for (auto& ch : b) ch = (char)std::tolower((unsigned char)ch);
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

// Build the compile-and-link command for a generated source + the runtime
// archive, in the dialect of the chosen compiler. `opt` is the Unix-style
// optimization flag ("-O2", "-O0", …); it is translated for cl.
static std::string compileCmd(const std::string& cxx, const std::string& opt,
                              const std::string& inc, const std::string& in,
                              const std::string& lib, const std::string& out) {
    if (msvcStyle(cxx)) {
        std::string o = opt == "-O0" ? "/Od" : opt == "-O1" ? "/O1" : "/O2";
        // /MT: static CRT, matching the /MT-built runtime archive (mixing
        // /MD stub objects with an /MT library is a link error)
        std::string c = cxx + " /nologo /std:c++17 /EHsc /MT /w " + o;
        if (!inc.empty()) c += " /I " + shq(inc);
        c += " " + shq(in) + " " + shq(lib) + " /Fe:" + shq(out) + " ws2_32.lib";
        // 256 MiB main-thread stack: Windows defaults to 1 MB, which is under
        // the recursion guard's 2 MiB headroom reserve — the first guarded
        // call in a natively-compiled program threw X::Recursion immediately
        c += " /link /STACK:268435456";
#ifdef _WIN32
        c = msvcEnvPrefix() + c; // bootstrap vcvars when cl isn't in this shell
#endif
        return c;
    }
    std::string c = cxx + " -std=c++17 " + (opt.empty() ? "-O2" : opt) + " -w -pthread -Wl,-w";
    if (!inc.empty()) c += " -I " + shq(inc);
    c += " " + shq(in) + " " + shq(lib) + " -o " + shq(out);
#ifdef _WIN32
    c += " -lws2_32";                 // MinGW: the runtime's sockets need Winsock
    c += " -Wl,--stack,268435456";    // and the same 256 MiB main stack as MSVC
#endif
#ifdef __APPLE__
    // The generated main() runs on the process main thread, whose default 8 MiB
    // stack gives natively-compiled recursion a far smaller budget than the
    // interpreter's 1 GiB big-stack thread. 512 MiB is the arm64 ld cap; the
    // recursion guard reads it via pthread_get_stacksize_np automatically.
    c += " -Wl,-stack_size,0x20000000";
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
                " const std::string&, const std::string&, const std::vector<std::string>&); void setConsoleUtf8(); }\n";
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
                mods = collectModuleGraph(pr, effectiveSearchPath(libPaths));
            } catch (const ParseError&) {}
            std::ostringstream decls, calls;
            emitModuleTable(mods, decls, calls, /*withSources=*/true);
            stub << decls.str();
            bundleModuleCalls = calls.str();
        }
        stub << "namespace rakupp { void rakuppRegisterModule(const std::string&, const char*, unsigned long, const std::string&);\n"
                "                  void rakuppRegisterModuleSource(const std::string&, const char*, unsigned long); }\n";
        stub << "int main(int argc, char** argv) {\n"
             << bundleModuleCalls
             << "  rakupp::setConsoleUtf8();\n"
                "  std::string src(reinterpret_cast<const char*>(SRC), SRC_LEN);\n"
                "  std::vector<std::string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);\n"
                "  std::string exe = argc > 0 ? argv[0] : \"program\";\n"
                "  char rp[4096]; if (RAKUPP_REALPATH(exe.c_str(), rp)) exe = rp;\n"
                "  return rakupp::rakuppRunBigStack(src, args, " << cppstr(baseOf(srcName)) << ", exe, {});\n"
                "}\n";
    }

    std::string cmd = compileCmd(nativeCxx(lib), "-O2", "", stubPath, lib, outPath);
    int rc = runCommand(cmd);
    removeFile(stubPath);
    if (rc != 0) {
        std::cerr << "Compilation failed (compiler exit " << rc << ")\n";
#ifdef _WIN32
        winCompilerHint(lib);
#endif
        return 5;
    }
    std::cerr << "Compiled " << srcName << " -> " << outPath << "\n";
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

// back with a clear message if the program uses an unsupported construct.
static int compileNative(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe, bool optimize = false, const std::string& ccOpt = "-O2",
                         const std::vector<std::string>& libPaths = {}) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);

    std::string cpp;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        parser.libPaths_ = effectiveSearchPath(libPaths); // find a `use`d module's operators
        Program prog = parser.parseProgram();
        // The program is compiled; its MODULES ride along as parsed ASTs, so the
        // binary is self-sufficient. (Natively compiling module code is a separate
        // step — most module files declare a package, which codegen does not take.)
        // The graph is walked BEFORE transpiling because codegen needs the names
        // those modules export: it resolves calls by name, so an exported sub that
        // collides with a built-in has to be known to reach it at all.
        std::set<std::string> moduleExports;
        auto mods = collectModuleGraph(prog, effectiveSearchPath(libPaths), &moduleExports);
        cpp = transpileToCpp(prog, optimize, absPath(srcName), moduleExports);
        if (!mods.empty()) {
            std::ostringstream decls, calls;
            emitModuleTable(mods, decls, calls);
            cpp = injectModuleTable(cpp, decls.str(), calls.str());
        }
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
    { std::ofstream g = openOut(genPath); if (!g) { std::cerr << "Cannot write " << genPath << "\n"; return 5; } g << cpp; }

    std::string cmd = compileCmd(nativeCxx(lib), ccOpt, inc, genPath, lib, outPath);
    int rc = runCommand(cmd);
    if (!std::getenv("RAKUPP_KEEPGEN")) removeFile(genPath);
    if (rc != 0) {
        std::cerr << "Compilation failed (compiler exit " << rc << ")\n";
#ifdef _WIN32
        winCompilerHint(lib);
#endif
        return 5;
    }
    std::cerr << "Compiled (native) " << srcName << " -> " << outPath << "\n";
    return 0;
}

// Real AOT: parse ahead of time, then emit C++ that rebuilds the AST at startup
// and interprets it (no lexing/parsing in the produced binary). Falls back to
// source bundling for any construct the AST emitter can't reconstruct.
static int compileAotAst(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe,
                         const std::vector<std::string>& libPaths = {}) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);
    std::string cpp, finish;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        parser.libPaths_ = effectiveSearchPath(libPaths); // find a `use`d module's operators
        finish = lexer.finishData();
        Program prog = parser.parseProgram();
        // Everything the program `use`s, resolved and parsed NOW, so the binary
        // carries it. Anything unresolvable is simply absent and falls back to a
        // run-time load — see collectModuleGraph.
        auto mods = collectModuleGraph(prog, effectiveSearchPath(libPaths));
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
    { std::ofstream g = openOut(genPath); if (!g) { std::cerr << "Cannot write " << genPath << "\n"; return 5; } g << cpp; }
    std::string cmd = compileCmd(nativeCxx(lib), "-O2", inc, genPath, lib, outPath);
    int rc = runCommand(cmd);
    if (!std::getenv("RAKUPP_KEEPGEN")) removeFile(genPath);
    if (rc != 0) {
        std::cerr << "Compilation failed (compiler exit " << rc << ")\n";
#ifdef _WIN32
        winCompilerHint(lib);
#endif
        return 5;
    }
    std::cerr << "Compiled (AOT) " << srcName << " -> " << outPath << "\n";
    return 0;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    rakupp::setConsoleUtf8();  // Windows: render UTF-8 output instead of mojibake (no-op elsewhere)
    std::string exePath = selfExePath(argv[0]); // resolve the real binary (argv[0] may be a bare PATH name)
#ifndef _WIN32
    { char rp[4096]; if (realpath(exePath.c_str(), rp)) exePath = rp; }
#endif // Windows: GetModuleFileNameW is already absolute; _fullpath would ANSI-mangle the UTF-8

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
                      Cpp, Bundle, Aot, Exe };
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
    std::string fieldSep;                 // -F: separator (implies -a)
    bool haveF = false, fieldSepRegex = false;
    long recMode = -1;                    // -0[octal]: 0 = NUL records, 0777 = slurp
    bool quiet = false;                   // --lint -q
    bool optimize = false;                // -O (compile modes and --cpp)
    bool sawHtml = false;                 // --html is only legal under --highlight

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
                else { std::cerr << "Unknown --target '" << t << "' (supported: parse, ast)\n"; return 4; }
                continue;
            }
            if (a == "--quiet" || a == "-q") { quiet = true; continue; }
            if (a == "-o") { if (i + 1 < argc) outPath = argv[++i]; continue; }
            if (a.rfind("-o", 0) == 0 && a.size() > 2) { outPath = a.substr(2); continue; }
            // any -O… turns on the codegen optimizer; a suffix (-O3/-Os/…)
            // is forwarded to the C++ compiler for the generated binary.
            if (a.rfind("-O", 0) == 0) { optimize = true; if (a.size() > 2) ccOpt = a; continue; }
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
                    else if (c == 'e') { sawE = true; j++; break; }
                    else { ok = false; break; } // -nfoo is not a flag cluster at all
                }
                if (ok && (sawN || sawP || sawA || sawL || sawE || saw0 >= 0)) {
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
        if (quiet && mode != Mode::Lint) return illegalOpt("-q");
        if (!outPath.empty() && !isCompileMode(mode)) return illegalOpt("-o");
        if (optimize && !isCompileMode(mode) && mode != Mode::Cpp) return illegalOpt("-O");
        // -M applies where the program is checked, compiled or run; the pure
        // source tools see the file exactly as written
        if (!preloadModules.empty() &&
            (mode == Mode::Highlight || mode == Mode::Ast || mode == Mode::AstRoundtrip ||
             mode == Mode::PrecompSetting || mode == Mode::PrecompInfo || mode == Mode::PrecompClean))
            return illegalOpt("-M");
        if (haveF && mode != Mode::Run) return illegalOpt("-F");
    }
    // the perl-family implications (perl 5.20+): -F implies -a, -a implies -n
    if (haveF) optA = true;
    if (optA && !optP) optN = true;
    if (recMode > 0 && recMode != 0777) { // 0777 is C++ octal — 511, perl's slurp marker
        std::cerr << "Only -0 (NUL records) and -0777 (slurp mode) are supported\n";
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
"  rakupp --highlight [SRC]     Syntax-highlight Raku (--html [default] / --ansi;\n"
"                               reads stdin if no SRC), e.g. as a pygmentize drop-in.\n"
"                               Flags compose in any order; bare `rakupp --ansi SRC`\n"
"                               is a shorthand for `--highlight --ansi SRC`\n"
"  rakupp -c SRC                Check syntax only (parse and report, don't run)\n"
"  rakupp --target=parse|ast    Rakudo-compatible aliases of -c / --ast\n"
"  rakupp --help, -h            Show this help\n"
"  rakupp --version, -V, -v     Show the version\n"
"  rakupp --ffi-info            Show which FFI backend NativeCall will use\n"
"\n"
"Environment:\n"
"  RAKULIB=dir1,dir2            Extra module search dirs (like -I); ',' or ':'\n"
"  RAKUPP_PARALLEL=1            Run start/worker threads on all cores (true CPU\n"
"                               parallelism; default coordinates under a GIL)\n"
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
"  ROAST=/path/to/roast rakupp tools/run-roast.raku [PATH-SUBSTRING]\n";
            return 0;
        }
    }
    if (mode == Mode::Version) {
#ifndef RAKUPP_VERSION
#define RAKUPP_VERSION "0.0.0"
#endif
            std::cout << "Raku++ (rakupp) " RAKUPP_VERSION
                         " — a Raku interpreter and compiler in C++ (implements Raku 6.d, with 6.e features)\n";
        return 0;
    }
    // Which FFI backend NativeCall will use. The first question to ask of a
    // native-call bug report, and how the test suite tells its two CI legs
    // apart, so it is worth a flag of its own.
    if (mode == Mode::FfiInfo) { std::cout << ffi::describe() << "\n"; return 0; }

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
            std::cout << "ok " << fileName << "  (" << blob.size() << " bytes)\n";
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
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            (void)parser.parseProgram();
        } catch (const ParseError& e) {
            std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
            return 2;
        }
        std::cout << "Syntax OK\n";
        return 0;
    }

    // --lint : static analysis only — parse, run the linter, report; never execute.
    // Prints `FILE:LINE: warning|note: message [rule]`. Exits 1 if any warning
    // was found (0 if only notes or nothing), 2 on a parse error.
    if (mode == Mode::Lint) {
        if (!haveSrc) { std::cerr << "Usage: rakupp --lint (FILE | -e CODE) [-q]\n"; return 4; }
        Program prog;
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            prog = parser.parseProgram();
        } catch (const ParseError& e) {
            std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
            return 2;
        }
        auto findings = lintProgram(prog);
        int warns = 0, notes = 0;
        for (auto& f : findings) {
            (f.severity == 'W' ? warns : notes)++;
            std::cout << fileName << ":" << f.line << ": "
                      << (f.severity == 'W' ? "warning" : "note") << ": "
                      << f.message << " [" << f.rule << "]\n";
        }
        if (!quiet) {
            if (findings.empty()) std::cerr << "rakupp --lint: no issues found in " << fileName << "\n";
            else std::cerr << "rakupp --lint: " << warns << " warning"
                           << (warns == 1 ? "" : "s") << ", " << notes << " note"
                           << (notes == 1 ? "" : "s") << " in " << fileName << "\n";
        }
        return warns ? 1 : 0;
    }

    // --cpp : print the C++ that `--exe` would transpile the program to (to stdout)
    if (mode == Mode::Cpp) {
        if (!haveSrc) { std::cerr << "Usage: rakupp --cpp (FILE | -e CODE) [-O]\n"; return 4; }
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            parser.libPaths_ = effectiveSearchPath(libPaths);
            Program prog = parser.parseProgram();
            // same module scan as --exe, so what this prints is what --exe compiles
            std::set<std::string> moduleExports;
            collectModuleGraph(prog, effectiveSearchPath(libPaths), &moduleExports);
            std::cout << transpileToCpp(prog, optimize, absPath(fileName), moduleExports);
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
        if (mode == Mode::Exe) return compileNative(src, fileName, outPath, exePath, optimize, ccOpt, libPaths);
        if (mode == Mode::Aot) return compileAotAst(src, fileName, outPath, exePath, libPaths);
        return compileToExe(src, fileName, outPath, exePath, libPaths); // --bundle
    }

    // ---- run (the default mode) --------------------------------------------
    if (!haveSrc) {
        if (rakupp::stdinIsTerminal() || rakupp::replForced()) {
            // Bare `rakupp` at a terminal: an interactive session.
            return rakupp::rakuppRepl(exePath, libPaths);
        }
        // Bare `rakupp` with stdin redirected — `echo … | rakupp`, `rakupp < f.raku`
        // — is a whole program arriving on stdin, exactly as before.
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        src = ss.str();
    }
    if (optN || optP) { // wrap the program in a record loop (files in @*ARGS, else $*IN)
        // -a: @F, split by .words or the -F separator, declared before the body.
        // A literal separator is emitted as a double-quoted Raku string with
        // per-char escaping (control chars as \x[..] — a raw NUL would truncate
        // the generated source); the /…/ form is spliced as Raku regex text.
        std::string pre;
        if (optA) {
            if (!haveF) pre = "my @F = .words; ";
            else if (fieldSepRegex) pre = "my @F = $_.split(/" + fieldSep + "/); ";
            else {
                std::string lit;
                for (unsigned char c : fieldSep) {
                    if (c == '"' || c == '\\' || c == '$' || c == '@' || c == '{' || c == '}') { lit += '\\'; lit += (char)c; }
                    else if (c < 0x20) { char b[16]; snprintf(b, sizeof b, "\\x[%02x]", c); lit += b; }
                    else lit += (char)c;
                }
                pre = "my @F = $_.split(\"" + lit + "\"); ";
            }
        }
        if (recMode == 0777) {
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
    return rakuppRunBigStack(src, std::move(progArgs), fileName, exePath, libPaths);
}
