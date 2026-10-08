// `--cnp`, the copy-and-patch backend — docs/dev/plans/CNP-PLAN.md.
//
// Three things live here:
//
//   1. THE HELPERS the stencils call. Every one of them is a catch-all
//      boundary: it does its work through the interpreter's own runtime, and
//      if that throws it puts the exception in the frame and returns a status.
//      Nothing may throw back into the code buffer, which has no unwind tables.
//   2. THE TRAMPOLINE. It unboxes the interpreter's containers into the
//      register file, calls the kernel, boxes them back, and rethrows anything
//      the frame is carrying — in a C++ frame, where a throw is ordinary again.
//   3. THE LOWERING: the whitelisted loop subtree into a linear register IR.
//
// The register file is the reason this is fast, and it is sound for exactly one
// reason, which is the same rule `--jit` rests on: the eligibility whitelist
// admits NO CALLS. With no call in the subtree, nothing running inside a kernel
// can observe a loop variable except through the kernel's own registers, so
// holding them unboxed from entry to exit is unobservable. Every widening of
// the whitelist has to answer that again.
#include "Cnp.h"
#include "Jit.h"
#include "Ast.h"
#include "Interpreter.h"
#include "Value.h"
#include "cnp/CnpAbi.h"
#include "cnp/CnpEmit.h"

#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <vector>

namespace rakupp {
namespace cnp {

// An element store: the register holding the array (an `@name` slot) and one
// key register per level, `@g[$y][$x]` being two.
struct IndexSite {
    int base = 0;
    std::vector<int> keys;
    std::vector<char> hash;   // per level: `{…}` (1) or `[…]` (0)
};


// A call in a kernel: the arguments the kernel evaluated go into a scratch
// scope as `$__cnp_argN`, and `synth` — the call rewritten to read them — is
// evaluated by the interpreter, so lookup, dispatch and builtins are its own.
struct KernelCall {
    std::unique_ptr<Call> synth;          // a sub call, or…
    std::unique_ptr<MethodCall> msynth;   // …a method call (its invocant is argRegs[0])
    std::unique_ptr<InterpStr> isynth;    // …or an interpolation, every part in argRegs
    std::string method;                   // the method's name, for the fast path
    bool pureMethod = false;              // a side-effect-free builtin method (see isPureMethod)
    std::vector<int> argRegs;
    std::vector<std::string> argNames;
    const BuiltinFn* builtin = nullptr;   // the builtin of that name, looked up on first use
    bool builtinChecked = false;
};


namespace {

inline double  bits2d(int64_t x) { double d; std::memcpy(&d, &x, 8); return d; }
inline int64_t d2bits(double d)  { int64_t x; std::memcpy(&x, &d, 8); return x; }

// The frame the trampoline owns and the stencils see. `abi` is the part the
// machine code knows about; everything else is C++ the helpers reach through it.
struct Frame {
    RkCnpFrame           abi{};
    std::vector<int64_t> r;
    std::vector<uint8_t> t;
    std::vector<Value>   boxes;
    std::vector<int64_t> den;   // the Rat lane's denominators
    std::exception_ptr   err;

