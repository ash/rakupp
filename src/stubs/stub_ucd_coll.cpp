// SLIM-PLAN P3: the stub half of the `unicode-collation` seam — linked in
// place of librakupp_ucd_coll.a when --slim cuts the feature. See
// stub_ucd_names.cpp for the mechanism.

#include "../ucd_seam.h"

namespace rakupp { namespace ucd {

const uint16_t* collceTable(size_t*) {
    featureMissing("unicode-collation", "unicmp/coll/.collate (the DUCET element table)");
}
const uint32_t* collsingTable(size_t*) {
    featureMissing("unicode-collation", "unicmp/coll/.collate (the DUCET singles table)");
}
const uint32_t* collcontrTable(size_t*) {
    featureMissing("unicode-collation", "unicmp/coll/.collate (the DUCET contractions table)");
}

} }
