#pragma once
// The shape of the generated stencil table — written by tools/cnp-extract at
// build time, read by src/Cnp.cpp at run time. docs/dev/plans/CNP-PLAN.md.
//
// Deliberately POD and pointer-free apart from the two array pointers, so the
// whole table lands in .rodata and costs nothing to have in a binary that never
// turns `--cnp` on.
#include <cstdint>

namespace rakupp {
namespace cnp {

// Which value a hole wants. The emitter answers these per instance of a
// stencil; `Helper` is the one that is answered at startup instead, from the
// address of a real function in this executable.
enum class Sym : uint8_t {
    Cont = 0,   // the next stencil in the chain
    Target,     // a branch target
    Slow,       // the cold block this stencil bails to
    Op0, Op1, Op2, Op3,
    Helper,     // one of the rk_cnp_* functions; `helper` says which
};

// How to write the value into the instruction stream. Abstract on purpose: the
// extractor knows about object formats, the patcher knows about instruction
// encodings, and this enum is the whole of what passes between them.
enum class Patch : uint8_t {
    Call,        // a direct branch — arm64 BL/B imm26, x86-64 call/jmp rel32
    Adrp21,      // arm64 ADRP: the page containing the value
    AddOff12,    // arm64 ADD/LDR immediate: the value's offset in its page
    GotAdrp21,   // as Adrp21, but of the GOT slot holding the value
    GotOff12,    // as AddOff12, but of the GOT slot
    Rel32,       // x86-64 rip-relative displacement to the value
    GotRel32,    // x86-64 rip-relative displacement to the GOT slot
    Abs64,       // a plain 64-bit word
};

struct HoleRef {
    uint32_t off;      // byte offset within the stencil
    Patch    patch;
    Sym      sym;
    uint8_t  helper;   // index into kHelperNames, when sym == Sym::Helper
    uint8_t  pad;
    int64_t  addend;
};

struct Stencil {
    const uint8_t* code;
    const HoleRef* holes;
    uint32_t       size;
    uint32_t       nholes;
};

// What the generated file defines. `kCount == 0` is the honest answer on a
// platform the extractor could not read: `--cnp` then says so and the program
// runs interpreted.
extern const Stencil     kStencils[];
extern const char* const kStencilNames[];
extern const char* const kHelperNames[];
extern const unsigned    kCount;
extern const unsigned    kHelperCount;
extern const char* const kArch;   // what this table was extracted for

}  // namespace cnp
}  // namespace rakupp
