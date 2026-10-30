# For t/regression/module-enum-is-our.raku: an enum whose qualified name
# starts with the unit's own package.
unit module RakuppEnumOurUnit;
enum RakuppEnumOurUnit::Mode <Off On>;
enum Level <Low High>;
