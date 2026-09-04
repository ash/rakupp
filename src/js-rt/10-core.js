// rakupp-rt.js — the runtime a `rakupp --target=js` program calls into.
// Assembled from src/js-rt/*.js in name order by tools/js/gen-rt-src.raku;
// this fragment is the value model and the numeric tower.
//
// Representations (TRANSPILE-PLAN.md, decided by measurement):
//   Int   a JS number while it is a safe integer, a BigInt past 2^53
//   Num   a JS number when it is NOT integral; an RNum box when it is
//         (so `2e0` stays a Num and `4e0/3` prints as a Num would)
//   Rat   RRat, exact, BigInt numerator and denominator
//   Str   a JS string (NFC bytes came from the parser; graphemes counted here)
//   Bool  a JS boolean
//   Nil / Any / type objects: RType instances — never null or undefined
//   Array/List/Seq: RList over a JS array; Hash: RHash over a Map
'use strict';
const R = {};

// ---------------------------------------------------------------- types ----
// A type object. `mro` is the method-resolution order (itself first), `methods`
// the table `mc` walks; `cache` memoizes each name's resolution.
class RType {
    constructor(name, parents = [], opts = {}) {
        this.name = name;
        this.parents = parents;
        this.methods = Object.create(null);
        this.cache = Object.create(null);
        this.roles = opts.roles || [];
        this.attrs = opts.attrs || [];      // user classes: attribute descriptors
        this.isEnum = !!opts.isEnum;
        this.enumValues = null;             // an enum type: [REnum...]
        this.isRole = !!opts.isRole;
        this.isUser = !!opts.isUser;
        this.mro = [this];
        for (const p of parents) for (const t of p.mro) if (!this.mro.includes(t)) this.mro.push(t);
    }
    isa(t) { return this.mro.includes(t) || (t.isRole && this.doesRole(t)); }
    doesRole(t) { for (const m of this.mro) if (m.roles.includes(t) || m === t) return true; return false; }
    // a method a USER type defines (never the core table's own implementation,
    // which the helpers below would otherwise call back into)
    findUser(name) {
        for (const t of this.mro) if (t.isUser) { const m = t.methods[name]; if (m) return m; }
        return null;
    }
    find(name) {
        const c = this.cache[name];
        if (c !== undefined) return c;
        let f = null;
        for (const t of this.mro) { const m = t.methods[name]; if (m) { f = m; break; } }
        return (this.cache[name] = f);
    }
    toString() { return this.name; }
}
const T = Object.create(null);           // name → RType, the core hierarchy
function mkType(name, parents, opts) { return (T[name] = new RType(name, parents, opts)); }
mkType('Mu', []);
mkType('Any', [T.Mu]);
mkType('Cool', [T.Any]);
mkType('Numeric', [T.Cool]);
mkType('Real', [T.Numeric]);
mkType('Int', [T.Real]);
mkType('Num', [T.Real]);
mkType('Rational', [T.Real]);
mkType('Rat', [T.Rational]);
mkType('FatRat', [T.Rational]);
mkType('Str', [T.Cool]);
mkType('Bool', [T.Int]);
mkType('Nil', [T.Cool]);
mkType('Positional', [T.Any]);
mkType('Iterable', [T.Any]);
mkType('List', [T.Positional, T.Iterable]);
mkType('Array', [T.List]);
mkType('Seq', [T.Positional, T.Iterable]);
mkType('Slip', [T.List]);
mkType('Range', [T.Positional, T.Iterable]);
mkType('Associative', [T.Any]);
mkType('Map', [T.Associative]);
mkType('Hash', [T.Map]);
mkType('Pair', [T.Associative]);
mkType('Callable', [T.Any]);
mkType('Code', [T.Callable]);
mkType('Block', [T.Code]);
mkType('Routine', [T.Block]);
mkType('Sub', [T.Routine]);
mkType('Method', [T.Routine]);
mkType('Whatever', [T.Any]);
mkType('WhateverCode', [T.Code]);
mkType('Exception', [T.Any]);
mkType('X::AdHoc', [T.Exception]);
mkType('Failure', [T.Nil]);
mkType('Junction', [T.Mu]);
mkType('Order', [T.Int]);
mkType('Setty', [T.Any]); mkType('Set', [T.Setty]); mkType('SetHash', [T.Setty]);
mkType('Baggy', [T.Any]); mkType('Bag', [T.Baggy]); mkType('BagHash', [T.Baggy]);
mkType('Mixy', [T.Baggy]); mkType('Mix', [T.Mixy]); mkType('MixHash', [T.Mixy]);
mkType('Complex', [T.Numeric]);
mkType('IO', [T.Any]); mkType('IO::Path', [T.IO]); mkType('IO::Handle', [T.IO]);
mkType('Stringy', [T.Cool]);
mkType('Version', [T.Any]);
mkType('Date', [T.Any]); mkType('DateTime', [T.Any]); mkType('Instant', [T.Any]);
mkType('Match', [T.Any]);
mkType('Regex', [T.Method]);
mkType('Promise', [T.Any]);
mkType('Proc', [T.Any]);
mkType('Capture', [T.Any]);
mkType('Signature', [T.Any]);
mkType('IterationEnd', [T.Any]);
const Nil = T.Nil;        // the Nil value IS its type object here (both stringify empty)
const Any = T.Any;
const Mu = T.Mu;

// ---------------------------------------------------------------- boxes ----
class RNum { constructor(v) { this.v = v; } }             // an integral-valued Num
class RRat {                                              // exact; d > 0, gcd 1
    constructor(n, d) { this.n = n; this.d = d; }
}
class RPair { constructor(k, v) { this.k = k; this.v = v; } }
class RSlip { constructor(a) { this.a = a; } }            // `|@x` / slip(...)
class REnum { constructor(ty, key, val) { this.ty = ty; this.key = key; this.val = val; } }
class RNamed { constructor(m) { this.m = m; } }           // named arguments, trailing
class RObj { constructor(ty) { this.ty = ty; } }          // a user-class instance
class RWhatever { }
const Whatever = new RWhatever();
class RJunction { constructor(kind, items) { this.kind = kind; this.items = items; } }
// Exceptions and control. Plain classes, not Error: a `die` in a loop must not
// pay for a captured JS stack trace.
class RakuError {
    constructor(message, type = 'X::AdHoc', payload = null) {
        this.message = message; this.type = type; this.payload = payload;
    }
    toString() { return this.message; }
}
class RFailure { constructor(err) { this.err = err; this.handled = false; } }
class NextCtl { constructor(label) { this.label = label; } }
class LastCtl { constructor(label) { this.label = label; } }
class RedoCtl { constructor(label) { this.label = label; } }
class RetCtl { constructor(token, v) { this.token = token; this.v = v; } }
class ExitCtl { constructor(code) { this.code = code; } }
class SuccCtl { }   // `when` succeeded / `proceed`/`succeed`

