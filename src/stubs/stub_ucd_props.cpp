// SLIM-PLAN P3: the stub half of the `unicode-props` seam — linked in place
// of librakupp_ucd_props.a when --slim cuts the feature. Covers the Script,
// Block and Bidi_Class range tables; the never-cut properties (general
// category, binary props, case, normalization) live in rt and are untouched.
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

} }
