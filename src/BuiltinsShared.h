#pragma once
// Helpers shared between Builtins.cpp and the method-dispatch segments split out
// of it (MethodCallTail.cpp, …). They were file-static in Builtins.cpp; splitting
// that file is what forced them into a header. Internal to the implementation —
// nothing outside src/ should include this.
#include "Value.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>

namespace rakupp {

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

bool deepEq(const Value& a, const Value& b);
bool matcherAccepts(Interpreter& I, const Value& v, const Value& mt);
uint32_t toLowerCp(uint32_t c);
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
