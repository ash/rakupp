// IO::Spec::{Unix,QNX,Win32,Cygwin} — path-manipulation class methods.
//
// These are pure string algorithms (no real I/O except tmpdir/path, which read
// the environment). They are built-in "type object" methods: `IO::Spec::Unix`
// resolves to a VT::Type value, and methodCall routes IO::Spec::* invocants
// here. Semantics mirror Rakudo's IO::Spec and are pinned by S32-io/io-spec-*.t.
#include "Interpreter.h"
#include "Platform.h"
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

// ───────────────────────────── Win32 ─────────────────────────────
// Both '/' and '\' separate; output uses '\'. A volume is a drive ("C:") or a
// UNC prefix ("\\server[\share]" — exactly two leading separators followed by a
// real name; "." / ".." can't be a server). Semantics pinned by io-spec-win.t.

bool wsep(char c) { return c == '/' || c == '\\'; }

struct WinVol { std::string vol; std::string rest; bool abs = false; };

// normalize: uppercase the drive / respell UNC with backslashes (canon paths);
// otherwise keep the original text (splitpath/split report the volume as typed).
WinVol winVolume(const std::string& p, bool normalize) {
    WinVol r;
    if (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':') {
        r.vol = normalize ? std::string(1, (char)std::toupper((unsigned char)p[0])) + ":" : p.substr(0, 2);
        r.rest = p.substr(2);
        r.abs = !r.rest.empty() && wsep(r.rest[0]);
        return r;
    }
    if (p.size() >= 3 && wsep(p[0]) && wsep(p[1]) && !wsep(p[2])) {
        size_t i = 2;
        while (i < p.size() && !wsep(p[i])) i++;
        std::string server = p.substr(2, i - 2);
        if (server != "." && server != "..") {
            size_t j = i; while (j < p.size() && wsep(p[j])) j++;
            size_t k = j; while (k < p.size() && !wsep(p[k])) k++;
            std::string share = p.substr(j, k - j);
            if (!share.empty()) { r.vol = normalize ? "\\\\" + server + "\\" + share : p.substr(0, k); r.rest = p.substr(k); }
            else                { r.vol = normalize ? "\\\\" + server : p.substr(0, i);                r.rest = p.substr(i); }
            r.abs = true;
            return r;
        }
    }
    if (!p.empty() && wsep(p[0])) r.abs = true;
    r.rest = p;
    return r;
}

// The shared canonicalizer behind canonpath/catdir/catfile/rel2abs. Volume and
// rootedness come from the FIRST part only; an empty first part roots the path
// (mirroring Rakudo's join-then-parse, e.g. catdir('','','..') is '\').
// Without :parent, '..' is kept except leading '..' on a rooted path (which
// cannot ascend past the root/UNC share); with :parent it resolves.
std::string winCanonCat(const std::vector<std::string>& parts, bool parent, bool emptyFirstAbs) {
    if (parts.empty()) return "";
    WinVol v = winVolume(parts[0], true);
    bool abs = v.abs || (emptyFirstAbs && parts[0].empty());
    bool unc = !v.vol.empty() && v.vol[0] == '\\';
    std::vector<std::string> stack;
    auto feed = [&](const std::string& s) {
        std::string cur;
        auto flush = [&]() {
            if (cur.empty() || cur == ".") { cur.clear(); return; }
            if (cur == "..") {
                if (parent) {
                    if (!stack.empty() && stack.back() != "..") stack.pop_back();
                    else if (!abs) stack.push_back("..");
                }
                else if (!(abs && stack.empty())) stack.push_back("..");
            }
            else stack.push_back(cur);
            cur.clear();
        };
        for (char c : s) { if (wsep(c)) flush(); else cur += c; }
        flush();
    };
    feed(v.rest);
    for (size_t i = 1; i < parts.size(); i++) feed(parts[i]);
    std::string joined;
    for (size_t i = 0; i < stack.size(); i++) { if (i) joined += '\\'; joined += stack[i]; }
    if (abs) {
        if (joined.empty()) return unc ? v.vol : v.vol + "\\";
        return v.vol + "\\" + joined;
    }
    if (joined.empty()) return !v.vol.empty() ? v.vol : (parent ? "." : "");
    return v.vol + joined;
}

} // namespace