const MAX_SAFE = Number.MAX_SAFE_INTEGER;
const BIG_MAX_SAFE = BigInt(MAX_SAFE), BIG_MIN_SAFE = -BIG_MAX_SAFE;
const TWO64 = 1n << 64n;

// A BigInt back to a number when it fits — the invariant every Int op keeps.
function normBig(b) { return (b >= BIG_MIN_SAFE && b <= BIG_MAX_SAFE) ? Number(b) : b; }
// A float result of a Num operation: box it when it happens to be integral.
function numResult(r) { return Number.isInteger(r) ? new RNum(r) : r; }
function mkNum(v) { return Number.isInteger(v) ? new RNum(v) : v; }

function bigGcd(a, b) { if (a < 0n) a = -a; if (b < 0n) b = -b; while (b) { [a, b] = [b, a % b]; } return a; }
function mkRat(n, d) {                 // n, d BigInt; normalizes; d == 0 kept (lazy divide-by-zero, as Rakudo)
    if (typeof n !== 'bigint') n = BigInt(n);
    if (typeof d !== 'bigint') d = BigInt(d);
    if (d < 0n) { n = -n; d = -d; }
    if (d === 0n) return new RRat(n < 0n ? -1n : n > 0n ? 1n : 0n, 0n);
    const g = bigGcd(n, d);
    if (g > 1n) { n /= g; d /= g; }
    return new RRat(n, d);
}
// Rat arithmetic past a 64-bit denominator degrades to Num (Rakudo's rule).
function ratResult(n, d) {
    const r = mkRat(n, d);
    if (r.d >= TWO64) return numResult(Number(r.n) / Number(r.d));
    return r;
}

// ------------------------------------------------------- classification ----
// The runtime type of any value, as an RType.
function typeOf(v) {
    switch (typeof v) {
        case 'number': return Number.isInteger(v) ? T.Int : T.Num;
        case 'bigint': return T.Int;
        case 'string': return T.Str;
        case 'boolean': return T.Bool;
        case 'function': return v.rtype || T.Block;
        case 'object':
            if (v === null) return T.Nil;
            if (v instanceof RType) return v;
            if (v instanceof RObj) return v.ty;
            if (v instanceof RList) return v.ty;
            if (v instanceof RHash) return v.ty;
            if (v instanceof RNum) return T.Num;
            if (v instanceof RRat) return T.Rat;
            if (v instanceof RPair) return T.Pair;
            if (v instanceof RRange) return T.Range;
            if (v instanceof RSeq) return T.Seq;
            if (v instanceof REnum) return v.ty;
            if (v instanceof RSlip) return T.Slip;
            if (v instanceof RakuError) return T[v.type] || mkExType(v.type);
            if (v instanceof RFailure) return T.Failure;
            if (v instanceof RJunction) return T.Junction;
            if (v instanceof RWhatever) return T.Whatever;
            if (v instanceof RSetty) return v.ty;
            if (v instanceof RComplex) return T.Complex;
            if (v instanceof RIOPath) return T['IO::Path'];
            if (v instanceof RSig) return T.Signature;
            if (v instanceof RJsObj) return JsObjectT;
            if (v instanceof RPromise) return T.Promise;
            if (v instanceof RIOHandle) return T['IO::Handle'];
            if (v instanceof RVersion) return T.Version;
            if (v instanceof RDate) return v.ty;
            if (v instanceof RCapture) return T.Capture;
            return T.Any;
        default: return T.Any;
    }
}
function typeName(v) { return typeOf(v).name; }
function isType(v) { return v instanceof RType; }            // a type object (undefined value)
function defined(v) {
    if (v instanceof RType) return false;
    if (v instanceof RFailure) return false;
    if (v instanceof RJsObj) return v.v != null;
    return v !== undefined && v !== null;
}
function isa(v, tname) {
    const t = typeof tname === 'string' ? T[tname] : tname;
    if (!t) return false;
    if (v instanceof RJunction) return t === T.Junction || t === T.Mu;
    if (t.isSubset) return isaSubset(v, t);
    return typeOf(v).isa(t);
}
function isNumeric(v) {
    const ty = typeof v;
    return ty === 'number' || ty === 'bigint' || ty === 'boolean' || v instanceof RNum || v instanceof RRat ||
        v instanceof REnum || v instanceof RComplex;
}
function isIntVal(v) { return (typeof v === 'number' && Number.isInteger(v)) || typeof v === 'bigint'; }

// ------------------------------------------------------------- truthiness --
function truthy(v) {
    switch (typeof v) {
        case 'boolean': return v;
        case 'number': return v !== 0 && !Number.isNaN(v);
        case 'bigint': return v !== 0n;
        case 'string': return v !== '' && v !== '0';
        case 'function': return true;
        case 'object':
            if (v === null) return false;
            if (v instanceof RType) return false;
            if (v instanceof RNum) return v.v !== 0;
            if (v instanceof RRat) return v.n !== 0n;
            if (v instanceof RList) return v.elems() !== 0;
            if (v instanceof RSeq) return !v.isEmpty();
            if (v instanceof RHash) return v.m.size !== 0;
            if (v instanceof RRange) return v.elemsOrInf() !== 0;
            if (v instanceof RFailure) { v.handled = true; return false; }
            if (v instanceof REnum) return truthy(v.val);
            if (v instanceof RJunction) return junctionBool(v);
            if (v instanceof RObj) { const m = v.ty.findUser('Bool'); if (m) return truthy(m(v)); return true; }
            if (v instanceof RSetty) return v.m.size !== 0;
            if (v instanceof RComplex) return v.re !== 0 || v.im !== 0;
            if (v instanceof RJsObj) return !!v.v;
            if (v instanceof RPromise) return v.status === Kept;
            return true;
        default: return false;
    }
}
function so(v) { return truthy(v); }
function not(v) { return !truthy(v); }

