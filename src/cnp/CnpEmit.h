#pragma once
// The copy-and-patch assembler: a list of IR ops in, a block of executable
// memory out. docs/dev/plans/CNP-PLAN.md.
//
// This half knows about instruction encodings and executable memory and
// NOTHING about Raku. tools/cnp-extract knows about object formats and nothing
// about either. src/Cnp.cpp knows about Raku and calls this. The three meet
// only at cnp/CnpTable.h, which is why each stays small enough to read.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rakupp {
namespace cnp {

// One instruction of the kernel. `stencil` picks the machine-code template;
// `operand` fills its Op0..Op3 holes; the three indices say where the three
// continuations go, as indices into the op vector.
struct Op {
    uint16_t stencil = 0;
    uint64_t operand[4] = {0, 0, 0, 0};
    int cont   = -1;   // -1 means "the next op in the vector"
    int target = -1;   // a branch target; required by the branch stencils
    int slow   = -1;   // the cold block a guard falls out to
};

// A finished kernel: executable memory that lives until this object dies.
class Code {
public:
    Code() = default;
    ~Code();
    Code(const Code&) = delete;
    Code& operator=(const Code&) = delete;

    void* entry() const { return entry_; }
    size_t bytes() const { return size_; }

    void*  mem_   = nullptr;
    size_t size_  = 0;
    void*  entry_ = nullptr;
};

// True when this build carries a stencil table this patcher can use.
bool stencilsAvailable();
// The reason it is not available, for `--cnp=verbose` and the error message.
const char* stencilsUnavailableReason();
// The index of a stencil by the name it has in stencils.c, or -1.
int stencilIndex(const char* name);
// What the table was extracted for ("arm64", "x86_64", …).
const char* arch();

// Copy and patch. Null on refusal, with `err` saying why.
std::unique_ptr<Code> assemble(const std::vector<Op>& ops, std::string& err);

}  // namespace cnp
}  // namespace rakupp