// IO::Spec::Win32 — the full method set, pinned by S32-io/io-spec-win.t.
static bool winSpecMethod(Interpreter& I, const std::string& m, ValueList& args, Value& out) {
    std::vector<std::string> pos;
    bool parentFlag = false, nofileFlag = false;
    for (auto& a : args) {
        if (a.t == VT::Pair) {
            bool on = a.pairVal ? a.pairVal->truthy() : true;
            if (a.s == "parent") parentFlag = on;
            else if (a.s == "nofile") nofileFlag = on;
        }
        else if (a.t == VT::Type) pos.push_back(""); // undefined (Any) → ''
        else pos.push_back(a.toStr());
    }
    auto P = [&](size_t i) { return i < pos.size() ? pos[i] : std::string(); };

    if (m == "curdir")  { out = Value::str("."); return true; }
    if (m == "updir")   { out = Value::str(".."); return true; }
    if (m == "rootdir") { out = Value::str("\\"); return true; }
    if (m == "devnull") { out = Value::str("nul"); return true; }
    if (m == "curupdir"){ out = Value::typeObj("IO::Spec::__curupdir"); return true; }

    if (m == "canonpath") {
        std::string p = P(0);
        out = Value::str(p.empty() ? "" : winCanonCat({p}, parentFlag, false));
        return true;
    }
    if (m == "catdir" || m == "catfile") {
        if (pos.empty()) { out = Value::str(""); return true; }
        out = Value::str(winCanonCat(pos, false, /*emptyFirstAbs=*/true));
        return true;
    }
    if (m == "is-absolute") {
        const std::string& p = P(0);
        bool r = (!p.empty() && wsep(p[0])) ||
                 (p.size() >= 3 && std::isalpha((unsigned char)p[0]) && p[1] == ':' && wsep(p[2]));
        out = Value::boolean(r); return true;
    }
    if (m == "splitpath") {
        std::string p = P(0);
        WinVol v = winVolume(p, false);
        std::string rest = v.rest, dir, file;
        if (nofileFlag) dir = rest;
        else {
            size_t s = rest.find_last_of("/\\");
            if (s == std::string::npos) file = rest;
            else { dir = rest.substr(0, s + 1); file = rest.substr(s + 1); }
            if (file == "." || file == "..") { dir = rest; file = ""; } // trailing "."/".." belongs to the dir
        }
        out = Value::array({Value::str(v.vol), Value::str(dir), Value::str(file)});
        return true;
    }
    if (m == "splitdir") {
        ValueList v; std::string cur;
        for (char c : P(0)) { if (wsep(c)) { v.push_back(Value::str(cur)); cur.clear(); } else cur += c; }
        v.push_back(Value::str(cur));
        out = Value::array(v); return true;
    }
    if (m == "split") {
        std::string p = P(0);
        WinVol v = winVolume(p, false);
        std::string rest = v.rest;
        while (rest.size() > 1 && wsep(rest.back())) rest.pop_back();
        std::string dir, base;
        if (rest.empty() || (rest.size() == 1 && wsep(rest[0]))) { dir = "\\"; base = "\\"; }
        else {
            size_t s = rest.find_last_of("/\\");
            if (s == std::string::npos) { dir = "."; base = rest; }
            else { base = rest.substr(s + 1); dir = rest.substr(0, s); if (dir.empty()) dir = "\\"; }
        }
        Value h = Value::makeHash();
        (*h.hash)["volume"] = Value::str(v.vol);
        (*h.hash)["dirname"] = Value::str(dir);
        (*h.hash)["basename"] = Value::str(base);
        out = h; return true;
    }
    if (m == "catpath" || m == "join") {
        std::string vol = P(0), dir = P(1), file = P(2);
        if (m == "join") {
            bool dsep = dir.size() == 1 && wsep(dir[0]), fsep = file.size() == 1 && wsep(file[0]);
            if (dsep && fsep) { out = Value::str(vol.empty() ? dir : vol); return true; }
            if (dir == "." && file == ".") { out = Value::str(vol.empty() ? "." : vol + "."); return true; }
            if (dir == ".") dir = "";
        }
        std::string r = vol + dir;
        if (!dir.empty() && !file.empty() && !wsep(dir.back())) r += "\\";
        r += file;
        out = Value::str(r); return true;
    }
    if (m == "abs2rel") {
        std::string cp = winCanonCat({P(0)}, false, false);
        std::string cb = winCanonCat({pos.size() > 1 ? P(1) : cwdStr()}, false, false);
        WinVol vp = winVolume(cp, true), vb = winVolume(cb, true);
        if (vp.vol != vb.vol) { out = Value::str(cp); return true; } // different volumes: stay absolute
        auto segsOf = [](const std::string& s) {
            std::vector<std::string> r; std::string cur;
            for (char c : s) { if (wsep(c)) { if (!cur.empty()) r.push_back(cur); cur.clear(); } else cur += c; }
            if (!cur.empty()) r.push_back(cur);
            return r;
        };
        auto ps = segsOf(vp.rest), bs = segsOf(vb.rest);
        size_t i = 0; while (i < ps.size() && i < bs.size() && ps[i] == bs[i]) i++;
        std::vector<std::string> res;
        for (size_t k = i; k < bs.size(); k++) res.push_back("..");
        for (size_t k = i; k < ps.size(); k++) res.push_back(ps[k]);
        std::string r; for (size_t k = 0; k < res.size(); k++) { if (k) r += '\\'; r += res[k]; }
        out = Value::str(r.empty() ? "." : r); return true;
    }
    if (m == "rel2abs") {
        std::string path = P(0);
        std::string base = pos.size() > 1 ? P(1) : cwdStr();
        WinVol vp = winVolume(path, true);
        if (vp.abs && !vp.vol.empty()) { out = Value::str(winCanonCat({path}, false, false)); return true; }
        if (vp.abs) { // rooted but volumeless ('\foo'): graft the base's volume
            WinVol vb = winVolume(base, true);
            out = Value::str(winCanonCat({vb.vol + path}, false, false)); return true;
        }
        out = Value::str(winCanonCat({base, path}, false, false)); return true;
    }
    if (m == "basename") {
        std::string p = P(0); size_t s = p.find_last_of("/\\");
        out = Value::str(s == std::string::npos ? p : p.substr(s + 1)); return true;
    }
    if (m == "extension") {
        std::string p = P(0); size_t d = p.rfind('.');
        out = Value::str(d == std::string::npos ? "" : p.substr(d + 1)); return true;
    }
    if (m == "path") {
        // %*ENV<PATH> wins over <Path> (even when set to ''); entries split on ';',
        // '"' stripped, empties dropped; the result always begins with curdir.
        std::string pv; bool have = false;
        Value* env = I.global_->find("%*ENV");
        if (env && env->hash) {
            auto it = env->hash->find("PATH");
            if (it == env->hash->end()) it = env->hash->find("Path");
            if (it != env->hash->end()) { pv = it->second.toStr(); have = true; }
        }
        ValueList res; res.push_back(Value::str("."));
        if (have && !pv.empty())
            for (auto& part : splitAll(pv, ';')) {
                std::string q; for (char c : part) if (c != '"') q += c;
                if (!q.empty()) res.push_back(Value::str(q));
            }
        Value v = Value::list(res); v.s = "Seq"; out = v; return true;
    }
    if (m == "tmpdir") {
        const char* t = getenv("TMPDIR"); std::string d = (t && *t) ? t : "/tmp";
        while (d.size() > 1 && d.back() == '/') d.pop_back();
        Value v = Value::str(d); v.hashKind = "IO"; out = v; return true;
    }
    return false;
}

