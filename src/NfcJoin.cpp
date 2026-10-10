// NfcJoin.cpp — joining NFC text: `~`, `~=` and the joins built from them
//
// `a ~ b` is nfcNormalize(a + b). When both sides are NFC already, that can
// only change where they meet: uniNfcJoin finds the stretch normalization can
// reach (from the last NFC boundary of `a` to the first of `b` after its
// start, usually one codepoint each side) and normalizes that alone.
// Normalizing the whole result instead made each non-ASCII `$s ~= "é"` cost
// the length of `$s`: 20k appends took 5.25 s.
//
// Most Strs are NFC, but not every one: a file's or a socket's bytes, `.subst`
// and `sprintf` results, `%*ENV` are kept as they came. So a side that is not
// NFC is normalized whole first — NFC(x ~ y) is NFC(NFC(x) ~ y), and that is
// exactly the old answer and the old cost, once. Whether a Str is NFC is
// cached on its body (StrBody::nfc), and a join's result is NFC, so `~=` in a
// loop checks its accumulator once and its appended text each time.
//
// RAKUPP_NFC_CHECK=1 checks every join against the whole-string pass, and
// every cached "this body is NFC" against nfcNormalize, and reports a
// mismatch on stderr (or appends it to the file the variable names, when it is
// a path). It pays the whole-string pass this saves, as RAKUPP_NO_KERNELS pays
// for the interpreter: a debug mode, the one that proves the shortcut.
#include "InterpreterParts.h"
#include "BuiltinsShared.h"
#include "Unicode.h"
#include <cstdio>
#include <cstdlib>