// ------------------------------------------------------------ numeric ops --
// Coerce to a numeric value: Int (number|bigint) / Num (number|RNum) / RRat.
function toNumeric(v, op) {
    switch (typeof v) {
        case 'number': case 'bigint': return v;
        case 'boolean': return v ? 1 : 0;
        case 'string': return strToNumeric(v);
        case 'object':
            if (v instanceof RNum || v instanceof RRat || v instanceof RComplex) return v;
            if (v instanceof REnum) return v.val;
            if (v === null) return 0;
            if (v instanceof RType) return 0;                       // (Any) in numeric context: 0, no warning here
            if (v instanceof RList) return v.elems();
            if (v instanceof RHash) return v.m.size;
            if (v instanceof RRange) return v.elemsOrInf();
            if (v instanceof RSeq) return v.elems();
            if (v instanceof RFailure) throw v.err;
            if (v instanceof RPair) return toNumeric(v.v);
            if (v instanceof RObj) {
                const m = v.ty.findUser('Numeric') || v.ty.findUser('Int') || v.ty.findUser('Num');
                if (m) return toNumeric(m(v));
                throw new RakuError(`Cannot convert ${v.ty.name} to a number`);
            }
            if (v instanceof RDate) return v.numeric();
            return 0;
        case 'function': throw new RakuError('Cannot convert a Code object to a number');
        default: return 0;
    }
}
// Rakudo's Str.Numeric: "42" → Int, "3.14" → Rat, "1e3" → Num, "Inf"/"NaN" → Num.
function strToNumeric(s) {
    const t = s.trim();
    if (t === '') return 0;
    if (/^[+-]?\d+$/.test(t)) { const n = Number(t); return Number.isSafeInteger(n) ? n : normBig(BigInt(t)); }
    let m;
    if ((m = /^([+-]?)(\d*)\.(\d+)$/.exec(t))) {
        const sign = m[1] === '-' ? -1n : 1n;
        const ip = m[2] || '0', fp = m[3];
        return ratResult(sign * BigInt(ip + fp), 10n ** BigInt(fp.length));
    }
    if ((m = /^([+-]?)(\d+)\/(\d+)$/.exec(t))) return ratResult((m[1] === '-' ? -1n : 1n) * BigInt(m[2]), BigInt(m[3]));
    if (/^[+-]?(\d+\.?\d*|\.\d+)[eE][+-]?\d+$/.test(t)) return numResult(Number(t));
    if (/^[+-]?Inf$/.test(t)) return t[0] === '-' ? -Infinity : Infinity;
    if (t === 'NaN') return NaN;
    if ((m = /^([+-]?)0x([0-9a-fA-F_]+)$/.exec(t))) return normBig((m[1] === '-' ? -1n : 1n) * BigInt('0x' + m[2].replace(/_/g, '')));
    if ((m = /^([+-]?)0b([01_]+)$/.exec(t))) return normBig((m[1] === '-' ? -1n : 1n) * BigInt('0b' + m[2].replace(/_/g, '')));
    if ((m = /^([+-]?)0o([0-7_]+)$/.exec(t))) return normBig((m[1] === '-' ? -1n : 1n) * BigInt('0o' + m[2].replace(/_/g, '')));
    if (/^[+-]?\d[\d_]*$/.test(t)) return strToNumeric(t.replace(/_/g, ''));
    throw new RakuError(`Cannot convert string to number: base-10 number must begin with valid digits or '.' in '${s}'`, 'X::Str::Numeric');
}
// A float view of any numeric.
function toFloat(v) {
    switch (typeof v) {
        case 'number': return v;
        case 'bigint': return Number(v);
        case 'boolean': return v ? 1 : 0;
        default: {
            const n = (v instanceof RNum || v instanceof RRat || v instanceof REnum) ? v : toNumeric(v);
            if (typeof n === 'number') return n;
            if (typeof n === 'bigint') return Number(n);
            if (n instanceof RNum) return n.v;
            if (n instanceof RRat) return ratToFloat(n);
            if (n instanceof REnum) return toFloat(n.val);
            if (n instanceof RComplex) return n.re;
            return Number(n);
        }
    }
}
function ratToFloat(r) {
    if (r.d === 0n) return r.n === 0n ? NaN : (r.n < 0n ? -Infinity : Infinity);
    const n = Number(r.n), d = Number(r.d);
    if (Number.isFinite(n) && Number.isFinite(d)) return n / d;
    // huge parts: scale down with integer division first
    const sh = r.d.toString().length - 300;
    if (sh > 0) { const p = 10n ** BigInt(sh); return Number(r.n / p) / Number(r.d / p); }
    return n / d;
}
// Int view (truncating), for Int-only operations; dies on a non-integral Num.
function toInt(v) {
    switch (typeof v) {
        case 'number': return Number.isInteger(v) ? v : (Number.isFinite(v) ? Math.trunc(v) : intOfInf(v));
        case 'bigint': return v;
        case 'boolean': return v ? 1 : 0;
        case 'string': return toInt(strToNumeric(v));
        default: {
            const n = toNumeric(v);
            if (n instanceof RNum) return toInt(n.v);
            if (n instanceof RRat) return n.d === 0n ? intOfInf(ratToFloat(n)) : normBig(n.n / n.d - ((n.n < 0n && n.n % n.d !== 0n) ? 1n : 0n) + ((n.n < 0n && n.n % n.d !== 0n) ? 1n : 0n));
            if (n instanceof REnum) return toInt(n.val);
            return toInt(n);
        }
    }
}
function intOfInf(v) { throw new RakuError(`Cannot coerce ${numToStr(v)} to an Int`, 'X::Numeric::CannotConvert'); }
function big(v) { return typeof v === 'bigint' ? v : BigInt(toInt(v)); }

// The numeric rank of a coerced value: 0 Int, 1 Rat, 2 Num, 3 Complex.
function rank(v) {
    if (typeof v === 'number') return Number.isInteger(v) ? 0 : 2;
    if (typeof v === 'bigint') return 0;
    if (v instanceof RNum) return 2;
    if (v instanceof RRat) return 1;
    if (v instanceof RComplex) return 3;
    return 0;
}
function asRat(v) {           // Int or Rat → [n, d] BigInt
    if (v instanceof RRat) return [v.n, v.d];
    return [big(v), 1n];
}

