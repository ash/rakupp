#pragma once
#include "Ast.h"
#include <string>

namespace rakupp {

// Binary (de)serialization of a parsed Program — the basis of the precompiled
// module cache. The tree that comes back is the tree the parser built: every
// field round-trips, including `line` and `label`, which diagnostics depend on.
//
// The mutable per-node CACHES (Binary::simpleOp/fastShape/litVal,
// Index::fastShape/litIdx, NumLit::cacheN/cacheD, Block::hoistNeed,
// StrLit::nfcDone) are deliberately not stored. Each has an "undecided"
// sentinel and is rebuilt lazily on first evaluation, so a loaded tree behaves
// identically to a freshly parsed one — it just has not warmed up yet.
//
// Both directions are driven by ONE visitor per node type (see AstSerial.cpp),
// so a field cannot be written but not read, which is the failure mode that
// makes a format like this quietly wrong instead of loudly broken.

// Bumped whenever the encoding or the AST changes shape. A cache entry carrying
// a different version is ignored, never reinterpreted.
inline constexpr uint32_t kAstSerialVersion = 14; // v14: VarExpr.declTypeExpr (a parameterized declared type)

struct AstSerialError { std::string msg; };

// Serialize `prog` into a self-describing byte string (header included).
std::string serializeAst(const Program& prog);

// Rebuild a Program. Throws AstSerialError if the header does not match this
// build, or the payload is truncated/corrupt — callers treat that as a cache
// miss and parse the source instead.
void deserializeAst(const std::string& blob, Program& out);

} // namespace rakupp
