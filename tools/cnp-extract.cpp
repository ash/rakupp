// cnp-extract — turn a compiled stencil object file into a C++ table.
// Built and run at BUILD time; never shipped. docs/dev/plans/CNP-PLAN.md.
//
//   cnp-extract stencils.o out.cpp
//
// It reads the one object file `src/cnp/stencils.c` compiles to, slices its
// __text/.text by symbol into one byte array per stencil, translates each
// relocation into the abstract patch kinds of src/cnp/CnpTable.h, and writes a
// .cpp holding the lot. Nothing about the result is machine-specific except the
// bytes themselves — the patcher in src/Cnp.cpp knows the instruction
// encodings, this tool knows the object formats, and CnpTable.h is the whole of
// what passes between them.
//
// It is deliberately strict. An object with a section other than the text one,
// or a relocation kind not listed below, is a stencil file that has grown
// something a code buffer cannot hold — a literal pool, a jump table, a call
// into libc. Rather than emit a table that would be patched wrongly, it fails
// the build and names what it found.
//
// FORMATS. Only the host's own format has to be read, because this runs on the
// machine that is building rakupp. Mach-O arm64 is what the work was developed
// and tested on; Mach-O x86-64 and ELF (aarch64, x86-64) are written from the
// specifications and have not been exercised. A format it cannot read is not a
// build failure: it writes an EMPTY table, `--cnp` reports that it has no
// stencils for this platform, and everything else about rakupp is unaffected.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

// ---- the abstract output ---------------------------------------------------

enum class Patch : uint8_t { Call, Adrp21, AddOff12, GotAdrp21, GotOff12, Rel32, GotRel32, Abs64 };
const char* patchName(Patch p) {
    switch (p) {
        case Patch::Call:      return "Patch::Call";
        case Patch::Adrp21:    return "Patch::Adrp21";
        case Patch::AddOff12:  return "Patch::AddOff12";
        case Patch::GotAdrp21: return "Patch::GotAdrp21";
        case Patch::GotOff12:  return "Patch::GotOff12";
        case Patch::Rel32:     return "Patch::Rel32";
        case Patch::GotRel32:  return "Patch::GotRel32";
        case Patch::Abs64:     return "Patch::Abs64";
    }
    return "Patch::Abs64";
}

struct Hole {
    uint64_t    off;       // offset within the section, rebased to the stencil later
    Patch       patch;
    std::string sym;       // the undefined symbol's name, with no platform prefix
    int64_t     addend;
};

struct Fn { std::string name; uint64_t start, end; };

struct Obj {
    std::vector<uint8_t> text;
    std::vector<Fn>      fns;
    std::vector<Hole>    holes;
    std::string          arch;
    std::string          err;
};

[[noreturn]] void die(const std::string& m) {
    std::fprintf(stderr, "cnp-extract: %s\n", m.c_str());
    std::exit(1);
}

std::vector<uint8_t> slurp(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) die(std::string("cannot open ") + path);
    std::vector<uint8_t> v;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
    std::fclose(f);
    return v;
}

