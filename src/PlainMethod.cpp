// PlainMethod.cpp — which routines take the lean call lanes: a sub
// callPlainSub (plainSubShape), a method callPlainMethod (plainMethodShape)
//
// invokeMethod (InterpreterCore.cpp) is written for everything a method may
// be: a native call, a wrapped routine, a multi's dispatcher, a body with
// phasers, a CATCH, `state`, nested subs, an `is rw` return, a signature of
// named, optional, slurpy, typed and attributive parameters. A method that is
// none of that — `method len2() { … }`, `method shift($d) { … }`, a multi
// candidate `(Int $x where * > 0)` its dispatcher has already judged — needs
// a frame, `self`, its arguments in their slots and its statements run:
// callPlainMethod. This is the test it is taken by, decided once per routine
// after a first call through the full path has settled what that path decides
// (the pads, the CATCH and phaser scans, the hoisting, `@_`).
#include "CallLanes.h"

namespace rakupp {

// A PLAIN SUB: a named or anonymous routine (not a block, method, multi,
// builtin or native) whose signature is plain `$x` positionals — no default,
// no trait, no where, no type but a LeanType — and whose body has no phaser,
// CATCH, CONTROL, nested named sub or hoisted `my`, and no `is rw` or `-->`
// but a LeanType on the routine. Most of callCallableRaw exists for what such
// a sub does not have. Decided once, after the first call has settled what it
// decides (pads, the CATCH scan, hoistNeed, the arity bounds); until then -1
// keeps the full path. 2 when a type is named (callPlainSub's typed arms).
signed char plainSubShape(Callable& c) {
    signed char v = c.ci().plainShape;
    if (v >= 0) return v;
    if (c.ci().padReady != 1 || c.ci().catchScan.get() < 0 || c.ci().controlScan.get() < 0 ||
        (signed char)c.ci().hoistNeed < 0 || c.ci().arityShape.get() < 0)
        return false;                                   // not settled yet
    bool plain = !c.ci().isBlock && !c.ci().isMethod && !c.ci().builtin && !c.cold().isNative && !c.ci().isMultiDispatcher &&
                 !c.ci().isMultiCandidate && !c.ci().isProto && !c.ci().isWhateverCode && !c.ci().isRegexRoutine &&
                 !c.cold().deprecated && !c.ci().testAssertion && !c.ci().isCached && c.ci().hadSig && c.ci().params && c.ci().body &&
                 c.ci().placeholders.empty() && !c.ci().usesArgs && !c.ci().implicitArgs && !c.cold().hasPrimed &&
                 !c.ci().retLiteral && !c.ci().retRw && c.ci().padLayout &&
                 bodyEntryWork(c) == 0 && c.ci().catchScan.get() == 0 && c.ci().controlScan.get() == 0 &&
                 (signed char)c.ci().hoistNeed == 0 && c.ci().arityShape.get() == 1 && !c.ci().arityUnbounded;
    const signed char ret = plain ? leanTypeOf(c.cold().retType, true) : 0;
    bool typed = ret > 0;
    if (ret < 0) plain = false;
    if (plain)
        for (auto& p : *c.ci().params) {
            const signed char k = leanTypeOf(p.type, false);
            if (p.sigil != '$' || p.named || p.slurpy || p.optional || p.invocant || p.isCopy ||
                p.isRw || p.isRaw || p.defaultVal || p.subSig || p.litVal || p.whereExpr ||
                p.hadWhere || p.defConstraint || p.coerce || p.typeCapture || p.codeSig ||
                !p.captureName.empty() || k < 0 || p.padSlot < 0 ||
                (const void*)p.padOwner != (const void*)c.ci().padLayout.get() ||
                (p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.'))) { plain = false; break; }
            p.leanType = k;
            if (k) typed = true;
        }
    c.ci().leanRet = ret > 0 ? ret : 0;
    c.ci().plainShape = !plain ? 0 : typed ? 2 : 1;
    return c.ci().plainShape;
}

// 0: the full path. 1: plain. 2: plain, and a parameter names a core type the
// argument must already be (leanArgFits). 3: the same with a `where` on a
// parameter, for a call whose dispatch has run it (whereVerified).
signed char plainMethodShape(Callable& c) {
    signed char v = c.ci().plainMeth;
    if (v >= 0) return v;
    if (c.ci().padReady != 1 || c.ci().catchScan.get() < 0 || c.ci().controlScan.get() < 0 ||
        c.ci().phaserScan.get() < 0 || (signed char)c.ci().hoistNeed < 0)
        return 0;                                       // not settled yet
    const bool noSig = !c.ci().params || c.ci().params->empty();
    if (noSig && (signed char)c.ci().atArgsScan < 0) return 0;
    bool plain = c.ci().isMethod && !c.ci().subAsMethod && !c.ci().isBlock && !c.ci().builtin && !c.cold().isNative &&
                 !c.ci().isMultiDispatcher && !c.ci().isProto && !c.ci().isProtoBody && !c.ci().isWhateverCode &&
                 !c.ci().isRegexRoutine && !c.ci().isStub && !c.cold().deprecated && !c.ci().testAssertion &&
                 !c.ci().isCached && c.ci().body && c.ci().placeholders.empty() && !c.ci().usesArgs &&
                 !c.ci().implicitArgs && !c.cold().hasPrimed && !c.ci().retLiteral && !c.ci().retRw &&
                 c.cold().retType.empty() && c.ci().padLayout && bodyEntryWork(c) == 0 &&
                 c.ci().catchScan.get() == 0 && c.ci().controlScan.get() == 0 && c.ci().phaserScan.get() == 0 &&
                 (signed char)c.ci().hoistNeed == 0 && (!noSig || (signed char)c.ci().atArgsScan == 0);
    signed char shape = 1;
    if (plain && !noSig)
        for (auto& p : *c.ci().params) {
            const signed char k = leanTypeOf(p.type, false);
            if (p.sigil != '$' || p.named || p.slurpy || p.optional || p.invocant || p.isCopy || p.isRw ||
                p.isRaw || p.defaultVal || p.subSig || p.litVal || p.defConstraint || p.coerce ||
                p.typeCapture || p.codeSig || !p.captureName.empty() || k < 0 || k == LeanNum || k == LeanInt ||
                p.padSlot < 0 || (const void*)p.padOwner != (const void*)c.ci().padLayout.get() ||
                (p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.'))) { plain = false; break; }
            p.leanType = k;
            if (k && shape < 2) shape = 2;
            if (p.whereExpr || p.hadWhere) shape = 3;
        }
    c.ci().plainMeth = plain ? shape : 0;
    return c.ci().plainMeth;
}

}  // namespace rakupp
