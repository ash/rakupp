// SLIM-PLAN P3: the stub half of the `unicode-props` seam — linked in place
// of librakupp_ucd_props.a when --slim cuts the feature. Covers the Script,
// Block and Bidi_Class range tables, and the properties only uniprop and
// <:Prop<Value>> name (unicode_props2_gen.cpp: Age, Line_Break,
// East_Asian_Width, …), and the binary properties (unicode_props_gen.cpp);
// the never-cut properties (general category, case, normalization, the bidi
// mirror map, and the ID_Continue / Unified_Ideograph copies in
// unicode_props_core_gen.cpp) live in rt and are untouched.
// See stub_ucd_names.cpp for the mechanism.

#include "../ucd_seam.h"

namespace rakupp { namespace ucd {

const ScriptEnt* scriptsTable(size_t*) {
    featureMissing("unicode-props", "uniprop('Script') / <:Script<...>> (the Script range table)");
}
const BlockEnt* blocksTable(size_t*) {
    featureMissing("unicode-props", "uniprop('Block') / <:Block<...>> (the Block range table)");
}
const BidiEnt* bidiTable(size_t*) {
    featureMissing("unicode-props", "uniprop('Bidi_Class') (the Bidi_Class range table)");
}
const uint32_t* enumPropTable(const char*, size_t*, const char* const**) {
    featureMissing("unicode-props", "uniprop('Line_Break') / <:Line_Break<...>> and the other "
                                    "enumerated properties (Age, East_Asian_Width, ...)");
}
const uint32_t* unicode1NameTable(size_t*, const char* const**) {
    featureMissing("unicode-props", "uniprop('Unicode_1_Name') (the Unicode 1.0 name table)");
}
const uint32_t* jamoShortNameTable(size_t*, const char* const**) {
    featureMissing("unicode-props", "uniprop('Jamo_Short_Name') (the jamo short-name table)");
}
const uint32_t* bidiBracketTable(size_t*) {
    featureMissing("unicode-props", "uniprop('Bidi_Paired_Bracket') (the paired-bracket table)");
}
const PropRange* binPropsTable(size_t*, const char* const**, size_t*) {
    featureMissing("unicode-props", "uniprop / <:Prop> naming a binary property (Math, Emoji, "
                                    "ID_Start, ...: the binary-property table)");
}
const char* const* valueAliasTable(size_t*) {
    featureMissing("unicode-props", "<:Prop<Value>> naming a value by another of its aliases "
                                    "(the PropertyValueAliases table)");
}

} }
