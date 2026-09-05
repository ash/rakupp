// DataZlib.cpp — see DataZlib.h. The format is src/Zlib.cpp; this is the
// argument handling, the return types, and the refusals.

#include "DataZlib.h"
#include "Zlib.h"
#include "Interpreter.h"

namespace rakupp {

namespace {

[[noreturn]] void zDie(const std::string& msg) {
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

bool isBlobKind(const Value& x) {
    return x.t == VT::Str &&
           (x.hashKind == "Buf" || x.hashKind == "Blob" || x.hashKind == "blob8" ||
            x.hashKind == "buf8" || x.hashKind == "utf8");
}

// `compress`/`uncompress` take a Blob, as the reference types them. The
// checksums also take a Str, because `crc32("123456789")` is how everyone
// checks a CRC-32 implementation and refusing it would be pedantry.
std::string blobBytes(const Value& v, const char* who, bool allowStr) {
    if (isBlobKind(v)) return v.s.str();
    if (allowStr && v.t == VT::Str && v.hashKind.empty()) return v.s.str();
    zDie(std::string(who) + ": expected a Blob" + (allowStr ? " or a Str" : "") +
         ", got a " + v.typeName());
}

// `:gzip` and `:raw`, mutually exclusive, and nothing else. An unknown adverb
// is an error rather than a silent no-op: `compress($d, :gzipp)` producing zlib
// framing is a bug found much later, in something else.
ZlibFormat framing(ValueList& a, const char* who) {
    bool g = false, r = false;
    for (auto& x : a) {
        if (!(x.t == VT::Pair && x.namedArg)) continue;
        if (x.s == "gzip") { g = x.pairVal() && x.pairVal()->truthy(); continue; }
        if (x.s == "raw")  { r = x.pairVal() && x.pairVal()->truthy(); continue; }
        zDie(std::string(who) + ": no such adverb :" + x.s.str());
    }
    if (g && r) zDie(std::string(who) + ": takes :gzip or :raw, not both");
    return g ? ZlibFormat::Gzip : r ? ZlibFormat::Raw : ZlibFormat::Zlib;
}

Value bufOf(const std::string& bytes) {
    Value b = Value::str(bytes);
    b.hashKind = "Buf";        // a Buf, as Compress::Zlib types both directions
    return b;
}

[[noreturn]] void rethrow(const ZlibError& e) {
    // X::AdHoc deliberately, so a CATCH written against Compress::Zlib still
    // catches. The prose is ours and says what was wrong with the stream.
    throw RakuError{Value::typeObj("X::AdHoc"), std::string(e.what())};
}

} // namespace

Value dataZlibCompress(Interpreter&, ValueList& a) {
    size_t np = positionals(a);
    if (np < 1 || np > 2) zDie("compress: expected ($data, $level?)");
    ZlibFormat fmt = framing(a, "compress");
    std::string in = blobBytes(*positional(a, 0), "compress", false);
    int level = 6;
    if (np == 2) {
        const Value* lv = positional(a, 1);
        if (!rtIsDefined(*lv)) level = 6;
        else level = (int)lv->toInt();
    }
    if (level < -1 || level > 9)
        zDie("compress: compression level must be between -1 and 9, not " + std::to_string(level));
    try { return bufOf(zlibDeflate(in, level, fmt)); }
    catch (const ZlibError& e) { rethrow(e); }
}

Value dataZlibUncompress(Interpreter&, ValueList& a) {
    if (positionals(a) != 1) zDie("uncompress: expected exactly one argument, the compressed data");
    ZlibFormat fmt = framing(a, "uncompress");
    std::string in = blobBytes(*positional(a, 0), "uncompress", false);
    try { return bufOf(zlibInflate(in, fmt)); }
    catch (const ZlibError& e) { rethrow(e); }
}

Value dataZlibGzslurp(Interpreter& I, ValueList& a) {
    size_t np = positionals(a);
    if (np != 1) zDie("gzslurp: expected exactly one argument, the path");
    const Value* bin = namedArg(a, "bin");
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg && x.s != "bin")
            zDie("gzslurp: no such adverb :" + x.s.str());

    ValueList sa;
    sa.push_back(*positional(a, 0));
    Value b = Value::pair("bin", Value::boolean(true));
    b.namedArg = true;
    sa.push_back(b);
    std::string raw = I.callBuiltin("slurp", std::move(sa)).s.str();

    std::string plain;
    try { plain = zlibInflate(raw, ZlibFormat::Gzip); }
    catch (const ZlibError& e) { rethrow(e); }
    if (bin && bin->truthy()) return bufOf(plain);
    return Value::str(plain);          // decoded as UTF-8, as .slurp would
}

Value dataZlibGzspurt(Interpreter& I, ValueList& a) {
    size_t np = positionals(a);
    if (np != 2) zDie("gzspurt: expected ($path, $stuff)");
    const Value* bin = namedArg(a, "bin");
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg && x.s != "bin")
            zDie("gzspurt: no such adverb :" + x.s.str());

    const Value& stuff = *positional(a, 1);
    bool wantBin = bin && bin->truthy();
    if (wantBin && !isBlobKind(stuff))
        zDie("gzspurt: :bin wants a Blob, got a " + stuff.typeName());
    std::string plain = isBlobKind(stuff) ? stuff.s.str() : stuff.toStr();

    std::string gz;
    try { gz = zlibDeflate(plain, 6, ZlibFormat::Gzip); }
    catch (const ZlibError& e) { rethrow(e); }

    ValueList sa;
    sa.push_back(*positional(a, 0));
    sa.push_back(bufOf(gz));
    return I.callBuiltin("spurt", std::move(sa));
}

Value dataZlibChecksum(Interpreter&, ValueList& a, bool crc) {
    const char* who = crc ? "crc32" : "adler32";
    size_t np = positionals(a);
    if (np < 1 || np > 2) zDie(std::string(who) + ": expected ($data, $init?)");
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg)
            zDie(std::string(who) + ": no such adverb :" + x.s.str());
    std::string in = blobBytes(*positional(a, 0), who, true);
    uint32_t init = crc ? 0u : 1u;
    if (np == 2) {
        const Value* iv = positional(a, 1);
        if (rtIsDefined(*iv)) init = (uint32_t)(uint64_t)iv->toInt();
    }
    uint32_t r = crc ? zlibCrc32(in, init) : zlibAdler32(in, init);
    return Value::integer((long long)(uint64_t)r);
}

} // namespace rakupp
