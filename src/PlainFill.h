// PlainFill.h — the default construction of a plain class, planned once
#pragma once

#include "Value.h"

#include <string>
#include <vector>

namespace rakupp {

// What `K.new(…)` stores for a class whose `.new` is the default construction
// (plainNewClass, Builtins.cpp) and whose attributes are all plain `$`,
// untyped or of one of four built-in types (Slot::kind): one
// slot an attribute, in the order runAttrDefaults stores them, each with the
// key it is stored under, that key's hash and the value it starts from. Built
// once per class and kept while the symbol generation finds it unchanged
// (ClassInfo::plainFill). The plans are the class's, freed with it: one it
// replaced may still be read by a construction on another thread.
struct PlainFill {
    struct Slot {
        std::string key;      // attrStoreKey's
        uint64_t hash = 0;    // ValueHash::keyHash(key)
        std::string name;     // the attribute's, which a named argument gives
        bool buildable = false;   // public or `is built`: a named argument may be for it
        bool takesArg = false;    // …and is, its class having no BUILD of its own
        Value dflt;           // Any (its type object, for a typed one), or its literal default
        // `has Int $.x`: 'I', 'N', 'S' or 'B' for Int, Num, Str or Bool — an
        // argument must then be a plain value of exactly that type for the
        // fill to store it (anything else is runAttrDefaults', check and all); 0 untyped
        char kind = 0;
    };
    std::vector<Slot> slots;
    bool ok = false;        // every attribute is one the fill stores as runAttrDefaults would
    bool noHooks = false;   // no POPULATE, BUILD, TWEAK or DESTROY in the ancestry: nothing follows the fill
    bool same(const PlainFill& o) const;
};

// Whether a plain stored value (plainStoredValue) is exactly of a typed slot's type
inline bool plainFillKindIs(char kind, const Value& v) {
    return v.t == (kind == 'I' ? VT::Int : kind == 'N' ? VT::Num : kind == 'S' ? VT::Str : VT::Bool);
}

// The plan for `ci`, a class plainNewClass answered 1 for. Never null.
const PlainFill* plainFillPlan(ClassInfo& ci);
// The fill: `od`'s attributes from the named arguments `args` and the
// defaults, as runAttrDefaults leaves them. False, with nothing stored, when
// the plan or an argument is not one it takes; runAttrDefaults answers then.
bool plainFillRun(const PlainFill& pf, ObjectData& od, ClassInfo& ci, const ValueList& args);

}  // namespace rakupp