function arith(op, a, b) {
    const x = toNumeric(a), y = toNumeric(b);
    const rk = Math.max(rank(x), rank(y));
    if (rk === 3) return complexArith(op, x, y);
    if (rk === 2) {
        const p = toFloat(x), q = toFloat(y);
        switch (op) {
            case '+': return numResult(p + q);
            case '-': return numResult(p - q);
            case '*': return numResult(p * q);
            case '/': return numResult(p / q);
            case '%': { if (q === 0) throw new RakuError(`Attempt to divide ${str(a)} by zero using infix:<%>`, 'X::Numeric::DivideByZero');
                        return numResult(p - Math.floor(p / q) * q); }
            case '**': return numResult(Math.pow(p, q));
        }
    }
    if (rk === 1) {
        const [n1, d1] = asRat(x), [n2, d2] = asRat(y);
        switch (op) {
            case '+': return ratResult(n1 * d2 + n2 * d1, d1 * d2);
            case '-': return ratResult(n1 * d2 - n2 * d1, d1 * d2);
            case '*': return ratResult(n1 * n2, d1 * d2);
            case '/': return ratResult(n1 * d2, d1 * n2);
            case '%': {
                if (n2 === 0n) throw new RakuError(`Attempt to divide ${str(a)} by zero using infix:<%>`, 'X::Numeric::DivideByZero');
                // a - floor(a/b)*b, exactly
                const qn = n1 * d2, qd = d1 * n2;               // a/b = qn/qd
                let fl = qn / qd; if ((qn % qd !== 0n) && ((qn < 0n) !== (qd < 0n))) fl -= 1n;
                return ratResult(n1 * d2 - fl * n2 * d1, d1 * d2);
            }
            case '**': {
                if (rank(y) === 0) {
                    let e = big(y);
                    if (e >= 0n) return ratResult(n1 ** e, d1 ** e);
                    e = -e; return ratResult(d1 ** e, n1 ** e);
                }
                return numResult(Math.pow(toFloat(x), toFloat(y)));
            }
        }
    }
    // both Int
    const p = big(x), q = big(y);
    switch (op) {
        case '+': return normBig(p + q);
        case '-': return normBig(p - q);
        case '*': return normBig(p * q);
        case '/': return q === 0n ? mkRat(p, 0n) : ratResult(p, q);
        case '%': {
            if (q === 0n) return failure(new RakuError(`Attempt to divide ${str(a)} by zero using infix:<%>`, 'X::Numeric::DivideByZero'));
            let r = p % q; if (r !== 0n && ((r < 0n) !== (q < 0n))) r += q;
            return normBig(r);
        }
        case '**': {
            if (q >= 0n) {
                if (q > 100000n && p !== 0n && p !== 1n && p !== -1n) return numResult(Math.pow(Number(p), Number(q)));
                return normBig(p ** q);
            }
            const e = -q;
            if (p === 0n) return Infinity;
            return ratResult(1n, p ** e);
        }
    }
    throw new RakuError('unknown arithmetic operator ' + op);
}

