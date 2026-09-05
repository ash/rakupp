// Zlib.h — DEFLATE, and the two framings around it (DATA-PLAN P4).
//
// RFC 1951 (deflate), RFC 1950 (the zlib wrapper), RFC 1952 (gzip), plus the
// two checksums those wrappers carry. No libz: the engine dlopens nothing for
// this, which is the whole point — a dlopen'd system library is not there to be
// found inside an `--exe` binary or in the WASM playground, and those are
// exactly where `Compress::Zlib`'s dependents are otherwise dead.
//
// TWIN: Compress::Zlib::Native's src/zlib.c, a SEPARATE implementation on
// purpose (NATIVE-MODULES-PLAN, "The architecture: independent C"). What holds
// the two together is t/vectors/zlib.vec — streams produced by real libz and
// the system gzip, including malformed ones that must be refused — read by both
// and weakened by neither.
//
// This file is the bytes-to-bytes half, with no Value in it; src/DataZlib.cpp
// is the Raku-facing surface.
//
// It is NOT faster than libz, and is not trying to be. libz is thirty years of
// tuned C. This is a clean-room implementation of a small closed specification,
// and the claim is availability, not throughput.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace rakupp {

enum class ZlibFormat { Zlib, Gzip, Raw };

// Every refusal is this, so a caller can tell "the stream is bad" from "the
// engine broke". The message says what was wrong and where it was noticed.
struct ZlibError : std::runtime_error {
    explicit ZlibError(const std::string& m) : std::runtime_error(m) {}
};

// `level` is -1..9 as the reference takes it: 0 stores, -1 means the default.
std::string zlibDeflate(const std::string& in, int level, ZlibFormat fmt);
// Throws ZlibError on anything malformed — a bad header, a truncated stream, a
// distance pointing before the start, a checksum that does not match.
std::string zlibInflate(const std::string& in, ZlibFormat fmt);

uint32_t zlibCrc32(const std::string& in, uint32_t init = 0);
uint32_t zlibAdler32(const std::string& in, uint32_t init = 1);

} // namespace rakupp
