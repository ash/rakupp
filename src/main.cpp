#include "Runtime.h"
#include <cstdint>
#include <cstdio>
#include "Codegen.h"
#include "Lexer.h"
#include "Parser.h"
#include "Highlight.h"
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
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

// The native compiler for --exe/--bundle: $CXX, else `cl` on Windows, `c++` elsewhere.
static std::string nativeCxx() {
    const char* e = std::getenv("CXX");
    if (e && *e) return e;
#ifdef _WIN32
    return "cl";
#else
    return "c++";
#endif
}

// Build the compile-and-link command for a generated source + the runtime
// archive, in the dialect of the chosen compiler. `opt` is the Unix-style
// optimization flag ("-O2", "-O0", …); it is translated for cl.
static std::string compileCmd(const std::string& cxx, const std::string& opt,
                              const std::string& inc, const std::string& in,
                              const std::string& lib, const std::string& out) {
    if (msvcStyle(cxx)) {
        std::string o = opt == "-O0" ? "/Od" : opt == "-O1" ? "/O1" : "/O2";
        std::string c = cxx + " /nologo /std:c++17 /EHsc /w " + o;
        if (!inc.empty()) c += " /I " + shq(inc);
        c += " " + shq(in) + " " + shq(lib) + " /Fe:" + shq(out) + " ws2_32.lib";
        return c;
    }
    std::string c = cxx + " -std=c++17 " + (opt.empty() ? "-O2" : opt) + " -w -pthread -Wl,-w";
    if (!inc.empty()) c += " -I " + shq(inc);
    c += " " + shq(in) + " " + shq(lib) + " -o " + shq(out);
#ifdef _WIN32
    c += " -lws2_32"; // MinGW: the runtime's sockets need Winsock
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
    DWORD wn = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (wn > 0 && wn < sizeof(buf)) return buf;
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
    dirs.push_back({d + "/../lib", d + "/../include/rakupp"}); // installed prefix
    // the archive is librakupp_rt.a (Unix toolchains) or rakupp_rt.lib (MSVC) —
    // accept whichever is present (github issue #1)
    for (auto& c : dirs)
        for (const char* nm : {"/librakupp_rt.a", "/rakupp_rt.lib"})
            if (std::ifstream(c.first + nm).good()) { lib = c.first + nm; inc = c.second; return true; }
    lib = dirs.front().first + (msvcStyle(nativeCxx()) ? "/rakupp_rt.lib" : "/librakupp_rt.a");
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
static int compileToExe(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);

    // The runtime static library sits next to this rakupp binary.
    std::string lib, inc;
    if (!findRuntime(selfExe, lib, inc)) {
        std::cerr << "Cannot find runtime library (looked for " << lib << ")\n"
                  << "(build rakupp first: cmake --build build; or set RAKUPP_HOME)\n";
        return 5;
    }

    // Generate the stub. The program source is embedded as a raw byte array so
    // that any content (quotes, delimiters, binary) round-trips exactly.
    std::string stubPath = outPath + ".rakupp.stub.cpp";
    {
        std::ofstream stub(stubPath);
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
                " const std::string&, const std::string&, const std::vector<std::string>&); }\n";
        stub << "static const unsigned char SRC[] = {";
        for (size_t i = 0; i < src.size(); i++) { if (i) stub << ","; stub << (int)(unsigned char)src[i]; }
        if (src.empty()) stub << "0"; // avoid zero-size array; length tracked separately
        stub << "};\n";
        stub << "static const unsigned long SRC_LEN = " << src.size() << "UL;\n";
        stub << "int main(int argc, char** argv) {\n"
                "  std::string src(reinterpret_cast<const char*>(SRC), SRC_LEN);\n"
                "  std::vector<std::string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);\n"
                "  std::string exe = argc > 0 ? argv[0] : \"program\";\n"
                "  char rp[4096]; if (RAKUPP_REALPATH(exe.c_str(), rp)) exe = rp;\n"
                "  return rakupp::rakuppRunBigStack(src, args, " << cppstr(baseOf(srcName)) << ", exe, {});\n"
                "}\n";
    }

    std::string cmd = compileCmd(nativeCxx(), "-O2", "", stubPath, lib, outPath);
    int rc = std::system(cmd.c_str());
    std::remove(stubPath.c_str());
    if (rc != 0) { std::cerr << "Compilation failed (compiler exit " << rc << ")\n"; return 5; }
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

// back with a clear message if the program uses an unsupported construct.
static int compileNative(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe, bool optimize = false, const std::string& ccOpt = "-O2") {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);

    std::string cpp;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        Program prog = parser.parseProgram();
        cpp = transpileToCpp(prog, optimize, absPath(srcName));
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        return 2;
    } catch (const CodegenError& e) {
        // Any construct outside the native subset: fall back to AOT bundling so
        // `--exe` still produces a correct binary for the full language.
        std::cerr << "note: " << e.msg << " — not yet natively compiled; "
                     "bundling the whole program with the interpreter instead.\n";
        return compileToExe(src, srcName, outPath, selfExe);
    }

    std::string lib, inc;
    if (!findRuntime(selfExe, lib, inc)) {
        std::cerr << "Cannot find runtime library (looked for " << lib << ")\n"
                  << "(build rakupp first: cmake --build build; or set RAKUPP_HOME)\n";
        return 5;
    }

    std::string genPath = outPath + ".rakupp.gen.cpp";
    { std::ofstream g(genPath); if (!g) { std::cerr << "Cannot write " << genPath << "\n"; return 5; } g << cpp; }

    std::string cmd = compileCmd(nativeCxx(), ccOpt, inc, genPath, lib, outPath);
    int rc = std::system(cmd.c_str());
    if (!std::getenv("RAKUPP_KEEPGEN")) std::remove(genPath.c_str());
    if (rc != 0) { std::cerr << "Compilation failed (compiler exit " << rc << ")\n"; return 5; }
    std::cerr << "Compiled (native) " << srcName << " -> " << outPath << "\n";
    return 0;
}

