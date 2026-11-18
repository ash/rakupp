// PlainFill.h — the default construction of a plain class, planned once
#pragma once

#include "Value.h"

#include <string>
#include <vector>

namespace rakupp {

// What `K.new(…)` stores for a class whose `.new` is the default construction
// (plainNewClass, Builtins.cpp) and whose attributes are all plain `$`: one
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
        Value dflt;           // Any, or its literal default
    };
    std::vector<Slot> slots;
    bool ok = false;        // every attribute is one the fill stores as runAttrDefaults would
    bool noHooks = false;   // no POPULATE, BUILD, TWEAK or DESTROY in the ancestry: nothing follows the fill
    bool same(const PlainFill& o) const;
};

// The plan for `ci`, a class plainNewClass answered 1 for. Never null.
const PlainFill* plainFillPlan(ClassInfo& ci);
// The fill: `od`'s attributes from the named arguments `args` and the
// defaults, as runAttrDefaults leaves them. False, with nothing stored, when
// the plan or an argument is not one it takes; runAttrDefaults answers then.
bool plainFillRun(const PlainFill& pf, ObjectData& od, ClassInfo& ci, const ValueList& args);

}  // namespace rakupp
