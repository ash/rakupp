// Glue the emitter relies on: closures with arity metadata, boxes for `is rw`,
// dynamic variables, coercions, adverbs, and the sub-type/enum helpers.

// a block with Raku arity/count metadata (map/sort/seqOp read it)
function blk(f, arity, count) { f.arity = arity; f.count = count === undefined ? arity : count; return f; }
// a WhateverCode: `* + 1`, `*.abs`
function wc(f, arity) { f.arity = arity; f.count = arity; f.rtype = T.WhateverCode; return f; }
function callCode(f, ...args) {
    if (typeof f === 'function') return f(...args);
    if (f instanceof RObj) { const m = f.ty.findUser('CALL-ME'); if (m) return m(f, ...args); }
    if (f instanceof RScalar) return callCode(f.v, ...args);
    if (f instanceof RType && f.isUser) return construct(f, ...args);
    throw new RakuError(`Cannot call a ${typeName(f)} (it is not a Callable)`);
}
// `is rw` parameters: a box with a `.v` accessor. Call sites hand in a
// getter/setter pair for a variable or a subscript; anything else is wrapped.
function rwBox(x) { if (x instanceof RScalar) return x; if (x && typeof x === 'object' && Object.getOwnPropertyDescriptor(x, 'v') && Object.getOwnPropertyDescriptor(x, 'v').get) return x; return new RScalar(x); }
function throwCtl(op, label) { throw op === 'next' ? new NextCtl(label) : op === 'last' ? new LastCtl(label) : new RedoCtl(label); }
// EXPR xx N with the left side re-evaluated per copy
function xxThunk(thunk, n) {
    if (n instanceof RWhatever || n === Infinity) return new RSeq((function* () { for (;;) yield thunk(); })(), true);
    const k = Number(toInt(n)); const out = [];
    for (let i = 0; i < k; i++) { const v = thunk(); if (v instanceof RSlip) out.push(...v.a); else out.push(v); }
    return mkList(out);
}
function namedFromHash(h, extra) { const m = new Map(); if (h instanceof RHash) for (const [k, v] of h.m) m.set(k, v); if (extra) for (const [k, v] of extra) m.set(k, v); return new RNamed(m); }
function kvAdverb(c, k, isHash) { const ex = isHash ? hexists(c, k) : aexists(c, k); return ex ? mkList([k, isHash ? hget(c, k) : aget(c, k)]) : mkSlip([]); }
function pAdverb(c, k, isHash) { const ex = isHash ? hexists(c, k) : aexists(c, k); return ex ? pair(k, isHash ? hget(c, k) : aget(c, k)) : mkSlip([]); }
function listAppendAssign(cur, v) { const items = itemsOf(cur).slice(); items.push(...itemsOf(v)); return mkList(items); }
// $obj.attr = v through an `is rw` accessor
function mcSet(inv, name, v) {
    if (inv instanceof RObj) { const m = inv.ty.findUser(name); if (m && m.lvKey) { inv[m.lvKey] = v; return v; } if (m) { const r = m(inv); if (r instanceof RScalar) { r.v = v; return v; } } }
    throw new RakuError(`Cannot modify an immutable value returned by .${name}`);
}
// $obj.^name / Foo.^methods
function meta(inv, name, ...args) { const t = inv instanceof RType ? inv : typeOf(inv); return mc(t, name, ...args); }
// Int($x) / Str(…) / a subset or class coercion
function coerce(ty, v) {
    if (ty === T.Int) return toInt(toNumeric(v));
    if (ty === T.Num) return mkNum(toFloat(v));
    if (ty === T.Str) return str(v);
    if (ty === T.Bool) return truthy(v);
    if (ty === T.Rat) return mc(v, 'Rat');
    if (ty === T.Numeric || ty === T.Real) return toNumeric(v);
    if (ty === T.Array) return newArray(v);
    if (ty === T.List) return mkList(itemsOf(v).slice());
    if (ty === T.Hash) return newHash(v);
    if (ty === T.Set || ty === T.Bag || ty === T.Mix) return toSetty(v, ty);
    if (ty === T.Complex) return new RComplex(toFloat(v), 0);
    if (ty === T.Version) return new RVersion(str(v));
    if (ty['IO::Path'] || ty === T['IO::Path'] || ty === T.IO) return ioPath(v);
    if (ty === T.Date || ty === T.DateTime) return dateNew(ty, [v]);
    if (ty.isUser) { const m = ty.find(ty.name); if (m) return m(v); if (isa(v, ty)) return v; throw new RakuError(`Impossible coercion from '${typeName(v)}' into '${ty.name}': no acceptable coercion method found`, 'X::Coerce::Impossible'); }
    if (ty.check) { if (isa(v, ty)) return v; throw new RakuError(`Type check failed in coercion; expected ${ty.name} but got ${typeName(v)}`); }
    return v;
}
// user dynamic variables: one process-wide table (dynamic scoping approximated)
const dynTable = new Map();
function dynGet(name) { if (dynTable.has(name)) return dynTable.get(name); try { return dynVar(name); } catch (e) { throw new RakuError(`Dynamic variable ${name} not found`, 'X::Dynamic::NotFound'); } }
function dynSet(name, v) { dynTable.set(name, v); return v; }
function approxEq(a, b) { const x = toFloat(a), y = toFloat(b); if (x === y) return true; const tol = 1e-15; return Math.abs(x - y) <= tol * Math.max(Math.abs(x), Math.abs(y)); }
// `for 1..$n`: a counted iterator on the Int fast path, the generic range otherwise
function rangeIter(from, to, exFrom, exTo) {
    if (typeof from === 'number' && typeof to === 'number' && Number.isInteger(from) && Number.isInteger(to)) {
        let i = exFrom ? from + 1 : from; const end = exTo ? to - 1 : to;
        return { [Symbol.iterator]() { return this; }, next() { return i <= end ? { value: i++, done: false } : { value: undefined, done: true }; } };
    }
    return range(from, to, exFrom, exTo)[Symbol.iterator]();
}
// subset NAME of BASE where …
function subset(name, base, check, def) {
    const ty = new RType(name, [base]);
    ty.check = check; ty.isSubset = true; ty.defConstraint = def || 0;
    T[name] = ty;
    return ty;
}
const isaCore = isa;
function isaSubset(v, t) {
    if (!t.isSubset) return false;
    if (!typeOf(v).isa(t.parents[0]) && !(t.parents[0].isSubset && isaSubset(v, t.parents[0]))) return false;
    if (t.defConstraint === 1 && !defined(v)) return false;
    return t.check ? truthy(t.check(v)) : true;
}
// *@args flattening: one level of iterables
function slurpyFlat(items) { const out = []; for (const x of items) { if (x instanceof RList && x.ty !== T.Array || x instanceof RSeq || x instanceof RRange || x instanceof RSlip) out.push(...listItems(x)); else if (x instanceof RList) out.push(...x.a); else out.push(x); } return out; }
function vivArray(v) { return (v instanceof RType || v === undefined) ? mkArray([]) : v; }
function withOf(c, ty) { c.of = ty; return c; }
function isAny(v) { return v !== Mu && !(v instanceof RJunction); }
// a minimal Signature object: what .signature.count / .arity / .params answer
class RSig { constructor(f) { this.f = f; } }

T.Signature.methods.count = s => s.f.count !== undefined ? s.f.count : (s.f.arity !== undefined ? s.f.arity : s.f.length);
T.Signature.methods.arity = s => s.f.arity !== undefined ? s.f.arity : s.f.length;
T.Signature.methods.params = s => mkList([]);
T.Signature.methods.gist = s => '(' + Array.from({ length: T.Signature.methods.arity(s) }, (_, i) => '$' + String.fromCharCode(97 + i)).join(', ') + ')';
T.Signature.methods.Str = T.Signature.methods.gist;
T.Signature.methods.returns = s => T.Mu;
Object.assign(R, { vivArray, withOf, isAny, RSig, blk, wc, callCode, rwBox, throwCtl, xxThunk, namedFromHash, kvAdverb, pAdverb, listAppendAssign, mcSet, meta, coerce, dynGet, dynSet, approxEq, rangeIter, subset, slurpyFlat, factorial, isaSubset });