bool ioSpecMethod(Interpreter& I, const std::string& cls, const std::string& m, ValueList& args, Value& out) {
    auto A = [&](size_t i) { return i < args.size() ? args[i].toStr() : std::string(); };

    // curupdir matcher object: .ACCEPTS is true for anything but "." / ".."
    if (cls == "IO::Spec::__curupdir") {
        if (m == "ACCEPTS") { std::string x = A(0); out = Value::boolean(x != "." && x != ".."); return true; }
        return false;
    }
    if (cls == "IO::Spec::Win32") return winSpecMethod(I, m, args, out);
    // Cygwin is Unix-flavoured: '\' translates to '/', canonpath keeps a QNX-style
    // '//' root, and split/splitpath/catpath/join/rel2abs know drive/UNC volumes.
    bool cyg = cls == "IO::Spec::Cygwin";
    bool unixLike = (cls == "IO::Spec::Unix" || cls == "IO::Spec::QNX" || cls == "IO::Spec" || cyg);
    if (!unixLike) return false;
    bool qnxish = cls == "IO::Spec::QNX" || cyg;
    if (cyg) {
        for (auto& a : args)
            if (a.t == VT::Str) for (auto& c : a.s) { if (c == '\\') c = '/'; }
        // drive "c:" (case kept) or UNC "//server/share" volume prefix
        auto cygVol = [](const std::string& p, std::string& vol, std::string& rest) {
            vol.clear(); rest = p;
            if (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':') { vol = p.substr(0, 2); rest = p.substr(2); return; }
            if (p.size() >= 3 && p[0] == '/' && p[1] == '/' && p[2] != '/') {
                size_t i = 2; while (i < p.size() && p[i] != '/') i++;
                size_t j = i; while (j < p.size() && p[j] == '/') j++;
                size_t k = j; while (k < p.size() && p[k] != '/') k++;
                if (j > i && k > j) { vol = p.substr(0, k); rest = p.substr(k); }
            }
        };
        if (m == "split") {
            std::string vol, rest; cygVol(A(0), vol, rest);
            std::string dir, base;
            while (rest.size() > 1 && rest.back() == '/') rest.pop_back();
            if (rest.empty() || rest == "/") { dir = "/"; base = "/"; if (rest.empty() && vol.empty()) { dir = "."; base = A(0); } }
            else {
                size_t s = rest.rfind('/');
                if (s == std::string::npos) { dir = "."; base = rest; }
                else { base = rest.substr(s + 1); std::string d = rest.substr(0, s); dir = d.empty() ? "/" : d; }
            }
            Value h = Value::makeHash();
            (*h.hash)["volume"] = Value::str(vol);
            (*h.hash)["dirname"] = Value::str(dir);
            (*h.hash)["basename"] = Value::str(base);
            out = h; return true;
        }
        if (m == "splitpath") {
            bool nofile = false;
            for (auto& a : args) if (a.t == VT::Pair && a.s == "nofile") nofile = a.pairVal ? a.pairVal->truthy() : true;
            std::string vol, rest; cygVol(A(0), vol, rest);
            std::string dir, file;
            if (nofile) dir = rest;
            else {
                size_t s = rest.rfind('/');
                if (s == std::string::npos) file = rest;
                else { dir = rest.substr(0, s + 1); file = rest.substr(s + 1); }
                if (file == "." || file == "..") { dir = rest; file = ""; }
            }
            out = Value::array({Value::str(vol), Value::str(dir), Value::str(file)});
            return true;
        }
        if (m == "catpath" || m == "join") {
            std::string vol = A(0), dir = A(1), file = A(2);
            if (m == "join") { if (dir == "/" && file == "/") dir = ""; else if (dir == ".") dir = ""; }
            std::string r = dir;
            if (!dir.empty() && !file.empty() && dir.back() != '/' && file[0] != '/') r += "/";
            r += file;
            out = Value::str(vol + r); return true;
        }
        if (m == "rel2abs") {
            std::string path = A(0);
            if (isAbs(path)) { out = Value::str(canon(path, false, true)); return true; }
            std::string base = args.size() > 1 && args[1].t != VT::Pair ? A(1) : cwdStr();
            if (!isAbs(base)) base = cwdStr() + "/" + base;
            out = Value::str(canon(base + "/" + path + "/", false, true)); return true;
        }
        if (m == "is-absolute") {
            const std::string& p = args.empty() ? "" : args[0].s;
            out = Value::boolean(isAbs(p) ||
                (p.size() >= 3 && std::isalpha((unsigned char)p[0]) && p[1] == ':' && p[2] == '/'));
            return true;
        }
    }

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
        out = Value::str(canon(p, parent, qnxish)); return true;
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
