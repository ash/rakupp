// PlainFill.cpp — the default construction of a plain class, planned once
//
// `Point.new(x => 1, y => 2)` on a class of plain `has $.x` attributes is, in
// the end, two values stored under two names. runAttrDefaults (MethodCallPart2.cpp)
// reaches that through the ancestor walk, the named-argument table, the BUILD
// question per level, the store key per attribute and the checks a typed,
// defaulted, `@`/`%` or trait-carrying attribute needs: about 2,000
// instructions an attribute. Everything in that list but the argument values
// is a property of the class, so it is worked out once (buildPlan) and a
// construction walks the plan's slots (plainFillRun). A class or an argument
// the plan does not cover takes runAttrDefaults as before, and
// RAKUPP_ATTR_LAYOUT_CHECK keeps every construction there.
#include "AttrLayout.h"
#include "BuiltinsParts.h"
#include "PlainFill.h"
#include "PlainValue.h"

#include <atomic>
#include <mutex>

namespace rakupp {

extern std::atomic<uint64_t> g_symbolGen;   // Interpreter.cpp: moves on every structural symbol change

bool PlainFill::same(const PlainFill& o) const {
    if (ok != o.ok || noHooks != o.noHooks || slots.size() != o.slots.size()) return false;
    for (size_t i = 0; i < slots.size(); i++) {
        const Slot& a = slots[i];
        const Slot& b = o.slots[i];
        if (a.key != b.key || a.name != b.name || a.buildable != b.buildable || a.takesArg != b.takesArg ||
            a.dflt.t != b.dflt.t || !a.dflt.sameRep(b.dflt) || (a.dflt.t == VT::Num && a.dflt.n != b.dflt.n))
            return false;
    }
    return true;
}

// A default the fill stores without evaluating anything: none (the attribute
// starts as Any), or an Int or Num literal. Any other default is evaluated in
// the class's scope with `self` bound, which is runAttrDefaults' to do.
static bool literalDefault(const ClassAttr& at, Value& out) {
    if (at.hasDefVal) {
        if (!plainStoredValue(at.defVal) || at.defVal.t == VT::Object) return false;
        out = at.defVal;
    }
    else if (!at.def) out = Value::any();
    else if (at.def->kind == NK::IntLit) out = Value::integer(static_cast<const IntLit*>(at.def)->v);
    else if (at.def->kind == NK::NumLit) out = Value::number(static_cast<const NumLit*>(at.def)->v);
    else return false;
    out.readonly = out.immutableBind = false;
    return true;
}

static PlainFill buildPlan(ClassInfo* ci) {
    PlainFill pf;
    // the chain runAttrDefaults walks (attrChainOf), when it is the parent
    // chain alone: a second CLASS parent brings a linearization the plan leaves alone
    ClassInfo* buf[8];
    size_t n = 0;
    for (ClassInfo* c = ci; c; c = c->parent.get()) {
        if (n == 8) return pf;
        buf[n++] = c;
        for (auto& p : c->extraParents) if (p && !p->isRole) return pf;
    }
    bool hooks = ci->hasPopulate || ci->roleCtorHooks || ci->findMethod("BUILD") || ci->findMethod("TWEAK");
    for (ClassInfo* c = ci; c && !hooks; c = c->parent.get()) {   // (maybeRegisterDestroy's walk)
        hooks = c->methods.count("DESTROY") != 0;
        for (auto& p : c->extraParents) if (p && p->methods.count("DESTROY")) hooks = true;
    }
    pf.noHooks = !hooks;
    const std::vector<ClassInfo*> spill;
    const AttrChainView chain{buf, &spill, n};
    for (size_t lvlIx = n; lvlIx-- > 0;) {   // least derived first, as the walk stores
        ClassInfo* lvl = buf[lvlIx];
        auto bm = lvl->methods.find("BUILD");
        const bool ownBuild = bm != lvl->methods.end() && bm->second.t == VT::Code && bm->second.code() &&
                              bm->second.code()->ci().isSubmethod && !lvl->roleSubmethods.count("BUILD");
        for (auto& at : lvl->attrs) {
            PlainFill::Slot s;
            if (at.sigil != '$' || !at.type.empty() || !at.containerIs.empty() || at.defaultTrait || at.shape ||
                at.where || !at.doesRoles.empty() || at.buildFn.t == VT::Code || at.containerProto.t == VT::Object ||
                at.required || at.defConstraint || at.coerce || at.objKeyed || at.inlined ||
                !literalDefault(at, s.dflt))
                return pf;
            // one attribute of a name on the chain: a name declared twice is
            // two slots a named argument may both fill
            for (auto& have : pf.slots) if (have.name == at.name) return pf;
            std::string keyBuf;
            s.key = attrStoreKey(chain, lvlIx, at, keyBuf);
            s.hash = ValueHash::keyHash(s.key);
            s.name = at.name;
            s.buildable = (at.pub || at.built) && !at.notBuilt;
            s.takesArg = s.buildable && !ownBuild;
            pf.slots.push_back(std::move(s));
        }
    }
    pf.ok = pf.slots.size() <= 16;
    return pf;
}

const PlainFill* plainFillPlan(ClassInfo& ci) {
    const uint64_t key = g_symbolGen.load(std::memory_order_relaxed) + 1;
    const PlainFill* have = ci.plainFill.get();
    if (have && ci.plainFillKey.get() == key) return have;
    // A plan is kept for as long as the class has not changed: a generation
    // moves for every class, role or subset declared anywhere. One it replaces
    // stays the class's until the class goes (another thread may be walking it).
    static std::mutex mu;
    PlainFill now = buildPlan(&ci);
    std::lock_guard<std::mutex> lk(mu);
    have = ci.plainFill.get();
    if (!have || !have->same(now)) {
        auto made = std::make_shared<const PlainFill>(std::move(now));
        have = made.get();
        ci.plainFillOwned.push_back(std::move(made));
        ci.plainFill = have;
    }
    ci.plainFillKey = key;   // (after the plan: a reader of this key finds that plan or a later one)
    return have;
}

bool plainFillRun(const PlainFill& pf, ObjectData& od, ClassInfo& ci, const ValueList& args) {
    if (!pf.ok || RAKUPP_UNLIKELY(g_attrLayoutCheck != nullptr)) return false;
    const size_t ns = pf.slots.size();
    const Value* given[16] = {};
    for (auto& a : args) {   // each a named Pair (the caller's test); a name given twice keeps the last
        const Value* pv = a.pairVal();
        if (!pv || !plainStoredValue(*pv)) return false;
        const std::string_view name = a.s;
        size_t i = 0;
        while (i < ns && pf.slots[i].name != name) i++;
        if (i < ns && pf.slots[i].takesArg) { given[i] = pv; continue; }
        if (i < ns && pf.slots[i].buildable) continue;   // its class has a BUILD: the default stands
        // no attribute of the chain takes the name: one reached another way
        // (through a role, a second parent) is runAttrDefaults' last pass
        const ClassAttr* other = ci.findAttr(name);
        if (other && (other->pub || other->built) && !other->notBuilt) return false;
    }
    od.attrs.reserve(ns);
    for (size_t i = 0; i < ns; i++) {
        const PlainFill::Slot& s = pf.slots[i];
        Value v = given[i] ? *given[i] : s.dflt;
        v.readonly = v.immutableBind = false;
        od.attrs.addFresh(s.key, s.hash, std::move(v));
    }
    return true;
}

}  // namespace rakupp