function add(a, b) {
    if (typeof a === 'number' && typeof b === 'number') {
        const r = a + b;
        if (Number.isInteger(a) && Number.isInteger(b)) return Number.isSafeInteger(r) ? r : normBig(BigInt(a) + BigInt(b));
        return Number.isInteger(r) ? new RNum(r) : r;
    }
    return arith('+', a, b);
}
function sub(a, b) {
    if (typeof a === 'number' && typeof b === 'number') {
        const r = a - b;
        if (Number.isInteger(a) && Number.isInteger(b)) return Number.isSafeInteger(r) ? r : normBig(BigInt(a) - BigInt(b));
        return Number.isInteger(r) ? new RNum(r) : r;
    }
    return arith('-', a, b);
}
function mul(a, b) {
    if (typeof a === 'number' && typeof b === 'number') {
        const r = a * b;
        if (Number.isInteger(a) && Number.isInteger(b)) return Number.isSafeInteger(r) ? r : normBig(BigInt(a) * BigInt(b));
        return Number.isInteger(r) ? new RNum(r) : r;
    }
    return arith('*', a, b);
}
function div(a, b) {
    if (typeof a === 'number' && typeof b === 'number' && !(Number.isInteger(a) && Number.isInteger(b))) {
        const r = a / b; return Number.isInteger(r) ? new RNum(r) : r;
    }
    return arith('/', a, b);
}
function mod(a, b) {
    if (typeof a === 'number' && typeof b === 'number' && Number.isInteger(a) && Number.isInteger(b) && b !== 0) {
        const r = a % b; return (r !== 0 && (r < 0) !== (b < 0)) ? r + b : r;
    }
    return arith('%', a, b);
}
function pow(a, b) { return arith('**', a, b); }
function neg(a) {
    if (typeof a === 'number') return Number.isInteger(a) ? (a === 0 ? 0 : -a) : -a;
    if (typeof a === 'bigint') return normBig(-a);
    const x = toNumeric(a);
    if (x instanceof RNum) return new RNum(-x.v);
    if (x instanceof RRat) return new RRat(-x.n, x.d);
    if (x instanceof RComplex) return new RComplex(-x.re, -x.im);
    return neg(x);
}
// the divisor of div/mod decides first: a non-finite one has no Int and counts as zero
function divisorInt(b) { const n = toNumeric(b); const f = (typeof n === 'number' || n instanceof RNum) ? toFloat(n) : null; if (f !== null && !Number.isFinite(f)) return 0n; return big(toIntStrict(n, 'div')); }
function idiv(a, b) {          // infix:<div>
    const q = divisorInt(b), p = big(toIntStrict(a, 'div'));
    if (q === 0n) return failure(new RakuError(`Attempt to divide ${str(a)} by zero using div`, 'X::Numeric::DivideByZero'));   // soft: a Failure that throws when used
    let r = p / q; if (p % q !== 0n && ((p < 0n) !== (q < 0n))) r -= 1n;
    return normBig(r);
}
function imod(a, b) {          // infix:<mod>
    const q = divisorInt(b), p = big(toIntStrict(a, 'mod'));
    if (q === 0n) throw new RakuError(`Attempt to divide ${str(a)} by zero using mod`, 'X::Numeric::DivideByZero');   // `mod` throws where `div` and `%` soft-fail
    let r = p % q; if (r !== 0n && ((r < 0n) !== (q < 0n))) r += q;
    return normBig(r);
}
function toIntStrict(v, op) {
    const n = toNumeric(v);
    if (isIntVal(n)) return n;
    if (n instanceof RRat || n instanceof RNum || typeof n === 'number') return toInt(n);
    return toInt(n);
}
function gcd(a, b) { return normBig(bigGcd(big(a), big(b))); }
function lcm(a, b) { const p = big(a), q = big(b); if (p === 0n || q === 0n) return 0; const g = bigGcd(p, q); let r = p / g * q; if (r < 0n) r = -r; return normBig(r); }
function bitand(a, b) { return normBig(big(a) & big(b)); }
function bitor(a, b) { return normBig(big(a) | big(b)); }
function bitxor(a, b) { return normBig(big(a) ^ big(b)); }
function bitneg(a) { return normBig(~big(a)); }
function shl(a, b) { return normBig(big(a) << big(b)); }
function shr(a, b) { return normBig(big(a) >> big(b)); }
function abs(a) {
    if (typeof a === 'number') return Number.isInteger(a) ? Math.abs(a) : Math.abs(a);
    const x = toNumeric(a);
    if (typeof x === 'bigint') return normBig(x < 0n ? -x : x);
    if (x instanceof RNum) return new RNum(Math.abs(x.v));
    if (x instanceof RRat) return new RRat(x.n < 0n ? -x.n : x.n, x.d);
    if (x instanceof RComplex) return numResult(Math.hypot(x.re, x.im));
    return Math.abs(x);
}
function numify(v) {           // prefix:<+>
    if (typeof v === 'number' || typeof v === 'bigint') return v;
    const x = toNumeric(v);
    return x;
}
// Numeric comparison: -1/0/1, or NaN when unordered.
function numCmp(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return a < b ? -1 : a > b ? 1 : a === b ? 0 : NaN;
    const x = toNumeric(a), y = toNumeric(b);
    const rk = Math.max(rank(x), rank(y));
    if (rk >= 2) { const p = toFloat(x), q = toFloat(y); return p < q ? -1 : p > q ? 1 : p === q ? 0 : NaN; }
    if (rk === 1) { const [n1, d1] = asRat(x), [n2, d2] = asRat(y); const l = n1 * d2, r = n2 * d1; return l < r ? -1 : l > r ? 1 : 0; }
    const p = big(x), q = big(y); return p < q ? -1 : p > q ? 1 : 0;
}
function numeq(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return a === b;
    if (a instanceof RJunction || b instanceof RJunction) return junctionOp(numeq, a, b);
    return numCmp(a, b) === 0;
}
function numne(a, b) { return a instanceof RJunction || b instanceof RJunction ? junctionOp(numne, a, b) : !numeq(a, b); }
function lt(a, b) { if (typeof a === 'number' && typeof b === 'number') return a < b; if (a instanceof RJunction || b instanceof RJunction) return junctionOp(lt, a, b); return numCmp(a, b) === -1; }
function le(a, b) { if (typeof a === 'number' && typeof b === 'number') return a <= b; if (a instanceof RJunction || b instanceof RJunction) return junctionOp(le, a, b); const c = numCmp(a, b); return c === -1 || c === 0; }
function gt(a, b) { if (typeof a === 'number' && typeof b === 'number') return a > b; if (a instanceof RJunction || b instanceof RJunction) return junctionOp(gt, a, b); return numCmp(a, b) === 1; }
function ge(a, b) { if (typeof a === 'number' && typeof b === 'number') return a >= b; if (a instanceof RJunction || b instanceof RJunction) return junctionOp(ge, a, b); const c = numCmp(a, b); return c === 1 || c === 0; }

// Order enum
const OrderT = mkType('Order', [T.Int], { isEnum: true });
const Less = new REnum(OrderT, 'Less', -1), Same = new REnum(OrderT, 'Same', 0), More = new REnum(OrderT, 'More', 1);
OrderT.enumValues = [Less, Same, More];
function order(c) { return c < 0 ? Less : c > 0 ? More : Same; }
function spaceship(a, b) { const c = numCmp(a, b); return Number.isNaN(c) ? Nil : order(c); }   // <=>
function leg(a, b) { return order(strCmp(str(a), str(b))); }
function strCmp(x, y) { return x < y ? -1 : x > y ? 1 : 0; }
// infix:<cmp>: numeric when both numeric, else string
function cmp(a, b) {
    if (isNumeric(a) && isNumeric(b)) return spaceship(a, b);
    if (a instanceof RType && b instanceof RType) return order(strCmp(a.name, b.name));
    if (a instanceof RList && b instanceof RList) {
        const x = a.arr(), y = b.arr(), n = Math.min(x.length, y.length);
        for (let i = 0; i < n; i++) { const c = cmp(x[i], y[i]); if (c !== Same) return c; }
        return order(x.length - y.length);
    }
    if (a instanceof RPair && b instanceof RPair) { const c = cmp(a.k, b.k); return c !== Same ? c : cmp(a.v, b.v); }
    if (a instanceof RVersion && b instanceof RVersion) return order(a.cmp(b));
    return leg(a, b);
}
function cmpNum(a, b) {         // the -1/0/1 of `cmp`, for sorting
    const o = cmp(a, b); return o instanceof REnum ? o.val : 0;
}

