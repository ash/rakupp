// Which.cpp — the one home for identity: what `.WHICH` answers, whether that is
// an ObjAt or a ValueObjAt, and the key a Set, Bag or Mix files an element under.
// Split out of Builtins.cpp, whose size it was; `===`, `.unique`, object hashes
// and `(elem)` all ask whichOf.
#include "BuiltinsParts.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rakupp {

// An object's WHICH is a SERIAL, not its address. Bodies come back from the
// slab pool, so a temporary's address was the next one's: `(1,2).WHICH eq
// ('1','2').WHICH` and `Foo.new.WHICH eq Foo.new.WHICH` were True, and
// `(^3).map({ [$_].WHICH }).unique` had one element, where Rakudo's three
// (#221). A body gets its serial the first time a WHICH asks for it and keeps
// it while it lives; its death (whichForget, from the release path, which the
// kPayWhichId bit sends here) gives the address back without the serial. The
// table is never destroyed: bodies still die during static destruction.
static std::atomic<std::uint64_t> g_whichSerial{0};
std::uint64_t nextWhichSerial() noexcept { return g_whichSerial.fetch_add(1, std::memory_order_relaxed) + 1; }
static std::mutex& whichMut() { static auto* m = new std::mutex; return *m; }
static std::unordered_map<const Payload*, std::uint64_t>& whichIds() {
    static auto* t = new std::unordered_map<const Payload*, std::uint64_t>;
    return *t;
}
static std::uint64_t bodySerial(const Payload* p) {
    std::lock_guard<std::mutex> lk(whichMut());
    if (p->pflags_.load(std::memory_order_relaxed) & kPayWhichId) {
        auto it = whichIds().find(p);
        if (it != whichIds().end()) return it->second;
    }
    const std::uint64_t id = nextWhichSerial();
    whichIds()[p] = id;
    p->pflags_.fetch_or(kPayWhichId, std::memory_order_relaxed);
    return id;
}
void whichForget(Payload* p) noexcept {
    try { std::lock_guard<std::mutex> lk(whichMut()); whichIds().erase(p); } catch (...) {}
}
// `Type|serial`, as Rakudo's `Array|2340682431952` reads
static std::string serialWhich(const std::string& type, std::uint64_t id) { return type + "|" + std::to_string(id); }