    explicit Frame(size_t n) : r(n, 0), t(n, RK_T_INT), boxes(n), den(n, 1) {
        abi.r = r.data();
        abi.t = t.data();
        abi.boxes = boxes.data();
        abi.err = &err;
        abi.den = den.data();
    }
};

// Everything a helper needs is reachable through the ABI struct's own fields,
// so nothing here has to cast the frame back to the C++ type that contains it —
// which would mean an offsetof on a type holding vectors.
Interpreter& interpOf(RkCnpFrame* f) { return *static_cast<Interpreter*>(f->interp); }
void stash(RkCnpFrame* f) { *static_cast<std::exception_ptr*>(f->err) = std::current_exception(); }

// ---- register <-> Value ----------------------------------------------------

Value regValue(RkCnpFrame* f, uint64_t k) {
    switch (f->t[k]) {
        case RK_T_INT:  return Value::integer(f->r[k]);
        case RK_T_NUM:  return Value::number(bits2d(f->r[k]));
        case RK_T_BOOL: return Value::boolean(f->r[k] != 0);
        case RK_T_RAT:  return Value::rat(BigInt(f->r[k]), BigInt(f->den[k]));
        default:        return static_cast<Value*>(f->boxes)[k];
    }
}

// The unboxing rules, and why each exclusion is there:
//   * a bignum Int cannot be an int64 at all;
//   * `enumName` is the value's identity — `Less` prints as "Less", and a
//     register that kept only its .i would print as -1 the moment it was copied
//     into another container;
//   * everything else is a Str, a Rat, an object … and stays a Value.
// `natBits`/`natFloat` are deliberately NOT excluded: they describe what
// happens when something is STORED into that container, and the harness already
// refuses a native container the kernel would write to. Reading one is fine.
void setReg(RkCnpFrame* f, uint64_t k, const Value& v) {
    Value* boxes = static_cast<Value*>(f->boxes);
    uint8_t was = f->t[k];
    if (v.enumName.empty() && v.t == VT::Int && !v.big()) { f->r[k] = v.i; f->t[k] = RK_T_INT; }
    else if (v.enumName.empty() && v.t == VT::Num)        { f->r[k] = d2bits(v.n); f->t[k] = RK_T_NUM; }
    else if (v.enumName.empty() && v.t == VT::Bool)       { f->r[k] = v.b ? 1 : 0; f->t[k] = RK_T_BOOL; }
    // a Rat whose parts fit an int64 rides the Rat lane (a zero denominator,
    // `1/0`, stays boxed: only applyArith knows what it means)
    else if (v.enumName.empty() && v.t == VT::Rat && v.hashKind.empty() && v.ratN() && v.ratD() &&
             v.ratN()->fitsLL() && v.ratD()->fitsLL() && !v.ratD()->isZero()) {
        f->r[k] = v.ratN()->toLL(); f->den[k] = v.ratD()->toLL(); f->t[k] = RK_T_RAT;
    }
    else { boxes[k] = v; f->t[k] = RK_T_BOX; return; }
    // Release whatever the register used to hold, so that a loop cannot pin a
    // string or an object it stopped using thousands of iterations ago.
    if (was == RK_T_BOX) boxes[k] = Value();
}

// ---- the operator table ----------------------------------------------------
//
// One table, read from both ends: the lowering turns an operator into an index,
// a helper turns the index back into the string `applyArith` dispatches on. A
// single table is what makes it impossible for the two to disagree.
const char* const kOpNames[] = {
    "+", "-", "*", "/", "%", "**",
    "div", "mod", "%%", "gcd", "lcm",
    "<", "<=", ">", ">=", "==", "!=", "<=>",
    "eq", "ne", "lt", "gt", "le", "ge",
    "leg", "cmp", "~", "x",
    "+&", "+|", "+^", "+<", "+>",
    "min", "max", "^^",
    "\0neg", "\0plus", "\0not", "\0so", "\0bnot", "\0str",
};
static_assert(sizeof(kOpNames) / sizeof(*kOpNames) == RK_OP__COUNT,
              "kOpNames and enum RkCnpOp must stay in step");

int opIndex(const std::string& op) {
    for (int i = 0; i < RK_OP_NEG; i++) if (op == kOpNames[i]) return i;
    // `and`/`or` are short-circuit and never reach here; `xor` is the word form
    // of `^^`, which applyArith does take.
    if (op == "xor") return RK_OP_XOR;
    return -1;
}

}  // namespace

// ---- the helpers -----------------------------------------------------------
//
// THE RULE: none of these throws. Each is the boundary between a code buffer
// with no unwind tables and a runtime whose errors are C++ exceptions.
extern "C" {

int rk_cnp_binop(RkCnpFrame* f, uint64_t op, uint64_t d, uint64_t a, uint64_t b) {
    try {
        // `$s ~= …`, the compound-assign shape the lowering emits as
        // `binop(dst, dst, src)`. The general path below copies the accumulator
        // OUT of its box (regValue returns by value), builds a whole new string
        // beside it, and copies that back in — three O(n) passes per append, so
        // a loop that appends n times moves O(n²) bytes. Measured: 400,000
        // appends took 34.4 s against the interpreter's 0.06 s, and the answer
        // was right the whole time, which is why nothing caught it.
        //
        // rtCatAssign appends into the box instead. It is the same definition
        // the interpreter and `--exe` already use for `~=` — the comment on it
        // in Interpreter.h says "one definition for the interpreter and both
        // compiling backends", and this is the caller that was missing.
        if (d == a && (op & ~(uint64_t)RK_OP_SELFCAT) == (uint64_t)RK_OP_CONCAT && f->t[a] == RK_T_BOX) {
            Value* boxes = static_cast<Value*>(f->boxes);
            const Value& acc = boxes[d];
            if (!(op & RK_OP_SELFCAT) ||
                (acc.t == VT::Str && acc.hashKind.empty() && acc.enumName.empty() && !acc.natBits)) {
                const Value vb = regValue(f, b);
                rtCatAssign(boxes[d], vb);
                return 0;
            }
        }
        // `$s = X ~ $s` (RK_OP_SELFCAT with the destination on the right): in
        // front of a plain Str in place; anything else takes the general `~`
        if (d == b && d != a && op == (uint64_t)(RK_OP_CONCAT | RK_OP_SELFCAT) && f->t[b] == RK_T_BOX) {
            Value* boxes = static_cast<Value*>(f->boxes);
            const Value& acc = boxes[d];
            if (acc.t == VT::Str && acc.hashKind.empty() && acc.enumName.empty() && !acc.natBits) {
                const Value va = regValue(f, a);
                rtCatPrepend(boxes[d], va);
                return 0;
            }
        }
        op &= ~(uint64_t)RK_OP_SELFCAT;
        // A native int operation wraps where the general one would grow: the
        // interpreter's own definition (nativeIntArith), on the two machine ints
        if (op & RK_OP_NATIVE) {
            op &= ~(uint64_t)RK_OP_NATIVE;
            if (f->t[a] == RK_T_INT && f->t[b] == RK_T_INT) {
                long long x;
                if (rtNativeIntOp(std::string(kOpNames[op]), f->r[a], f->r[b], x)) {
                    setReg(f, d, Value::integer(x));
                    return 0;
                }
                // `$i ** -1` on natives is the native candidate: an Int, 0
                if (op == (uint64_t)RK_OP_POW && f->r[b] < 0) { setReg(f, d, Value::integer(0)); return 0; }
            }
        }
        // Two machine Ints and an operator with no stencil of its own: the
        // answers applyArith gives, without boxing both sides and finding the
        // operator by its spelling. Anything at an edge — a zero divisor, an
        // overflow, a shift out of range — is left to applyArith.
        if (f->t[a] == RK_T_INT && f->t[b] == RK_T_INT) {
            const long long x = (long long)f->r[a], y = (long long)f->r[b];
            switch (op) {
                case RK_OP_MOD: case RK_OP_MODOP:
                    if (y != 0 && !(y == -1 && x == LLONG_MIN)) {
                        long long m = x % y;
                        if (m != 0 && ((m < 0) != (y < 0))) m += y;
                        setReg(f, d, Value::integer(m)); return 0;
                    }
                    break;
                case RK_OP_DIVIS:
                    if (y != 0) { setReg(f, d, Value::boolean(y == -1 || x % y == 0)); return 0; }
                    break;
                case RK_OP_IDIV:
                    if (y != 0 && !(y == -1 && x == LLONG_MIN)) {
                        long long q = x / y;
                        if ((x % y != 0) && ((x < 0) != (y < 0))) q--;
                        setReg(f, d, Value::integer(q)); return 0;
                    }
                    break;
                case RK_OP_BAND: setReg(f, d, Value::integer(x & y)); return 0;
                case RK_OP_BOR:  setReg(f, d, Value::integer(x | y)); return 0;
                case RK_OP_BXOR: setReg(f, d, Value::integer(x ^ y)); return 0;
                case RK_OP_SHL:
                    if (y >= 0 && y < 63 && x >= 0 && (x >> (62 - y)) == 0) { setReg(f, d, Value::integer(x << y)); return 0; }
                    break;
                case RK_OP_SHR:
                    if (y >= 0 && y < 64) { setReg(f, d, Value::integer(x >> y)); return 0; }
                    break;
                case RK_OP_MIN: setReg(f, d, Value::integer(x < y ? x : y)); return 0;
                case RK_OP_MAX: setReg(f, d, Value::integer(x > y ? x : y)); return 0;
                default: break;
            }
        }
        Value va = regValue(f, a), vb = regValue(f, b);
        setReg(f, d, applyArith(std::string(kOpNames[op]), va, vb));
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_call(RkCnpFrame* f, uint64_t site, uint64_t dst) {
    Interpreter& I = interpOf(f);
    auto saved = Interpreter::tctx_.cur;
    Value** slots = static_cast<Value**>(f->slots);
    const auto& written = *static_cast<const std::vector<bool>*>(f->written);
    const size_t n = (size_t)f->nslots;
    // The callee may see the loop's variables (a sub that closes over one),
    // so the registers go back to their containers first — keeping a native
    // container's tags, as the final write-back does — and are reloaded
    // after, since the callee may have written them.
    auto spill = [&] {
        for (size_t i = 0; i < n; i++)
            if (written[i]) {
                Value* c = slots[i];
                const int nb = c->natBits; const bool ns = c->natSigned, nf = c->natFloat;
                *c = regValue(f, i);
                if (nb) { c->natBits = nb; c->natSigned = ns; c->natFloat = nf; }
            }
    };
    auto reload = [&] { for (size_t i = 0; i < n; i++) setReg(f, i, *slots[i]); };
    try {
        auto& cs = (*static_cast<std::vector<rakupp::cnp::KernelCall>*>(f->calls))[site];
        // An interpolation whose parts are no objects reaches no user code
        // (only an object can carry a .Str of its own), so it needs neither
        // the write-back nor the reload a call has: the interpreter's own
        // interpolate() over the part values, directly.
        if (cs.isynth) {
            ValueList vals;
            vals.reserve(cs.argRegs.size());
            bool plain = true;
            for (int r : cs.argRegs) {
                vals.push_back(regValue(f, (uint64_t)r));
                if (vals.back().t == VT::Object) plain = false;
            }
            if (plain) { setReg(f, dst, I.interpolate(vals)); return 0; }
        }
        // A side-effect-free builtin METHOD on a value that is not an object
        // goes straight to the runtime's method dispatch, the route --exe
        // calls methods by. It reaches no user code and no container of the
        // loop's, so the registers neither go back first nor come back after
        // (and a throw leaves them to the kernel's exit, which writes them
        // back on both paths).
        if (cs.msynth && cs.pureMethod) {
            Value inv = regValue(f, (uint64_t)cs.argRegs[0]);
            if (inv.t != VT::Object) {
                ValueList args;
                for (size_t k = 1; k < cs.argRegs.size(); k++) args.push_back(regValue(f, (uint64_t)cs.argRegs[k]));
                setReg(f, dst, I.methodCall(inv, cs.method, std::move(args)));
                return 0;
            }
        }
        spill();
        // A BUILTIN the program has not shadowed goes straight to its function,
        // the route --exe calls builtins by: no scratch scope, no re-dispatch.
        if (!cs.builtinChecked) {
            cs.builtin = cs.synth ? I.builtinPtr(cs.synth->name) : nullptr;
            cs.builtinChecked = true;
        }
        if (cs.builtin && cs.synth && !saved->find("&" + cs.synth->name)) {
            ValueList args;
            args.reserve(cs.argRegs.size());
            for (int r : cs.argRegs) args.push_back(regValue(f, (uint64_t)r));
            Value res;
            try { res = rtCallB(I, cs.builtin, cs.synth->name.c_str(), std::move(args)); }
            catch (LastEx& e) { if (!e.label.empty()) throw; reload(); f->ctl = 1; return 0; }
            catch (NextEx& e) { if (!e.label.empty()) throw; reload(); f->ctl = 2; return 0; }
            reload();
            setReg(f, dst, res);
            return 0;
        }
        auto env = std::make_shared<Env>();
        env->parent = saved;
        for (size_t k = 0; k < cs.argRegs.size(); k++) env->define(cs.argNames[k], regValue(f, cs.argRegs[k]));
        Interpreter::tctx_.cur = env;
        Value res;
        try { res = cs.isynth ? I.eval(cs.isynth.get()) : cs.msynth ? I.eval(cs.msynth.get()) : I.eval(cs.synth.get()); }
        catch (LastEx& e) {
            if (!e.label.empty()) throw;
            Interpreter::tctx_.cur = saved; reload(); f->ctl = 1; return 0;
        }
        catch (NextEx& e) {
            if (!e.label.empty()) throw;
            Interpreter::tctx_.cur = saved; reload(); f->ctl = 2; return 0;
        }
        Interpreter::tctx_.cur = saved;
        reload();
        setReg(f, dst, res);
        return 0;
    } catch (...) {
        Interpreter::tctx_.cur = saved;
        stash(f);
        return 1;
    }
}

int rk_cnp_idxget(RkCnpFrame* f, uint64_t base, uint64_t key, uint64_t dst, uint64_t isHash) {
    try {
        Value v = rtIndexGet(regValue(f, base), regValue(f, key), isHash != 0);
        setReg(f, dst, v);
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_newarr(RkCnpFrame* f, uint64_t dst) {
    try {
        setReg(f, dst, rtTypedDefault("", '@'));   // what the interpreter's `my @x` starts as
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_chain(RkCnpFrame* f, uint64_t op, uint64_t d, uint64_t a, uint64_t b) {
    try {
        const bool ok = interpOf(f).chainLink(std::string(kOpNames[op]), regValue(f, a), regValue(f, b));
        setReg(f, d, Value::boolean(ok));
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_alen(RkCnpFrame* f, uint64_t base, uint64_t dst) {
    try {
        if (f->t[base] == RK_T_BOX) {
            const Value& b = static_cast<Value*>(f->boxes)[base];
            if (b.t == VT::Array && b.arr() && !b.ext() && b.hashKind.empty() && b.enumName.empty()) {
                setReg(f, dst, Value::integer((long long)b.arr()->size()));
                return 0;
            }
        }
        ValueList none;
        setReg(f, dst, interpOf(f).methodCall(regValue(f, base), "elems", none));
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_idxset(RkCnpFrame* f, uint64_t site, uint64_t val) {
    try {
        const auto& s = (*static_cast<const std::vector<rakupp::cnp::IndexSite>*>(f->isites))[site];
        // The array register is a box sharing its element storage with the
        // variable, so references into it are references into the variable.
        Value* boxes = static_cast<Value*>(f->boxes);
        if (f->t[s.base] != RK_T_BOX) throw RakuError{Value::typeObj("X::AdHoc"), "Cannot index into a non-container"};
        Value* cur = &boxes[s.base];
        for (size_t i = 0; i + 1 < s.keys.size(); i++)
            cur = &rtIndexRef(*cur, regValue(f, (uint64_t)s.keys[i]), s.hash[i] != 0);
        // stored as the interpreter stores an element: Nil resets it to the
        // default (Any), a container goes in as one item, a native's tags stay
        // behind, and the stored value is a fresh writable one
        Value v = regValue(f, val);
        if (v.t == VT::Nil) v = Value::typeObj("Any");
        v.readonly = v.immutableBind = false;
        if ((v.t == VT::Array || v.t == VT::Hash) && !v.itemized) v.itemized = true;
        v.natBits = 0; v.natSigned = v.natFloat = false;
        rtIndexRef(*cur, regValue(f, (uint64_t)s.keys.back()), s.hash.back() != 0) = std::move(v);
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_natchk(RkCnpFrame* f, uint64_t reg, uint64_t kind, uint64_t name, uint64_t dst) {
    try {
        Value v = regValue(f, reg);
        reg = dst;   // the checked value lands in the variable, not the source
        const bool fromNative = (kind & 0x100) != 0;
        kind &= 0xff;
        const bool isFloat = kind == RK_T_NUM;
        // a native of the other kind converts (the registers carry no tags, so
        // the lowering said which values are natives)
        if (fromNative && (v.t == VT::Int || v.t == VT::Num)) {
            if (isFloat) setReg(f, reg, Value::number(v.toNum()));
            else setReg(f, reg, Value::integer(v.toInt()));   // toward zero, saturating, NaN 0
            return 0;
        }
        const auto& consts = *static_cast<const std::vector<Value>*>(f->consts);
        nativeAssignCheck(v, 64, isFloat, consts[name].s.str(), true);
        // …and what the check accepts is stored as the interpreter stores it
        if (isFloat) setReg(f, reg, Value::number(v.toNum()));
        else if (v.t == VT::Int && v.big()) setReg(f, reg, Value::integer((long long)v.big()->toU64Wrap()));
        else setReg(f, reg, Value::integer(v.toInt()));
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_unop(RkCnpFrame* f, uint64_t op, uint64_t d, uint64_t a) {
    try {
        // `-$n` on a native int wraps: `-int.min` is int.min
        if (op & RK_OP_NATIVE) {
            op &= ~(uint64_t)RK_OP_NATIVE;
            if (op == (uint64_t)RK_OP_NEG && f->t[a] == RK_T_INT) {
                setReg(f, d, Value::integer((long long)(0ULL - (unsigned long long)f->r[a])));
                return 0;
            }
        }
        Value v = regValue(f, a);
        // Verbatim from what Codegen emits for the same operators, so that a
        // kernel and a compiled program cannot disagree about them.
        switch (op) {
            case RK_OP_NEG:  setReg(f, d, applyArith(std::string("-"), Value::integer(0), v)); break;
            case RK_OP_PLUS: setReg(f, d, applyArith(std::string("+"), Value::integer(0), v)); break;
            case RK_OP_NOT:  setReg(f, d, Value::boolean(!interpOf(f).boolify(v))); break;
            case RK_OP_SO:   setReg(f, d, Value::boolean(interpOf(f).boolify(v))); break;
            case RK_OP_STR:  setReg(f, d, Value::str(v.toStr())); break;
            case RK_OP_BNOT: setReg(f, d, Value::integer(~v.toInt())); break;
            default: return 1;
        }
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_cmp(RkCnpFrame* f, uint64_t op, uint64_t a, uint64_t b) {
    try {
        Value va = regValue(f, a), vb = regValue(f, b);
        return interpOf(f).boolify(applyArith(std::string(kOpNames[op]), va, vb)) ? 1 : 0;
    } catch (...) { stash(f); return -1; }
}

int rk_cnp_truthy(RkCnpFrame* f, uint64_t a) {
    try { return interpOf(f).boolify(regValue(f, a)) ? 1 : 0; }
    catch (...) { stash(f); return -1; }
}

int rk_cnp_defined(RkCnpFrame* f, uint64_t a) {
    try { return rtIsDefined(regValue(f, a)) ? 1 : 0; }
    catch (...) { stash(f); return -1; }
}

int rk_cnp_loadk(RkCnpFrame* f, uint64_t d, uint64_t k) {
    try { setReg(f, d, (*static_cast<const std::vector<Value>*>(f->consts))[k]); return 0; }
    catch (...) { stash(f); return 1; }
}

int rk_cnp_move(RkCnpFrame* f, uint64_t d, uint64_t a) {
    try { setReg(f, d, static_cast<Value*>(f->boxes)[a]); return 0; }
    catch (...) { stash(f); return 1; }
}

}  // extern "C"

// ---- the kernel ------------------------------------------------------------

struct Kernel {
    std::unique_ptr<Code> code;
    std::vector<Value>    consts;
    std::vector<KernelCall> calls;
    std::vector<IndexSite>  isites;
    size_t                nregs = 0;
    size_t                nops  = 0;
};

bool makesCalls(const Kernel* k) { return k && !k->calls.empty(); }

namespace {

// ---- lowering --------------------------------------------------------------

// The stencils, by the name they have in stencils.c. Resolved once; a name that
// is not there turns `--cnp` off with a message rather than emitting a wrong
// index.
struct StencilIds {
    int loadi, loadn, loadb, loadk, move, movebox;
    int add, sub, mul, addi, incr, neg, notOp, binop, unop;
    int cmp[6];          // lt le gt ge eq ne — the value form
    int jcmp[6], jcmpi[6], jncmp[6], jncmpi[6];
    int jmp, jt, jf, jdef, ret, jtslow, jfslow, jdefslow;
    int natchk, natchkslow, call, jctl, idxget, idxset, div, alen, chain, newarr;
    bool ok = false;
};

const StencilIds& ids() {
    static StencilIds s = [] {
        StencilIds v{};
        auto g = [&](const char* n) { int i = stencilIndex(n); if (i < 0) v.ok = false; return i; };
        v.ok = true;
        v.loadi = g("rk_st_loadi"); v.loadn = g("rk_st_loadn"); v.loadb = g("rk_st_loadb");
        v.loadk = g("rk_st_loadk"); v.move  = g("rk_st_move");  v.movebox = g("rk_st_movebox");
        v.add = g("rk_st_add"); v.sub = g("rk_st_sub"); v.mul = g("rk_st_mul");
        v.addi = g("rk_st_addi"); v.incr = g("rk_st_incr");
        v.neg = g("rk_st_neg"); v.notOp = g("rk_st_not");
        v.binop = g("rk_st_binop"); v.unop = g("rk_st_unop");
        static const char* cv[6] = {"rk_st_cmplt","rk_st_cmple","rk_st_cmpgt","rk_st_cmpge","rk_st_cmpeq","rk_st_cmpne"};
        static const char* jb[6] = {"rk_st_jlt","rk_st_jle","rk_st_jgt","rk_st_jge","rk_st_jeq","rk_st_jne"};
        static const char* ji[6] = {"rk_st_jlti","rk_st_jlei","rk_st_jgti","rk_st_jgei","rk_st_jeqi","rk_st_jnei"};
        static const char* nb[6] = {"rk_st_jnlt","rk_st_jnle","rk_st_jngt","rk_st_jnge","rk_st_jneq","rk_st_jnne"};
        static const char* ni[6] = {"rk_st_jnlti","rk_st_jnlei","rk_st_jngti","rk_st_jngei","rk_st_jneqi","rk_st_jnnei"};
        for (int i = 0; i < 6; i++) {
            v.cmp[i] = g(cv[i]); v.jcmp[i] = g(jb[i]); v.jcmpi[i] = g(ji[i]);
            v.jncmp[i] = g(nb[i]); v.jncmpi[i] = g(ni[i]);
        }
        v.jmp = g("rk_st_jmp"); v.jt = g("rk_st_jt"); v.jf = g("rk_st_jf"); v.jdef = g("rk_st_jdef");
        v.ret = g("rk_st_ret");
        v.jtslow = g("rk_st_jtslow"); v.jfslow = g("rk_st_jfslow"); v.jdefslow = g("rk_st_jdefslow");
        v.natchk = g("rk_st_natchk"); v.natchkslow = g("rk_st_natchkslow");
        v.call = g("rk_st_call"); v.jctl = g("rk_st_jctl");
        v.idxget = g("rk_st_idxget"); v.idxset = g("rk_st_idxset");
        v.div = g("rk_st_div");
        v.alen = g("rk_st_alen");
        v.chain = g("rk_st_chain");
        v.newarr = g("rk_st_newarr");
        return v;
    }();
    return s;
}

// Which of the six comparisons an operator is, or -1.
int cmpSlot(const std::string& op) {
    if (op == "<")  return 0;
    if (op == "<=") return 1;
    if (op == ">")  return 2;
    if (op == ">=") return 3;
    if (op == "==") return 4;
    if (op == "!=") return 5;
    return -1;
}
int cmpOpIndex(int slot) {
    static const int m[6] = { RK_OP_LT, RK_OP_LE, RK_OP_GT, RK_OP_GE, RK_OP_EQ, RK_OP_NE };
    return m[slot];
}

// A cold block, deferred until every hot op exists. Emitting it inline would
// have meant knowing the index of the op that comes AFTER the one bailing out,
// which is not known while that one is being emitted.
struct ColdReq {
    enum Kind { Binop, BinopImm, CmpBranch, CmpBranchImm, Unop, Movebox, Truthy, Defined, NatChk } kind;
    int      hot = -1;            // the op whose guard bails here
    uint64_t a = 0, b = 0, c = 0; // meaning depends on kind
    int      op = 0;              // an RkCnpOp
    int      scratch = 0, tmp = 0;
    bool     negate = false;      // the branch forms: bail when the test is FALSE
};

struct Lower {
    std::string err;
    const std::map<std::string, const Callable*>* inl = nullptr;
    std::vector<KernelCall> calls;
    std::vector<IndexSite>  isites;
    // `@a[$i]` / `@g[$y][$x]`: the root array's register and each level's key,
    // evaluated outermost first, each into a temp of its own
    std::vector<char> chainHash;   // filled by indexChain, beside `keys`
    bool indexChain(Index* ix, int& base, std::vector<int>& keys) {
        if (ix->base->kind == NK::Index) {
            if (!indexChain(static_cast<Index*>(ix->base.get()), base, keys)) return false;
        }
        else { base = expr(ix->base.get()); chainHash.clear(); }
        if (bad()) return false;
        int k = temp();
        exprInto(ix->index.get(), k);
        if (bad()) return false;
        keys.push_back(k);
        chainHash.push_back(ix->isHash ? 1 : 0);
        return true;
    }
    // read the element a chain names, into a fresh register
    int chainGet(int base, const std::vector<int>& keys) {
        int cur = base;
        for (size_t i = 0; i < keys.size(); i++) {
            int d = temp();
            emit(ids().idxget, (uint64_t)cur, (uint64_t)keys[i], (uint64_t)d, (uint64_t)chainHash[i]);
            cur = d;
        }
        return cur;
    }
    void chainSet(int base, const std::vector<int>& keys, int val) {
        isites.push_back(IndexSite{base, keys, chainHash});
        emit(ids().idxset, (uint64_t)(isites.size() - 1), (uint64_t)val);
    }
    std::vector<Op>      ops;
    std::vector<ColdReq> colds;
    std::vector<Value>   consts;
    std::vector<std::map<std::string, int>> scopes;
    int nNamed = 0;          // registers reserved for slots and `my` variables
    int namedTop = 0;
    int tempTop = 0, maxTemp = 0;
    // `last` and `next` sites waiting for their loop to finish being lowered.
    struct LoopCtx { std::vector<int> breaks, continues; };
    std::vector<LoopCtx> loops;

    void fail(const std::string& m) { if (err.empty()) err = m; }
    bool bad() const { return !err.empty(); }

    int emit(int stencil, uint64_t o0 = 0, uint64_t o1 = 0, uint64_t o2 = 0, uint64_t o3 = 0) {
        Op o;
        o.stencil = (uint16_t)stencil;
        o.operand[0] = o0; o.operand[1] = o1; o.operand[2] = o2; o.operand[3] = o3;
        ops.push_back(o);
        return (int)ops.size() - 1;
    }

    // Temps are handed out above the named registers and given back only at
    // statement boundaries, which is the whole of their lifetime: no expression
    // value outlives the statement that produced it.
    int temp() { int r = nNamed + tempTop++; if (tempTop > maxTemp) maxTemp = tempTop; return r; }
    struct TempMark {
        Lower& L; int save;
        explicit TempMark(Lower& l) : L(l), save(l.tempTop) {}
        ~TempMark() { L.tempTop = save; }
    };

    int declare(const std::string& n) {
        int r = namedTop++;
        if (namedTop > nNamed) { fail("more variables than the register pre-count found"); return 0; }
        scopes.back()[n] = r;
        return r;
    }
    int lookup(const std::string& n) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return f->second;
        }
        fail("variable " + n + " has no register");
        return 0;
    }

    int constant(const Value& v) {
        consts.push_back(v);
        return (int)consts.size() - 1;
    }

    // A variable declared native `int` or `num` (the parser resolved it, see
    // VarExpr::nativeIntRead) is a register of that kind: every store to it is
    // checked as the interpreter checks one, and its operations wrap.
    static char natKind(const VarExpr* v) {
        if (v->declare)
            return v->declType == "int" || v->declType == "int64" ? 'i'
                 : v->declType == "num" || v->declType == "num64" ? 'n' : 0;
        return v->nativeIntRead ? 'i' : v->nativeNumRead ? 'n' : 0;
    }
    // Store `src` into the variable `v`'s register `dst`. Into a native the
    // value is checked FIRST, so a refusal leaves the variable holding what it
    // held, as the interpreter's does (a CATCH outside reads it). The cold half
    // converts what the check accepts (a Bool into an int) straight into `dst`
    // and continues past the move.
    // `srcNative`: the value is itself a native (Ast.h nativeNumericStatic),
    // so a native of the other kind CONVERTS into this one, as in Rakudo.
    void storeChecked(const VarExpr* v, int src, int dst, bool srcNative) {
        char k = natKind(v);
        int h = -1;
        if (k) {
            const uint64_t tag = k == 'i' ? RK_T_INT : RK_T_NUM;
            h = emit(ids().natchk, (uint64_t)src, tag);
            // bit 8 of the kind says "from a native"; the hot compare reads the low byte
            ColdReq c{ColdReq::NatChk, h, (uint64_t)src, tag | (srcNative ? 0x100u : 0u),
                      (uint64_t)constant(Value::str(v->name)), 0, 0, 0, false};
            c.scratch = dst;
            colds.push_back(c);
        }
        if (src != dst) {
            int m = emit(ids().move, (uint64_t)dst, (uint64_t)src);
            colds.push_back({ColdReq::Movebox, m, (uint64_t)dst, (uint64_t)src, 0, 0, 0, 0, false});
        }
        if (h >= 0) patch(h, here());   // the cold half resumes after the move
    }

    // ---- expressions -------------------------------------------------------

    // The register holding `e`'s value. May be a variable's own register, so a
    // caller that means to WRITE must copy first.
    int expr(Expr* e);
    // `e` into a specific register.
    void exprInto(Expr* e, int dst);
    // Branch to a site (returned, for patching) when `cond` is true / false.
    int branch(Expr* cond, bool whenTrue);
    void stmt(Stmt* s);
    void block(Block* b, bool ownScope);
    void headerExpr(Expr* e);
    void loopStmt(Stmt* s, bool outermost);

    void patch(int site, int target) { ops[site].target = target; }
    int  here() const { return (int)ops.size(); }

    // Resolve fallthroughs, then build every cold block. Hot ops were all
    // emitted first, so appending the cold ones leaves the vector already
    // partitioned and no renumbering is needed.
    void finish();
};

// Builtin methods with no side effect and no view of the caller: on a value
// that is not an object they go straight to the runtime's method dispatch.
bool isPureMethod(const std::string& m) {
    static const std::set<std::string> k = {
        "elems", "end", "abs", "sqrt", "floor", "ceiling", "round", "truncate", "sign",
        "Int", "Num", "Str", "Bool", "Rat", "Numeric", "chars", "defined", "exp", "log",
        "log10", "log2", "sin", "cos", "tan", "atan", "is-prime", "succ", "pred",
        "uc", "lc", "tc", "fc", "flip", "key", "value", "keys", "values", "min", "max"};
    return k.count(m) > 0;
}

// An integer literal small enough to ride in the instruction stream.
bool intLit(Expr* e, long long& out) {
    if (!e || e->kind != NK::IntLit) return false;
    auto* l = static_cast<IntLit*>(e);
    if (!l->big.empty()) return false;
    out = l->v;
    return true;
}

// The Value a literal node evaluates to. Copied from the interpreter's own arms
// rather than re-derived, because the interesting cases are not obvious: a
// decimal literal like `3.14` is a RAT in Raku and not a Num, `1i` is a Complex,
// and an integer literal too big for an int64 carries its digits as a string.
Value literalValue(Expr* e) {
    switch (e->kind) {
        case NK::IntLit: {
            auto* il = static_cast<IntLit*>(e);
            return il->big.empty() ? Value::integer(il->v) : Value::bigint(BigInt::fromString(il->big));
        }
        case NK::NumLit: {
            auto* nl = static_cast<NumLit*>(e);
            if (nl->imaginary) return Value::complex(0, nl->v);
            if (nl->isRat)
                return nl->bigNum.empty()
                    ? Value::ratZ(BigInt(nl->ratNum), BigInt(nl->ratDen))
                    : Value::ratZ(BigInt::fromString(nl->bigNum), BigInt::fromString(nl->bigDen));
            return Value::number(nl->v);
        }
        case NK::StrLit:  return Value::str(static_cast<StrLit*>(e)->v);
        case NK::BoolLit: return Value::boolean(static_cast<BoolLit*>(e)->v);
        default:          return Value();
    }
}

void Lower::exprInto(Expr* e, int dst) {
    int r = expr(e);
    if (bad() || r == dst) return;
    int m = emit(ids().move, (uint64_t)dst, (uint64_t)r);
    colds.push_back({ColdReq::Movebox, m, (uint64_t)dst, (uint64_t)r, 0, 0, 0, 0, false});
}

int Lower::expr(Expr* e) {
    if (bad()) return 0;
    if (!e) { fail("an empty expression"); return 0; }
    switch (e->kind) {
        case NK::IntLit: {
            auto* l = static_cast<IntLit*>(e);
            int d = temp();
            if (l->big.empty()) emit(ids().loadi, (uint64_t)d, (uint64_t)l->v);
            else                emit(ids().loadk, (uint64_t)d, (uint64_t)constant(literalValue(e)));
            return d;
        }
        case NK::NumLit: {
            auto* nl = static_cast<NumLit*>(e);
            int d = temp();
            // Only a plain Num rides unboxed; a Rat or a Complex is a constant.
            if (!nl->isRat && !nl->imaginary) emit(ids().loadn, (uint64_t)d, (uint64_t)d2bits(nl->v));
            else emit(ids().loadk, (uint64_t)d, (uint64_t)constant(literalValue(e)));
            return d;
        }
        case NK::BoolLit: {
            int d = temp();
            emit(ids().loadb, (uint64_t)d, static_cast<BoolLit*>(e)->v ? 1u : 0u);
            return d;
        }
        case NK::StrLit: {
            int d = temp();
            emit(ids().loadk, (uint64_t)d, (uint64_t)constant(Value::str(static_cast<StrLit*>(e)->v)));
            return d;
        }
        case NK::InterpStr: {
            auto* is = static_cast<InterpStr*>(e);
            bool allLit = true;
            for (auto& p : is->parts) if (!p || p->kind != NK::StrLit) { allLit = false; break; }
            if (allLit) {
                std::string s;
                for (auto& p : is->parts) s += static_cast<StrLit*>(p.get())->v;
                int d = temp();
                emit(ids().loadk, (uint64_t)d, (uint64_t)constant(Value::str(s)));
                return d;
            }
            // Each part into a register, then the interpreter's interpolate()
            // over them, as a call (rk_cnp_call): a part that is an object may
            // run a user .Str, which may see the loop's variables.
            KernelCall cs;
            cs.isynth = std::make_unique<InterpStr>();
            cs.isynth->line = is->line;
            for (size_t k = 0; k < is->parts.size(); k++) {
                int r = temp();
                exprInto(is->parts[k].get(), r);
                if (bad()) return 0;
                const std::string nm = "$__cnp_arg" + std::to_string(k);
                cs.argRegs.push_back(r);
                cs.argNames.push_back(nm);
                auto ve = std::make_unique<VarExpr>(nm);
                ve->line = is->line;
                cs.isynth->parts.push_back(std::move(ve));
            }
            int d = temp();
            calls.push_back(std::move(cs));
            emit(ids().call, (uint64_t)(calls.size() - 1), (uint64_t)d);
            return d;
        }
        case NK::VarExpr: {
            auto* v = static_cast<VarExpr*>(e);
            if (v->declare) {
                int r = declare(v->name);
                // `my $x;` with no initialiser is an undefined Any, and it is
                // reset on every pass through the declaration. A native starts
                // at its zero instead: `my int $k;` is 0, `my num $t;` 0e0.
                char nk = natKind(v);
                if (v->name[0] == '@') emit(ids().newarr, (uint64_t)r);
                else if (nk == 'i') emit(ids().loadi, (uint64_t)r, 0);
                else if (nk == 'n') emit(ids().loadn, (uint64_t)r, (uint64_t)d2bits(0.0));
                else emit(ids().loadk, (uint64_t)r, (uint64_t)constant(Value()));
                return r;
            }
            return lookup(v->name);
        }
        case NK::Assign: {
            auto* a = static_cast<Assign*>(e);
            if (a->target->kind == NK::Index) {
                // an element store: the keys first (Raku evaluates the target's
                // subscripts before the value), then the value, then the store
                int base; std::vector<int> keys;
                if (!indexChain(static_cast<Index*>(a->target.get()), base, keys)) return 0;
                if (a->op == "=") {
                    int v = temp();
                    exprInto(a->value.get(), v);
                    if (bad()) return 0;
                    chainSet(base, keys, v);
                    return v;
                }
                std::string op = a->op.substr(0, a->op.size() - 1);
                int oi = opIndex(op);
                if (oi < 0) { fail("compound operator '" + a->op + "'"); return 0; }
                int cur = chainGet(base, keys);
                int src = expr(a->value.get());
                if (bad()) return 0;
                int d = temp();
                emit(ids().binop, (uint64_t)d, (uint64_t)cur, (uint64_t)src, (uint64_t)oi);
                chainSet(base, keys, d);
                return d;
            }
            if (a->target->kind != NK::VarExpr) { fail("assignment to a non-variable"); return 0; }
            auto* tv = static_cast<VarExpr*>(a->target.get());
            // `=`, and the `-> @row` bind a `for @a` kernel makes (Jit.cpp
            // kBindOp): a register move shares the element's storage, which
            // is what a bind is
            // `$s = $s ~ X` is `$s ~= X` when $s holds a Str: appended into
            // its box (rk_cnp_binop) instead of a whole new string each time
            // (issue #130); `$s = X ~ $s` is prepended into the free space in
            // front of a shared buffer (APPEND-PLAN.md) — the destination is
            // then the RIGHT operand. X is a literal or a variable, so reading
            // it after the append starts cannot see a different $s.
            if (a->op == "=" && !tv->declare && natKind(tv) == 0 && a->value->kind == NK::Binary) {
                auto* b = static_cast<Binary*>(a->value.get());
                auto self = [&](const ExprPtr& e) {
                    return e && e->kind == NK::VarExpr && !static_cast<VarExpr*>(e.get())->declare &&
                           static_cast<VarExpr*>(e.get())->name == tv->name;
                };
                auto plainX = [](const ExprPtr& e) {
                    if (!e) return false;
                    if (e->kind == NK::InterpStr) {   // "abcde": a constant when nothing in it interpolates
                        for (auto& part : static_cast<InterpStr*>(e.get())->parts)
                            if (!part || part->kind != NK::StrLit) return false;
                        return true;
                    }
                    return e->kind == NK::StrLit || e->kind == NK::IntLit || e->kind == NK::NumLit ||
                           e->kind == NK::VarExpr;
                };
                const bool back = self(b->lhs) && plainX(b->rhs);
                const bool front = !back && self(b->rhs) && plainX(b->lhs);
                if (b->op == "~" && (back || front)) {
                    int dst = lookup(tv->name);
                    if (bad()) return 0;
                    int src = expr((back ? b->rhs : b->lhs).get());
                    if (bad()) return 0;
                    emit(ids().binop, (uint64_t)dst, (uint64_t)(back ? dst : src), (uint64_t)(back ? src : dst),
                         (uint64_t)(RK_OP_CONCAT | RK_OP_SELFCAT));
                    return dst;
                }
            }
            if (a->op == "=" || a->op == "\x01bind") {
                // The VALUE first, so that `my $x = $x` reads the OUTER `$x`
                // before the declaration shadows it — Raku's own order, and the
                // one the eligibility walk assumed.
                int src = expr(a->value.get());
                if (bad()) return 0;
                int dst = tv->declare ? declare(tv->name) : lookup(tv->name);
                if (bad()) return 0;
                storeChecked(tv, src, dst, nativeNumericStatic(a->value.get()));
                return dst;
            }
            // Compound assignment. `$x op= V` is `$x = $x op V`, and the
            // destination is the variable's own register.
            if (tv->declare) { fail("a compound assignment to a declaration"); return 0; }
            int dst = lookup(tv->name);
            if (bad()) return 0;
            std::string op = a->op.substr(0, a->op.size() - 1);
            long long k = 0;
            // `$n op= V` on a native int is the native operation when V is a
            // native int or an integer literal, as `$n = $n op V` would be
            const bool litV = intLit(a->value.get(), k);
            const int nat = tv->nativeIntRead && (litV || nativeIntStatic(a->value.get())) ? RK_OP_NATIVE : 0;
            // Into a native the result goes to a temp first, so that a refused
            // one (`$i /= 2` is a Rat) leaves the variable as it was.
            const bool checked = natKind(tv) != 0;
            const int res = checked ? temp() : dst;
            if ((op == "+" || op == "-") && litV) {
                long long delta = op == "+" ? k : -k;
                int scratch = temp();
                int h = checked ? emit(ids().addi, (uint64_t)res, (uint64_t)dst, (uint64_t)delta, (uint64_t)scratch)
                                : emit(ids().incr, (uint64_t)dst, (uint64_t)delta, (uint64_t)scratch);
                colds.push_back({ColdReq::BinopImm, h, (uint64_t)res, (uint64_t)dst, (uint64_t)delta,
                                 RK_OP_ADD | nat, scratch, 0, false});
                if (checked) storeChecked(tv, res, dst, true);   // native op= integer literal
                return dst;
            }
            int src = expr(a->value.get());
            if (bad()) return 0;
            int oi = opIndex(op);
            if (oi < 0) { fail("compound operator '" + a->op + "'"); return 0; }
            int fast = op == "+" ? ids().add : op == "-" ? ids().sub : op == "*" ? ids().mul
                     : op == "/" ? ids().div : -1;
            if (fast >= 0) {
                int h = emit(fast, (uint64_t)res, (uint64_t)dst, (uint64_t)src);
                colds.push_back({ColdReq::Binop, h, (uint64_t)res, (uint64_t)dst, (uint64_t)src, oi | nat, 0, 0, false});
            } else {
                emit(ids().binop, (uint64_t)res, (uint64_t)dst, (uint64_t)src, (uint64_t)(oi | nat));
            }
            // `$n op= V` is native when V is (the target itself is)
            if (checked) {
                // native when V is a native, or a literal of the target's kind
                const bool vNum = a->value->kind == NK::NumLit &&
                                  !static_cast<NumLit*>(a->value.get())->isRat &&
                                  !static_cast<NumLit*>(a->value.get())->imaginary;
                const bool srcNat = (op == "+" || op == "-" || op == "*" || op == "**") &&
                    (nativeNumericStatic(a->value.get()) || (natKind(tv) == 'i' ? litV : vNum));
                storeChecked(tv, res, dst, srcNat);
            }
            return dst;
        }
        case NK::ChainExpr: {
            // `a < b < c`: each operand once, left to right, each link the
            // chain's own rule (rk_cnp_chain), False at the first that fails
            // without evaluating what follows it
            auto* ch = static_cast<ChainExpr*>(e);
            int d = temp();
            int prev = temp();
            exprInto(ch->operands[0].get(), prev);
            if (bad()) return 0;
            std::vector<int> outs;
            for (size_t k = 0; k < ch->ops.size(); k++) {
                int oi = opIndex(ch->ops[k]);
                if (oi < 0) { fail("operator '" + ch->ops[k] + "'"); return 0; }
                int next = temp();
                exprInto(ch->operands[k + 1].get(), next);
                if (bad()) return 0;
                emit(ids().chain, (uint64_t)oi, (uint64_t)d, (uint64_t)prev, (uint64_t)next);
                if (k + 1 < ch->ops.size()) {
                    int skip = emit(ids().jf, (uint64_t)d);
                    colds.push_back({ColdReq::Truthy, skip, (uint64_t)d, 0, 0, 0, 0, 0, false});
                    outs.push_back(skip);
                }
                prev = next;
            }
            for (int s : outs) patch(s, here());
            return d;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            const std::string& op = b->op;
            // The three short-circuit operators are control flow, not calls
            // into the operator table — `$a && $b` never evaluates `$b` when
            // `$a` is false, and it yields an OPERAND, not a Bool.
            if (op == "&&" || op == "and" || op == "||" || op == "or" || op == "//") {
                int d = temp();
                exprInto(b->lhs.get(), d);
                if (bad()) return 0;
                int skip;
                if (op == "//") skip = emit(ids().jdef, (uint64_t)d);
                else if (op == "&&" || op == "and") skip = emit(ids().jf, (uint64_t)d);
                else skip = emit(ids().jt, (uint64_t)d);
                colds.push_back({op == "//" ? ColdReq::Defined : ColdReq::Truthy,
                                 skip, (uint64_t)d, 0, 0, 0, 0, 0, false});
                exprInto(b->rhs.get(), d);
                patch(skip, here());
                return d;
            }
            int oi = opIndex(op);
            if (oi < 0) { fail("operator '" + op + "'"); return 0; }
            const int nat = nativeIntStatic(b) ? RK_OP_NATIVE : 0;
            int cs = cmpSlot(op);
            long long k = 0;
            if ((op == "+" || op == "-") && intLit(b->rhs.get(), k)) {
                int a = expr(b->lhs.get());
                if (bad()) return 0;
                int d = temp(), scratch = temp();
                long long delta = op == "+" ? k : -k;
                int h = emit(ids().addi, (uint64_t)d, (uint64_t)a, (uint64_t)delta, (uint64_t)scratch);
                colds.push_back({ColdReq::BinopImm, h, (uint64_t)d, (uint64_t)a, (uint64_t)delta,
                                 RK_OP_ADD | nat, scratch, 0, false});
                return d;
            }
            int a = expr(b->lhs.get());
            if (bad()) return 0;
            int r = expr(b->rhs.get());
            if (bad()) return 0;
            int d = temp();
            int fast = op == "+" ? ids().add : op == "-" ? ids().sub : op == "*" ? ids().mul
                     : op == "/" ? ids().div : cs >= 0 ? ids().cmp[cs] : -1;
            if (fast >= 0) {
                int h = emit(fast, (uint64_t)d, (uint64_t)a, (uint64_t)r);
                colds.push_back({ColdReq::Binop, h, (uint64_t)d, (uint64_t)a, (uint64_t)r, oi | nat, 0, 0, false});
            } else {
                emit(ids().binop, (uint64_t)d, (uint64_t)a, (uint64_t)r, (uint64_t)(oi | nat));
            }
            return d;
        }
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            const std::string& op = u->op;
            if ((op == "++" || op == "--") && u->operand->kind == NK::Index) {
                int base; std::vector<int> keys;
                if (!indexChain(static_cast<Index*>(u->operand.get()), base, keys)) return 0;
                int old = chainGet(base, keys);
                int one = temp();
                emit(ids().loadi, (uint64_t)one, (uint64_t)(long long)1);
                int nv = temp();
                emit(ids().binop, (uint64_t)nv, (uint64_t)old, (uint64_t)one,
                     (uint64_t)(op == "++" ? RK_OP_ADD : RK_OP_SUB));
                chainSet(base, keys, nv);
                return u->postfix ? old : nv;
            }
            if (op == "++" || op == "--") {
                if (u->operand->kind != NK::VarExpr) { fail("++/-- on a non-variable"); return 0; }
                int r = lookup(static_cast<VarExpr*>(u->operand.get())->name);
                if (bad()) return 0;
                const int nat = static_cast<VarExpr*>(u->operand.get())->nativeIntRead ? RK_OP_NATIVE : 0;
                long long delta = op == "++" ? 1 : -1;
                int old = -1;
                if (u->postfix) {            // the value is what it held BEFORE
                    old = temp();
                    int m = emit(ids().move, (uint64_t)old, (uint64_t)r);
                    colds.push_back({ColdReq::Movebox, m, (uint64_t)old, (uint64_t)r, 0, 0, 0, 0, false});
                }
                int scratch = temp();
                int h = emit(ids().incr, (uint64_t)r, (uint64_t)delta, (uint64_t)scratch);
                colds.push_back({ColdReq::BinopImm, h, (uint64_t)r, (uint64_t)r, (uint64_t)delta,
                                 RK_OP_ADD | nat, scratch, 0, false});
                return u->postfix ? old : r;
            }
            int a = expr(u->operand.get());
            if (bad()) return 0;
            int d = temp();
            if (op == "-") {
                int h = emit(ids().neg, (uint64_t)d, (uint64_t)a);
                colds.push_back({ColdReq::Unop, h, (uint64_t)d, (uint64_t)a, 0,
                                 RK_OP_NEG | (nativeIntStatic(u) ? RK_OP_NATIVE : 0), 0, 0, false});
                return d;
            }
            if (op == "!" || op == "not") {
                int h = emit(ids().notOp, (uint64_t)d, (uint64_t)a);
                colds.push_back({ColdReq::Unop, h, (uint64_t)d, (uint64_t)a, 0, RK_OP_NOT, 0, 0, false});
                return d;
            }
            int oi = op == "+" ? RK_OP_PLUS : op == "?" ? RK_OP_SO
                   : op == "~" ? RK_OP_STR  : op == "+^" ? RK_OP_BNOT : -1;
            if (oi < 0) { fail("prefix operator '" + op + "'"); return 0; }
            emit(ids().unop, (uint64_t)d, (uint64_t)a, (uint64_t)oi);
            return d;
        }
        case NK::Index: {
            int base; std::vector<int> keys;
            if (!indexChain(static_cast<Index*>(e), base, keys)) return 0;
            return chainGet(base, keys);
        }
        case NK::MethodCall: {
            auto* m = static_cast<MethodCall*>(e);
            // `@a.elems`: a length, not a call — no write-back of every slot
            // around it (rk_cnp_alen; a `for @a` kernel asks it per iteration)
            if (m->method == "elems" && m->args.empty() && m->methodQual.empty() && !m->methodExpr &&
                !m->meta && !m->hyper && !m->bang && !m->maybe && !m->allMode && !m->mutate &&
                m->inv && m->inv->kind == NK::VarExpr &&
                static_cast<VarExpr*>(m->inv.get())->name.size() > 1 &&
                static_cast<VarExpr*>(m->inv.get())->name[0] == '@') {
                int base = expr(m->inv.get());
                if (bad()) return 0;
                int d = temp();
                emit(ids().alen, (uint64_t)base, (uint64_t)d);
                return d;
            }
            // As a sub call (see NK::Call): the invocant and the arguments the
            // kernel evaluated are bound in a scratch scope, the invocant under
            // its own sigil so that `@a.elems` is still an array's method
            KernelCall cs;
            cs.msynth = std::make_unique<MethodCall>();
            cs.msynth->method = m->method;
            cs.msynth->methodQual = m->methodQual;
            cs.msynth->line = m->line;
            cs.method = m->method;
            cs.pureMethod = isPureMethod(m->method) && m->methodQual.empty();
            char sig = '$';
            if (m->inv->kind == NK::VarExpr) {
                const std::string& in = static_cast<VarExpr*>(m->inv.get())->name;
                if (!in.empty() && (in[0] == '@' || in[0] == '%')) sig = in[0];
            }
            auto bind = [&](Expr* x, const std::string& nm) -> std::unique_ptr<VarExpr> {
                int r = temp();
                exprInto(x, r);
                cs.argRegs.push_back(r);
                cs.argNames.push_back(nm);
                auto ve = std::make_unique<VarExpr>(nm);
                ve->line = m->line;
                return ve;
            };
            cs.msynth->inv = bind(m->inv.get(), std::string(1, sig) + "__cnp_inv");
            if (bad()) return 0;
            for (size_t k = 0; k < m->args.size(); k++) {
                cs.msynth->args.push_back(bind(m->args[k].get(), "$__cnp_arg" + std::to_string(k)));
                if (bad()) return 0;
            }
            int d = temp();
            calls.push_back(std::move(cs));
            emit(ids().call, (uint64_t)(calls.size() - 1), (uint64_t)d);
            if (!loops.empty()) {
                int hl = emit(ids().jctl, 1);
                loops.back().breaks.push_back(hl);
                int hn = emit(ids().jctl, 2);
                loops.back().continues.push_back(hn);
            }
            return d;
        }
        case NK::Call: {
            auto* c = static_cast<Call*>(e);
            // A sub compiled in place (see inlineBody): each argument into a
            // register of its own, the parameters bound to those, the body
            // lowered as an expression. No call happens at all.
            if (inl) {
                auto it = inl->find(c->name);
                if (it != inl->end() && it->second->params && it->second->params->size() == c->args.size()) {
                    const Callable& cb = *it->second;
                    std::map<std::string, int> ps;
                    for (size_t k = 0; k < c->args.size(); k++) {
                        int r = temp();
                        exprInto(c->args[k].get(), r);
                        if (bad()) return 0;
                        ps[(*cb.params)[k].name] = r;
                    }
                    scopes.push_back(std::move(ps));
                    auto* body = static_cast<ExprStmt*>((*cb.body)[0].get())->e.get();
                    int r = expr(body);
                    scopes.pop_back();
                    return r;
                }
            }
            KernelCall cs;
            cs.synth = std::make_unique<Call>();
            cs.synth->name = c->name;
            cs.synth->parenned = c->parenned;
            cs.synth->line = c->line;
            for (size_t k = 0; k < c->args.size(); k++) {
                // each argument into a temp of its own: a later argument that
                // writes a variable (`f($i, $i++)`) must not change an earlier one
                int r = temp();
                exprInto(c->args[k].get(), r);
                if (bad()) return 0;
                std::string nm = "$__cnp_arg" + std::to_string(k);
                cs.argRegs.push_back(r);
                cs.argNames.push_back(nm);
                auto ve = std::make_unique<VarExpr>(nm);
                ve->line = c->line;
                cs.synth->args.push_back(std::move(ve));
            }
            int d = temp();
            calls.push_back(std::move(cs));
            emit(ids().call, (uint64_t)(calls.size() - 1), (uint64_t)d);
            // a `last` / `next` the callee raised leaves / continues the
            // innermost loop, as it would the interpreter's
            if (!loops.empty()) {
                int hl = emit(ids().jctl, 1);
                loops.back().breaks.push_back(hl);
                int hn = emit(ids().jctl, 2);
                loops.back().continues.push_back(hn);
            }
            return d;
        }
        case NK::Ternary: {
            auto* t = static_cast<Ternary*>(e);
            int d = temp();
            int toElse = branch(t->cond.get(), false);
            if (bad()) return 0;
            exprInto(t->then.get(), d);
            int done = emit(ids().jmp);
            patch(toElse, here());
            exprInto(t->els.get(), d);
            patch(done, here());
            return d;
        }
        case NK::ListExpr:
            fail("a list in value position");
            return 0;
        default:
            fail("an expression the lowering has no stencil for");
            return 0;
    }
}

// A conditional branch. Returns the op whose target still has to be patched.
//
// A comparison is FUSED into the branch — `while $i < $n` is one stencil with
// the test and the back edge in it, and no Bool is ever built. The negated
// stencils exist so that "jump when the condition is false" needs no operator
// inversion: `!(a < b)` and `a >= b` are different questions once an operand is
// NaN, and getting that wrong would have been invisible until it mattered.
int Lower::branch(Expr* cond, bool whenTrue) {
    if (bad()) return 0;
    if (cond && cond->kind == NK::Binary) {
        auto* b = static_cast<Binary*>(cond);
        int cs = cmpSlot(b->op);
        if (cs >= 0) {
            long long k = 0;
            if (intLit(b->rhs.get(), k)) {
                int a = expr(b->lhs.get());
                if (bad()) return 0;
                int scratch = temp(), tmp = temp();
                int st = whenTrue ? ids().jcmpi[cs] : ids().jncmpi[cs];
                int h = emit(st, (uint64_t)a, (uint64_t)k, (uint64_t)scratch);
                colds.push_back({ColdReq::CmpBranchImm, h, (uint64_t)a, (uint64_t)k, 0,
                                 cmpOpIndex(cs), scratch, tmp, !whenTrue});
                return h;
            }
            int a = expr(b->lhs.get());
            if (bad()) return 0;
            int r = expr(b->rhs.get());
            if (bad()) return 0;
            int tmp = temp();
            int st = whenTrue ? ids().jcmp[cs] : ids().jncmp[cs];
            int h = emit(st, (uint64_t)a, (uint64_t)r);
            colds.push_back({ColdReq::CmpBranch, h, (uint64_t)a, (uint64_t)r, 0,
                             cmpOpIndex(cs), 0, tmp, !whenTrue});
            return h;
        }
    }
    int c = expr(cond);
    if (bad()) return 0;
    int h = emit(whenTrue ? ids().jt : ids().jf, (uint64_t)c);
    colds.push_back({ColdReq::Truthy, h, (uint64_t)c, 0, 0, 0, 0, 0, !whenTrue});
    return h;
}

void Lower::headerExpr(Expr* e) {
    if (bad() || !e) return;
    if (e->kind == NK::ListExpr) {
        for (auto& it : static_cast<ListExpr*>(e)->items) { TempMark m(*this); expr(it.get()); if (bad()) return; }
        return;
    }
    TempMark m(*this);
    expr(e);
}

void Lower::block(Block* b, bool ownScope) {
    if (bad() || !b) return;
    if (ownScope) scopes.push_back({});
    for (auto& s : b->stmts) { stmt(s.get()); if (bad()) break; }
    if (ownScope) scopes.pop_back();
}

void Lower::loopStmt(Stmt* s, bool outermost) {
    loops.push_back({});
    if (s->kind == NK::WhileStmt) {
        auto* w = static_cast<WhileStmt*>(s);
        int top = here();
        // `until` is `while not`: the same test, taken the other way.
        int out = branch(w->cond.get(), w->isUntil);
        if (bad()) { loops.pop_back(); return; }
        block(w->body.get(), true);
        int back = emit(ids().jmp);
        patch(back, top);
        patch(out, here());
        for (int c : loops.back().continues) patch(c, top);
        for (int c : loops.back().breaks)    patch(c, here());
    } else {
        auto* l = static_cast<LoopStmt*>(s);
        scopes.push_back({});
        // The OUTERMOST loop's init is NOT emitted at all. The interpreter ran
        // it before the kernel was entered, and every name it declared is
        // already a slot the kernel binds — so there is nothing left to walk,
        // and walking it anyway re-ran `my $i = 0` on entry and restarted the
        // loop from the top. The gate caught that as one extra iteration, which
        // is what re-running a zeroed counter looks like from the outside.
        if (!outermost) headerExpr(l->init.get());
        int top = here();
        int out = -1;
        if (l->cond) { out = branch(l->cond.get(), false); if (bad()) { scopes.pop_back(); loops.pop_back(); return; } }
        block(l->body.get(), true);
        int step = here();
        headerExpr(l->incr.get());
        int back = emit(ids().jmp);
        patch(back, top);
        if (out >= 0) patch(out, here());
        // `next` in a three-clause loop has to reach the STEP, which is what a
        // C++ `continue` in a `for` does and what rewriting it as a `while`
        // would silently break.
        for (int c : loops.back().continues) patch(c, step);
        for (int c : loops.back().breaks)    patch(c, here());
        scopes.pop_back();
    }
    loops.pop_back();
}

void Lower::stmt(Stmt* s) {
    if (bad() || !s) return;
    switch (s->kind) {
        case NK::EmptyStmt: return;
        case NK::ExprStmt: { TempMark m(*this); expr(static_cast<ExprStmt*>(s)->e.get()); return; }
        case NK::LastStmt:
            if (loops.empty()) { fail("a `last` outside the kernel's loops"); return; }
            loops.back().breaks.push_back(emit(ids().jmp));
            return;
        case NK::NextStmt:
            if (loops.empty()) { fail("a `next` outside the kernel's loops"); return; }
            loops.back().continues.push_back(emit(ids().jmp));
            return;
        case NK::IfStmt: {
            auto* f = static_cast<IfStmt*>(s);
            std::vector<int> ends;
            for (size_t i = 0; i < f->branches.size(); i++) {
                int nextArm;
                // `unless COND` runs its block when COND is FALSE, so it skips
                // the block when COND is true (the flag was ignored, and every
                // `unless` compiled as an `if`)
                { TempMark m(*this); nextArm = branch(f->branches[i].first.get(), f->isUnless); }
                if (bad()) return;
                // A postfix `STMT if COND` has no block of its own, so a `my` in
                // it declares in the ENCLOSING scope.
                block(f->branches[i].second.get(), !f->modifier);
                if (bad()) return;
                bool more = (i + 1 < f->branches.size()) || f->elseBlock;
                if (more) ends.push_back(emit(ids().jmp));
                patch(nextArm, here());
            }
            if (f->elseBlock) block(f->elseBlock.get(), !f->modifier);
            for (int e : ends) patch(e, here());
            return;
        }
        case NK::Block:     block(static_cast<Block*>(s), true); return;
        case NK::WhileStmt:
        case NK::LoopStmt:  loopStmt(s, false); return;
        case NK::ForStmt: {   // a nested `for` over a numeric range (jit::nestedForLoop)
            if (LoopStmt* lp = jit::nestedForLoop(static_cast<ForStmt*>(s))) { loopStmt(lp, false); return; }
            fail("a nested `for` of this shape");
            return;
        }
        default: fail("a statement the lowering has no stencil for"); return;
    }
}

void Lower::finish() {
    if (bad()) return;
    // Fallthroughs first, so a cold block can read where its hot op was going.
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].cont < 0) ops[i].cont = (int)i + 1;

    for (const ColdReq& c : colds) {
        int cont = ops[c.hot].cont, target = ops[c.hot].target;
        int start = here();
        switch (c.kind) {
            case ColdReq::Binop:
                emit(ids().binop, c.a, c.b, c.c, (uint64_t)c.op);
                ops.back().cont = cont;
                break;
            case ColdReq::BinopImm:
                emit(ids().loadi, (uint64_t)c.scratch, c.c);
                ops.back().cont = (int)ops.size();
                emit(ids().binop, c.a, c.b, (uint64_t)c.scratch, (uint64_t)c.op);
                ops.back().cont = cont;
                break;
            case ColdReq::Unop:
                emit(ids().unop, c.a, c.b, (uint64_t)c.op);
                ops.back().cont = cont;
                break;
            case ColdReq::NatChk:
                emit(ids().natchkslow, c.a, c.b, c.c, (uint64_t)c.scratch);
                ops.back().cont = target;   // past the move the hot path would have made
                break;
            case ColdReq::Movebox:
                emit(ids().movebox, c.a, c.b);
                ops.back().cont = cont;
                break;
            case ColdReq::Truthy: {
                int h = emit(c.negate ? ids().jfslow : ids().jtslow, c.a);
                ops[h].cont = cont; ops[h].target = target;
                break;
            }
            case ColdReq::Defined: {
                int h = emit(ids().jdefslow, c.a);
                ops[h].cont = cont; ops[h].target = target;
                break;
            }
            case ColdReq::CmpBranch:
            case ColdReq::CmpBranchImm: {
                uint64_t rhs = c.b;
                if (c.kind == ColdReq::CmpBranchImm) {
                    emit(ids().loadi, (uint64_t)c.scratch, c.b);
                    ops.back().cont = (int)ops.size();
                    rhs = (uint64_t)c.scratch;
                }
                emit(ids().binop, (uint64_t)c.tmp, c.a, rhs, (uint64_t)c.op);
                ops.back().cont = (int)ops.size();
                // The comparison's Bool decides the same way the fast lane did.
                int h = emit(c.negate ? ids().jf : ids().jt, (uint64_t)c.tmp);
                ops[h].cont = cont; ops[h].target = target;
                int sl = emit(c.negate ? ids().jfslow : ids().jtslow, (uint64_t)c.tmp);
                ops[sl].cont = cont; ops[sl].target = target;
                ops[h].slow = sl;
                break;
            }
        }
        ops[c.hot].slow = start;
    }
    // The cold ops just appended default to falling through to whatever follows
    // them, which is another cold block. Every one of them was given an explicit
    // cont above, except the jt/jf pairs, which have both.
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].cont < 0) ops[i].cont = (int)i + 1;
}

// How many registers the named things need. A pre-pass rather than an
// allocation as we go, because a `my` inside an expression would otherwise be
// handed a register a live temporary was already using.
int countDecls(Expr* e);
int countDecls(Stmt* s);

int countDecls(Expr* e) {
    if (!e) return 0;
    switch (e->kind) {
        case NK::VarExpr: return static_cast<VarExpr*>(e)->declare ? 1 : 0;
        case NK::Assign: { auto* a = static_cast<Assign*>(e); return countDecls(a->target.get()) + countDecls(a->value.get()); }
        case NK::Binary: { auto* b = static_cast<Binary*>(e); return countDecls(b->lhs.get()) + countDecls(b->rhs.get()); }
        case NK::Unary:  return countDecls(static_cast<Unary*>(e)->operand.get());
        case NK::Ternary: { auto* t = static_cast<Ternary*>(e);
                            return countDecls(t->cond.get()) + countDecls(t->then.get()) + countDecls(t->els.get()); }
        case NK::ListExpr: { int n = 0; for (auto& i : static_cast<ListExpr*>(e)->items) n += countDecls(i.get()); return n; }
        default: return 0;
    }
}

int countDecls(Stmt* s) {
    if (!s) return 0;
    switch (s->kind) {
        case NK::ExprStmt: return countDecls(static_cast<ExprStmt*>(s)->e.get());
        case NK::Block: { int n = 0; for (auto& x : static_cast<Block*>(s)->stmts) n += countDecls(x.get()); return n; }
        case NK::IfStmt: {
            auto* f = static_cast<IfStmt*>(s);
            int n = 0;
            for (auto& br : f->branches) { n += countDecls(br.first.get()); n += countDecls(br.second.get()); }
            if (f->elseBlock) n += countDecls(f->elseBlock.get());
            return n;
        }
        case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s);
                              return countDecls(w->cond.get()) + countDecls(w->body.get()); }
        case NK::LoopStmt: { auto* l = static_cast<LoopStmt*>(s);
                             return countDecls(l->init.get()) + countDecls(l->cond.get()) +
                                    countDecls(l->incr.get()) + countDecls(l->body.get()); }
        case NK::ForStmt: {   // as the loop it lowers as (jit::nestedForLoop)
            LoopStmt* l = jit::nestedForLoop(static_cast<ForStmt*>(s));
            return l ? countDecls(l) : 0;
        }
        default: return 0;
    }
}

// A kernel bigger than this is not what `--cnp` is for: the copy is linear in
// the op count, and a loop with thousands of nodes is not where the time goes.
constexpr size_t kMaxOps = 4096;
constexpr size_t kMaxRegs = 1024;

}  // namespace

bool available() { return stencilsAvailable() && ids().ok; }

const char* unavailableReason() {
    if (!ids().ok && stencilsAvailable())
        return "the stencil table does not match this build's lowering";
    return stencilsUnavailableReason();
}

namespace {
bool inlineExpr(const Expr* e, const std::set<std::string>& params, Env* env) {
    if (!e) return false;
    auto overloaded = [&](const std::string& key) { return env && env->find(key); };
    switch (e->kind) {
        case NK::IntLit: case NK::BoolLit: return true;
        case NK::NumLit: return !static_cast<const NumLit*>(e)->imaginary;
        case NK::VarExpr: {
            auto* v = static_cast<const VarExpr*>(e);
            return !v->declare && params.count(v->name);
        }
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            static const std::set<std::string> ok = {
                "+", "-", "*", "/", "%", "div", "mod", "**", "<", "<=", ">", ">=", "==", "!=", "&&", "||"};
            if (!ok.count(b->op) || overloaded("&infix:<" + b->op + ">")) return false;
            return inlineExpr(b->lhs.get(), params, env) && inlineExpr(b->rhs.get(), params, env);
        }
        case NK::Unary: {
            auto* u = static_cast<const Unary*>(e);
            if (u->postfix || !(u->op == "-" || u->op == "+" || u->op == "!")) return false;
            if (overloaded("&prefix:<" + u->op + ">")) return false;
            return inlineExpr(u->operand.get(), params, env);
        }
        case NK::Ternary: {
            auto* t = static_cast<const Ternary*>(e);
            return inlineExpr(t->cond.get(), params, env) && inlineExpr(t->then.get(), params, env) &&
                   inlineExpr(t->els.get(), params, env);
        }
        default: return false;
    }
}
}  // namespace

const Expr* inlineBody(const Callable& c, Env* env) {
    if (c.isMultiDispatcher || c.isMultiCandidate || c.isProto || c.isMethod || c.isBlock ||
        c.isWhateverCode || c.isRegexRoutine || c.isNative || c.deprecated || !c.retType.empty() ||
        c.retRw || !c.wrappers.empty() || !c.placeholders.empty() || !c.params || !c.body ||
        c.body->size() != 1)
        return nullptr;
    std::set<std::string> params;
    for (const Param& p : *c.params) {
        if (p.name.size() < 2 || p.sigil != '$' || !(std::isalpha((unsigned char)p.name[1]) || p.name[1] == '_'))
            return nullptr;
        if (!p.type.empty() || p.typeCapture || p.whereExpr || p.litVal || p.defaultVal || p.subSig ||
            p.codeSig || p.named || p.slurpy || p.optional || p.isRw || p.isRaw || p.isCopy || p.coerce ||
            p.defConstraint || !p.userTraits.empty() || !p.shapeDims.empty() || p.invocant)
            return nullptr;
        params.insert(p.name);
    }
    const Stmt* st = (*c.body)[0].get();
    if (!st || st->kind != NK::ExprStmt) return nullptr;
    const Expr* e = static_cast<const ExprStmt*>(st)->e.get();
    return inlineExpr(e, params, env) ? e : nullptr;
}

Kernel* compile(Stmt* loop, const std::vector<std::string>& slots, std::string& why,
                const std::map<std::string, const Callable*>* inl) {
    if (!available()) { why = unavailableReason(); return nullptr; }
    Lower L;
    L.inl = inl;
    L.nNamed = (int)slots.size() + countDecls(loop) + 4;
    if (L.nNamed > (int)kMaxRegs) { why = "too many variables"; return nullptr; }
    L.scopes.push_back({});
    for (size_t i = 0; i < slots.size(); i++) L.scopes.back()[slots[i]] = (int)i;
    L.namedTop = (int)slots.size();

    L.loopStmt(loop, true);
    if (!L.bad()) L.emit(ids().ret);
    L.finish();
    if (L.bad()) { why = L.err; return nullptr; }
    if (L.ops.size() > kMaxOps) { why = "the loop lowers to more ops than a kernel holds"; return nullptr; }
    size_t nregs = (size_t)L.nNamed + (size_t)L.maxTemp;
    if (nregs > kMaxRegs) { why = "the loop needs more registers than a kernel holds"; return nullptr; }

    std::string err;
    auto code = assemble(L.ops, err);
    if (!code) { why = "the assembler refused: " + err; return nullptr; }

    Kernel* k = new Kernel();
    k->code = std::move(code);
    k->consts = std::move(L.consts);
    k->calls = std::move(L.calls);
    k->isites = std::move(L.isites);
    k->nregs = nregs;
    k->nops = L.ops.size();
    return k;
}

bool run(Kernel* k, Interpreter& I, Value** slots, const std::vector<bool>& written,
         std::string& why) {
    size_t n = written.size();
    // Two names that resolve to ONE container would be two registers here and
    // one cell in the interpreter, and the write-back would drop whichever went
    // last. The harness's own guards do not see this case, so it is checked on
    // the spot; `n` is a handful, so the pairwise scan is cheaper than a set.
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < i; j++)
            if (slots[i] == slots[j]) {
                why = "two of this loop's variables share one container";
                return false;
            }

    Frame f(k->nregs);
    f.abi.interp = &I;
    f.abi.consts = (void*)&k->consts;
    f.abi.calls = (void*)&k->calls;
    f.abi.slots = (void*)slots;
    f.abi.written = (void*)&written;
    f.abi.nslots = (int64_t)n;
    f.abi.ctl = 0;
    f.abi.isites = (void*)&k->isites;

    for (size_t i = 0; i < n; i++) setReg(&f.abi, i, *slots[i]);

    int rc = reinterpret_cast<RkCnpFn>(k->code->entry())(&f.abi, f.r.data(), f.t.data());

    // The write-back happens on BOTH exits. A loop that dies half way has to
    // leave the same values behind as an interpreted one would, because a CATCH
    // outside is about to read them.
    // A native container keeps being native: the register holds the number,
    // the tags belong to the container.
    for (size_t i = 0; i < n; i++)
        if (written[i]) {
            Value* c = slots[i];
            const int nb = c->natBits; const bool ns = c->natSigned, nf = c->natFloat;
            *c = regValue(&f.abi, i);
            if (nb) { c->natBits = nb; c->natSigned = ns; c->natFloat = nf; }
        }

    if (rc != RK_CNP_OK) {
        // Every RK_CNP_ERR follows a helper that stashed something, so the
        // second arm is unreachable by construction. It is here so that a
        // future stencil which returns an error WITHOUT one does not silently
        // look like a successful loop.
        if (f.err) std::rethrow_exception(f.err);
        why = "the kernel reported an error it did not record";
        return false;
    }
    return true;
}

size_t codeBytes(const Kernel* k) { return k && k->code ? k->code->bytes() : 0; }
size_t opCount(const Kernel* k)   { return k ? k->nops : 0; }
size_t regCount(const Kernel* k)  { return k ? k->nregs : 0; }

}  // namespace cnp
}  // namespace rakupp