namespace rakupp {

static const char* const g_nfcCheck = [] {
    const char* e = std::getenv("RAKUPP_NFC_CHECK");
    return e && *e ? e : nullptr;
}();
static std::string nfcCheckShow(const std::string& t) {
    std::string o;
    size_t k = 0;
    for (uint32_t c : utf8cp(t)) {
        if (k++ == 24) { o += ",…"; break; }
        char buf[16];
        std::snprintf(buf, sizeof buf, "%s%X", o.empty() ? "" : ",", c);
        o += buf;
    }
    return "\\x[" + o + "]";
}
static void nfcCheckReport(const std::string& what) {
    const std::string msg = "rakupp: NFC check (" + (g_revInterp ? g_revInterp->progName() : std::string()) + "): " + what + "\n";
    FILE* f = g_nfcCheck[0] == '/' ? std::fopen(g_nfcCheck, "a") : nullptr;
    std::fputs(msg.c_str(), f ? f : stderr);
    if (f) std::fclose(f);
}
static void nfcCheckJoin(const std::string& a, const std::string& b, const std::string& got) {
    if (nfcNormalize(a + b) == got) return;
    nfcCheckReport("join differs from the whole-string pass: left " +
                   nfcCheckShow(a.size() > 96 ? a.substr(a.size() - 96) : a) + ", right " + nfcCheckShow(b.substr(0, 96)));
}

bool isNfcText(const char* s, size_t n) {
    const int q = uniNfcQuickCheckUtf8(s, n);
    if (q >= 0) return q == 1;
    const std::string t(s, n);
    return nfcNormalize(t) == t;
}
bool cowIsNfc(const CowStr& s) {
    const StrBody* b = s.body();
    if (!b) return isNfcText(s.str());
    signed char c = b->nfc.load(std::memory_order_relaxed);
    if (c < 0) {
        c = isNfcText(s.bytes(), s.size()) ? 1 : 0;
        b->nfc.store(c, std::memory_order_relaxed);
    }
    else if (g_nfcCheck && c == 1 && !isNfcText(s.bytes(), s.size()))
        nfcCheckReport("a Str marked NFC is not: " + nfcCheckShow(s.str().substr(0, 96)));
    return c == 1;
}
int nfcKnown(const Value& v) {
    return v.t == VT::Str && v.hashKind.empty() && v.enumName.empty() ? (cowIsNfc(v.s) ? 1 : 0) : -1;
}
static void markNfc(const CowStr& s) {
    if (const StrBody* b = s.body()) b->nfc.store(1, std::memory_order_relaxed);
}

// the join itself, for two NFC sides
static std::string joinNfc(const std::string& a, const std::string& b) {
    const UniNfcJoin J = uniNfcJoin(a.data(), a.size(), b.data(), b.size());
    std::string out;
    if (!J.changed) {
        out.reserve(a.size() + b.size());
        out += a;
        out += b;
    }
    else {
        out.reserve(J.i + J.mid.size() + (b.size() - J.j));
        out.append(a, 0, J.i);
        out += J.mid;
        out.append(b, J.j, std::string::npos);
    }
    return out;
}

std::string nfcConcat(const std::string& a, const std::string& b, int aNfc, int bNfc) {
    std::string na, nb;
    const std::string& A = aNfc == 1 || (aNfc < 0 && isNfcText(a)) ? a : (na = nfcNormalize(a));
    const std::string& B = bNfc == 1 || (bNfc < 0 && isNfcText(b)) ? b : (nb = nfcNormalize(b));
    std::string out = joinNfc(A, B);
    if (g_nfcCheck) nfcCheckJoin(a, b, out);
    return out;
}
Value nfcConcatStr(const CowStr& a, const CowStr& b) {
    Value out = Value::str(nfcConcat(a.str(), b.str(), cowIsNfc(a) ? 1 : 0, cowIsNfc(b) ? 1 : 0));
    markNfc(out.s);
    return out;
}

void nfcAppend(std::string& dst, const std::string& v, long long* chars, signed char* dstNfc) {
    if (&v == &dst) { const std::string copy = v; nfcAppend(dst, copy, chars, dstNfc); return; }
    std::string before;
    if (g_nfcCheck) before = dst;
    const bool dn = dstNfc && *dstNfc >= 0 ? *dstNfc == 1 : isNfcText(dst);
    if (dstNfc) *dstNfc = 1;   // whichever way it goes, the result is NFC
    if (!dn) {   // the old way, once: all of it
        dst = nfcNormalize(dst + v);
        if (chars) *chars = -1;
    }
    else {
        std::string nv;
        const std::string& V = isNfcText(v) ? v : (nv = nfcNormalize(v));
        const UniNfcJoin J = uniNfcJoin(dst.data(), dst.size(), V.data(), V.size());
        UniGraphemeJoin G;
        if (chars && *chars >= 0) G = uniGraphemeJoin(dst.data(), dst.size(), J, V.data(), V.size());
        if (!J.changed) dst += V;
        else {
            dst.resize(J.i);
            dst += J.mid;
            dst.append(V, J.j, std::string::npos);
        }
        if (chars)
            *chars = G.ok ? *chars - (long long)G.tailA + (long long)uniGraphemeCountUtf8(dst.data() + G.p, dst.size() - G.p)
                          : -1;
    }
    if (g_nfcCheck) {
        nfcCheckJoin(before, v, dst);
        if (chars && *chars >= 0 && *chars != graphemeCount(dst))
            nfcCheckReport("grapheme count " + std::to_string(*chars) + " after a join, not " +
                           std::to_string(graphemeCount(dst)));
    }
}

void nfcAppendCow(CowStr& dst, const char* v, size_t nv, int vNfc) {
    const char* d = dst.bytes();
    const size_t nd = dst.size();
    if (v < d + nd && d < v + nv) {   // its own text, or part of it: `$s ~= $s`
        const std::string copy(v, nv);
        nfcAppendCow(dst, copy.data(), copy.size(), vNfc);
        return;
    }
    std::string before;
    if (g_nfcCheck) before.assign(d, nd);
    if (!cowIsNfc(dst)) {   // the old way, once: all of it
        dst = nfcNormalize(dst.str() + std::string(v, nv));
        markNfc(dst);
        if (g_nfcCheck) nfcCheckJoin(before, std::string(v, nv), dst.str());
        return;
    }
    std::string nvText;
    if (!(vNfc == 1 || (vNfc < 0 && isNfcText(v, nv)))) {
        nvText = nfcNormalize(std::string(v, nv));
        v = nvText.data();
        nv = nvText.size();
    }
    const UniNfcJoin J = uniNfcJoin(d, nd, v, nv);
    const StrBody* b0 = dst.body();
    const long long chars = b0 ? b0->nGraphemes.load(std::memory_order_relaxed) : -1;
    UniGraphemeJoin G;
    if (chars >= 0) G = uniGraphemeJoin(d, nd, J, v, nv);
    if (!J.changed) dst.appendText(v, nv);
    else dst.spliceTail(J.i, J.mid, v + J.j, nv - J.j);
    if (const StrBody* b = dst.body()) {
        b->nfc.store(1, std::memory_order_relaxed);
        bool ascii = true;
        for (size_t k = 0; k < nv && ascii; k++) ascii = (unsigned char)v[k] < 0x80;
        if (!ascii) b->allAscii.store(0, std::memory_order_relaxed);
        if (G.ok) {
            const long long n = chars - (long long)G.tailA + (long long)uniGraphemeCountUtf8(dst.bytes() + G.p, dst.size() - G.p);
            b->nGraphemes.store(n, std::memory_order_relaxed);
        }
    }
    if (g_nfcCheck) {
        nfcCheckJoin(before, nvText.empty() ? std::string(v, nv) : nvText, dst.str());
        if (G.ok && cowGraphemeCount(dst) != graphemeCount(dst.str()))
            nfcCheckReport("cached grapheme count " + std::to_string(cowGraphemeCount(dst)) + " after a join, not " +
                           std::to_string(graphemeCount(dst.str())));
    }
}

void nfcPrependCow(CowStr& dst, const char* x, size_t nx) {
    const char* d = dst.bytes();
    const size_t nd = dst.size();
    if (x < d + nd && d < x + nx) {
        const std::string copy(x, nx);
        nfcPrependCow(dst, copy.data(), copy.size());
        return;
    }
    std::string before;
    if (g_nfcCheck) before.assign(d, nd);
    std::string nxText;
    if (!isNfcText(x, nx)) {
        nxText = nfcNormalize(std::string(x, nx));
        x = nxText.data();
        nx = nxText.size();
    }
    if (!cowIsNfc(dst)) dst = nfcNormalize(std::string(x, nx) + dst.str());
    else {
        const UniNfcJoin J = uniNfcJoin(x, nx, d, nd);
        if (!J.changed) dst.prependText(x, nx);
        else {
            std::string t;
            t.reserve(J.i + J.mid.size() + (nd - J.j));
            t.append(x, J.i);
            t += J.mid;
            t.append(d + J.j, nd - J.j);
            dst = std::move(t);
        }
    }
    markNfc(dst);
    if (g_nfcCheck) nfcCheckJoin(nxText.empty() ? std::string(x, nx) : nxText, before, dst.str());
}

void nfcAppendPartSlow(std::string& out, const std::string& part, int partNfc) {
    std::string np;
    const std::string& P = partNfc == 1 || (partNfc < 0 && isNfcText(part)) ? part : (np = nfcNormalize(part));
    if (out.empty() || P.empty() || (unsigned char)P[0] < 0x80) { out += P; return; }
    signed char known = 1;   // (out is built by these joins alone)
    nfcAppend(out, P, nullptr, &known);
}

} // namespace rakupp
