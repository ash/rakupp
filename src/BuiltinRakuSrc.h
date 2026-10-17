#pragma once
// The builtin Raku: classes and combinators the runtime keeps as Raku source
// and compiles (evalString) the first time a program needs them, because they
// are simplest written in Raku. They are gathered in ONE table so a compile
// mode can parse them ahead of time. A --slim binary that cut its parser
// carries their ASTs, registered at start-up, and evalString reads the AST in
// place of the text: before, a slim'd `$supply.map(…)`, `$channel.Supply` or
// `IO::CatHandle.new` died needing the parser the scan had proved unused.
//
// A snippet is found by its exact TEXT, so a call site passes the constant
// below, never a copy of it spelled again.

#include <cstddef>
#include <string>

namespace rakupp {

struct BuiltinRakuSrc {
    const char* name;   // for the generated file's comments
    const char* src;
    // the SlimScan tag a program must show for a compile to carry this one's
    // AST (SlimScanResult::builtinTags); nullptr = carried always (they are
    // small, and reached by names too common to read anything into: .map)
    const char* needs;
};
extern const BuiltinRakuSrc kBuiltinRakuSrcs[];
extern const size_t kBuiltinRakuSrcsN;

// the snippets, each beside its one caller or in BuiltinRakuSrc.cpp
extern const char* const kCatHandleSrc;        // CatHandleSrc.cpp: IO::CatHandle
extern const char* const kIoNotificationSrc;   // MethodCallPart1c.cpp: IO::Notification
extern const char* const kChannelSupplySrc;    // $channel.Supply
extern const char* const kSupplyStableSrc;     // $supply.stable($t)
extern const char* const kSupplyDelayedSrc;    // $supply.delayed($t)
extern const char* const kSupplyMapSrc;        // supply { … }.map(&f)
extern const char* const kSupplyGrepSrc;       // supply { … }.grep($t)
extern const char* const kSupplyDoSrc;         // supply { … }.do(&f)

// The compiled binary's start-up registers entry `index`'s AST (an AstSerial
// blob) — the CLI that compiled it has the same table, from the same build.
void rakuppRegisterPreparsed(size_t index, const char* blob, size_t len);
// The registered AST for this exact source text, or null.
const std::string* rakuppPreparsedFor(const std::string& src);

}
