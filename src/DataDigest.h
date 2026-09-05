// DataDigest.h — the `digest` tag's engine primitives (DATA-PLAN P3).
//
// Fourteen names: six bare digests answering a blob8, six `-hex` twins
// answering a Str, and `hmac` / `hmac-hex`. The surface is the union three
// unrelated authors converged on — `Digest`/`Digest::SHA2` for the bare names,
// bduggan's `Digest::SHA*::Native` and `OpenSSL::Digest` for the `-hex`
// spelling, `Digest::HMAC` for the MAC — so a program written against any of
// them swaps in `use Data::Native <digest>` unchanged.
//
// The ALGORITHMS are in src/Digest.{h,cpp}, shared with the Jupyter kernel's
// message signatures. This file is only the Raku-facing half: what an argument
// may be, what comes back, and what is refused.
//
// TWIN: Digest::Native's lib/Digest/Native.rakumod. Where that module says
// `Digest::Native: …` in an error, this says the routine's own name, because
// naming a module that was not involved would be a lie — the same deliberate
// difference DataCsv.cpp carries, and the only one.
#pragma once

#include "Value.h"

namespace rakupp {

class Interpreter;

// `md5($in)` … `sha512($in)`, and the `-hex` twins. `algo` is the tag name and
// `hex` picks the return type.
Value dataDigestHash(Interpreter& I, ValueList& args, const char* algo, bool hex);

// `hmac($key, $message, &hash, $blocksize?)`, and `hmac-hex`.
Value dataDigestHmac(Interpreter& I, ValueList& args, bool hex);

} // namespace rakupp