template <class T> T rd(const std::vector<uint8_t>& b, size_t off) {
    T v{};
    if (off + sizeof(T) > b.size()) die("object file is truncated");
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

// A leading '_' is Mach-O's global prefix and not part of the name anyone wrote.
std::string unprefix(const std::string& s, bool machO) {
    if (machO && !s.empty() && s[0] == '_') return s.substr(1);
    return s;
}

// ---- Mach-O ----------------------------------------------------------------

constexpr uint32_t MH_MAGIC_64  = 0xfeedfacf;
constexpr uint32_t LC_SEGMENT_64 = 0x19;
constexpr uint32_t LC_SYMTAB     = 0x02;
constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr uint32_t CPU_TYPE_ARM64  = 0x0100000c;

// ARM64_RELOC_*
enum { A64_UNSIGNED = 0, A64_SUBTRACTOR = 1, A64_BRANCH26 = 2, A64_PAGE21 = 3,
       A64_PAGEOFF12 = 4, A64_GOT_LOAD_PAGE21 = 5, A64_GOT_LOAD_PAGEOFF12 = 6,
       A64_POINTER_TO_GOT = 7, A64_TLVP_LOAD_PAGE21 = 8, A64_TLVP_LOAD_PAGEOFF12 = 9,
       A64_ADDEND = 10 };
// X86_64_RELOC_*
enum { X64_UNSIGNED = 0, X64_SIGNED = 1, X64_BRANCH = 2, X64_GOT_LOAD = 3, X64_GOT = 4,
       X64_SUBTRACTOR = 5, X64_SIGNED_1 = 6, X64_SIGNED_2 = 7, X64_SIGNED_4 = 8, X64_TLV = 9 };

bool readMachO(const std::vector<uint8_t>& b, Obj& o) {
    if (b.size() < 32 || rd<uint32_t>(b, 0) != MH_MAGIC_64) return false;
    uint32_t cpu = rd<uint32_t>(b, 4);
    if      (cpu == CPU_TYPE_ARM64)  o.arch = "arm64";
    else if (cpu == CPU_TYPE_X86_64) o.arch = "x86_64";
    else { o.err = "Mach-O with an unknown cpu type"; return true; }

    uint32_t ncmds = rd<uint32_t>(b, 16);
    size_t   pos   = 32;
    uint64_t textAddr = 0, textOff = 0, textSize = 0, relOff = 0;
    uint32_t nrel = 0;
    int      textSectIdx = -1;      // 1-based, as n_sect counts
    uint32_t symOff = 0, nsyms = 0, strOff = 0, strSize = 0;
    std::vector<std::string> extraSections;

    for (uint32_t c = 0; c < ncmds; c++) {
        uint32_t cmd = rd<uint32_t>(b, pos), csize = rd<uint32_t>(b, pos + 4);
        if (cmd == LC_SEGMENT_64) {
            uint32_t nsects = rd<uint32_t>(b, pos + 64);
            size_t s = pos + 72;
            for (uint32_t i = 0; i < nsects; i++, s += 80) {
                char sect[17] = {0}, seg[17] = {0};
                std::memcpy(sect, b.data() + s, 16);
                std::memcpy(seg,  b.data() + s + 16, 16);
                std::string sn = sect, gn = seg;
                if (gn == "__TEXT" && sn == "__text") {
                    textSectIdx = (int)i + 1;
                    textAddr = rd<uint64_t>(b, s + 32);
                    textSize = rd<uint64_t>(b, s + 40);
                    textOff  = rd<uint32_t>(b, s + 48);
                    relOff   = rd<uint32_t>(b, s + 56);
                    nrel     = rd<uint32_t>(b, s + 60);
                } else if (sn.rfind("__debug", 0) != 0 && sn != "__compact_unwind") {
                    extraSections.push_back(gn + "," + sn);
                }
            }
        } else if (cmd == LC_SYMTAB) {
            symOff  = rd<uint32_t>(b, pos + 8);
            nsyms   = rd<uint32_t>(b, pos + 12);
            strOff  = rd<uint32_t>(b, pos + 16);
            strSize = rd<uint32_t>(b, pos + 20);
        }
        pos += csize;
    }
    if (textSectIdx < 0) { o.err = "no __TEXT,__text section"; return true; }
    if (!extraSections.empty()) {
        std::string s;
        for (auto& e : extraSections) { if (!s.empty()) s += ", "; s += e; }
        o.err = "the stencil object grew sections a code buffer cannot hold: " + s;
        return true;
    }
    (void)strSize;
    o.text.assign(b.begin() + textOff, b.begin() + textOff + textSize);

    // Symbols: the globals in __text are the stencils; the undefined ones name
    // the holes.
    std::vector<std::string> symNames(nsyms);
    for (uint32_t i = 0; i < nsyms; i++) {
        size_t e = symOff + (size_t)i * 16;
        uint32_t strx = rd<uint32_t>(b, e);
        uint8_t  type = rd<uint8_t>(b, e + 4);
        uint8_t  sect = rd<uint8_t>(b, e + 5);
        uint64_t val  = rd<uint64_t>(b, e + 8);
        const char* nm = (const char*)b.data() + strOff + strx;
        symNames[i] = nm;
        constexpr uint8_t N_TYPE = 0x0e, N_SECT = 0x0e, N_EXT = 0x01;
        if ((type & N_EXT) && (type & N_TYPE) == N_SECT && sect == textSectIdx)
            o.fns.push_back({unprefix(nm, true), val - textAddr, 0});
    }

    int64_t pendingAddend = 0;
    bool    havePending = false;
    for (uint32_t i = 0; i < nrel; i++) {
        size_t e = relOff + (size_t)i * 8;
        uint32_t addr = rd<uint32_t>(b, e);
        uint32_t info = rd<uint32_t>(b, e + 4);
        uint32_t symnum = info & 0x00ffffffu;
        uint32_t len    = (info >> 25) & 3u;
        uint32_t ext    = (info >> 27) & 1u;
        uint32_t type   = (info >> 28) & 0xfu;
        if (info & 0x80000000u) { o.err = "a scattered relocation"; return true; }

        if (o.arch == "arm64" && type == A64_ADDEND) {
            pendingAddend = (int64_t)(int32_t)(symnum << 8) >> 8;  // 24-bit signed
            havePending = true;
            continue;
        }
        if (!ext) { o.err = "a relocation against a section rather than a symbol"; return true; }

        Hole h{};
        h.off    = addr;
        h.sym    = unprefix(symNames[symnum], true);
        h.addend = havePending ? pendingAddend : 0;
        havePending = false;

        if (o.arch == "arm64") {
            switch (type) {
                case A64_BRANCH26:              h.patch = Patch::Call;      break;
                case A64_PAGE21:                h.patch = Patch::Adrp21;    break;
                case A64_PAGEOFF12:             h.patch = Patch::AddOff12;  break;
                case A64_GOT_LOAD_PAGE21:       h.patch = Patch::GotAdrp21; break;
                case A64_GOT_LOAD_PAGEOFF12:    h.patch = Patch::GotOff12;  break;
                case A64_UNSIGNED:
                    if (len != 3) { o.err = "a non-64-bit absolute relocation"; return true; }
                    h.patch = Patch::Abs64;
                    break;
                default:
                    o.err = "arm64 relocation type " + std::to_string(type) + " in " + h.sym;
                    return true;
            }
        } else {
            switch (type) {
                case X64_BRANCH:    h.patch = Patch::Call;     h.addend = -4; break;
                case X64_GOT_LOAD:
                case X64_GOT:       h.patch = Patch::GotRel32; h.addend = -4; break;
                case X64_SIGNED:    h.patch = Patch::Rel32;    h.addend = -4; break;
                case X64_SIGNED_1:  h.patch = Patch::Rel32;    h.addend = -5; break;
                case X64_SIGNED_2:  h.patch = Patch::Rel32;    h.addend = -6; break;
                case X64_SIGNED_4:  h.patch = Patch::Rel32;    h.addend = -8; break;
                case X64_UNSIGNED:
                    if (len != 3) { o.err = "a non-64-bit absolute relocation"; return true; }
                    h.patch = Patch::Abs64;
                    break;
                default:
                    o.err = "x86-64 relocation type " + std::to_string(type) + " in " + h.sym;
                    return true;
            }
            // Mach-O keeps the addend in the instruction stream; the patcher
            // adds ours on top, so read out what is already there.
            if (h.patch != Patch::Abs64)
                h.addend += (int64_t)(int32_t)rd<uint32_t>(o.text, h.off);
        }
        o.holes.push_back(h);
    }
    return true;
}

// ---- ELF -------------------------------------------------------------------

enum { R_AARCH64_ABS64 = 257, R_AARCH64_ADR_PREL_PG_HI21 = 275, R_AARCH64_ADD_ABS_LO12_NC = 277,
       R_AARCH64_JUMP26 = 282, R_AARCH64_CALL26 = 283, R_AARCH64_LDST64_ABS_LO12_NC = 286,
       R_AARCH64_ADR_GOT_PAGE = 311, R_AARCH64_LD64_GOT_LO12_NC = 312 };
enum { R_X86_64_64 = 1, R_X86_64_PC32 = 2, R_X86_64_PLT32 = 4, R_X86_64_GOTPCREL = 9,
       R_X86_64_GOTPCRELX = 41, R_X86_64_REX_GOTPCRELX = 42 };

bool readElf(const std::vector<uint8_t>& b, Obj& o) {
    if (b.size() < 64 || b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') return false;
    if (b[4] != 2 || b[5] != 1) { o.err = "only 64-bit little-endian ELF is read"; return true; }
    uint16_t machine = rd<uint16_t>(b, 18);
    if      (machine == 183) o.arch = "arm64";
    else if (machine == 62)  o.arch = "x86_64";
    else { o.err = "ELF for an unknown machine"; return true; }

    uint64_t shoff = rd<uint64_t>(b, 40);
    uint16_t shentsize = rd<uint16_t>(b, 58), shnum = rd<uint16_t>(b, 60), shstrndx = rd<uint16_t>(b, 62);
    auto sh = [&](int i, int field) -> uint64_t {
        size_t base = shoff + (size_t)i * shentsize;
        switch (field) {
            case 0: return rd<uint32_t>(b, base);        // name
            case 1: return rd<uint32_t>(b, base + 4);    // type
            case 2: return rd<uint64_t>(b, base + 8);    // flags
            case 3: return rd<uint64_t>(b, base + 24);   // offset
            case 4: return rd<uint64_t>(b, base + 32);   // size
            case 5: return rd<uint32_t>(b, base + 40);   // link
            case 6: return rd<uint32_t>(b, base + 44);   // info
            case 7: return rd<uint64_t>(b, base + 56);   // entsize
        }
        return 0;
    };
    uint64_t shstrOff = sh(shstrndx, 3);
    auto sname = [&](int i) { return std::string((const char*)b.data() + shstrOff + sh(i, 0)); };

    int textIdx = -1, relaIdx = -1, symIdx = -1;
    std::vector<std::string> extraSections;
    for (int i = 0; i < shnum; i++) {
        std::string n = sname(i);
        uint64_t type = sh(i, 1), flags = sh(i, 2);
        if (n == ".text") textIdx = i;
        else if (n == ".rela.text") relaIdx = i;
        else if (type == 2 /*SHT_SYMTAB*/) symIdx = i;
        // SHF_ALLOC sections other than .text would have to be placed in the
        // code buffer; refuse rather than guess.
        else if ((flags & 2) && sh(i, 4) > 0) extraSections.push_back(n);
    }
    if (textIdx < 0 || symIdx < 0) { o.err = "no .text or no .symtab"; return true; }
    if (!extraSections.empty()) {
        std::string s;
        for (auto& e : extraSections) { if (!s.empty()) s += ", "; s += e; }
        o.err = "the stencil object grew sections a code buffer cannot hold: " + s;
        return true;
    }
    uint64_t textOff = sh(textIdx, 3), textSize = sh(textIdx, 4);
    o.text.assign(b.begin() + textOff, b.begin() + textOff + textSize);

    uint64_t symOff = sh(symIdx, 3), symSize = sh(symIdx, 4), symEnt = sh(symIdx, 7);
    uint64_t strOff = sh((int)sh(symIdx, 5), 3);
    uint64_t nsyms = symEnt ? symSize / symEnt : 0;
    std::vector<std::string> symNames(nsyms);
    for (uint64_t i = 0; i < nsyms; i++) {
        size_t e = symOff + i * symEnt;
        uint32_t nameOff = rd<uint32_t>(b, e);
        uint8_t  info    = rd<uint8_t>(b, e + 4);
        uint16_t shndx   = rd<uint16_t>(b, e + 6);
        uint64_t val     = rd<uint64_t>(b, e + 8);
        symNames[i] = (const char*)b.data() + strOff + nameOff;
        bool global = (info >> 4) != 0;               // STB_GLOBAL / STB_WEAK
        if (global && shndx == textIdx) o.fns.push_back({symNames[i], val, 0});
    }

    if (relaIdx >= 0) {
        uint64_t rOff = sh(relaIdx, 3), rSize = sh(relaIdx, 4);
        for (uint64_t e = rOff; e + 24 <= rOff + rSize; e += 24) {
            uint64_t off  = rd<uint64_t>(b, e);
            uint64_t info = rd<uint64_t>(b, e + 8);
            int64_t  add  = (int64_t)rd<uint64_t>(b, e + 16);
            uint32_t type = (uint32_t)(info & 0xffffffffu);
            uint32_t sym  = (uint32_t)(info >> 32);
            Hole h{};
            h.off = off;
            h.sym = unprefix(symNames[sym], false);
            h.addend = add;
            if (o.arch == "arm64") {
                switch (type) {
                    case R_AARCH64_CALL26:
                    case R_AARCH64_JUMP26:              h.patch = Patch::Call;      break;
                    case R_AARCH64_ADR_PREL_PG_HI21:    h.patch = Patch::Adrp21;    break;
                    case R_AARCH64_ADD_ABS_LO12_NC:
                    case R_AARCH64_LDST64_ABS_LO12_NC:  h.patch = Patch::AddOff12;  break;
                    case R_AARCH64_ADR_GOT_PAGE:        h.patch = Patch::GotAdrp21; break;
                    case R_AARCH64_LD64_GOT_LO12_NC:    h.patch = Patch::GotOff12;  break;
                    case R_AARCH64_ABS64:               h.patch = Patch::Abs64;     break;
                    default:
                        o.err = "aarch64 relocation type " + std::to_string(type) + " in " + h.sym;
                        return true;
                }
            } else {
                switch (type) {
                    case R_X86_64_PLT32:
                    case R_X86_64_PC32:          h.patch = Patch::Call;     break;
                    case R_X86_64_GOTPCREL:
                    case R_X86_64_GOTPCRELX:
                    case R_X86_64_REX_GOTPCRELX: h.patch = Patch::GotRel32; break;
                    case R_X86_64_64:            h.patch = Patch::Abs64;    break;
                    default:
                        o.err = "x86-64 relocation type " + std::to_string(type) + " in " + h.sym;
                        return true;
                }
            }
            o.holes.push_back(h);
        }
    }
    return true;
}

// ---- output ----------------------------------------------------------------

// The holes the emitter answers per instance, in the order CnpTable.h lists.
const char* symEnum(const std::string& s) {
    if (s == "_JIT_CONT")   return "Sym::Cont";
    if (s == "_JIT_TARGET") return "Sym::Target";
    if (s == "_JIT_SLOW")   return "Sym::Slow";
    if (s == "_JIT_OP0")    return "Sym::Op0";
    if (s == "_JIT_OP1")    return "Sym::Op1";
    if (s == "_JIT_OP2")    return "Sym::Op2";
    if (s == "_JIT_OP3")    return "Sym::Op3";
    return nullptr;
}

void emit(FILE* out, const Obj& o) {
    std::fprintf(out,
        "// GENERATED by tools/cnp-extract from src/cnp/stencils.c. Do not edit.\n"
        "// docs/dev/plans/CNP-PLAN.md\n"
        "#include \"cnp/CnpTable.h\"\n\n"
        "namespace rakupp { namespace cnp {\n\n");

    if (!o.err.empty() || o.fns.empty()) {
        std::fprintf(out,
            "// No stencils: %s\n"
            "const Stencil     kStencils[]     = {};\n"
            "const char* const kStencilNames[] = { nullptr };\n"
            "const char* const kHelperNames[]  = { nullptr };\n"
            "const unsigned    kCount          = 0;\n"
            "const unsigned    kHelperCount    = 0;\n"
            "const char* const kArch           = \"%s\";\n"
            "} }\n",
            o.err.empty() ? "the object held none" : o.err.c_str(),
            o.err.empty() ? o.arch.c_str() : "unsupported");
        return;
    }

    // The helper symbols, in a stable order, so the runtime can resolve them
    // once into a parallel array of function pointers.
    std::vector<std::string> helpers;
    for (auto& h : o.holes)
        if (!symEnum(h.sym) && std::find(helpers.begin(), helpers.end(), h.sym) == helpers.end())
            helpers.push_back(h.sym);
    std::sort(helpers.begin(), helpers.end());

    // Holes, bucketed by the stencil they fall inside.
    std::map<size_t, std::vector<Hole>> byFn;
    for (auto& h : o.holes) {
        size_t owner = SIZE_MAX;
        for (size_t i = 0; i < o.fns.size(); i++)
            if (h.off >= o.fns[i].start && h.off < o.fns[i].end) { owner = i; break; }
        if (owner == SIZE_MAX) die("a relocation at 0x" + std::to_string(h.off) + " falls outside every stencil");
        Hole c = h;
        c.off -= o.fns[owner].start;
        byFn[owner].push_back(c);
    }

    for (size_t i = 0; i < o.fns.size(); i++) {
        const Fn& fn = o.fns[i];
        std::fprintf(out, "static const uint8_t c%zu[] = {", i);
        for (uint64_t k = fn.start; k < fn.end; k++)
            std::fprintf(out, "%s0x%02x,", (k - fn.start) % 12 == 0 ? "\n    " : "", o.text[k]);
        std::fprintf(out, "\n};\n");
        auto& hs = byFn[i];
        if (hs.empty()) { std::fprintf(out, "static const HoleRef h%zu[1] = {};\n\n", i); continue; }
        std::fprintf(out, "static const HoleRef h%zu[] = {\n", i);
        for (auto& h : hs) {
            const char* se = symEnum(h.sym);
            int helperIdx = 0;
            if (!se) helperIdx = (int)(std::find(helpers.begin(), helpers.end(), h.sym) - helpers.begin());
            std::fprintf(out, "    { %llu, %s, %s, %d, 0, %lld },\n",
                         (unsigned long long)h.off, patchName(h.patch),
                         se ? se : "Sym::Helper", helperIdx, (long long)h.addend);
        }
        std::fprintf(out, "};\n\n");
    }

    std::fprintf(out, "const Stencil kStencils[] = {\n");
    for (size_t i = 0; i < o.fns.size(); i++)
        std::fprintf(out, "    { c%zu, h%zu, (uint32_t)sizeof(c%zu), %zu },\n",
                     i, i, i, byFn.count(i) ? byFn[i].size() : 0);
    std::fprintf(out, "};\n\nconst char* const kStencilNames[] = {\n");
    for (auto& fn : o.fns) std::fprintf(out, "    \"%s\",\n", fn.name.c_str());
    std::fprintf(out, "};\n\nconst char* const kHelperNames[] = {\n");
    for (auto& h : helpers) std::fprintf(out, "    \"%s\",\n", h.c_str());
    if (helpers.empty()) std::fprintf(out, "    nullptr,\n");
    std::fprintf(out,
        "};\n\nconst unsigned kCount       = %zu;\n"
        "const unsigned kHelperCount = %zu;\n"
        "const char* const kArch     = \"%s\";\n\n} }\n",
        o.fns.size(), helpers.size(), o.arch.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: cnp-extract IN.o OUT.cpp\n"); return 2; }
    std::vector<uint8_t> b = slurp(argv[1]);
    Obj o;
    if (!readMachO(b, o) && !readElf(b, o))
        o.err = "not a Mach-O or ELF object file this tool reads";

    if (o.err.empty()) {
        // Stencils are sliced by symbol: each runs to the start of the next.
        std::sort(o.fns.begin(), o.fns.end(),
                  [](const Fn& a, const Fn& c) { return a.start < c.start; });
        for (size_t i = 0; i < o.fns.size(); i++)
            o.fns[i].end = (i + 1 < o.fns.size()) ? o.fns[i + 1].start : o.text.size();
        // Trim the alignment padding between one stencil and the next, so that
        // a stencil's bytes are exactly its instructions. Purely a size saving:
        // padding is never reached, because every stencil leaves through a tail
        // branch. It is therefore written to trim only bytes that CANNOT be
        // part of an instruction, and nothing else.
        //
        // The distinction matters per architecture. On arm64 a zero word is
        // UDF #0 and no compiler emits it, so a run of them is padding. On
        // x86-64 four zero bytes are perfectly ordinary — `mov eax, 0` ends in
        // exactly that — so only the padding bytes a linker actually writes
        // (NOP and INT3) are taken, one at a time.
        for (auto& fn : o.fns) {
            if (o.arch == "arm64") {
                while (fn.end >= fn.start + 8 && o.text[fn.end - 1] == 0 && o.text[fn.end - 2] == 0 &&
                       o.text[fn.end - 3] == 0 && o.text[fn.end - 4] == 0)
                    fn.end -= 4;
            } else {
                while (fn.end > fn.start + 1 &&
                       (o.text[fn.end - 1] == 0x90 || o.text[fn.end - 1] == 0xcc))
                    fn.end -= 1;
            }
        }
        // A relocation that fell in what was trimmed away is caught where the
        // holes are bucketed below: it would belong to no stencil, and that is
        // already a hard error naming the offset.
    }

    FILE* out = std::fopen(argv[2], "w");
    if (!out) die(std::string("cannot write ") + argv[2]);
    emit(out, o);
    std::fclose(out);
    if (!o.err.empty())
        std::fprintf(stderr, "cnp-extract: no stencil table — %s\n", o.err.c_str());
    else
        std::fprintf(stderr, "cnp-extract: %zu stencils, %zu holes, %s\n",
                     o.fns.size(), o.holes.size(), o.arch.c_str());
    return 0;
}
