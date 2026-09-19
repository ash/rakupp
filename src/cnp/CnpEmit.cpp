// The copy-and-patch assembler — docs/dev/plans/CNP-PLAN.md.
//
// Layout of one kernel, in a single mapping:
//
//     [ stencil code, one block per op ] [ veneers ] [ GOT slots ]
//
// Everything a patched instruction can reach is therefore inside the mapping:
// a branch between stencils is always within the ±128 MB a BL can express, and
// an ADRP can always reach the GOT area, which is a few kilobytes away rather
// than wherever the heap happened to put us relative to the executable.
//
// The veneers exist for the other direction. A stencil that calls one of the
// rk_cnp_* helpers is calling into the host executable, and nothing says the
// heap and the executable are within branch range of each other. So a call to a
// helper branches to a three-word thunk in the buffer, which loads the real
// address from the word after it and jumps. Cold path, and it costs nothing on
// a path that never calls a helper.
#include "cnp/CnpEmit.h"
#include "cnp/CnpTable.h"
#include "cnp/CnpAbi.h"

#include <cstring>
#include <map>
#include <set>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif
#if defined(__APPLE__)
#  include <libkern/OSCacheControl.h>
#  include <pthread.h>
#endif

namespace rakupp {
namespace cnp {

namespace {

#if defined(__aarch64__) || defined(_M_ARM64)
constexpr bool kArm64 = true;
#else
constexpr bool kArm64 = false;
#endif
#if defined(__x86_64__) || defined(_M_X64)
constexpr bool kX86_64 = true;
#else
constexpr bool kX86_64 = false;
#endif

// The helpers, in the order cnp-extract sorted their names into. Resolved once,
// by taking the address of the real function: no dlsym, no symbol table, and it
// cannot go stale because the linker fills it.
void* helperAddr(const char* name) {
    struct E { const char* n; void* p; };
    static const E table[] = {
        { "rk_cnp_binop",   (void*)&rk_cnp_binop   },
        { "rk_cnp_cmp",     (void*)&rk_cnp_cmp     },
        { "rk_cnp_defined", (void*)&rk_cnp_defined },
        { "rk_cnp_loadk",   (void*)&rk_cnp_loadk   },
        { "rk_cnp_move",    (void*)&rk_cnp_move    },
        { "rk_cnp_truthy",  (void*)&rk_cnp_truthy  },
        { "rk_cnp_unop",    (void*)&rk_cnp_unop    },
    };
    for (const E& e : table) if (std::strcmp(e.n, name) == 0) return e.p;
    return nullptr;
}

std::string g_why;

// Every stencil the emitter is allowed to use must be present and every helper
// the table names must be one we can resolve. Checked once, so that a mismatch
// between stencils.c and this file is a clear message at startup rather than a
// crash in a patched instruction.
bool checkTable() {
    if (kCount == 0) { g_why = "this build carries no stencils (see the build log for cnp-extract)"; return false; }
    if (!kArm64 && !kX86_64) { g_why = "no patcher for this instruction set"; return false; }
    for (unsigned i = 0; i < kHelperCount; i++)
        if (!helperAddr(kHelperNames[i])) {
            g_why = std::string("the stencils call a helper this build does not have: ") + kHelperNames[i];
            return false;
        }
    return true;
}

// ---- executable memory -----------------------------------------------------
//
// Written as read/write, then flipped to read/execute. Per REGION and never
// both at once, which is what W^X asks for and what Apple silicon enforces.
//
// The alternative on Apple is MAP_JIT plus pthread_jit_write_protect_np, and it
// is deliberately the FALLBACK rather than the default: that call toggles
// write protection for every MAP_JIT mapping in the process and for the calling
// thread only, so a second thread executing one kernel while this thread builds
// another is a race by construction. rakupp runs work on several threads. The
// mprotect route has no such coupling.
struct Mapping { void* p = nullptr; size_t n = 0; };

Mapping mapRW(size_t n) {
#if defined(_WIN32)
    void* p = ::VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return { p, n };
#else
    void* p = ::mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p != MAP_FAILED) return { p, n };
#  if defined(__APPLE__) && defined(MAP_JIT)
    p = ::mmap(nullptr, n, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (p != MAP_FAILED) { ::pthread_jit_write_protect_np(0); return { p, n }; }
#  endif
    return { nullptr, 0 };
#endif
}

bool makeExecutable(const Mapping& m) {
#if defined(_WIN32)
    DWORD old = 0;
    if (!::VirtualProtect(m.p, m.n, PAGE_EXECUTE_READ, &old)) return false;
    ::FlushInstructionCache(::GetCurrentProcess(), m.p, m.n);
    return true;
#else
    if (::mprotect(m.p, m.n, PROT_READ | PROT_EXEC) != 0) {
#  if defined(__APPLE__) && defined(MAP_JIT)
        ::pthread_jit_write_protect_np(1);      // the MAP_JIT fallback took this route
#  else
        return false;
#  endif
    }
#  if defined(__APPLE__)
    ::sys_icache_invalidate(m.p, m.n);
#  else
    __builtin___clear_cache((char*)m.p, (char*)m.p + m.n);
#  endif
    return true;
#endif
}

void unmap(void* p, size_t n) {
#if defined(_WIN32)
    (void)n; ::VirtualFree(p, 0, MEM_RELEASE);
#else
    ::munmap(p, n);
#endif
}

size_t pageSize() {
#if defined(_WIN32)
    SYSTEM_INFO si; ::GetSystemInfo(&si); return si.dwPageSize;
#else
    long v = ::sysconf(_SC_PAGESIZE);
    return v > 0 ? (size_t)v : 4096;
#endif
}

// ---- instruction editing ---------------------------------------------------

uint32_t ld32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
void     st32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

bool fits(int64_t v, int bits) {
    int64_t lo = -(int64_t(1) << (bits - 1)), hi = (int64_t(1) << (bits - 1)) - 1;
    return v >= lo && v <= hi;
}

// arm64: a B/BL with a 26-bit word displacement.
bool patchBranch26(uint8_t* at, uint64_t target, uint64_t here, std::string& err) {
    int64_t d = (int64_t)target - (int64_t)here;
    if (d & 3) { err = "a branch target that is not instruction-aligned"; return false; }
    d >>= 2;
    if (!fits(d, 26)) { err = "a branch further than a single instruction can reach"; return false; }
    uint32_t insn = ld32(at);
    st32(at, (insn & 0xfc000000u) | (uint32_t)(d & 0x03ffffff));
    return true;
}

// arm64: ADRP — the signed page delta, split across immlo[30:29] and immhi[23:5].
bool patchAdrp(uint8_t* at, uint64_t value, uint64_t here, std::string& err) {
    int64_t d = (int64_t)(value & ~0xfffULL) - (int64_t)(here & ~0xfffULL);
    d >>= 12;
    if (!fits(d, 21)) { err = "a page reference out of ADRP range"; return false; }
    uint32_t insn = ld32(at);
    insn &= ~((3u << 29) | (0x7ffffu << 5));
    insn |= (uint32_t)(d & 3) << 29;
    insn |= (uint32_t)((d >> 2) & 0x7ffff) << 5;
    st32(at, insn);
    return true;
}

// arm64: the low 12 bits, into an ADD immediate or a scaled LDR/STR offset.
bool patchOff12(uint8_t* at, uint64_t value, std::string& err) {
    uint32_t insn = ld32(at);
    uint32_t off = (uint32_t)(value & 0xfff);
    unsigned scale = 0;
    if ((insn & 0x3b000000u) == 0x39000000u) scale = insn >> 30;   // LDR/STR, unsigned offset
    if (off & ((1u << scale) - 1)) { err = "an unaligned offset for a scaled load"; return false; }
    st32(at, (insn & ~(0xfffu << 10)) | ((off >> scale) << 10));
    return true;
}

// The rewrite that earns most of what copy-and-patch is for. The compiler had
// to assume an arbitrary symbol, so it emitted ADRP+LDR through the GOT to get
// one 64-bit number. We know the number now, and if it fits in 32 bits the same
// two instruction slots hold MOVZ+MOVK — no GOT slot, and no load at all on the
// hot path.
//
// Refused unless the two instructions are ADJACENT and all three register
// fields agree (ADRP Xd, then LDR Xt, [Xn] with d == n == t). Anything looser
// and the ADRP's scratch register might be what some other instruction in the
// stencil is reading, and overwriting it would be a wrong answer rather than a
// slow one. The check is deliberately made against the TEMPLATE bytes, before
// anything is copied, so that the decision and the count of GOT slots are taken
// in the same pass.
bool foldable(const uint8_t* code, uint32_t adrpOff, uint32_t size, uint64_t value) {
    if (!kArm64) return false;
    if (value > 0xffffffffULL) return false;
    if (adrpOff + 8 > size) return false;
    uint32_t adrp = ld32(code + adrpOff);
    uint32_t ldr  = ld32(code + adrpOff + 4);
    if ((adrp & 0x9f000000u) != 0x90000000u) return false;          // ADRP Xd, page
    if ((ldr  & 0xffc00000u) != 0xf9400000u) return false;          // LDR Xt, [Xn, #imm]
    uint32_t rd = adrp & 31, rn = (ldr >> 5) & 31, rt = ldr & 31;
    return rd == rn && rn == rt;
}

void writeImmediatePair(uint8_t* code, uint32_t adrpOff, uint64_t value) {
    uint32_t rt = ld32(code + adrpOff + 4) & 31;
    uint32_t lo = (uint32_t)(value & 0xffff), hi = (uint32_t)((value >> 16) & 0xffff);
    st32(code + adrpOff,     0xd2800000u | (lo << 5) | rt);         // MOVZ Xt, #lo
    st32(code + adrpOff + 4, 0xf2a00000u | (hi << 5) | rt);         // MOVK Xt, #hi, LSL #16
}

// x86-64: a 32-bit displacement from the end of the instruction.
bool patchRel32(uint8_t* at, uint64_t value, uint64_t here, int64_t addend, std::string& err) {
    int64_t d = (int64_t)value + addend - (int64_t)here;
    if (!fits(d, 32)) { err = "a displacement out of 32-bit range"; return false; }
    int32_t v = (int32_t)d;
    std::memcpy(at, &v, 4);
    return true;
}

// A three-word thunk: load the real address from the word that follows, jump.
// x16 on arm64 is the intra-procedure scratch register, which is exactly what
// it is reserved for; on x86-64 an indirect jump through RIP needs no register
// at all.
size_t veneerSize() { return kArm64 ? 16 : 14; }

void writeVeneer(uint8_t* at, uint64_t target) {
    if (kArm64) {
        st32(at + 0, 0x58000050u);   // LDR X16, .+8
        st32(at + 4, 0xd61f0200u);   // BR  X16
        std::memcpy(at + 8, &target, 8);
    } else {
        at[0] = 0xff; at[1] = 0x25;                      // JMP [RIP+0]
        uint32_t zero = 0; std::memcpy(at + 2, &zero, 4);
        std::memcpy(at + 6, &target, 8);
    }
}

// Every hole reads one of these. Keeping the answer in one place is what makes
// the patch loop below format-independent.
uint64_t holeValue(const std::vector<Op>& ops, size_t i, const HoleRef& h,
                   const std::vector<uint64_t>& addr, bool& bad, std::string& err) {
    auto at = [&](int idx) -> uint64_t {
        if (idx < 0 || (size_t)idx >= ops.size()) { bad = true; err = "a continuation with no op"; return 0; }
        return addr[idx];
    };
    switch (h.sym) {
        case Sym::Cont:   return at(ops[i].cont >= 0 ? ops[i].cont : (int)i + 1);
        case Sym::Target: return at(ops[i].target);
        case Sym::Slow:   return at(ops[i].slow);
        case Sym::Op0:    return ops[i].operand[0];
        case Sym::Op1:    return ops[i].operand[1];
        case Sym::Op2:    return ops[i].operand[2];
        case Sym::Op3:    return ops[i].operand[3];
        case Sym::Helper: {
            void* p = h.helper < kHelperCount ? helperAddr(kHelperNames[h.helper]) : nullptr;
            if (!p) { bad = true; err = "an unresolved helper"; }
            return (uint64_t)p;
        }
    }
    bad = true;
    err = "an unknown hole";
    return 0;
}

}  // namespace

Code::~Code() { if (mem_) unmap(mem_, size_); }

bool stencilsAvailable() {
    static const bool ok = checkTable();
    return ok;
}
const char* stencilsUnavailableReason() { stencilsAvailable(); return g_why.empty() ? "" : g_why.c_str(); }
const char* arch() { return kArch; }

int stencilIndex(const char* name) {
    for (unsigned i = 0; i < kCount; i++)
        if (std::strcmp(kStencilNames[i], name) == 0) return (int)i;
    return -1;
}

std::unique_ptr<Code> assemble(const std::vector<Op>& ops, std::string& err) {
    if (!stencilsAvailable()) { err = stencilsUnavailableReason(); return nullptr; }
    if (ops.empty()) { err = "no ops"; return nullptr; }

    // ---- pass 1: sizes, and how many veneers and GOT slots we will need -----
    //
    // Neither count depends on any address, which is what breaks the circle:
    // the sizes fix the addresses, and the addresses are what the patches need.
    std::vector<uint64_t> off(ops.size());
    uint64_t codeBytes = 0;
    for (size_t i = 0; i < ops.size(); i++) {
        if (ops[i].stencil >= kCount) { err = "an op with no stencil"; return nullptr; }
        off[i] = codeBytes;
        codeBytes += kStencils[ops[i].stencil].size;
    }

    // Which (op, symbol) pairs really need a GOT slot, and which can be folded
    // into the instruction stream instead. Decided here because the answer
    // depends only on the value, and every value but a continuation is known.
    // One key per (op instance, symbol): two references to the same hole inside
    // one stencil share a GOT slot, two different ops never do.
    auto keyOf = [](size_t i, const HoleRef& h) {
        return ((uint64_t)i << 16) | ((uint64_t)(uint8_t)h.sym << 8) | h.helper;
    };
    std::map<uint64_t, size_t> gotSlot;         // (op, symbol) -> slot index
    std::map<uint64_t, size_t> veneer;          // helper address -> veneer index
    // The byte offsets, within the whole buffer, of pairs that fold into the
    // instruction stream. Keyed by OFFSET rather than by symbol, because one
    // stencil can reference the same hole twice and the compiler is free to
    // schedule one of those pairs apart and leave the other adjacent.
    std::set<uint64_t> foldAdrp, foldSkip;

    for (size_t i = 0; i < ops.size(); i++) {
        const Stencil& st = kStencils[ops[i].stencil];
        for (uint32_t k = 0; k < st.nholes; k++) {
            const HoleRef& h = st.holes[k];
            if (h.patch == Patch::Call && h.sym == Sym::Helper) {
                bool bad = false;
                uint64_t v = holeValue(ops, i, h, off, bad, err);
                if (bad) return nullptr;
                if (!veneer.count(v)) { size_t n = veneer.size(); veneer[v] = n; }
            } else if (h.patch == Patch::GotAdrp21) {
                // A continuation is always reached by a branch, never by this
                // form, and its address is not known yet — so only an operand
                // or a helper address can be folded.
                bool bad = false;
                uint64_t v = holeValue(ops, i, h, off, bad, err);
                if (bad) return nullptr;
                uint64_t key = keyOf(i, h);
                // Both halves have to line up before the pair can be folded:
                // there must be a low-half HOLE for the same symbol at exactly
                // the next instruction, and the two instructions must really be
                // the ADRP/LDR shape. Checking only the encodings would let an
                // unrelated LDR that happens to sit there be overwritten while
                // the real low half, further along, kept loading from a GOT slot
                // the rewritten code no longer reads.
                bool mate = false;
                for (uint32_t m = 0; m < st.nholes; m++)
                    if (st.holes[m].patch == Patch::GotOff12 && st.holes[m].off == h.off + 4 &&
                        keyOf(i, st.holes[m]) == key) { mate = true; break; }
                if (mate && h.sym != Sym::Cont && h.sym != Sym::Target && h.sym != Sym::Slow &&
                    foldable(st.code, h.off, st.size, v)) {
                    foldAdrp.insert(off[i] + h.off);
                    foldSkip.insert(off[i] + h.off + 4);
                    continue;
                }
                if (!gotSlot.count(key)) { size_t n = gotSlot.size(); gotSlot[key] = n; }
            }
        }
        // The low halves in a second pass, so that a GotOff12 the extractor
        // happened to list BEFORE its ADRP still sees the decision.
        for (uint32_t k = 0; k < st.nholes; k++) {
            const HoleRef& h = st.holes[k];
            if (h.patch != Patch::GotOff12 && h.patch != Patch::GotRel32) continue;
            if (foldSkip.count(off[i] + h.off)) continue;
            uint64_t key = keyOf(i, h);
            if (!gotSlot.count(key)) { size_t n = gotSlot.size(); gotSlot[key] = n; }
        }
    }

    uint64_t veneerBase = (codeBytes + 15) & ~15ULL;
    uint64_t gotBase    = veneerBase + veneer.size() * veneerSize();
    gotBase = (gotBase + 7) & ~7ULL;
    uint64_t total      = gotBase + gotSlot.size() * 8;
    size_t   page       = pageSize();
    size_t   alloc      = ((size_t)total + page - 1) & ~(page - 1);
    if (alloc == 0) alloc = page;

    Mapping m = mapRW(alloc);
    if (!m.p) { err = "could not map executable memory"; return nullptr; }
    auto code = std::make_unique<Code>();
    code->mem_ = m.p;
    code->size_ = alloc;

    uint8_t* base = (uint8_t*)m.p;
    std::memset(base, 0, alloc);
    std::vector<uint64_t> addr(ops.size());
    for (size_t i = 0; i < ops.size(); i++) {
        const Stencil& st = kStencils[ops[i].stencil];
        std::memcpy(base + off[i], st.code, st.size);
        addr[i] = (uint64_t)(base + off[i]);
    }
    for (auto& v : veneer) writeVeneer(base + veneerBase + v.second * veneerSize(), v.first);

    // ---- pass 2: fill every hole -------------------------------------------
    for (size_t i = 0; i < ops.size(); i++) {
        const Stencil& st = kStencils[ops[i].stencil];
        for (uint32_t k = 0; k < st.nholes; k++) {
            const HoleRef& h = st.holes[k];
            bool bad = false;
            uint64_t value = holeValue(ops, i, h, addr, bad, err);
            if (bad) return nullptr;
            uint8_t* at   = base + off[i] + h.off;
            uint64_t here = (uint64_t)at;
            uint64_t key  = keyOf(i, h);

            switch (h.patch) {
                case Patch::Call: {
                    uint64_t t = value;
                    if (h.sym == Sym::Helper) {
                        auto it = veneer.find(value);
                        if (it == veneer.end()) { err = "a helper with no veneer"; return nullptr; }
                        t = (uint64_t)(base + veneerBase + it->second * veneerSize());
                    }
                    bool ok = kArm64 ? patchBranch26(at, (uint64_t)((int64_t)t + h.addend), here, err)
                                     : patchRel32(at, t, here, h.addend, err);
                    if (!ok) return nullptr;
                    break;
                }
                case Patch::GotAdrp21:
                case Patch::GotOff12: {
                    if (h.patch == Patch::GotAdrp21 && foldAdrp.count(off[i] + h.off)) {
                        writeImmediatePair(base + off[i], h.off, value);
                        break;
                    }
                    if (h.patch == Patch::GotOff12 && foldSkip.count(off[i] + h.off))
                        break;   // written by its ADRP partner
                    auto it = gotSlot.find(key);
                    if (it == gotSlot.end()) { err = "a GOT reference with no slot"; return nullptr; }
                    uint8_t* slot = base + gotBase + it->second * 8;
                    uint64_t sv = (uint64_t)((int64_t)value + h.addend);
                    std::memcpy(slot, &sv, 8);
                    bool ok = (h.patch == Patch::GotAdrp21)
                                  ? patchAdrp(at, (uint64_t)slot, here, err)
                                  : patchOff12(at, (uint64_t)slot, err);
                    if (!ok) return nullptr;
                    break;
                }
                case Patch::GotRel32: {
                    auto it = gotSlot.find(key);
                    if (it == gotSlot.end()) { err = "a GOT reference with no slot"; return nullptr; }
                    uint8_t* slot = base + gotBase + it->second * 8;
                    uint64_t sv = (uint64_t)((int64_t)value + h.addend);
                    std::memcpy(slot, &sv, 8);
                    if (!patchRel32(at, (uint64_t)slot, here, h.addend, err)) return nullptr;
                    break;
                }
                case Patch::Adrp21:
                case Patch::AddOff12: {
                    // Only ever correct for something that really is an address.
                    if (h.sym != Sym::Helper) {
                        err = "an operand reached through a page-relative reference, "
                              "which cannot hold an arbitrary value";
                        return nullptr;
                    }
                    bool ok = (h.patch == Patch::Adrp21)
                                  ? patchAdrp(at, (uint64_t)((int64_t)value + h.addend), here, err)
                                  : patchOff12(at, (uint64_t)((int64_t)value + h.addend), err);
                    if (!ok) return nullptr;
                    break;
                }
                case Patch::Rel32:
                    if (!patchRel32(at, value, here, h.addend, err)) return nullptr;
                    break;
                case Patch::Abs64: {
                    uint64_t v = (uint64_t)((int64_t)value + h.addend);
                    std::memcpy(at, &v, 8);
                    break;
                }
            }
        }
    }

    if (!makeExecutable(m)) { err = "could not make the code buffer executable"; return nullptr; }
    code->entry_ = base;
    return code;
}

}  // namespace cnp
}  // namespace rakupp
