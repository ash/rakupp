#pragma once
// The SLIM seam (docs/dev/plans/SLIM-PLAN.md, P1): the four cuttable Unicode
// table groups are reached ONLY through these accessors, each defined in the
// same translation unit as its data. That placement is the whole mechanism:
// when P3 exists, `--exe` links either the real table's archive or a stub that
// throws X::Feature::NotBuilt, and nothing else in the runtime can tell the
// difference — or reach the data behind the accessor's back. Unicode.cpp no
// longer declares these tables at all, so a new direct reference is a compile
// error rather than a hole in the seam.
//
// The never-cut tables (general category, binary properties, normalization,
// case, grapheme break — reached by ordinary string operations) keep their
// direct references on purpose; a seam there would be indirection with no
// stub ever standing behind it.
//
// Accessors return pointer + count ONCE; callers hoist both into locals before
// any loop, so inner loops still index raw memory. `ucaElements()` touches
// COLLCE per collation element — a call per element would be a measured
// regression, and P1's gate (perf-guard flat on the collation paths) exists
// for exactly that.
//
// The structs are defined identically in the generated data files (which stay
// header-free so regeneration is trivial); ODR requires the copies to match
// token for token — change one, change all, regenerate.

#include <cstddef>
#include <cstdint>

namespace rakupp {

// What every stub throws (defined in FeatureGate.cpp, so it is always in rt):
// X::Feature::NotBuilt, naming the missing feature and the operation that
// needed it. The stubs (src/stubs/, librakupp_stubs.a) are the P3 half of the
// seam — linked in place of a feature's real archive when --slim cuts it.
[[noreturn]] void featureMissing(const char* feature, const char* neededFor);

namespace ucd {

struct NameEnt { const char* name; uint32_t cp; };
struct BlockEnt { uint32_t lo, hi; const char* name; };
struct ScriptEnt { uint32_t lo, hi; const char* name; };
struct BidiEnt { uint32_t lo, hi; const char* bc; };

// feature `unicode-names` — unicode_names.cpp (tools/gen-unicode.raku)
const NameEnt*  namesTable(size_t* n);   // sorted by name; n = entries
const int64_t*  numvTable(size_t* n);    // flat (cp,num,den) triples; n = int64 count (rows × 3)
// The PROBE spelling: null when the feature is cut, instead of throwing. For
// classification questions whose answer is almost always "no" — the LEXER asks
// "is this cp an Nl/No numeral?" for every non-ASCII char it meets, comments
// included, and a » in a comment must not detonate a slim binary at startup.
// Value-PRODUCING askers (.unival) keep the throwing accessor: cuts throw.
const int64_t*  numvTableOrNull(size_t* n);

// feature `unicode-collation` — unicode_coll_gen.cpp (tools/gen_unicode_coll.py)
const uint16_t* collceTable(size_t* n);    // flat (L1,L2,L3) triples; n = uint16 count
const uint32_t* collsingTable(size_t* n);  // rows of 3: cp, off, cnt; n = rows
const uint32_t* collcontrTable(size_t* n); // rows of 5: cp0, cp1, cp2, off, cnt; n = rows

// feature `unicode-props` — one file per table
const ScriptEnt* scriptsTable(size_t* n);  // unicode_scripts_gen.cpp
const BlockEnt*  blocksTable(size_t* n);   // unicode_blocks_gen.cpp
const BidiEnt*   bidiTable(size_t* n);     // unicode_bidi_gen.cpp

} }
