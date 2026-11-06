// AttrLayout.h — the keys an instance of a class stores its declared
// attributes under, in the order runAttrDefaults stores them
// (docs/dev/plans/OBJECT-SLOTS-PLAN.md). Batch 1 builds the layout and checks
// it against every object runAttrDefaults builds; nothing reads it yet.
#pragma once

#include "Value.h"

#include <memory>
#include <string>
#include <vector>

namespace rakupp {

// The ancestor chain runAttrDefaults walks (attrChainOf: the class itself,
// then its parents, and a second class parent's whole linearization), as it
// holds it: the first eight on its stack, the rest spilled.
struct AttrChainView {
    ClassInfo* const* buf;
    const std::vector<ClassInfo*>* spill;
    size_t n;
    ClassInfo* at(size_t i) const { return i < 8 ? buf[i] : (*spill)[i - 8]; }
};

// `has Digest $.digest` beside `has &!digest`: same bare name, different
// sigils. The table is keyed by bare name, so the twins clobbered each other
// (Auth::SCRAM's callable ended up holding the enum). A non-$ twin stores
// under "&name"/"@name"/"%name"; the read/write paths try that spelling first.
// Twins are a matter of ONE class. The same name declared again by a subclass,
// whatever its sigil, is shadowing: the ancestor's slot is the class-qualified
// one and the subclass keeps the bare name, which is where its accessor reads
// (ClassInfo::findAccessorAttr). Counting sigils over the whole chain put
// `class B is A { has %.data }` under "%data", beside A's `has @.data`, and
// left `B.new.data` answering Any. Only a NON-$ attribute can need the
// sigil-prefixed spelling, so a class of plain `$` attributes never scans.
inline bool attrHasTwin(const ClassInfo* lvl, const ClassAttr& at) {
    for (auto& a2 : lvl->attrs)
        if (a2.sigil != at.sigil && a2.name == at.name) return true;
    return false;
}

// The key the attribute `at`, declared at level `lvlIx` of `chain`, is stored
// under: its bare name; "@name" for a non-$ attribute with a same-named twin
// of another sigil in its own class; "Owner\x01name" when a more derived
// level declares the name too. The one rule runAttrDefaults and the layout
// share, inline because runAttrDefaults asks it for every attribute of every
// construction (always inline: out of line, the call cost every construction
// ~86 instructions, objnew +0.8%). Points into `at` or into `buf`.
RAKUPP_ALWAYS_INLINE const std::string& attrStoreKey(const AttrChainView& chain, size_t lvlIx, const ClassAttr& at, std::string& buf) {
    const ClassInfo* lvl = chain.at(lvlIx);
    // a MORE-DERIVED class declares this name too: this level's storage is its
    // own, under a class-qualified key
    for (size_t k = 0; k < lvlIx; k++)
        for (auto& a2 : chain.at(k)->attrs)
            if (a2.name == at.name) { buf = lvl->name + "\x01" + at.name; return buf; }
    if (at.sigil != '$' && attrHasTwin(lvl, at)) { buf = std::string(1, at.sigil) + at.name; return buf; }
    return at.name;
}

struct AttrLayout {
    std::vector<std::string> keys;   // in store order: least derived level first, declaration order within
    // the chain it was built from — each level and its attribute count — so a
    // class whose attributes change (makeTypedEx appends to exception classes,
    // the MOP adds) gets a new layout, and an object keeps the one it was built with
    std::vector<std::pair<const ClassInfo*, size_t>> built;
};

// The layout for `ci` over `chain`, built once per version of the chain.
std::shared_ptr<const AttrLayout> attrLayoutFor(ClassInfo* ci, const AttrChainView& chain);

// RAKUPP_ATTR_LAYOUT_CHECK=<file> (batch 1): every object runAttrDefaults
// builds is compared with its class's layout, and each distinct difference is
// appended to the file as one line — pid, kinds, class, the keys stored, the
// layout's keys — with a summary line per process at exit. Null when unset.
extern const char* const g_attrLayoutCheck;
void attrLayoutCheck(const ObjectData& od, ClassInfo* ci, const AttrChainView& chain, bool wasEmpty);

}  // namespace rakupp