// The canonical identity of a value — what `.WHICH` answers and what a Set/Bag/Mix
// keys on. Two values are the same element exactly when these agree, so it has to
// separate things that merely LOOK alike when stringified:
//   * an allomorph carries BOTH of its halves (`IntStr|Int|42|Str|42`), which is
//     what keeps `<42>` distinct from `42` and from `"42"`;
//   * a Rat identifies by its exact numerator/denominator (`Rat|421/10`), not by
//     its decimal rendering;
//   * a Complex by its two parts;
//   * a Bool by 1/0.
// Is this value's identity its OWN (an ObjAt), rather than its content (a
// ValueObjAt)? Measured against Rakudo 2026.08: Array, List, Seq, Hash, Buf,
// Instant, IO::Path, Code and every user object are ObjAt; Int, Rat, Num, Str,
// Bool, Range, the Setty/Baggy family, Date, Complex, a type object and Nil are
// ValueObjAt (sheet LA-36). A PAIR is whichever its parts are (sheet HM-19):
// `(a => 1)` is a value, `(a => [1])` is an object, because the Array inside it
// is one — which is why two such pairs are not `===` and `.unique` keeps both.
// …unless its class writes its own `method WHICH` answering a ValueObjAt.
static bool userWhichIsValue(const Value& v) {
    std::string w; bool isValue = false;
    return g_userWhich && g_userWhich(v, w, isValue) && isValue;
}
// The built-in objects that keep their state in a Hash payload: each `.new` is
// a different object however alike their fields read, so they identify by the
// payload (its serial). Rendering the fields made every fresh Promise
// `Promise|status Planned`, so `.unique`, a Set or an object hash merged them,
// and the WHICH moved when the Promise was kept. (Rakudo 2026.09: all ObjAt.)
static bool hashIsIdentity(const Value& v) {
    if (v.t != VT::Hash || !v.hash()) return false;
    static const char* const kinds[] = {
        "Promise", "Channel", "Supplier", "Supply", "Tap", "Lock", "Lock::Async",
        "Lock::ConditionVariable", "Semaphore", "Thread", "Cancellation", "Proc", "Proc::Async",
        "Failure",
        // the handles: IO::Handle and IO::Pipe, IO::Socket::INET, IO::Socket::Async
        "FileHandle", "ProcIn", "Socket", "AsyncSocket"};
    for (const char* k : kinds) if (v.hashKind == k) return true;
    return false;
}
bool whichIsObjAt(const Value& v) {
    if (v.t == VT::Pair)
        return v.pairLive() ||   // a live container inside: the Pair is an object (pair.t)
               (v.hasPairKey() && whichIsObjAt(v.nonStrKey())) ||
               (v.pairVal() && whichIsObjAt(*v.pairVal()));
    return (v.t == VT::Array && v.hashKind != "Capture" && v.enumName().empty()) ||
           (v.t == VT::Hash && v.hashKind.empty()) ||
           (v.t == VT::Hash && (v.hashKind == "Hash" || v.hashKind == "SetHash" ||
                                v.hashKind == "BagHash" || v.hashKind == "MixHash")) ||
           hashIsIdentity(v) ||
           (v.t == VT::Str && (v.hashKind == "Buf" || v.hashKind == "IO")) ||
           (v.isNumeric() && (v.hashKind == "Instant" || v.hashKind == "Duration")) ||
           v.t == VT::Code || v.t == VT::Match || (v.t == VT::Object && !userWhichIsValue(v));
}
std::string whichOf(const Value& v) {
    auto ratPart = [](const Value& r) {
        if (r.ratN() && r.ratD()) return r.ratN()->toString() + "/" + r.ratD()->toString();
        return r.toStr();
    };
    // a Buf/Instant/Duration is a reference type wearing a scalar's clothes: its
    // identity is the token stamped at construction (see identify() in Value.h),
    // not its bytes or its seconds. Without this `Buf.new(1,2).WHICH` was the
    // useless "Buf|" — the bytes are unprintable — for every buffer alive.
    // A token-less one (a construction site that predates the stamp) keeps the
    // old value rendering rather than claiming to be a different object.
    if (identityScalar(v) && v.ext())
        return serialWhich(v.typeName(), static_cast<const IdentityToken*>(v.ext().get())->id);
    if (v.isAllomorph()) {
        // the numeric half is the same value with the string side dropped
        Value num = v; num.hashKind.clear(); num.s.clear();
        std::string numName = v.typeName() == "IntStr"     ? "Int"
                            : v.typeName() == "RatStr"     ? "Rat"
                            : v.typeName() == "NumStr"     ? "Num"
                            : v.typeName() == "ComplexStr" ? "Complex" : "Num";
        std::string numId = numName == "Rat" ? ratPart(num) : num.toStr();
        if (numName == "Complex") numId = Value::number(num.n).toStr() + "|" + Value::number(num.im()).toStr();
        return v.typeName() + "|" + numName + "|" + numId + "|Str|" + v.s;
    }
    // A Date identifies by its DAYCOUNT — Rakudo's is "Date|57385". A
    // `:formatter` changes how it PRINTS, and the default arm below builds the
    // identity out of exactly that rendering, so a formatted Date stopped being
    // `===` to the same day unformatted. (A DateTime's WHICH *is* its .Str,
    // formatter and all — that is Rakudo's too, so it keeps the default arm;
    // what makes the two eqv regardless is valueEqv, which skips the key.)
    if (v.t == VT::Hash && v.hashKind == "Date" && v.hash())
        return "Date|" + std::to_string((long long)dateNumeric(v));
    switch (v.t) {
        case VT::Bool:    return "Bool|" + std::string(v.b ? "1" : "0");
        case VT::Rat:     return "Rat|" + ratPart(v);
        case VT::Complex: return "Complex|" + Value::number(v.n).toStr() + "|" + Value::number(v.im()).toStr();
        // An OBJECT is identified by being itself, not by its contents — two
        // instances with equal attributes are different elements of a Set. This
        // lived only in the `.WHICH` arm, so `.WHICH` said they differed while
        // baggyKeyStr (which keys on the RENDERING, `A<obj>` for every instance
        // of A) merged them: `set($x, $y).elems` was 1.
        case VT::Object:  if (v.obj()) {
                              std::string w; bool isV = false;
                              if (g_userWhich && g_userWhich(v, w, isV)) return w;
                              return serialWhich(v.typeName(), bodySerial(v.p_.get()));
                          }
                          return v.typeName() + "|<null>";
        // a Range's identity is its GIST, exclusion markers and all — expanding
        // it makes `1..^5` and `1..4` the same value, and builds a huge string
        // for a large range on the way
        case VT::Range:   return "Range|" + v.gist();
        // A MATCH is an object: two matches of the same text are two nodes of
        // the tree (issue #109), and a copy of one is still it — the payload
        // every copy shares, which is what `===` compares
        case VT::Match:   if (v.p_) return serialWhich(v.typeName(), bodySerial(v.p_.get()));
                          return v.typeName() + "|" + v.toStr();
        // A Pair identifies by its PARTS — but only while both are values. With
        // an Array (or any other reference type) inside, the Pair is an object
        // and identifies by BEING itself: the payload it was built with, which
        // every copy of the Pair shares and no other Pair has.
        case VT::Pair:    if (whichIsObjAt(v) || (v.pairValRO && v.i)) {
                              // (a FROZEN container pair keeps the identity it had)
                              char buf[24];
                              std::snprintf(buf, sizeof buf, "|%p",
                                            v.pairValRO && v.i ? (void*)(intptr_t)v.i : (void*)v.pairVal());
                              return "Pair" + std::string(buf);
                          }
                          return "Pair|" + (v.hasPairKey() ? whichOf(v.nonStrKey()) : "Str|" + v.s) +
                                 "|" + (v.pairVal() ? whichOf(*v.pairVal()) : "Any|");
        // a CAPTURE is a VALUE — `\(1,2) === \(1,2)` is True in Rakudo, alone
        // among the Arrays — so it identifies by its PARTS, each with its own
        // identity. Rendering them (the old "Capture|1 2") merged `\(1)` with
        // `\("1")` and `\(:a)` with a positional "a".
        // A Hash — and a Map — is an OBJECT: it identifies by BEING itself, the
        // payload every copy of the value shares. A refill (`%h = …`) and a
        // write into a nested Hash both leave that payload where it is, so the
        // WHICH holds still, as Rakudo's `Map|…`/`Hash|…` does (S32-hash/map.t).
        case VT::Hash:    if (v.hash() && (v.hashKind.empty() || v.hashKind == "Hash" || v.hashKind == "Map" ||
                                       hashIsIdentity(v)))
                              return serialWhich(v.typeName(), bodySerial(v.p_.get()));
                          return v.typeName() + "|" + v.toStr();
        case VT::Array:   if (!v.enumType().empty() && v.enumName().empty()) goto typeObject; // an enum's type
                          if (v.hashKind == "Capture") {
                              std::string pos;
                              std::vector<std::string> named; // named parts are unordered
                              if (v.arr()) for (auto& e : *v.arr()) {
                                  if (e.t == VT::Pair && e.namedArg)
                                      named.push_back(":" + e.s + "(" +
                                                      (e.pairVal() ? whichOf(*e.pairVal()) : "") + ")");
                                  else pos += "(" + whichOf(e) + ")";
                              }
                              std::sort(named.begin(), named.end());
                              for (auto& nm : named) pos += nm;
                              return "Capture|" + pos;
                          }
                          // a JUNCTION is a value too: its kind and its eigenstates, so
                          // two `any(True, True)` are one key of a Mu-keyed hash and
                          // `any(True, False)` another (classify.t)
                          if (v.arr() && (v.enumName() == "any" || v.enumName() == "all" ||
                                          v.enumName() == "one" || v.enumName() == "none")) {
                              std::string s = "Junction|" + v.enumName().str() + "(";
                              for (auto& e : *v.arr()) s += whichOf(e) + ";";
                              return s + ")";
                          }
                          // An Array, a List or a Seq is an OBJECT, as `===` says: it
                          // identifies by its payload, which every copy shares and a push
                          // or a refill leaves where it is (a packed one is not unpacked
                          // to ask). Its rendering made two equal arrays one Set element
                          // and `[1,2]` the same as `["1","2"]` (#221). `Empty` is the one
                          // empty Slip, however it was built; `my Int @a` is an Array[Int].
                          if (v.s == "Slip" && v.arr() && v.arr()->empty()) return "Slip|Empty";
                          if (!v.p_) return v.typeName() + "|<null>";
                          return serialWhich(v.typeName() + (v.ofType().empty() ? "" : "[" + v.ofType() + "]"),
                                             bodySerial(v.p_.get()));
        // a TYPE OBJECT is undefined, and Rakudo marks that with a `U` before
        // the type's own number: `Rat|U4360041773904`. The number here is the
        // name's hash, so it is stable for the run and distinct per type.
        case VT::Type: case VT::Any: case VT::Nil: typeObject: {
            std::string nm = v.typeName();
            uint64_t h = 1469598103934665603ULL;               // FNV-1a
            for (unsigned char c : nm) { h ^= c; h *= 1099511628211ULL; }
            return nm + "|U" + std::to_string(h % 9000000000000ULL + 1000000000000ULL);
        }
        default:          return v.typeName() + "|" + v.toStr();
    }
}

