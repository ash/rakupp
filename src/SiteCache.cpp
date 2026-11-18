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

}  // namespace rakupp
