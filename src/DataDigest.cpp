// DataDigest.cpp — see DataDigest.h. The algorithms are src/Digest.cpp; this is
// the argument handling, the return types, and the refusals.

#include "DataDigest.h"
#include "Digest.h"
#include "Interpreter.h"

#include <cstring>
#include <cstdio>

namespace rakupp {

namespace {

[[noreturn]] void digDie(const std::string& msg) {
    throw RakuError{Value::typeObj("X::AdHoc"), msg};
}

const Value* namedArg(ValueList& a, const char* name) {
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg && x.s == name) return x.pairVal();
    return nullptr;
}
const Value* positional(ValueList& a, size_t which) {
    size_t seen = 0;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) continue;
        if (seen++ == which) return &x;
    }
    return nullptr;
}
size_t positionals(ValueList& a) {
    size_t n = 0;
    for (auto& x : a) if (!(x.t == VT::Pair && x.namedArg)) n++;
    return n;
}

// A Str's `.s` is already its UTF-8, and a Buf-kinded Str's is already its raw
// bytes, so the two common inputs need no conversion at all — which is the
// whole reason the module's own note says the same thing about the ABI.
bool isBlobKind(const Value& x) {
    return x.t == VT::Str &&
           (x.hashKind == "Buf" || x.hashKind == "Blob" || x.hashKind == "blob8" ||
            x.hashKind == "buf8" || x.hashKind == "utf8");
}

// The module takes a Str, a Blob, an IO::Path or an IO::Handle and dies on
// anything else. Nothing here coerces: `md5(42)` hashing "42" would be a
// silent answer to a question nobody asked.
std::string bytesOf(Interpreter& I, const Value& v, const char* who) {
    if (isBlobKind(v)) return v.s.str();
    if (v.t == VT::Str && v.hashKind.empty()) return v.s.str();
    bool isPath   = (v.t == VT::Str  && v.hashKind == "IO");
    bool isHandle = (v.t == VT::Hash && v.hashKind == "FileHandle");
    if (isPath || isHandle) {
        // slurp(:bin) — a file's digest is of its BYTES. Slurping as text would
        // decode UTF-8 and re-encode it: a no-op for valid UTF-8, and silently
        // wrong for a PNG.
        ValueList sb;
        sb.push_back(v);
        Value bin = Value::pair("bin", Value::boolean(true));
        bin.namedArg = true;
        sb.push_back(bin);
        return I.callBuiltin("slurp", std::move(sb)).s.str();
    }
    digDie(std::string(who) + ": cannot digest a " + v.typeName() +
           "; pass a Str, a Blob, an IO::Path or an IO::Handle");
}

Value blobOf(const std::string& raw) {
    Value b = Value::str(raw);
    b.hashKind = "Blob";          // blob8, as Digest::SHA2 types it
    return b;
}

DigestAlgo algoOr(const char* name) {
    DigestAlgo a = DigestAlgo::SHA256;
    digestAlgoByName(name, a);
    return a;
}

// Which of the tag's own six a `&hash` is, or nothing. By IDENTITY against the
// one stable Callable the engine mints per builtin — never by name, or a user's
// own `sub sha256` would be taken for ours and quietly not called.
bool algoOfCallable(Interpreter& I, const Value& fn, DigestAlgo& out) {
    if (fn.t != VT::Code || !fn.code()) return false;
    static const char* kNames[] = {"md5", "sha1", "sha224", "sha256", "sha384", "sha512"};
    for (const char* n : kNames) {
        const Value* ref = I.builtinRef(std::string("rakupp-") + n);
        if (ref && ref->code() == fn.code()) { out = algoOr(n); return true; }
    }
    return false;
}

} // namespace

Value dataDigestHash(Interpreter& I, ValueList& a, const char* algo, bool hex) {
    std::string who = std::string(algo) + (hex ? "-hex" : "");
    if (positionals(a) != 1)
        digDie(who + ": expected exactly one argument, the thing to digest");
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg) {
            // :initial-hash is a Digest::SHA2 internal that leaks through its
            // signature. Refused rather than ignored: silently hashing with the
            // standard IV would answer a different question.
            if (x.s == "initial-hash")
                digDie(who + ": :initial-hash is not supported; it is a "
                             "Digest::SHA2 internal, not part of this interface");
            digDie(who + ": no such adverb :" + x.s.str());
        }
    std::string raw = digestBytes(algoOr(algo), bytesOf(I, *positional(a, 0), who.c_str()));
    return hex ? Value::str(digestToHex(raw)) : blobOf(raw);
}

Value dataDigestHmac(Interpreter& I, ValueList& a, bool hex) {
    const char* who = hex ? "hmac-hex" : "hmac";
    size_t np = positionals(a);
    if (np < 3 || np > 4)
        digDie(std::string(who) + ": expected ($key, $message, &hash, $blocksize?)");
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg)
            digDie(std::string(who) + ": no such adverb :" + x.s.str());

    std::string key = bytesOf(I, *positional(a, 0), who);
    std::string msg = bytesOf(I, *positional(a, 1), who);
    const Value& fn = *positional(a, 2);
    if (fn.t != VT::Code || !fn.code())
        digDie(std::string(who) + ": the third argument is the hash, as a Callable");

    size_t B = 0;
    if (np == 4) {
        const Value* bv = positional(a, 3);
        long long n = bv->toInt();
        // The upper bound is not the RFC's business — B is the hash's block
        // size and nothing else is meaningful — but a block size arrives from
        // the program, and `hmac($k, $m, &sha256, 2**40)` must not try to
        // allocate a terabyte of padding before finding that out.
        if (n < 1 || n > 1024)
            digDie(std::string(who) + ": $blocksize must be between 1 and 1024, not " +
                   std::to_string(n));
        B = (size_t)n;
    }

    DigestAlgo algo;
    if (algoOfCallable(I, fn, algo)) {
        std::string raw = digestHmac(algo, key, msg, B);
        return hex ? Value::str(digestToHex(raw)) : blobOf(raw);
    }

    // A hash of the caller's own. HMAC is the hash called twice, so this keeps
    // working — and Digest::HMAC's own default of 64 for every hash is the only
    // defensible guess when we do not know the block size.
    if (!B) B = 64;
    std::string k = key;
    if (k.size() > B) {
        ValueList ka; ka.push_back(blobOf(k));
        k = bytesOf(I, I.callCallable(fn, std::move(ka)), who);
    }
    k.resize(B, '\0');
    std::string ipad(B, '\0'), opad(B, '\0');
    for (size_t i = 0; i < B; i++) {
        ipad[i] = (char)((unsigned char)k[i] ^ 0x36);
        opad[i] = (char)((unsigned char)k[i] ^ 0x5c);
    }
    ValueList ia; ia.push_back(blobOf(ipad + msg));
    std::string inner = bytesOf(I, I.callCallable(fn, std::move(ia)), who);
    ValueList oa; oa.push_back(blobOf(opad + inner));
    std::string raw = bytesOf(I, I.callCallable(fn, std::move(oa)), who);
    return hex ? Value::str(digestToHex(raw)) : blobOf(raw);
}

} // namespace rakupp