// The typed key of an element, preserved in the count Value's `pairKey` so
// .keys/.pairs/.min/.max recover the original type (a Bag of Ints keeps Int
// keys, not the stringified form). Null for a plain Str — that round-trips
// through the string key, so Set-of-strings behaviour stays byte-identical.
std::shared_ptr<Value> baggyKey(const Value& v) {
    if (v.t == VT::Str && v.hashKind.empty() && v.enumName().empty()) return nullptr;
    return std::make_shared<Value>(v);
}
// The LOOKUP key of a quanthash element.
//
// This SHOULD be the element's identity (`whichOf`), because two elements are the
// same one exactly when their `.WHICH` agrees — keying on the rendering makes
// `42`, `"42"` and `<42>` a single element and `set(1,"1")` one-element instead of
// two. That change was written, measured and backed out, because it exposes a
// deeper mismatch it cannot fix on its own: rakupp's Hash keys are plain strings,
// so `{42 => 'a'}` contributes the STRING "42" to a set while a literal `42`
// contributes an Int. Today both render "42" and compare equal; under identity
// keys they become different elements, and roast's set operators — which compare
// an operator result against a `set(…)` literal — lose about 30 assertions.
//
// Fixing it properly means Hash keys carrying their original key object, which is
// its own piece of work. Until then the rendering below is what makes results
// print correctly; the keys stay renderings.
std::string baggyKeyStr(const Value& v) {
    if (v.t == VT::Type || v.t == VT::Any) return v.gist(); // type objects ARE elements
    // An ALLOMORPH is neither of its halves: `<42>` renders "42" exactly like the
    // Int 42, so keying on the rendering merged them and `42 ∈ <42 55 1>` was True.
    // Narrow on purpose — plain Str and Int keys keep their rendering, so the set
    // operators that compare a result against a `set(…)` literal are untouched.
    if (v.isAllomorph()) return whichOf(v);
    // …and an OBJECT, whose rendering is `Class<obj>` for every instance, so two
    // distinct objects collapsed into one Set element.
    if (v.t == VT::Object) return whichOf(v);
    // …and a MATCH: two matches of the same text are two elements
    if (v.t == VT::Match) return whichOf(v);
    // …and a Dateish, whose rendering a `:formatter` rewrites: two Dates of the
    // same day are ONE set element however either of them prints (whichOf keys a
    // Date on its daycount, a DateTime on its rendering — both as Rakudo does).
    if (v.t == VT::Hash && (v.hashKind == "Date" || v.hashKind == "DateTime")) return whichOf(v);
    // …and a Promise, Channel, Lock and the rest: each is its own element
    if (hashIsIdentity(v)) return whichOf(v);
    // …and an Array or a List, whose rendering made two equal ones one element (#221)
    if (v.t == VT::Array && whichIsObjAt(v)) return whichOf(v);
    return v.toStr();
}

} // namespace rakupp