// -------------------------------------------------------------- strings ----
function isAscii(s) { for (let i = 0; i < s.length; i++) if (s.charCodeAt(i) > 127) return false; return true; }
// numToStr: Rakudo's Num.Str — the shortest %g that round-trips, as Value.cpp does it.
function numToStr(n) {
    if (n === Infinity) return 'Inf';
    if (n === -Infinity) return '-Inf';
    if (Number.isNaN(n)) return 'NaN';
    if (n === 0) return Object.is(n, -0) ? '-0' : '0';
    if (Number.isInteger(n) && Math.abs(n) < 1e15) return String(n);
    for (let prec = 15; prec <= 17; prec++) {
        const s = fmtG(n, prec);
        if (Number(s) === n) return s;
    }
    return fmtG(n, 17);
}
// C's %.<prec>g
function fmtG(n, prec) {
    if (n === 0) return '0';
    let e = Math.floor(Math.log10(Math.abs(n)));
    // toExponential gives the exact rounded mantissa; recompute the exponent from it
    let ex = n.toExponential(prec - 1);            // d.ddddde±x
    let m = /^(-?\d(?:\.\d+)?)e([+-]\d+)$/.exec(ex);
    e = parseInt(m[2], 10);
    if (e < -4 || e >= prec) {
        let mant = m[1]; if (mant.includes('.')) mant = mant.replace(/0+$/, '').replace(/\.$/, '');
        const es = Math.abs(e) < 10 ? '0' + Math.abs(e) : String(Math.abs(e));
        return mant + 'e' + (e < 0 ? '-' : '+') + es;
    }
    let f = n.toFixed(Math.max(0, prec - 1 - e));
    if (f.includes('.')) f = f.replace(/0+$/, '').replace(/\.$/, '');
    return f;
}
function ratToStr(r) {
    if (r.d === 0n) throw new RakuError('Attempt to divide by zero when coercing Rational to Str', 'X::Numeric::DivideByZero');
    let n = r.n, d = r.d;
    const neg = n < 0n; if (neg) n = -n;
    const ip = n / d, rem = n % d;
    if (rem === 0n) return (neg && ip !== 0n ? '-' : '') + ip.toString();
    const fracDigits = Math.max(6, d.toString().length + 1);
    const scale = 10n ** BigInt(fracDigits);
    let q = (n * scale) / d; const rr = (n * scale) % d;
    if (rr * 2n - d >= 0n) q += 1n;
    let digits = q.toString();
    while (digits.length <= fracDigits) digits = '0' + digits;
    const ipart = digits.slice(0, digits.length - fracDigits);
    let fpart = digits.slice(digits.length - fracDigits).replace(/0+$/, '');
    const res = (neg ? '-' : '') + ipart + (fpart ? '.' + fpart : '');
    return res === '-0' ? '0' : res;
}
// .Str
function str(v) {
    switch (typeof v) {
        case 'string': return v;
        case 'number': return Number.isInteger(v) ? String(v) : numToStr(v);
        case 'bigint': return v.toString();
        case 'boolean': return v ? 'True' : 'False';
        case 'function': return v.rname ? v.rname : '';
        case 'object':
            if (v === null) return '';
            if (v instanceof RNum) return numToStr(v.v);
            if (v instanceof RRat) return ratToStr(v);
            if (v instanceof RType) return v === T.IterationEnd ? 'IterationEnd' : '';
            if (v instanceof RList) return v.arr().map(str).join(' ');
            if (v instanceof RSeq) return v.arr().map(str).join(' ');
            if (v instanceof RPair) return str(v.k) + '\t' + str(v.v);
            if (v instanceof RHash) { const o = []; for (const [k, x] of v.m) o.push(k + '\t' + str(x)); return o.join('\n'); }
            if (v instanceof RRange) return v.Str();
            if (v instanceof REnum) return typeof v.val === 'string' && v.ty !== OrderT ? v.val : v.key;
            if (v instanceof RakuError) return v.message;
            if (v instanceof RFailure) throw v.err;
            if (v instanceof RSlip) return v.a.map(str).join(' ');
            if (v instanceof RObj) {
                const m = v.ty.findUser('Str'); if (m) return str(m(v));
                return v.ty.name + '<' + objId(v) + '>';
            }
            if (v instanceof RJunction) return junctionStr(v);
            if (v instanceof RWhatever) return '*';
            if (v instanceof RSetty) return v.Str();
            if (v instanceof RComplex) return v.Str();
            if (v instanceof RIOPath) return v.path;
            if (v instanceof RVersion) return v.Str();
            if (v instanceof RDate) return v.Str();
            if (v instanceof RCapture) return v.Str();
            if (v instanceof RJsObj) return jsStr(v);
            return String(v);
        default: return '';
    }
}
let objIdCounter = 1;
const objIds = new WeakMap();
function objId(o) { let id = objIds.get(o); if (!id) { id = objIdCounter++; objIds.set(o, id); } return id; }

