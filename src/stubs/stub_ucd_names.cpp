// SLIM-PLAN P3: the stub half of the `unicode-names` seam. This object (in
// librakupp_stubs.a) is linked in place of librakupp_ucd_names.a when --slim
// cuts the feature; the accessors throw X::Feature::NotBuilt instead of
// returning tables. One object per feature, so the linker pulls exactly the
// stubs whose real archive is absent — see ucd_seam.h for the seam itself.
//
// NOT part of rt or librakupp (CMakeLists removes src/stubs/ from the glob):
// an interpreter with both the real table and a throwing double would be a
// one-definition-rule violation waiting for a link-order change.

#include "../ucd_seam.h"

namespace rakupp { namespace ucd {

const NameEnt* namesTable(size_t*) {
    featureMissing("unicode-names", "uniname/uniparse (the Unicode name table)");
}
const int64_t* numvTable(size_t*) {
    featureMissing("unicode-names", "unival (the Unicode numeric-value table)");
}
// the probe form: a cut table answers "table absent" instead of throwing —
// the lexer classifies every non-ASCII cp against it (see ucd_seam.h)
const int64_t* numvTableOrNull(size_t* n) { *n = 0; return nullptr; }

} }
