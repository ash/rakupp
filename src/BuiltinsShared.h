#pragma once
// Helpers shared between Builtins.cpp and the method-dispatch segments split out
// of it (MethodCallTail.cpp, …). They were file-static in Builtins.cpp; splitting
// that file is what forced them into a header. Internal to the implementation —
// nothing outside src/ should include this.
#include "Value.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>

namespace rakupp {

// An Instant in this engine is POSIX plus the ten pre-1972 leap seconds:
// `Instant.from-posix` adds them and `.to-posix` takes them back off. Every
// producer of an Instant — `now`, `DateTime.Instant` — has to add them too, or
// the pair does not round-trip (BSON::Simple encodes datetimes through exactly
// that pair, and every timestamp came out ten seconds early).
inline constexpr double kInstantEpochOffset = 10.0;

// Seconds since the epoch, on the same clock the `now` term reads (so timer
// promises and `now` arithmetic can't disagree — by a truncated fraction, or by
// the offset above: `sleep-until(now - 5)` compares against this).
inline double epochNowSecs() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()
           + kInstantEpochOffset;
}

// Remaining delay, in seconds, of a timer Promise (hashKind "Promise", kind
// "timer"). Negative means the fire time has passed. Promise.in/.at stamp the
// absolute fire time as `fires_at` at CREATION, so a timer consumed late (a
// `whenever` armed after the promise was made, an `await` reached mid-program)
// waits only the remainder — and Promise.at's absolute instant is never
// mistaken for a relative delay. `seconds` (relative, from consumption) is the
// fallback for a timer hash made without a stamp.
inline double timerRemainingSecs(const Value& p) {
    auto f = p.hash()->find("fires_at");
    if (f != p.hash()->end()) return f->second.toNum() - epochNowSecs();
    auto s = p.hash()->find("seconds");
    return s != p.hash()->end() ? s->second.toNum() : 0.0;
}

// The next LOGICAL NEWLINE in a UTF-8 string at or after `from`, as (offset,
// length), or (npos, 0) when there is none. Raku's `.lines` breaks on the whole
// Unicode set — LF, CR, CRLF, FF, VT, NEL, LS, PS — not just "\n" and "\r\n";
// a FILE's `.lines` does not (its nl-in is ["\n", "\r\n"]), so this is for the
// Str/Supply side only.
std::pair<size_t, size_t> nextLogicalNewline(const std::string& s, size_t from);
// How many trailing bytes of `s` might still grow into a newline if more text
// arrives: a lone "\r" (could become "\r\n"), or a truncated NEL/LS/PS lead.
// A stream splitter must hold these back; 0 when the tail is unambiguous.
size_t danglingNewlinePrefix(const std::string& s);

std::shared_ptr<Value> baggyKey(const Value& v);
size_t charToByte(const std::string& s, long long chars);
size_t codeArity(const Value& code);
std::string cpToUtf8(uint32_t cp);
std::string joinValues(const ValueList& items, const std::string& sep);
Value makeInfArray(long long start);
std::string markFold(const std::string& in);
ValueList toList(const Value& v);
std::vector<uint32_t> utf8cp(const std::string& s);
// Leading ASCII bytes of `s`, capped at `limit`; over such a run a codepoint
// index and a byte index are the same thing, so utf8cp() can be skipped.
size_t asciiRun(const std::string& s, size_t limit);
bool allAscii(const std::string& s);
// True when a byte index into `s` is also a grapheme index — ASCII and CR-free,
// so Raku's grapheme-indexed string methods can work on bytes without decoding.
bool byteIsGraphemeIndex(const std::string& s);
// The cached forms: same answers, memoized on a long string's immutable body.
// The scanning ops call these once per character examined, so the difference
// between memoized and not is the difference between a linear tokenizer and a
// quadratic one. See the definitions in Builtins.cpp.
// One stripe per Supplier, serializing emit/done/quit and tap registration.
// Shared so `Supply.wait` can read the supplier's completion state without
// racing an emit on another thread. Defined in Builtins.cpp.
std::recursive_mutex& supplierMutex(const void* key);