function strLit(s) {              // .raku of a Str
    return '"' + s.replace(/[\\"$@{]/g, m => '\\' + m).replace(/\n/g, '\\n').replace(/\t/g, '\\t').replace(/\r/g, '\\r').replace(/\0/g, '\\0') + '"';
}
// .gist
function gist(v) {
    switch (typeof v) {
        case 'string': return v;
        case 'number': case 'bigint': case 'boolean': return str(v);
        case 'function': return v.rname ? '&' + v.rname : '-> ;; $_? is raw = OUTER::<$_> { #`(Block|' + objId(v) + ') ... }';
        case 'object':
            if (v === null) return 'Nil';
            if (v instanceof RType) return v === Nil ? 'Nil' : '(' + v.name + ')';
            if (v instanceof RNum || v instanceof RRat) return str(v);
            if (v instanceof RList) return v.gist();
            if (v instanceof RSeq) return v.gist();
            if (v instanceof RHash) return v.gist();
            if (v instanceof RPair) return pairGist(v);
            if (v instanceof RRange) return v.gist();
            if (v instanceof REnum) return v.key;
            if (v instanceof RakuError) return v.message;
            if (v instanceof RFailure) return '(HANDLED) ' + v.err.message;
            if (v instanceof RSlip) return listGistOf(v.a, '(', ')');
            if (v instanceof RObj) {
                const m = v.ty.findUser('gist'); if (m) return str(m(v));
                return objGist(v);
            }
            if (v instanceof RJunction) return junctionGist(v);
            if (v instanceof RWhatever) return '*';
            if (v instanceof RSetty) return v.gist();
            if (v instanceof RComplex) return v.Str();
            if (v instanceof RIOPath) return strLit(v.path) + '.IO';
            if (v instanceof RVersion) return 'v' + v.Str();
            if (v instanceof RDate) return v.Str();
            if (v instanceof RCapture) return v.gist();
            if (v instanceof RJsObj) return jsStr(v);
            return str(v);
        default: return str(v);
    }
}
function pairGist(p) { return pairKeyGist(p.k) + ' => ' + gist(p.v); }
function pairKeyGist(k) { return typeof k === 'string' ? k : gist(k); }
function listGistOf(items, open, close) {
    const parts = [];
    const n = items.length;
    for (let i = 0; i < n && i < 100; i++) parts.push(gist(items[i]));
    if (n > 100) parts.push('...');
    return open + parts.join(' ') + close;
}
// the default object gist is its .raku: derived class's attributes first, values as .raku
function objGist(o) {
    const parts = [];
    for (const a of o.ty.allAttrs()) if (a.pub) parts.push(a.name + ' => ' + raku(o['a_' + a.name]));
    return o.ty.name + '.new' + (parts.length ? '(' + parts.join(', ') + ')' : '');
}
// .raku
function raku(v) {
    switch (typeof v) {
        case 'string': return strLit(v);
        case 'number': return Number.isInteger(v) ? String(v) : numRaku(v);
        case 'bigint': return v.toString();
        case 'boolean': return v ? 'Bool::True' : 'Bool::False';
        case 'function': return v.rname ? 'sub ' + v.rname + ' (…) { #`(Sub|' + objId(v) + ') ... }' : '-> ;; $_? is raw = OUTER::<$_> { #`(Block|' + objId(v) + ') ... }';
        case 'object':
            if (v === null) return 'Nil';
            if (v instanceof RType) return v === Nil ? 'Nil' : v.name;
            if (v instanceof RNum) return numRaku(v.v);
            if (v instanceof RRat) return ratRaku(v);
            if (v instanceof RList) return v.raku();
            if (v instanceof RSeq) return v.raku();
            if (v instanceof RHash) return v.raku();
            if (v instanceof RPair) return pairRaku(v);
            if (v instanceof RRange) return v.raku();
            if (v instanceof REnum) return v.ty.name + '::' + v.key;
            if (v instanceof RakuError) return v.type + '.new(message => ' + strLit(v.message) + ')';
            if (v instanceof RSlip) return 'slip(' + v.a.map(raku).join(', ') + ')';
            if (v instanceof RObj) {
                const m = v.ty.findUser('raku'); if (m) return str(m(v));
                const parts = [];
                for (const a of v.ty.allAttrs()) if (a.pub) parts.push(a.name + ' => ' + raku(v['a_' + a.name]));
                return v.ty.name + '.new(' + parts.join(', ') + ')';
            }
            if (v instanceof RJunction) return junctionRaku(v);
            if (v instanceof RWhatever) return '*';
            if (v instanceof RSetty) return v.raku();
            if (v instanceof RComplex) return '<' + v.Str() + '>';
            if (v instanceof RIOPath) return 'IO::Path.new(' + strLit(v.path) + ')';
            if (v instanceof RVersion) return 'v' + v.Str();
            if (v instanceof RDate) return v.raku();
            if (v instanceof RCapture) return v.raku();
            return str(v);
        default: return str(v);
    }
}
function numRaku(n) {
    if (!Number.isFinite(n)) return numToStr(n);
    const s = numToStr(n);
    if (Number.isInteger(n) && !s.includes('e')) return s + 'e0';
    return s;
}
function ratRaku(r) {
    if (r.d === 0n) return '<' + r.n + '/0>';
    // a terminating decimal prints as such; otherwise <n/d>
    let d = r.d, twos = 0, fives = 0;
    while (d % 2n === 0n) { d /= 2n; twos++; }
    while (d % 5n === 0n) { d /= 5n; fives++; }
    if (d === 1n) {
        const places = Math.max(twos, fives);
        if (places === 0) return r.n.toString() + '.0';
        const scale = 10n ** BigInt(places);
        const neg = r.n < 0n; const n = neg ? -r.n : r.n;
        const whole = (n * scale) / r.d;
        let s = whole.toString(); while (s.length <= places) s = '0' + s;
        return (neg ? '-' : '') + s.slice(0, s.length - places) + '.' + s.slice(s.length - places);
    }
    return '<' + r.n + '/' + r.d + '>';
}
function pairRaku(p) {
    const k = p.k;
    if (typeof k === 'string' && /^[A-Za-z_][\w-]*$/.test(k)) {
        if (p.v === true) return ':' + k;
        if (p.v === false) return ':!' + k;
        return ':' + k + '(' + raku(p.v) + ')';
    }
    return raku(k) + ' => ' + raku(p.v);
}

// String coercions/operators used by generated code
function concat(a, b) { return (typeof a === 'string' ? a : str(a)) + (typeof b === 'string' ? b : str(b)); }
function seq(a, b) { if (a instanceof RJunction || b instanceof RJunction) return junctionOp(seq, a, b); return str(a) === str(b); }
function sne(a, b) { if (a instanceof RJunction || b instanceof RJunction) return junctionOp(sne, a, b); return str(a) !== str(b); }
function slt(a, b) { return str(a) < str(b); }
function sle(a, b) { return str(a) <= str(b); }
function sgt(a, b) { return str(a) > str(b); }
function sge(a, b) { return str(a) >= str(b); }
function xrepeat(s, n) {         // infix:<x>
    const k = toInt(n); if (typeof k === 'bigint' || k <= 0) return '';
    return str(s).repeat(k);
}
// ===  and eqv
function identical(a, b) {
    if (a === b) return true;
    if (typeof a !== typeof b) return false;
    if (typeof a === 'string' || typeof a === 'number' || typeof a === 'bigint' || typeof a === 'boolean') return a === b;
    if (a instanceof RNum && b instanceof RNum) return a.v === b.v;
    if (a instanceof RRat && b instanceof RRat) return a.n === b.n && a.d === b.d;
    if (a instanceof REnum && b instanceof REnum) return a.ty === b.ty && a.key === b.key;
    if (a instanceof RType && b instanceof RType) return a === b;
    return false;
}
function eqv(a, b) {
    if (identical(a, b)) return true;
    if (typeof a !== typeof b) return false;
    if (typeof a === 'number') return Number.isNaN(a) && Number.isNaN(b);
    if (a instanceof RList && b instanceof RList) {
        if (a.ty !== b.ty) return false;
        const x = a.arr(), y = b.arr();
        if (x.length !== y.length) return false;
        for (let i = 0; i < x.length; i++) if (!eqv(x[i], y[i])) return false;
        return true;
    }
    if (a instanceof RSeq && b instanceof RSeq) return eqv(a.list(), b.list());
    if (a instanceof RHash && b instanceof RHash) {
        if (a.m.size !== b.m.size) return false;
        for (const [k, v] of a.m) { if (!b.m.has(k) || !eqv(v, b.m.get(k))) return false; }
        return true;
    }
    if (a instanceof RPair && b instanceof RPair) return eqv(a.k, b.k) && eqv(a.v, b.v);
    if (a instanceof RRange && b instanceof RRange) return eqv(a.from, b.from) && eqv(a.to, b.to) && a.exFrom === b.exFrom && a.exTo === b.exTo;
    if (a instanceof RObj && b instanceof RObj) {
        if (a.ty !== b.ty) return false;
        for (const at of a.ty.allAttrs()) if (!eqv(a['a_' + at.name], b['a_' + at.name])) return false;
        return true;
    }
    if (a instanceof RSetty && b instanceof RSetty) return a.eqv(b);
    if (a instanceof RComplex && b instanceof RComplex) return a.re === b.re && a.im === b.im;
    if (a instanceof RDate && b instanceof RDate) return a.ty === b.ty && a.d.getTime() === b.d.getTime();
    if (a instanceof RVersion && b instanceof RVersion) return a.cmp(b) === 0;
    return false;
}

// ++ / --: numbers, then the string magic increment, then an undefined value
function inc(v) {
    if (typeof v === 'number') { if (Number.isInteger(v)) { const r = v + 1; return r > MAX_SAFE ? normBig(BigInt(v) + 1n) : r; } return numResult(v + 1); }
    if (typeof v === 'bigint') return normBig(v + 1n);
    if (typeof v === 'string') return strSucc(v);
    if (v instanceof RType || v === null || v === undefined) return 1;
    if (typeof v === 'boolean') return true;
    if (v instanceof REnum) { const vs = v.ty.enumValues; const i = vs.indexOf(v); return vs[Math.min(i + 1, vs.length - 1)]; }
    return add(v, 1);
}
function dec(v) {
    if (typeof v === 'number') { if (Number.isInteger(v)) { const r = v - 1; return r < -MAX_SAFE ? normBig(BigInt(v) - 1n) : r; } return numResult(v - 1); }
    if (typeof v === 'bigint') return normBig(v - 1n);
    if (typeof v === 'string') return strPred(v);
    if (v instanceof RType || v === null || v === undefined) return -1;
    if (typeof v === 'boolean') return false;
    if (v instanceof REnum) { const vs = v.ty.enumValues; const i = vs.indexOf(v); return vs[Math.max(i - 1, 0)]; }
    return sub(v, 1);
}
function strSucc(s) {
    if (s === '') return '1';
    // find the rightmost alphanumeric run
    let end = s.length; while (end > 0 && !/[0-9A-Za-z]/.test(s[end - 1])) end--;
    if (end === 0) return s + '1';
    let start = end; while (start > 0 && /[0-9A-Za-z]/.test(s[start - 1])) start--;
    const chars = s.slice(start, end).split('');
    let i = chars.length - 1, carry = true;
    while (i >= 0 && carry) {
        const c = chars[i];
        if (c === 'z') { chars[i] = 'a'; } else if (c === 'Z') { chars[i] = 'A'; } else if (c === '9') { chars[i] = '0'; }
        else { chars[i] = String.fromCharCode(c.charCodeAt(0) + 1); carry = false; }
        i--;
    }
    if (carry) { const f = chars[0]; chars.unshift(f === 'a' ? 'a' : f === 'A' ? 'A' : '1'); }
    return s.slice(0, start) + chars.join('') + s.slice(end);
}
function strPred(s) {
    if (s === '') return s;
    let end = s.length; while (end > 0 && !/[0-9A-Za-z]/.test(s[end - 1])) end--;
    if (end === 0) return s;
    let start = end; while (start > 0 && /[0-9A-Za-z]/.test(s[start - 1])) start--;
    const chars = s.slice(start, end).split('');
    let i = chars.length - 1, borrow = true;
    while (i >= 0 && borrow) {
        const c = chars[i];
        if (c === 'a') { chars[i] = 'z'; } else if (c === 'A') { chars[i] = 'Z'; } else if (c === '0') { chars[i] = '9'; }
        else { chars[i] = String.fromCharCode(c.charCodeAt(0) - 1); borrow = false; }
        i--;
    }
    if (borrow) throw new RakuError('Decrement out of range');
    return s.slice(0, start) + chars.join('') + s.slice(end);
}

// Complex, minimal (P1 keeps it so `i` literals and .sqrt of negatives have a home)
class RComplex {
    constructor(re, im) { this.re = re; this.im = im; }
    Str() { const r = numToStr(this.re), i = numToStr(this.im); return r + (this.im < 0 || i[0] === '-' ? '' : '+') + i + 'i'; }
}
function complexArith(op, x, y) {
    const a = x instanceof RComplex ? x : new RComplex(toFloat(x), 0);
    const b = y instanceof RComplex ? y : new RComplex(toFloat(y), 0);
    switch (op) {
        case '+': return new RComplex(a.re + b.re, a.im + b.im);
        case '-': return new RComplex(a.re - b.re, a.im - b.im);
        case '*': return new RComplex(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
        case '/': { const d = b.re * b.re + b.im * b.im; return new RComplex((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d); }
        case '**': {
            const r = Math.hypot(a.re, a.im), th = Math.atan2(a.im, a.re);
            if (r === 0) return new RComplex(0, 0);
            const lr = Math.log(r);
            const nr = Math.exp(b.re * lr - b.im * th), nth = b.im * lr + b.re * th;
            return new RComplex(nr * Math.cos(nth), nr * Math.sin(nth));
        }
    }
    throw new RakuError('Complex ' + op + ' not implemented');
}

Object.assign(R, {
    T, RType, Nil, Any, Mu, RNum, RRat, RPair, RSlip, REnum, RNamed, RObj, RWhatever, Whatever, RJunction,
    RakuError, RFailure, NextCtl, LastCtl, RedoCtl, RetCtl, ExitCtl, SuccCtl, RComplex,
    typeOf, typeName, isType, defined, isa, truthy, so, not,
    toNumeric, toFloat, toInt, big, mkRat, mkNum, numResult, normBig,
    add, sub, mul, div, mod, pow, neg, idiv, imod, gcd, lcm, bitand, bitor, bitxor, bitneg, shl, shr, abs, numify,
    numCmp, numeq, numne, lt, le, gt, ge, spaceship, leg, cmp, cmpNum, order, Less, Same, More,
    numToStr, ratToStr, str, gist, raku, strLit, concat, seq, sne, slt, sle, sgt, sge, xrepeat, identical, eqv, inc, dec,
    strSucc, strPred, isAscii, objId,
});