// Real AOT: parse ahead of time, then emit C++ that rebuilds the AST at startup
// and interprets it (no lexing/parsing in the produced binary). Falls back to
// source bundling for any construct the AST emitter can't reconstruct.
static int compileAotAst(const std::string& src, const std::string& srcName, std::string outPath, const std::string& selfExe) {
    if (outPath.empty()) outPath = defaultOut(srcName);
    ensureExeSuffix(outPath);
    std::string cpp, finish;
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        finish = lexer.finishData();
        Program prog = parser.parseProgram();
        std::ostringstream ss;
        emitAstProgram(prog, ss, baseOf(srcName), finish);
        cpp = ss.str();
    } catch (const ParseError& e) {
        std::cerr << "===SORRY!=== Parse error at line " << e.line << ": " << e.what() << "\n";
        return 2;
    } catch (const AstEmitError& e) {
        std::cerr << "note: " << e.msg << " — falling back to source bundling.\n";
        return compileToExe(src, srcName, outPath, selfExe);
    }

    std::string lib, inc;
    if (!findRuntime(selfExe, lib, inc)) {
        std::cerr << "Cannot find runtime library (looked for " << lib << ")\n"
                  << "(build rakupp first: cmake --build build; or set RAKUPP_HOME)\n";
        return 5;
    }
    std::string genPath = outPath + ".rakupp.ast.cpp";
    { std::ofstream g(genPath); if (!g) { std::cerr << "Cannot write " << genPath << "\n"; return 5; } g << cpp; }
    std::string cmd = compileCmd(nativeCxx(), "-O2", inc, genPath, lib, outPath);
    int rc = std::system(cmd.c_str());
    if (!std::getenv("RAKUPP_KEEPGEN")) std::remove(genPath.c_str());
    if (rc != 0) { std::cerr << "Compilation failed (compiler exit " << rc << ")\n"; return 5; }
    std::cerr << "Compiled (AOT) " << srcName << " -> " << outPath << "\n";
    return 0;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string exePath = selfExePath(argv[0]); // resolve the real binary (argv[0] may be a bare PATH name)
    { char rp[4096]; if (realpath(exePath.c_str(), rp)) exePath = rp; }

    // --help / -h : print usage and exit.  --version / -V : print the version.
    if (argc >= 2) {
        std::string a1 = argv[1];
        if (a1 == "--help" || a1 == "-h") {
            std::cout <<
"Raku++ — a Raku interpreter and compiler in C++\n"
"\n"
"Usage:\n"
"  rakupp FILE [ARGS...]        Run a Raku program from a file\n"
"  rakupp -e 'CODE' [ARGS...]   Run a one-liner\n"
"  rakupp                       Read a program from standard input\n"
"\n"
"Options:\n"
"  -I <path>                    Add a directory to the module search path\n"
"                               (repeatable; -I<path> also works)\n"
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
"  rakupp --ast SRC             Print the parsed AST as an indented tree\n"
"  rakupp --cpp SRC [-O]        Print the C++ that --exe would transpile to\n"
"                               (add -O to print the optimized codegen instead)\n"
"  rakupp --highlight [SRC]     Syntax-highlight Raku (--html [default] / --ansi;\n"
"                               reads stdin if no SRC), e.g. as a pygmentize drop-in\n"
"  rakupp --help, -h            Show this help\n"
"  rakupp --version, -V         Show the version\n"
"\n"
"Environment:\n"
"  RAKULIB=dir1:dir2            Extra module search dirs (like -I)\n"
"  RAKUPP_PARALLEL=1            Run start/worker threads on all cores (true CPU\n"
"                               parallelism; default coordinates under a GIL)\n"
"  RAKUPP_DUMPTOKENS=1          Dump the lexer token stream before running\n"
"  RAKUPP_HOME=dir              Where --exe finds its runtime (dir/lib + dir/include/rakupp);\n"
"                               only needed if rakupp is moved away from its build/install tree\n"
"\n"
"Run the spec-test harness (self-hosted, in Raku):\n"
"  ROAST=/path/to/roast rakupp tools/run-roast.raku [PATH-SUBSTRING]\n";
            return 0;
        }
        if (a1 == "--version" || a1 == "-V") {
#ifndef RAKUPP_VERSION
#define RAKUPP_VERSION "0.0.0"
#endif
            std::cout << "Raku++ (rakupp) " RAKUPP_VERSION
                         " — a Raku interpreter and compiler in C++ (implements Raku 6.d, with 6.e features)\n";
            return 0;
        }
    }

    // --highlight [--html|--ansi] [FILE | -e CODE | -]  : syntax-highlight Raku
    // and exit. Default format is html (the course consumer); `-`/no file reads stdin,
    // so it drops in for `pygmentize -f html -l raku`.
    if (argc >= 2 && std::string(argv[1]) == "--highlight") {
        std::string fmt = "html", src, srcFile;
        bool haveSrc = false;
        for (int k = 2; k < argc; k++) {
            std::string a = argv[k];
            if (a == "--html") fmt = "html";
            else if (a == "--ansi" || a == "--terminal") fmt = "ansi";
            else if (a == "-e" && k + 1 < argc) { src = argv[++k]; haveSrc = true; }
            else if (a.rfind("-e", 0) == 0 && a.size() > 2) { src = a.substr(2); haveSrc = true; } // attached form: -e"say 123"
            else if (a == "-") { /* explicit stdin */ }
            else if (!haveSrc && a[0] != '-') { srcFile = a; }
        }
        if (!haveSrc) {
            std::istream* in = &std::cin;
            std::ifstream f;
            if (!srcFile.empty()) {
                f.open(srcFile);
                if (!f) { std::cerr << "Cannot open file: " << srcFile << "\n"; return 4; }
                in = &f;
            }
            std::ostringstream ss; ss << in->rdbuf(); src = ss.str();
        }
        std::cout << highlight(src, fmt);
        return 0;
    }

    // --ast FILE | --ast -e CODE : print the parsed AST and exit
    // (--dump-ast is kept as a backward-compatible alias)
    if (argc >= 2 && (std::string(argv[1]) == "--ast" || std::string(argv[1]) == "--dump-ast")) {
        std::string src, fname = "-e";
        if (argc >= 4 && std::string(argv[2]) == "-e") src = argv[3];
        else if (argc >= 3 && std::string(argv[2]).rfind("-e", 0) == 0 &&
                 std::string(argv[2]).size() > 2) src = std::string(argv[2]).substr(2); // -e"say 123"
        else if (argc >= 3) {
            std::ifstream in(argv[2]);
            if (!in) { std::cerr << "Cannot open file: " << argv[2] << "\n"; return 4; }
            std::ostringstream ss; ss << in.rdbuf(); src = ss.str(); fname = argv[2];
        } else { std::cerr << "Usage: rakupp --ast FILE | --ast -e CODE\n"; return 4; }
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

    // -c : syntax check only (like Rakudo's -c) — parse, report, never execute
    if (argc >= 2 && std::string(argv[1]) == "-c") {
        std::string src, fname = "-e"; bool haveSrc = false;
        for (int i = 2; i < argc; i++) {
            std::string a = argv[i];
            if (a == "-e" && i + 1 < argc) { src = argv[++i]; haveSrc = true; }
            else if (a.rfind("-e", 0) == 0 && a.size() > 2) { src = a.substr(2); haveSrc = true; } // attached form: -e"say 123"
            else if (!haveSrc) {
                std::ifstream in(a);
                if (!in) { std::cerr << "Cannot open file: " << a << "\n"; return 4; }
                std::ostringstream ss; ss << in.rdbuf(); src = ss.str(); fname = a; haveSrc = true;
            }
        }
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
    // --cpp : print the C++ that `--exe` would transpile the program to (to stdout)
    if (argc >= 2 && (std::string(argv[1]) == "--cpp" || std::string(argv[1]) == "--emit-cpp")) {
        std::string src, fname = "-e"; bool haveSrc = false, optimize = false;
        for (int i = 2; i < argc; i++) {
            std::string a = argv[i];
            if (a == "-O" || a == "-O1") optimize = true;
            else if (a == "-e" && i + 1 < argc) { src = argv[++i]; haveSrc = true; }
            else if (a.rfind("-e", 0) == 0 && a.size() > 2) { src = a.substr(2); haveSrc = true; } // attached form: -e"say 123"
            else if (!haveSrc) {
                std::ifstream in(a);
                if (!in) { std::cerr << "Cannot open file: " << a << "\n"; return 4; }
                std::ostringstream ss; ss << in.rdbuf(); src = ss.str(); fname = a; haveSrc = true;
            }
        }
        if (!haveSrc) { std::cerr << "Usage: rakupp --cpp (FILE | -e CODE) [-O]\n"; return 4; }
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            Program prog = parser.parseProgram();
            std::cout << transpileToCpp(prog, optimize, absPath(fname));
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
    //   each: (FILE | -e CODE) [-o OUT]
    {
        std::string mode = argc >= 2 ? argv[1] : "";
        if (mode == "--bundle" || mode == "--aot" || mode == "--exe") {
            std::string src, srcName, outPath, ccOpt = "-O2";
            bool haveSrc = false, optimize = false;
            for (int i = 2; i < argc; i++) {
                std::string a = argv[i];
                if (a == "-o" && i + 1 < argc) { outPath = argv[++i]; }
                else if (a.rfind("-o", 0) == 0 && a.size() > 2) { outPath = a.substr(2); }
                // any -O… turns on the codegen optimizer; a suffix (-O3/-Os/-Ofast/…)
                // is forwarded to the C++ compiler for the generated binary.
                else if (a.rfind("-O", 0) == 0) { optimize = true; if (a.size() > 2) ccOpt = a; }
                else if (a == "-e" && i + 1 < argc) { src = argv[++i]; srcName = "-e"; haveSrc = true; }
                else if (a.rfind("-e", 0) == 0 && a.size() > 2) { src = a.substr(2); srcName = "-e"; haveSrc = true; }
                else if (!haveSrc) { // a source file
                    std::ifstream in(a);
                    if (!in) { std::cerr << "Cannot open file: " << a << "\n"; return 4; }
                    std::ostringstream ss; ss << in.rdbuf(); src = ss.str(); srcName = a; haveSrc = true;
                }
            }
            if (!haveSrc) { std::cerr << "Usage: rakupp " << mode << " (FILE | -e CODE) [-o OUT] [-O[level]]\n"; return 4; }
            if (mode == "--exe") return compileNative(src, srcName, outPath, exePath, optimize, ccOpt);
            if (mode == "--aot") return compileAotAst(src, srcName, outPath, exePath);
            return compileToExe(src, srcName, outPath, exePath); // --bundle
        }
    }

    std::string src;
    std::string fileName = "-e";
    std::vector<std::string> args;
    // Collect leading -I <path> / -I<path> lib dirs (rakudo-compatible), then
    // treat everything from the first non -I token as the program + its args.
    std::vector<std::string> libPaths;
    std::vector<std::string> rest; // argv[1..], with -I options removed
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-I") {           // `-I PATH` : path is the next argument
            if (i + 1 < argc) libPaths.push_back(argv[++i]);
        } else if (a.rfind("-I", 0) == 0) { // `-IPATH` : path attached to the flag
            libPaths.push_back(a.substr(2));
        } else if (a == "--doc") { // render the program's POD to text after running
            rakupp::rakuppSetDocMode(true);
        } else {
            for (int j = i; j < argc; j++) rest.push_back(argv[j]);
            break;
        }
    }
    // perl-style line-loop flags: -n (loop over $*IN.lines), -p (loop + print $_),
    // combinable with -e as -ne / -pe / -np (the trailing 'e' means -e follows).
    bool optN = false, optP = false;
    while (!rest.empty()) {
        const std::string& a = rest[0];
        if (a.size() < 2 || a[0] != '-' || a[1] == '-') break;
        if (a.find_first_not_of("npe", 1) != std::string::npos) break; // not a pure n/p/e combo
        bool hasE = a.find('e') != std::string::npos;
        if (a.find('n') != std::string::npos) optN = true;
        if (a.find('p') != std::string::npos) optP = true;
        if (hasE) { rest[0] = "-e"; break; }   // let the -e handling below take the code
        rest.erase(rest.begin());              // pure -n / -p / -np : consume and continue
    }
    size_t nrest = rest.size();
    if (nrest >= 1 && rest[0].rfind("-e", 0) == 0) {
        std::string a1 = rest[0];
        if (a1 == "-e") {              // `-e CODE` : code is the next argument
            if (nrest < 2) { std::cerr << "No code given for -e\n"; return 4; }
            src = rest[1];
            for (size_t i = 2; i < nrest; i++) args.push_back(rest[i]);
        } else {                       // `-e'CODE'` / `-eCODE` : code attached to the flag
            src = a1.substr(2);
            for (size_t i = 1; i < nrest; i++) args.push_back(rest[i]);
        }
    } else if (nrest >= 1 && rest[0] == "-") { // `-` : read the program from STDIN
        std::ostringstream ss; ss << std::cin.rdbuf(); src = ss.str();
        fileName = "-";
        for (size_t i = 1; i < nrest; i++) args.push_back(rest[i]);
    } else if (nrest >= 1 && rest[0].size() > 1 && rest[0][0] == '-') {
        // an unrecognized command-line option (the known ones — -I, --doc, -e,
        // -n/-p, -, --help/-h, --version — are handled above). Rakudo prints a
        // usage banner to STDERR and exits 0 without attempting to run anything.
        std::cerr << "Illegal option " << rest[0].substr(0, rest[0].find('=')) << "\n";
        std::cerr << "  [switches] [--] [programfile] [arguments]\n";
        return 0;
    } else if (nrest >= 1) {
        struct stat st;
        if (stat(rest[0].c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            std::cerr << "Can not run directory " << rest[0] << "\n"; return 1;
        }
        std::ifstream in(rest[0]);
        if (!in) { std::cerr << "Could not open " << rest[0] << "\n"; return 1; }
        std::ostringstream ss;
        ss << in.rdbuf();
        src = ss.str();
        fileName = rest[0];
        for (size_t i = 1; i < nrest; i++) args.push_back(rest[i]);
    } else {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        src = ss.str();
    }
    if (optN || optP) // wrap the program in a line loop over $*ARGFILES (files in @*ARGS, else $*IN)
        src = "for lines() -> $_ is copy {\n" + src + "\n" + (optP ? "$_.say;\n" : "") + "}\n";
    return rakuppRunBigStack(src, std::move(args), fileName, exePath, libPaths);
}
