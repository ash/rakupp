// SiteCache.cpp — per-site facts about the classes that reach a node, and the
// lanes that read them
#include "BuiltinsParts.h"
#include "InterpreterParts.h"
#include "SiteCache.h"

#include <unordered_set>

namespace rakupp {

extern std::atomic<uint64_t> g_symbolGen;   // Interpreter.cpp: moves on every structural symbol change

// ---------------------------------------------------------------------------
// `$!x` and `$.x` read in a method body (eval's VarExpr arm).
//
// evalVarExpr's attribute arm asks, for every read: is `self` an object, is
// its class a C struct, does a method of the name exist (`$.x` is `self.x`),
// has the attribute a sigil twin, does another class of the chain declare the
// name too (attrSlotFor) — and then hashes the name to find the slot. For a
// `$` attribute of a class with no native representation whose chain declares
// the name once, and, for `$.x`, has no method of the name, the answers are
// "no" each time and the read is the slot under the bare name. Those two facts
// are kept per class at the node; the hash of the name with them.
//
// Anything else — no `self`, a self that is not an object, a class that
// fails either test, a slot the object does not have — is evalVarExpr's.
// ---------------------------------------------------------------------------
static signed char attrLaneShape(const VarExpr* ve) {
    const std::string& n = ve->name;
    if (ve->pkgSymbol || ve->viaPseudoPkg || ve->padSlot >= 0 || n.size() < 3 || n[0] != '$' ||
        (n[1] != '!' && n[1] != '.') || !ve->attrTwin.empty() || ve->attrBare.empty() ||
        n.compare(1, std::string::npos, "!anon") == 0)
        return 0;
    return n[1] == '!' ? 1 : 2;
}

bool Interpreter::attrReadLane(const VarExpr* ve, ExecContext& tc, Value& out) {
    signed char lane = ve->attrLane;
    if (lane <= 0) {
        if (lane == 0) return false;
        ve->attrLane = lane = attrLaneShape(ve);
        if (!lane) return false;
    }
    Value* selfp = tc.cur->findSelf();
    if (!selfp || selfp->t != VT::Object) return false;
    ObjectData* od = selfp->obj();
    ClassInfo* ci = od ? od->cls.get() : nullptr;
    if (!ci) return false;
    const uint64_t gen = g_symbolGen.load(std::memory_order_relaxed) + 1;
    SiteEntry* e = ve->attrSite.find(ci, ci->serial);
    uint64_t okFact = 0, hash = 0;
    if (!e || !SiteCache::read(*e, gen, okFact, hash)) {
        if (!e && !(e = ve->attrSite.add(ci, ci->serial))) return false;
        const std::string& an = ve->attrBare;
        bool ok = ci->repr.empty();
        if (ok && lane == 1) {   // the bare name is the slot's key (attrSlotFor, whatever routine asks)
            std::string buf;
            ok = &attrSlotFor(od, an, buf) == &an && ci->shadowsAttrs == 0;
        }
        if (ok && lane == 2)     // no method of the name: `$.x` is the attribute's own accessor
            for (ClassInfo* c = ci; c && ok; c = c->parent.get()) ok = !c->methods.count(an);
        SiteCache::publish(*e, gen, okFact = ok ? 1 : 0, hash = ValueHash::keyHash(an));
    }
    if (!okFact) return false;
    Value* slot = od->attrs.findPre(ve->attrBare, hash);
    if (!slot) return false;
    {
        ParStripe rs(*this, slot);   // torn-copy contract, as evalVarExpr reads it
        raceAttr(od, slot, RaceAcc::Read);
        out = *slot;
    }
    // `$.a` is `$(self.a)`: an item
    if (lane == 2 && (out.t == VT::Array || out.t == VT::Hash) && out.hashKind != "Proxy") out.itemized = true;
    return true;
}

// ---------------------------------------------------------------------------
// `$obj.name(…)` on an instance of a class (leanMethodCall).
//
// methodCall is one function for every invocant there is: a call on a user
// object passes some sixty tests of the method's name and the invocant's kind
// on its way to methodCallOwn, which looks the name up through the class's
// ancestry, and invokeMethodChain, which asks whether an ancestor or a
// built-in stands behind the method (for `callsame`). None of those answers
// depends on the object, only on its class, the name and the symbol tables:
//
//   1  the name is a public `$` attribute's accessor and nothing else
//      (plainAccessorSlot): the slot, read by a hash made once
//   2  the class's own method, with no ancestor's of the name and no built-in
//      behind it: invokeMethod, as invokeMethodChain's plain arm calls it
//   3  the class's own method otherwise: invokeMethodChain, handed the lookup
//   0  anything methodCall decides some other way
//
// The names methodCall treats after the call (the Seq mark of `.keys` and its
// family) or around it (`new`, `bless`) are 0, as are a native representation,
// a delegated name and a stub. What the tests ask of the CALL is asked at each
// call: a Junction argument autothreads in methodCall, and a language-revision
// switch, a user metaobject or the race detector keep every call there.
// ---------------------------------------------------------------------------
static bool siteLongWayName(const std::string& m) {
    static const std::unordered_set<std::string> names = {
        "new", "bless", "CREATE", "perl", "Stringy", "printf", "sprintf", "kv", "keys", "values", "pairs",
        "antipairs", "invert", "reverse", "rotate", "sort", "unique", "squish", "head", "tail", "skip", "rotor",
        "batch", "toggle", "collate", "repeated"};
    return m.empty() || m[0] == '^' || m[0] == '!' || names.count(m) != 0;
}

void Interpreter::siteMethodFill(const MethodCall* mc, const Value& inv, SiteEntry& e, uint64_t gen) {
    ClassInfo* ci = inv.obj()->cls.get();
    const std::string& m = mc->method;
    uint64_t a = 0, b = 0;
    if (ci->repr.empty() && !siteLongWayName(m) && ci->name != "PseudoStash" &&
        !(!ci->delegatedNames.empty() && ci->delegatedNames.count(m))) {
        ClassInfo* owner = nullptr;
        if (Value* um = ci->findMethodForCall(m, langRev_ < 2, &owner)) {
            if (um->t == VT::Code && um->code() && !um->code()->ci().isStub) {
                // invokeMethodChain's plain arm: no method of the name above the
                // owner, one line of inheritance, no built-in parent
                bool plain = owner && !hasMultipleInheritance(ci) &&
                             !(owner->parent && owner->parent->findMethod(m));
                for (ClassInfo* c = owner; c && plain; c = c->parent.get()) plain = c->nativeParent.empty();
                a = plain ? 2 : (3 | (uint64_t)(uintptr_t)owner);
                b = (uint64_t)(uintptr_t)um;
            }
        }
        else if (mc->args.empty() && !(m[0] >= 'A' && m[0] <= 'Z')) {
            const ClassAttr* at = ci->findAttr(m);
            if (at && at->sigil == '$' && plainAccessorSlot(inv, m)) { a = 1; b = ValueHash::keyHash(m); }
        }
    }
    SiteCache::publish(e, gen, a, b);
}

bool Interpreter::siteMethodCall(MethodCall* mc, const Value& inv, Value& out) {
    ObjectData* od = inv.obj();
    ClassInfo* ci = od ? od->cls.get() : nullptr;
    if (!ci || mc->bang) return false;
    const uint64_t gen = g_symbolGen.load(std::memory_order_relaxed) + 1;
    SiteEntry* e = mc->site.find(ci, ci->serial);
    uint64_t a = 0, b = 0;
    if (!e || !SiteCache::read(*e, gen, a, b)) {
        if (!e && !(e = mc->site.add(ci, ci->serial))) return false;
        siteMethodFill(mc, inv, *e, gen);
        if (!SiteCache::read(*e, gen, a, b)) return false;
    }
    if (!a || RAKUPP_UNLIKELY(anyRevSwitch_ || haveCustomHows_.load(std::memory_order_relaxed) || RAKUPP_RACE_ON()))
        return false;
    if (a == 1) {
        if (const Value* slot = od->attrs.findPre(mc->method, b)) {
            ParStripe rs(*this, slot);   // torn-copy contract
            raceAttr(od, slot, RaceAcc::Read);
            out = *slot;
        }
        else out = Value::any();
        seqMint(out, inv);
        return true;
    }
    ExecContext& t = tctx();
    struct RwInvG { ExecContext& t; Expr* prev; ~RwInvG() { t.rwInvocantExpr = prev; } } rwInvG{t, t.rwInvocantExpr};
    t.rwInvocantExpr = mc->inv.get();
    ValueList args = evalArgs(mc->args);
    bool junction = false;
    for (auto& x : args) if (x.t == VT::Array && isJunction(x)) { junction = true; break; }
    Value* um = (Value*)(uintptr_t)b;
    if (RAKUPP_UNLIKELY(junction)) out = methodCall(inv, mc->method, std::move(args), &mc->args);
    else if (a == 2) {
        // (as invokeMethodChain hands it on: the built-in a `callsame` in the body falls back to)
        ExecContext::BuiltinFallback fb{&mc->method, &inv, &args, 0};
        out = invokeMethod(*um, inv, args, &mc->args, /*ownFrame=*/false, /*selfBack=*/nullptr,
                           /*skipWrappers=*/false, /*whereVerified=*/false, &fb);
    }
    else out = invokeMethodChain(mc->method, ci, inv, std::move(args), &mc->args, um,
                                 (ClassInfo*)(uintptr_t)(a & ~uint64_t(3)));
    if (RAKUPP_UNLIKELY(!out.hashKind.empty()) && !junction) {   // (an error tagged to be raised here, as methodCall raises it)
        if (isNoMethod(out)) throwNoMethod(std::move(out));
        if (out.t == VT::Pair && out.hashKind == kThrowTag) throwMarked(std::move(out));
    }
    seqMint(out, inv);
    return true;
}

}  // namespace rakupp