bool cowAllAscii(const CowStr& s);
bool cowByteIsGraphemeIndex(const CowStr& s);
long long cowGraphemeCount(const CowStr& s);
// Byte-offset tables for positional ops on non-ASCII promoted strings (one
// entry per codepoint / per grapheme, +end sentinel), cached on the body.
// nullptr for short (unpromoted) strings — rescanning those is free.
const std::vector<uint32_t>* cowCpIndex(const CowStr& s);
const std::vector<uint32_t>* cowGraphemeIndex(const CowStr& s);
uint32_t cpAtByte(const std::string& s, size_t b); // decode ONE codepoint at byte b

bool deepEq(const Value& a, const Value& b);
bool matcherAccepts(Interpreter& I, const Value& v, const Value& mt);
bool predAnswerTruthy(Interpreter& I, const Value& res, const Value& elem);
uint32_t toLowerCp(uint32_t c);
// `:smartcase` (6.e) folds case only when the needle carries none of its own:
// a needle spelled in lower case matches either case, one with a capital in it
// means that spelling exactly. "Does it carry case" is "does folding change it",
// which is the same question the fold already answers.
bool strHasNoUpper(const std::string& s);
// Unicode whitespace, the `\s` set: category Z plus the ASCII controls Raku
// counts (tab, LF, VT, FF, CR) and NEL. `.words` and friends split on this —
// `std::istream >>` only knows the C locale's ASCII notion of it.
bool uniIsSpaceCp(uint32_t cp);
// A Junction value: an Array tagged with its kind. Declared here because both the
// interpreter and the method dispatcher have to ask.
bool isJunction(const Value& v);
std::string typeOfVal(const Value& v);
// hashEntryKey: the real key of a hash entry — pairKey, object-hash key type,
// or the plain Str. Defined in Builtins.cpp; see the comment there.
Value hashEntryKey(const Value& h, const std::string& k, const Value& stored);
std::string objHashKeyType(const Value& h);

std::string lubType(const std::string& a, const std::string& b);

struct SemaphoreState { std::mutex m; std::condition_variable cv; long count = 0; };
struct LockState { std::recursive_mutex m; };            // Raku Lock (used reentrantly by protect)
Value coerceToSigil(Value v, char sigil);
// Set by the Interpreter (it takes calling a user method to answer): the values
// an OBJECT contributes when it lands in a `%` container — its own `.list` or
// `.iterator`. See Interpreter::objListItems.
extern std::function<bool(const Value&, ValueList&)> g_objListItems;
bool defined(const Value& v);
bool isCoreTypeName(const std::string& n);
Value makeSignature(const Callable* c);
std::shared_ptr<Param> signatureParamCopy(const Param& p);
const std::vector<std::string>& typeAncestry(const std::string& t);

// G1 (GRAMMAR-PLAN): the last FAILED grammar parse's highwater on this
// thread — CHARACTER position plus the rule that was trying there. valid is
// false when the last parse on this thread succeeded (or none ran). Written
// by grammarParse, read by the `rakupp-parse-diagnosis` builtin.
struct GrammarParseDiag { bool valid = false; long pos = 0; std::string rule; };
GrammarParseDiag& grammarParseDiag();

// The grammar-service shim source baked into the library (GrammarShim.cpp,
// generated — see tools/grammar/gen-shim-src.raku). Served to hosts by
// rk_grammar_shim().
const char* grammarShimSource();
extern std::function<Value(const Value&)> g_deproxy; // reads a Proxy container
std::string rakuRepr(const Value& v, int depth, std::set<const void*>& seen);
std::string rakuRepr(const Value& v);
void spawnWithInput(const std::vector<std::string>& argv, const std::string& input,
                           std::string& out, int& exitCode, Interpreter* gil = nullptr,
                           const std::vector<std::string>* envKV = nullptr,
                           const std::string& cwd = "");

bool isBuiltinRole(const std::string& n);

Value complexSqrt(double re, double im);
// Parse a JSON document into a Value (the codec lives in Builtins.cpp); returns
// Any when the text is not valid JSON.
Value jsonParseDoc(const std::string& text);
const char* quantValueType(const std::string& kind);
void rejectNulPath(const std::string& path);

long long graphemeCount(const std::string& s);
[[noreturn]] void throwFailedOpen(const std::string& path);

long long cpCount(const std::string& s);
std::string mapCase(const std::string& s, int kind, int tcMode);
bool substSelectKnowsAdverb(const std::string& k);
uint32_t toUpperCp(uint32_t c);

} // namespace rakupp
