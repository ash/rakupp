// The builtin Raku table and its pre-parsed registry (BuiltinRakuSrc.h).

#include "BuiltinRakuSrc.h"

#include <utility>
#include <vector>

namespace rakupp {

// Channel.Supply: through the channel's queue on the scheduler, as Rakudo's
// (MethodCallPart1c.cpp binds $__rakupp_chan before evaluating it).
const char* const kChannelSupplySrc = "supply { whenever $__rakupp_chan -> \\v { emit v } }";
// .stable / .delayed: Rakudo's own shape, a supply block with a Promise.in per
// value (MethodCallPart2.cpp). Cro's `cro run` debounces its watcher with .stable(1).
const char* const kSupplyStableSrc =
    "(-> $s, $time { supply { my $gen = 0; whenever $s -> \\v { my $mine = ++$gen; "
    "whenever Promise.in($time) { emit v if $gen == $mine } } } })";
const char* const kSupplyDelayedSrc =
    "(-> $s, $time { supply { whenever $s -> \\v { "
    "whenever Promise.in($time) { emit v } } } })";
// .map / .grep / .do on a supply block: on demand, tapping the source when
// the result is tapped (MethodCallPart2.cpp)
const char* const kSupplyMapSrc  = "(-> $s, &f { supply { whenever $s -> \\v { emit f(v) } } })";
const char* const kSupplyGrepSrc = "(-> $s, Mu $t { supply { whenever $s -> \\v { emit v if v ~~ $t } } })";
const char* const kSupplyDoSrc   = "(-> $s, &f { supply { whenever $s -> \\v { f(v); emit v } } })";

const BuiltinRakuSrc kBuiltinRakuSrcs[] = {
    {"Channel.Supply",   kChannelSupplySrc,  nullptr},
    {"Supply.stable",    kSupplyStableSrc,   nullptr},
    {"Supply.delayed",   kSupplyDelayedSrc,  nullptr},
    {"Supply.map",       kSupplyMapSrc,      nullptr},
    {"Supply.grep",      kSupplyGrepSrc,     nullptr},
    {"Supply.do",        kSupplyDoSrc,       nullptr},
    // CompUnit::Handle.globalish-package / .unit / .export-package (Builtins.cpp)
    {"GLOBAL",           "GLOBAL",           nullptr},
    {"UNIT::",           "UNIT::",           nullptr},
    {"EXPORT",           "EXPORT",           nullptr},
    // the two classes: big enough to carry only for a program that names them
    {"IO::CatHandle",    kCatHandleSrc,      "cathandle"},
    {"IO::Notification", kIoNotificationSrc, "notification"},
};
const size_t kBuiltinRakuSrcsN = sizeof kBuiltinRakuSrcs / sizeof kBuiltinRakuSrcs[0];

// Filled by static initializers in the generated code, before main, and only
// read afterwards — so no lock. Empty everywhere but a binary that cut `eval`.
static std::vector<std::pair<size_t, std::string>>& preparsed() {
    static std::vector<std::pair<size_t, std::string>> v;
    return v;
}
void rakuppRegisterPreparsed(size_t index, const char* blob, size_t len) {
    if (index < kBuiltinRakuSrcsN) preparsed().emplace_back(index, std::string(blob, len));
}
const std::string* rakuppPreparsedFor(const std::string& src) {
    for (auto& p : preparsed())
        if (src == kBuiltinRakuSrcs[p.first].src) return &p.second;
    return nullptr;
}

}
