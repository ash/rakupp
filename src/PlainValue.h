// PlainValue.h — the values a plain `$` container holds as they are
#pragma once

#include "Value.h"

namespace rakupp {

// A value a plain, untyped `$` — a `my $x`, a `has $.x` — stores as it is: an
// Int, Num, Str or Bool with no tag on it, or an instance of a class. Not a
// list or a hash (itemized by the store), a Failure, a type object, Nil, a
// native, an enum value, an allomorph or a bound container: the general
// stores have an arm for each of those. The lanes that skip those stores
// (Interpreter::declCallLane, plainFillRun) ask this first.
inline bool plainStoredValue(const Value& v) {
    if (v.natBits() || !v.hashKind.empty() || v.x_ || !v.enumName().empty() || !v.enumType().empty() ||
        v.isList || v.itemized)
        return false;
    if (v.t == VT::Object) return v.obj() && v.obj()->cls && !v.obj()->hasBoxed;
    return v.pk_ == PK::None && (v.t == VT::Int || v.t == VT::Num || v.t == VT::Str || v.t == VT::Bool);
}

}  // namespace rakupp
