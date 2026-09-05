// DataZlib.h — the `zlib` tag's engine primitives (DATA-PLAN P4).
//
// Seven names: `compress`, `uncompress`, `gzslurp`, `gzspurt`, `crc32`,
// `adler32`, `zlib-backend`. The first four are `Compress::Zlib`'s signatures
// character for character, so a program swaps `use Data::Native <zlib>` in for
// `use Compress::Zlib` unchanged; `:gzip` and `:raw` and the two checksums are
// the documented superset, which that module reaches only through its
// `Compress::Zlib::Stream` class or not at all.
//
// The FORMAT is src/Zlib.{h,cpp}. This file is what an argument may be, what
// comes back, and what is refused.
//
// TWIN: Compress::Zlib::Native's lib/Compress/Zlib/Native.rakumod. Where that
// module says `Compress::Zlib::Native: …` in an error this says the routine's
// own name, naming a module that was not involved being a lie — the same
// deliberate difference DataCsv.cpp and DataDigest.cpp carry.
#pragma once

#include "Value.h"

namespace rakupp {

class Interpreter;

Value dataZlibCompress(Interpreter& I, ValueList& args);
Value dataZlibUncompress(Interpreter& I, ValueList& args);
Value dataZlibGzslurp(Interpreter& I, ValueList& args);
Value dataZlibGzspurt(Interpreter& I, ValueList& args);
// `crc32($data, $init = 0)` and `adler32($data, $init = 1)` — an optional
// running value, so a stream can be fed in pieces.
Value dataZlibChecksum(Interpreter& I, ValueList& args, bool crc);

} // namespace rakupp
