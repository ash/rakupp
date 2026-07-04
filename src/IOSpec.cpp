// IO::Spec::{Unix,QNX,Win32,Cygwin} — path-manipulation class methods.
//
// These are pure string algorithms (no real I/O except tmpdir/path, which read
// the environment). They are built-in "type object" methods: `IO::Spec::Unix`
// resolves to a VT::Type value, and methodCall routes IO::Spec::* invocants
// here. Semantics mirror Rakudo's IO::Spec and are pinned by S32-io/io-spec-*.t.
#include "Interpreter.h"
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>

namespace rakupp {

namespace {

// split on `sep`, keeping empty fields (so "a//b" -> ["a","","b"], "" -> [""]).
std::vector<std::string> splitAll(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
// meaningful path segments: drop empty and "." components.
std::vector<std::string> segs(const std::string& s) {
    std::vector<std::string> out;
    for (auto& p : splitAll(s, '/')) if (!p.empty() && p != ".") out.push_back(p);
    return out;
}
bool isAbs(const std::string& p) { return !p.empty() && p[0] == '/'; }

// canonpath. Without :parent, "." and redundant "/" collapse but ".." is kept
// (except leading ".." on an absolute path, which can't ascend past root). With
// :parent, ".." is resolved against the preceding real segment.
std::string canon(const std::string& path, bool parent, bool qnx = false) {
    if (path.empty()) return "";
    bool abs = isAbs(path);
    // QNX/POSIX: a path beginning with exactly two slashes keeps them.
    bool dslash = qnx && path.size() >= 2 && path[0] == '/' && path[1] == '/' && (path.size() < 3 || path[2] != '/');
    std::vector<std::string> stack;
    for (auto& seg : splitAll(path, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (parent) {
                if (!stack.empty() && stack.back() != "..") { stack.pop_back(); continue; }
                if (abs && !dslash) continue;   // absolute: drop leading ".." (but "//" root keeps them)
                stack.push_back(seg); continue; // relative / "//": keep unresolved ".."
            }
            if (abs && stack.empty()) continue; // absolute leading "..": drop
            stack.push_back(seg); continue;     // otherwise keep ".."
        }
        stack.push_back(seg);
    }
    std::string r;
    for (size_t i = 0; i < stack.size(); i++) { if (i) r += '/'; r += stack[i]; }
    std::string prefix = dslash ? "//" : "/";
    if (r.empty()) return abs ? prefix : (parent ? "." : "");
    return abs ? prefix + r : r;
}

// catdir(@parts) == canonpath( (@parts, '').join('/') ).
// Appending '/' after each part yields exactly parts.join('/') ~ '/'.
std::string catdir(ValueList& args) {
    if (args.empty()) return "";
    std::string joined;
    for (auto& a : args) { joined += a.toStr(); joined += '/'; }
    return canon(joined, false);
}

std::string cwdStr() { char buf[4096]; return getcwd(buf, sizeof buf) ? std::string(buf) : "."; }

std::string rel2abs(const std::string& path, const std::string& base) {
    if (isAbs(path)) return canon(path, false);
    std::string b = isAbs(base) ? base : (cwdStr() + "/" + base);
    return canon(b + "/" + path + "/", false);
}

} // namespace

bool ioSpecMethod(Interpreter& I, const std::string& cls, const std::string& m, ValueList& args, Value& out) {
    auto A = [&](size_t i) { return i < args.size() ? args[i].toStr() : std::string(); };

    // curupdir matcher object: .ACCEPTS is true for anything but "." / ".."
    if (cls == "IO::Spec::__curupdir") {
        if (m == "ACCEPTS") { std::string x = A(0); out = Value::boolean(x != "." && x != ".."); return true; }
        return false;
    }
    // Only Unix/QNX fully implemented for now; unknown IO::Spec::* falls through.
    bool unixLike = (cls == "IO::Spec::Unix" || cls == "IO::Spec::QNX" || cls == "IO::Spec");
    if (!unixLike) return false;

    if (m == "curdir")  { out = Value::str("."); return true; }
    if (m == "updir")   { out = Value::str(".."); return true; }
    if (m == "rootdir") { out = Value::str("/"); return true; }
    if (m == "devnull") { out = Value::str("/dev/null"); return true; }
    if (m == "curupdir"){ out = Value::typeObj("IO::Spec::__curupdir"); return true; }

    if (m == "canonpath") {
        bool parent = false;
        for (auto& a : args) if (a.t == VT::Pair && a.s == "parent") parent = a.pairVal ? a.pairVal->truthy() : true;
        std::string p = args.empty() ? "" : args[0].toStr();
        if (!args.empty() && args[0].t == VT::Type) p = ""; // undefined (Any) -> ''
        out = Value::str(canon(p, parent, cls == "IO::Spec::QNX")); return true;
    }
    if (m == "catdir")  { out = Value::str(catdir(args)); return true; }
    if (m == "catfile") { out = Value::str(catdir(args)); return true; } // Unix: catfile == catdir
    if (m == "is-absolute") { out = Value::boolean(isAbs(A(0))); return true; }

    if (m == "splitpath") {
        std::string p = A(0); size_t s = p.rfind('/');
        std::string dir = s == std::string::npos ? "" : p.substr(0, s + 1);
        std::string file = s == std::string::npos ? p : p.substr(s + 1);
        if (file == "." || file == "..") { dir = p; file = ""; } // trailing "."/".." is part of the dir
        out = Value::array({Value::str(""), Value::str(dir), Value::str(file)});
        return true;
    }
    if (m == "splitdir") {
        ValueList v; for (auto& p : splitAll(A(0), '/')) v.push_back(Value::str(p));
        out = Value::array(v); return true;
    }
    if (m == "split") {
        std::string p = A(0);
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        std::string dir, base;
        if (p == "/") { dir = "/"; base = "/"; }
        else {
            size_t s = p.rfind('/');
            if (s == std::string::npos) { dir = "."; base = p; }
            else { base = p.substr(s + 1); std::string d = p.substr(0, s); dir = d.empty() ? "/" : d; }
        }
        Value h = Value::makeHash();
        (*h.hash)["volume"] = Value::str("");
        (*h.hash)["dirname"] = Value::str(dir);
        (*h.hash)["basename"] = Value::str(base);
        out = h; return true;
    }
    if (m == "catpath" || m == "join") {
        std::string dir = A(1), file = A(2);
        if (m == "join") { if (dir == "/" && file == "/") dir = ""; else if (dir == ".") dir = ""; }
        std::string r;
        if (!dir.empty() && !file.empty() && dir.back() != '/' && file[0] != '/') r = dir + "/" + file;
        else r = dir + file;
        out = Value::str(r); return true;
    }
    if (m == "abs2rel") {
        std::string path = canon(A(0), false), base = canon(A(1), false);
        auto ps = segs(path), bs = segs(base);
        size_t i = 0; while (i < ps.size() && i < bs.size() && ps[i] == bs[i]) i++;
        std::vector<std::string> res;
        for (size_t k = i; k < bs.size(); k++) res.push_back("..");
        for (size_t k = i; k < ps.size(); k++) res.push_back(ps[k]);
        std::string r; for (size_t k = 0; k < res.size(); k++) { if (k) r += '/'; r += res[k]; }
        out = Value::str(r.empty() ? "." : r); return true;
    }
    if (m == "rel2abs") {
        std::string base = args.size() > 1 ? A(1) : cwdStr();
        out = Value::str(rel2abs(A(0), base)); return true;
    }
    if (m == "basename") {
        std::string p = A(0); size_t s = p.rfind('/');
        out = Value::str(s == std::string::npos ? p : p.substr(s + 1)); return true;
    }
    if (m == "extension") {
        std::string p = A(0); size_t d = p.rfind('.');
        out = Value::str(d == std::string::npos ? "" : p.substr(d + 1)); return true;
    }
    if (m == "path") {
        std::string pathenv; bool have = false;
        Value* env = I.global_->find("%*ENV");
        if (env && env->hash) { auto it = env->hash->find("PATH"); if (it != env->hash->end()) { pathenv = it->second.toStr(); have = true; } }
        if (!have || pathenv.empty()) { out = Value::list({}); return true; }
        ValueList res;
        for (auto& part : splitAll(pathenv, ':')) res.push_back(Value::str(part.empty() ? "." : part));
        out = Value::list(res); return true;
    }
    if (m == "tmpdir") {
        const char* t = getenv("TMPDIR"); std::string d = (t && *t) ? t : "/tmp";
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        Value v = Value::str(d); v.hashKind = "IO"; out = v; return true;
    }
    return false;
}

} // namespace rakupp
