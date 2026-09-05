// DataRandom.h — the `random` tag's engine primitives (DATA-PLAN P5).
//
// `crypt_random_buf`, `crypt_random`, `crypt_random_uniform`, `random-backend`.
// The underscore spellings are `Crypt::Random`'s and are kept: that is the only
// CSPRNG distribution in the top hundred, so it is the converged interface by
// default, and a tag that ships `hmac` invites "and where do I get a key".
//
// One file, not the two the other tags have. There is no algorithm to separate
// out — the whole of this is asking the OS for bytes and shaping them, and a
// `src/Random.cpp` holding one function would be a pattern rather than a
// reason.
//
// This is the one tag where NOT using the OS primitive would be the error: a
// CSPRNG is not a thing to implement. `getentropy(2)` on macOS and the BSDs,
// `getrandom(2)` on Linux, `BCryptGenRandom` on Windows, `/dev/urandom` when
// none of those is there.
#pragma once

#include "Value.h"

namespace rakupp {

class Interpreter;

Value dataRandomBuf(Interpreter& I, ValueList& args);      // → Buf of $len bytes
Value dataRandomInt(Interpreter& I, ValueList& args);      // → Int from $size bytes
Value dataRandomUniform(Interpreter& I, ValueList& args);  // → Int in [0, $upper)

} // namespace rakupp
