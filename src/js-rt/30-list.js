// Collections: RList (Array/List/Slip), RSeq (lazy, memoizing), RRange, RHash,
// iteration, flattening, sorting, junctions.

class RList {
    constructor(a, ty) { this.a = a; this.ty = ty || T.List; this.src = null; this.item = false; }   // src: a lazy RSeq still feeding `a`; item: an item view (see item())
    reify(n) { const s = this.src; if (!s) return; while (this.a.length <= n) { if (!s.pull()) { this.src = null; return; } this.a.push(s.memo[s.memo.length - 1]); } }
    arr() { if (this.src) this.reify(Infinity); return this.a; }
    elems() { return this.arr().length; }
    gist() { return this.ty === T.Array ? listGistOf(this.a, '[', ']') : listGistOf(this.a, '(', ')'); }
    raku() {
        if (this.ty === T.Array) return '[' + this.a.map(x => raku(decont(x))).join(', ') + (this.a.length === 1 && this.a[0] instanceof RList ? ',' : '') + ']';   // [(1, 2),] — one list inside stays a list; slots render without their $
        const seq = this.ty === T.Seq ? '.Seq' : '';
        if (this.a.length === 1 && this.ty !== T.Slip) return '(' + raku(this.a[0]) + ',)' + seq;
        const body = this.a.map(raku).join(', ');
        return this.ty === T.Slip ? 'slip(' + body + ')' : '(' + body + ')' + seq;
    }
    *[Symbol.iterator]() { if (!this.src) { yield* this.a; return; } for (let i = 0; ; i++) { this.reify(i); if (i >= this.a.length) return; yield this.a[i]; } }
    // the Seq-shaped surface, so an eager Seq (an RList typed Seq) answers like a lazy one
    list() { return this.ty === T.Seq ? mkList(this.arr()) : this; }   // .list / .List of a Seq is a List
    isEmpty() { return this.a.length === 0; }
    at(i) { return this.a[i]; }
    get lazy() { return false; }
}
function mkList(a, ty) { return new RList(a, ty); }
function mkArray(a) { return new RList(a, T.Array); }
function mkSeq(a) { return new RList(a, T.Seq); }           // an eager Seq (a materialized result)
function mkSlip(a) { return new RSlip(a); }

// A lazy sequence over a JS iterator, memoized as it is pulled, so `.elems`,
// indexing and a second iteration all see the same elements.
class RSeq {
    constructor(iter, lazy) { this.it = iter; this.memo = []; this.done = false; this.lazy = !!lazy; }
    pull() {                                  // fetch one more into the cache; false at the end
        if (this.done) return false;
        const r = this.it.next();
        if (r.done) { this.done = true; this.it = null; return false; }
        this.memo.push(r.value);
        return true;
    }
    at(i) { while (this.memo.length <= i && this.pull()) {} return i < this.memo.length ? this.memo[i] : undefined; }
    arr() { while (this.pull()) {} return this.memo; }
    elems() { return this.arr().length; }
    isEmpty() { return this.memo.length === 0 && !this.pull(); }
    isLazy() { return this.lazy; }
    list() { return mkList(this.arr()); }
    cache() { return mkSeq(this.arr()); }
    gist() {
        if (this.lazy) {
            const parts = []; for (let i = 0; i < 100; i++) { const v = this.at(i); if (v === undefined) return '(' + parts.join(' ') + ')'; parts.push(gist(v)); }
            return '(' + parts.join(' ') + ' ...)';
        }
        return listGistOf(this.arr(), '(', ')');
    }
    raku() { if (this.lazy) return '(...).lazy'; return '(' + this.arr().map(raku).join(', ') + (this.arr().length === 1 ? ',' : '') + ').Seq'; }
    *[Symbol.iterator]() {
        let i = 0;
        for (;;) {
            if (i < this.memo.length) { yield this.memo[i++]; continue; }
            if (!this.pull()) return;
        }
    }
}
function seqOf(genFn, lazy) { return new RSeq(genFn(), lazy); }

class RRange {
    constructor(from, to, exFrom, exTo) { this.from = from; this.to = to; this.exFrom = !!exFrom; this.exTo = !!exTo; }
    isInfinite() { return this.to === Infinity || this.to instanceof RWhatever || (typeof this.to === 'number' && this.to === Infinity); }
    isNumeric() { return isNumeric(this.from) && (isNumeric(this.to) || this.to instanceof RWhatever); }
    isIntRange() { return isIntVal(this.from) && (isIntVal(this.to) || this.isInfinite()); }
    lo() { return this.exFrom ? add(this.from, 1) : this.from; }
    hi() { return this.exTo ? sub(this.to, 1) : this.to; }
    elemsOrInf() {
        if (this.isInfinite()) return Infinity;
        if (this.isNumeric()) {
            if (this.isIntRange()) { const n = Number(sub(this.hi(), this.lo())) + 1; return n < 0 ? 0 : n; }
            // 1.5..3: from, from+1, … while <= to
            let n = 0; let v = this.from; const last = this.to;
            const okEnd = this.exTo ? lt : le;
            if (this.exFrom) v = add(v, 1);
            while (okEnd(v, last)) { n++; v = add(v, 1); if (n > 1e7) break; }
            return n;
        }
        return this.strElems().length;
    }
    elems() { return this.elemsOrInf(); }
    strElems() {
        const a = str(this.from), b = str(this.to);
        const out = [];
        if (a === '' || b === '') return out;
        if (chars(a) === 1 && chars(b) === 1) {
            let lo = a.codePointAt(0), hi = b.codePointAt(0);
            if (this.exFrom) lo++;
            if (this.exTo) hi--;
            for (let c = lo; c <= hi; c++) out.push(String.fromCodePoint(c));
            return out;
        }
        let v = a; let n = 0;
        if (this.exFrom) v = strSucc(v);
        while (n++ < 100000) {
            if (v === b) { if (!this.exTo) out.push(v); break; }
            if (v.length > b.length) break;
            out.push(v); v = strSucc(v);
        }
        return out;
    }
    arr() {
        if (this.isInfinite()) throw new RakuError('Cannot materialize an infinite Range');
        if (this.isIntRange()) {
            const lo = this.lo(), hi = this.hi();
            if (typeof lo === 'number' && typeof hi === 'number') { const out = new Array(Math.max(0, hi - lo + 1)); for (let i = lo, k = 0; i <= hi; i++, k++) out[k] = i; return out; }
            const out = []; for (let v = lo; le(v, hi); v = add(v, 1)) out.push(v); return out;
        }
        if (this.isNumeric()) { const out = []; const okEnd = this.exTo ? lt : le; for (let v = this.lo(), n = 0; okEnd(v, this.to) && n < 1e8; v = add(v, 1), n++) out.push(v); return out; }
        return this.strElems();
    }
    *[Symbol.iterator]() {
        if (this.isIntRange()) {
            const lo = this.lo();
            if (this.isInfinite()) { for (let v = lo; ; v = add(v, 1)) yield v; }
            const hi = this.hi();
            if (typeof lo === 'number' && typeof hi === 'number') { for (let i = lo; i <= hi; i++) yield i; return; }
            for (let v = lo; le(v, hi); v = add(v, 1)) yield v;
            return;
        }
        if (this.isNumeric()) {
            const okEnd = this.exTo ? lt : le;
            if (this.isInfinite()) { for (let v = this.lo(); ; v = add(v, 1)) yield v; }
            for (let v = this.lo(); okEnd(v, this.to); v = add(v, 1)) yield v;
            return;
        }
        yield* this.strElems();
    }
    Str() {
        if (this.isInfinite() || (isNumeric(this.from) && toFloat(this.from) === -Infinity)) {
            const side = v => (v instanceof RWhatever || (typeof v === 'number' && !Number.isFinite(v))) ? '*' : str(v);
            return side(this.from) + (this.exFrom ? '^' : '') + '..' + (this.exTo ? '^' : '') + side(this.to);
        }
        return this.arr().map(str).join(' ');
    }
    // `^5` however it was written; other endpoints render as .raku (0.5..<1/3>, "a".."z")
    gist() { if (this.from === 0 && this.exTo && !this.exFrom) return '^' + this.endStr(this.to); return this.endStr(this.from) + (this.exFrom ? '^' : '') + '..' + (this.exTo ? '^' : '') + this.endStr(this.to); }
    endStr(v) { return v instanceof RWhatever ? 'Inf' : raku(v); }
    raku() { if (this.from === 0 && !this.exFrom && this.exTo && (typeof this.to === 'number' && Number.isInteger(this.to) || typeof this.to === 'bigint')) return '^' + raku(this.to); return raku(this.from) + (this.exFrom ? '^' : '') + '..' + (this.exTo ? '^' : '') + raku(this.to); }
    contains(v) {
        if (!isNumeric(v) && !(typeof v === 'string' && !this.isNumeric())) return false;
        if (this.isNumeric()) {
            if (!isNumeric(v)) { try { v = toNumeric(v); } catch (e) { return false; } }
            const loOk = this.exFrom ? gt(v, this.from) : ge(v, this.from);
            if (!loOk) return false;
            if (this.isInfinite()) return true;
            return this.exTo ? lt(v, this.to) : le(v, this.to);
        }
        const s = str(v), a = str(this.from), b = str(this.to);
        return (this.exFrom ? s > a : s >= a) && (this.exTo ? s < b : s <= b);
    }
    min() { return this.from; }
    max() { return this.to instanceof RWhatever ? Infinity : this.to; }
    reverse() { return mkSeq(this.arr().reverse()); }
}
function rangeEnd(v) { return (v instanceof RList || v instanceof RSeq || v instanceof RHash || v instanceof RSetty || v instanceof RRange) ? elemsOf(v) : (v instanceof RNum ? v : v); }
function range(from, to, exFrom, exTo) {
    from = rangeEnd(from); to = rangeEnd(to);
    if (typeof from === 'string' && isNumeric(to)) from = strToNumeric(from);
    else if (typeof to === 'string' && isNumeric(from)) to = strToNumeric(to);
    return new RRange(from, to, exFrom, exTo);
}
function upto(n) { const e = rangeEnd(n); return new RRange(0, typeof e === 'string' ? strToNumeric(e) : e, false, true); }           // ^n

// Hash over a Map; keys are Str, insertion-ordered like the native engine.
class RHash {
    constructor(m, ty) { this.m = m || new Map(); this.ty = ty || T.Hash; this.item = false; }
    get(k) { const v = this.m.get(k); return v === undefined ? Any : v; }
    set(k, v) { this.m.set(k, v); return v; }
    elems() { return this.m.size; }
    keys() { return Array.from(this.m.keys()); }
    values() { return Array.from(this.m.values()); }
    pairs() { const o = []; for (const [k, v] of this.m) o.push(new RPair(k, v)); return o; }
    sortedPairs() { return this.pairs().sort((a, b) => strCmp(a.k, b.k)); }
    gist() {
        const ps = this.sortedPairs(); const parts = [];
        for (let i = 0; i < ps.length && i < 100; i++) parts.push(pairGist(ps[i]));
        if (ps.length > 100) parts.push('...');
        return '{' + parts.join(', ') + '}';
    }
    raku() {   // a hash's values live in scalar containers: an Array or Hash value renders as $[…] / ${…}
        const ps = this.sortedPairs();
        return '{' + ps.map(p => ((p.v instanceof RList && p.v.ty !== T.Slip) || p.v instanceof RHash) ? pairRaku(new RPair(p.k, new RakuItem(p.v))) : pairRaku(p)).join(', ') + '}';
    }
    [Symbol.iterator]() { return this.pairs()[Symbol.iterator](); }
}
function mkHash(m) { return new RHash(m); }
function hashKey(k) {
    if (typeof k === 'string') return k;
    if (k instanceof RList || k instanceof RSeq) return k.arr().map(str).join(' ');
    return str(k);
}
// %h{k}
function hget(h, k) {
    if (h === Nil) return Nil;   // Nil<k> is Nil
    if (h instanceof RHash) { if (typeof k !== 'string') { if (k instanceof RList || k instanceof RSeq || k instanceof RRange) return hslice(h, k); k = hashKey(k); } const v = h.m.get(k); return v === undefined ? (h.dflt === undefined ? Any : h.dflt) : v; }
    if (h instanceof RSetty) return h.get(k);
    if (h instanceof RPair) return str(k) === str(h.k) ? h.v : Nil;
    if (h instanceof RObj) { const m = h.ty.findUser('AT-KEY'); if (m) return m(h, k); throw new RakuError(`Type ${h.ty.name} does not support associative indexing`); }
    if (h instanceof RType) return Any;
    if (h instanceof RList) { if (typeof k === 'string' && !/^\s*[+-]?\d/.test(k)) throw new RakuError(`Type ${h.ty.name} does not support associative indexing.`); return aget(h, k); }
    if (h instanceof RCapture) return h.named.get(str(k)) ?? Any;
    if (h instanceof RMatch) return h.name(str(k));
    if (h instanceof RJsObj) return jsGet(h, k);
    throw new RakuError(`Type ${typeName(h)} does not support associative indexing.`);
}
function hset(h, k, v) {
    if (h instanceof RHash) {
        if (v instanceof RSlip) v = mkArray(v.a.slice());
        if (v === Nil || h.of) v = checkOf(h, v);
        if (typeof k !== 'string') {
            if (k instanceof RList || k instanceof RSeq) { const ks = k.arr(); const vs = arr(v); ks.forEach((kk, i) => h.m.set(hashKey(kk), vs[i] === undefined ? Any : vs[i])); return v; }
            k = hashKey(k);
        }
        h.m.set(k, v); return v;
    }
    if (h instanceof RSetty) return h.set(k, v);
    if (h instanceof RJsObj) return jsSet(h, k, v);
    if (h instanceof RObj) { const m = h.ty.findUser('ASSIGN-KEY'); if (m) return m(h, k, v); const at = h.ty.findUser('AT-KEY'); if (at) { const c = at(h, k); if (c instanceof RScalar) { c.v = v; return v; } } throw new RakuError(`Type ${h.ty.name} does not support associative indexing`); }
    throw new RakuError(`Cannot modify an immutable ${typeName(h)}`);
}
function hexists(h, k) { if (h instanceof RHash) return h.m.has(hashKey(k)); if (h instanceof RSetty) return h.m.has(whichKey(k)); if (h instanceof RObj) { const m = h.ty.findUser('EXISTS-KEY'); if (m) return truthy(m(h, k)); } if (h instanceof RPair) return str(k) === str(h.k); return false; }
function hdelete(h, k) { if (h instanceof RHash) { const kk = hashKey(k); const v = h.m.get(kk); h.m.delete(kk); return v === undefined ? Any : v; } if (h instanceof RSetty) return h.delete(k); if (h instanceof RObj) { const m = h.ty.findUser('DELETE-KEY'); if (m) return m(h, k); } return Any; }
function hslice(h, ks) { return mkList(arr(ks).map(k => hget(h, k))); }
// autovivify %h{k} as a container of the given sigil, then return it
function hviv(h, k, sigil) {
    const kk = hashKey(k);
    if (h instanceof RHash) {
        let v = h.m.get(kk);
        if (v === undefined || v instanceof RType) { v = sigil === '%' ? mkHash() : mkArray([]); h.m.set(kk, v); }
        return v;
    }
    return hget(h, k);
}
function aviv(a, i, sigil) {
    if (a instanceof RList) {
        const idx = Number(toInt(i));
        let v = a.a[idx];
        if (v === undefined || v instanceof RType) { v = sigil === '%' ? mkHash() : mkArray([]); while (a.a.length < idx) a.a.push(Any); a.a[idx] = v; }
        return v;
    }
    return aget(a, i);
}
function assignHash(h, src) {
    const m = new Map();
    const items = src instanceof RHash ? src.pairs() : src instanceof RType ? [] : listItems(src);
    for (let i = 0; i < items.length; i++) {
        const it = items[i];
        if (it instanceof RPair) m.set(hashKey(it.k), it.v);
        else if (it instanceof RHash) for (const [k, v] of it.m) m.set(k, v);
        else if (it instanceof RSetty) for (const [k, e] of it.m) m.set(str(e.v), e.n);
        else { const v = items[++i]; if (v === undefined) throw new RakuError('Odd number of elements found where hash initializer expected', 'X::Hash::Store::OddNumber'); m.set(hashKey(it), v instanceof RSlip ? mkArray(v.a) : v); }
    }
    h.m = m;
    return h;
}
function hashLit(items) { return assignHash(new RHash(), mkList(items)); }
function hashFrom(pairs) { const h = new RHash(); for (const [k, v] of pairs) h.m.set(k, v); return h; }

// Pairs
function pair(k, v) { return new RPair(k, v); }
function pairKey(p) { return p.k; }
function pairValue(p) { return p.v; }

// --- flattening and iteration ---------------------------------------------
// The items a value contributes in list context (the single-argument rule).
function objItems(v) { const m = v.ty.findUser('iterator'); if (!m) return null; const it = m(v); return Array.from(it && typeof it.next === 'function' ? { [Symbol.iterator]() { return it; } } : iter(it)); }
// An item view: a List or Hash as it sits in a scalar container — ONE element in
// list context (`for $x`, `my @a = $x`), `$`-prefixed by .raku, a leaf to `flat`
// and `»` — sharing the container's storage: `a`/`m` read through to the
// original, so `@a = …` after `my $x = @a` reaches $x, as in Raku. Method calls
// decont (mc), so the view is invisible to everything else. A lazy array is
// reified first (the view cannot share a half-pulled source).
function item(v) {
    if (v === null || typeof v !== 'object' || v.item === true) return v;
    if (v instanceof RList) {
        if (v.src) v.arr();
        const w = new RList(v.a, v.ty); Object.assign(w, v);
        Object.defineProperty(w, 'a', { get() { return v.a; }, set(x) { v.a = x; } });
        Object.defineProperty(w, 'src', { get() { return v.src; }, set(x) { v.src = x; } });
        w.item = true; w.__t = v; return w;
    }
    if (v instanceof RHash) {
        const w = new RHash(v.m, v.ty); Object.assign(w, v);
        Object.defineProperty(w, 'm', { get() { return v.m; }, set(x) { v.m = x; } });
        w.item = true; w.__t = v; return w;
    }
    return v;
}
function decont(v) { return v !== null && typeof v === 'object' && v.item === true ? v.__t : v; }
// a bound `@` parameter: a List stays a List (its slots are bare), a Seq or Range binds as the List it is
function bindArray(v) { return v instanceof RList ? (v.ty === T.Seq ? mkList(v.arr()) : v) : (v instanceof RSeq || v instanceof RRange) ? mkList(arr(v)) : newArray(v); }
// `for @a { … }`: an Array's slots are scalar containers, so a list or hash
// element reaches the bare topic as an item; a List's elements arrive bare
function iterTopic(v) {
    if (v instanceof RList && v.ty === T.Array && v.item !== true) {
        const a = v.a;
        for (let i = 0; i < a.length; i++) { const x = a[i]; if (x !== null && typeof x === 'object' && x.item !== true && (x instanceof RList || x instanceof RHash)) return a.map(y => item(y)); }
        return a;
    }
    return iter(v);
}
function listItems(v) {
    if (v instanceof RList) return v.a;
    if (v instanceof RJsObj) return Array.from(v.v == null ? [] : v.v, fromJs);
    if (v instanceof RObj) { const items = objItems(v); if (items) return items; }
    if (Array.isArray(v)) return v;                   // already an item list (the emitter's itemized form)
    if (v instanceof RSeq) return v.arr();
    if (v instanceof RRange) return v.arr();
    if (v instanceof RSlip) return v.a;
    if (v instanceof RHash) return v.pairs();
    if (v instanceof RSetty) return v.listItems();
    if (v === Nil) return [];
    if (v instanceof RJunction) return [v];
    if (v instanceof RCapture) return v.pos;
    return [v];
}
// A JS array of items from anything iterable-ish, for the runtime's own loops.
function arr(v) {
    if (v instanceof RList) return v.a;
    if (Array.isArray(v)) return v;
    return listItems(v);
}
// `for` iteration: one pass, lazily where the source is lazy.
function iter(v) {
    if (v instanceof RList) return v.item === true ? [v] : v.a;
    if (v instanceof RJsObj) return Array.from(v.v == null ? [] : v.v, fromJs);
    if (v instanceof RObj) { const items = objItems(v); if (items) return items; }
    if (Array.isArray(v)) return v;
    if (v instanceof RSeq) return v;
    if (v instanceof RRange) return v;
    if (v instanceof RHash) return v.item === true ? [v] : v.pairs();
    if (v instanceof RSlip) return v.a;
    if (v instanceof RSetty) return v.listItems();
    if (v === Nil) return [];
    if (v instanceof RCapture) return v.pos;
    return [v];
}
// `for LIST -> $a, $b` : groups of n
function* iterN(v, n) {
    let buf = [];
    for (const x of iter(v)) { buf.push(x); if (buf.length === n) { yield buf; buf = []; } }
    if (buf.length) { while (buf.length < n) buf.push(Any); yield buf; }
}
// Build a List from comma-separated items: Slips flatten, nothing else does.
function list(...items) { return mkList(spliceSlips(items)); }
function spliceSlips(items) {
    let has = false;
    for (let i = 0; i < items.length; i++) if (items[i] instanceof RSlip) { has = true; break; }
    if (!has) return items;
    const out = [];
    for (const it of items) if (it instanceof RSlip) out.push(...it.a); else out.push(it);
    return out;
}
// `[ ... ]`: `single` is the one-argument form [@a] / [1..3] (its elements), else the items
function arrayLit(items, single) {
    if (single) { const it = items[0]; return mkArray(itemsOf(it).slice()); }
    return mkArray(spliceSlips(items));
}
// The single-argument rule: an iterable's elements, an itemized value alone.
function itemsOf(v) {
    if (v !== null && typeof v === 'object' && v.item === true) return [v];
    if (v instanceof RList || v instanceof RSeq || v instanceof RRange || v instanceof RSlip || v instanceof RHash || v instanceof RSetty) return listItems(v);
    if (v instanceof RObj) { const items = objItems(v); if (items) return items; }
    if (Array.isArray(v)) return v;
    if (v === Nil) return [];
    return [v];
}
// `my @a = ...` / `@a = ...` : replace the container's contents
// a typed container (`my Int @a`, `has Str @.d`) checks what enters it; Nil restores the default
function checkOf(a, v) { if (v === Nil) return a.dflt !== undefined ? a.dflt : (a.of ? a.of : Any); if (a.of && !(v instanceof RType ? v.isa(a.of) : isa(v, a.of))) throw new RakuError(`Type check failed in assignment to ${a instanceof RHash ? '%' : '@'}container; expected ${a.of.name} but got ${typeName(v)} (${raku(v)})`, 'X::TypeCheck::Assignment'); return v; }
function assignArray(a, src) {
    if (src instanceof RSeq && src.lazy) { a.a = []; a.src = src; return a; }
    const items = src === Nil ? [Nil] : itemsOf(src);   // `my @a = Nil` is one (default) element
    const copy = items === a.a ? items.slice() : items.slice();
    for (let i = 0; i < copy.length; i++) { const x = copy[i]; if (x instanceof RSlip) copy[i] = mkArray(x.a.slice()); else if (a.of || x === Nil) copy[i] = checkOf(a, x); }   // an item view assigned in stays one: `my @b = $x, 3; @b.List.raku` shows the $
    a.a = copy;
    return a;
}
function newArray(src) { return assignArray(mkArray([]), src === undefined ? [] : src); }
function newHash(src) { return src === undefined ? new RHash() : assignHash(new RHash(), src); }
// flat: recursive flattening of what is not itemized. An Array's slots are
// scalar containers, so they are pushed as they are (`flat [[1,2],[3,4]]` is two
// items, `flat ((1,2),(3,4))` four); a List's elements descend unless they are items.
function flat(v) {
    if (v !== null && typeof v === 'object' && v.item === true) return mkSeq([v]);
    const out = [];
    const walk = (x, slot) => {
        if (slot) out.push(item(x));   // an Array slot is a container: its list or hash stays an item
        else if (x !== null && typeof x === 'object' && x.item === true) out.push(x);
        else if (x instanceof RList) { const isArr = x.ty === T.Array; for (const y of x.arr()) walk(y, isArr); }
        else if (x instanceof RSeq || x instanceof RRange || x instanceof RSlip) for (const y of iter(x)) walk(y, false);
        else out.push(x);
    };
    if (v instanceof RList || v instanceof RSeq || v instanceof RRange || v instanceof RSlip) walk(v, false);
    else for (const x of iter(v)) walk(x, false);   // a Hash's pairs, an object's own iterator
    return mkSeq(out);
}
function slip(v) { return new RSlip(itemsOf(decont(v)).slice()); }      // |$x slips the container's contents
function spreadArgs(v) { if (v instanceof RCapture) return v.named.size ? v.pos.concat([new RNamed(new Map(v.named))]) : v.pos.slice(); return itemsOf(decont(v)); }      // f(|@a), f(|c)

// --- indexing ------------------------------------------------------------
function aget(a, i) {
    if (a === Nil) return (i instanceof RList || i instanceof RSeq || i instanceof RRange) ? mkList(arr(i).map(() => Nil)) : Nil;   // Nil[0] is Nil, Nil[0,1] is (Nil Nil)
    if (a instanceof RMatch) {   // $m[0], $m[0,1], $m[0..1], $m[*]
        if (typeof i === 'number') return a.pos(i);
        if (i instanceof RWhatever || typeof i === 'function') return matchList(a);
        if (i instanceof RList || i instanceof RSeq || i instanceof RRange) return mkList(arr(i).map(j => a.pos(Number(toInt(j)))));
    }
    if (a instanceof RList) {
        if (typeof i === 'number' && i < 0) throw new RakuError(`Index out of range. Is: ${i}, should be in 0..^Inf`, 'X::OutOfRange');
        if (typeof i === 'number') { if (a.src) a.reify(i); const v = a.a[i]; return v === undefined ? (a.dflt === undefined ? Any : a.dflt) : v; }
        if (typeof i === 'function') return aget(a, i(a.a.length));
        if (i instanceof RList || i instanceof RSeq || i instanceof RRange) return aslice(a, i);
        if (i instanceof RWhatever) return mkList(a.a.slice());
        const k = Number(toInt(i)); if (k < 0) throw new RakuError(`Unsupported use of a negative ${k} subscript to index from the end. In Raku please use: a function such as *-1`);
        const v = a.a[k]; return v === undefined ? Any : v;
    }
    if (a instanceof RSeq) { if (typeof i === 'function') return aget(a.list(), i); if (i instanceof RList || i instanceof RSeq || i instanceof RRange) return aslice(a, i); const v = a.at(Number(toInt(i))); return v === undefined ? Any : v; }
    if (a instanceof RRange) { if (typeof i === 'function') return aget(mkList(a.arr()), i); if (i instanceof RList || i instanceof RSeq || i instanceof RRange) return aslice(a, i); const k = Number(toInt(i)); if (a.isIntRange()) { const lo = a.lo(); const v = add(lo, k); return a.isInfinite() || le(v, a.hi()) ? v : Any; } const v = a.arr()[k]; return v === undefined ? Any : v; }
    if (a instanceof RHash) return hget(a, i);
    if (a instanceof RObj) { const m = a.ty.findUser('AT-POS'); if (m) return m(a, i); throw new RakuError(`Type ${a.ty.name} does not support positional indexing`); }
    if (a instanceof RType) return Any;
    if (a instanceof RPair) return Number(toInt(i)) === 0 ? a : Any;
    if (a instanceof RMatch) return a.pos(Number(toInt(i)));
    if (a instanceof RCapture) { const v = a.pos[Number(toInt(i))]; return v === undefined ? Any : v; }
    if (a instanceof RSetty) return a.listItems()[Number(toInt(i))] ?? Any;
    if (a instanceof RJsObj) return jsGet(a, Number(toInt(i)));
    // a scalar indexed as a one-element list
    if (typeof i === 'function') i = i(1);
    return Number(toInt(i)) === 0 ? a : Any;
}
function aset(a, i, v) {
    if (a instanceof RList) {
        if (a.ty !== T.Array) throw new RakuError(`Cannot modify an immutable List`);
        if (typeof i === 'number' && i < 0) throw new RakuError(`Index out of range. Is: ${i}, should be in 0..^Inf`, 'X::OutOfRange');
        v = checkOf(a, v);   // Nil restores the default; a typed container checks the value
        if (typeof i === 'function') i = i(a.a.length);
        if (i instanceof RList || i instanceof RSeq || i instanceof RRange) { const is = arr(i), vs = arr(v); is.forEach((ix, k) => aset(a, ix, vs[k] === undefined ? Any : vs[k])); return v; }
        const k = Number(toInt(i));
        if (k < 0) throw new RakuError(`Unsupported use of a negative ${k} subscript to index from the end. In Raku please use: a function such as *-1`);
        while (a.a.length < k) a.a.push(Any);
        if (v instanceof RSlip) v = mkArray(v.a.slice());
        a.a[k] = v; return v;
    }
    if (a instanceof RHash) return hset(a, i, v);
    if (a instanceof RJsObj) return jsSet(a, Number(toInt(i)), v);
    if (a instanceof RObj) { const m = a.ty.findUser('ASSIGN-POS'); if (m) return m(a, i, v); const at = a.ty.findUser('AT-POS'); if (at) { const c = at(a, i); if (c instanceof RScalar) { c.v = v; return v; } } throw new RakuError(`Type ${a.ty.name} does not support positional indexing`); }
    throw new RakuError(`Cannot modify an immutable ${typeName(a)}`);
}
function aslice(a, idxs) {
    // the element count is only needed for `*-1` style and open-ended indices, and forcing it would reify a lazy array
    const n = () => a instanceof RList ? a.elems() : a instanceof RSeq ? a.elems() : a.arr().length;
    if (idxs instanceof RRange && (typeof idxs.from === 'function' || typeof idxs.to === 'function' || idxs.to instanceof RWhatever || idxs.isInfinite())) {
        const cnt = n();
        const lo = typeof idxs.from === 'function' ? idxs.from(cnt) : idxs.from;
        const hi = typeof idxs.to === 'function' ? idxs.to(cnt) : (idxs.to instanceof RWhatever || idxs.to === Infinity) ? cnt - 1 : idxs.to;
        idxs = range(lo, hi, idxs.exFrom, idxs.exTo && !(idxs.to instanceof RWhatever));   /* @a[1..*] stops at the end */
    }
    const out = []; for (const i of iter(idxs)) { if (i instanceof RWhatever) { out.push(...arr(a)); continue; } const k = typeof i === 'function' ? i(n()) : i; out.push(aget(a, k)); } return mkList(out); }
function aexists(a, i) { if (a instanceof RList) { const k = typeof i === 'function' ? i(a.a.length) : Number(toInt(i)); return k >= 0 && k < a.a.length && a.a[k] !== undefined; } if (a instanceof RHash) return hexists(a, i); if (a instanceof RObj) { const m = a.ty.findUser('EXISTS-POS'); if (m) return truthy(m(a, i)); } return false; }
function adelete(a, i) { if (a instanceof RList) { const k = typeof i === 'function' ? i(a.a.length) : Number(toInt(i)); const v = a.a[k]; if (k === a.a.length - 1) a.a.pop(); else if (k < a.a.length) a.a[k] = Any; return v === undefined ? Any : v; } if (a instanceof RHash) return hdelete(a, i); return Any; }
function elemsOf(v) {
    if (v instanceof RList) return v.a.length;
    if (v instanceof RSeq) return v.elems();
    if (v instanceof RHash) return v.m.size;
    if (v instanceof RRange) return v.elemsOrInf();
    if (v instanceof RSetty) return v.m.size;
    if (v instanceof RSlip) return v.a.length;
    if (v === Nil) return 0;
    if (v instanceof RObj) { const m = v.ty.findUser('elems'); if (m) return m(v); return 1; }
    if (v instanceof RCapture) return v.pos.length;
    return 1;
}

// --- list operations -------------------------------------------------------
function callBlock(f, ...args) { return f(...args); }
function mapList(v, f) {
    const src = iter(v);
    const arity = f.arity !== undefined ? f.arity : f.length;
    const lazy = v instanceof RSeq ? v.lazy : v instanceof RRange ? v.isInfinite() : false;
    return new RSeq((function* () {
        if (arity <= 1) {
            for (const x of src) { const r = f(x); if (r instanceof RSlip) yield* r.a; else yield r; }
        } else {
            let buf = [];
            for (const x of src) { buf.push(x); if (buf.length === arity) { const r = f(...buf); buf = []; if (r instanceof RSlip) yield* r.a; else yield r; } }
            if (buf.length) { const r = f(...buf); if (r instanceof RSlip) yield* r.a; else yield r; }
        }
    })(), lazy);
}
function grepList(v, f) {
    const src = iter(v);
    const lazy = v instanceof RSeq ? v.lazy : v instanceof RRange ? v.isInfinite() : false;
    const test = matcherOf(f);
    return new RSeq((function* () { for (const x of src) if (test(x)) yield x; })(), lazy);
}
// The predicate a smartmatch-taking method uses: a Code is called, anything else is ~~'d
function matcherOf(f) {
    if (typeof f === 'function') return x => truthy(f(x));
    return x => truthy(smartmatch(x, f));
}
function firstOf(v, f, named) {
    const test = f === undefined ? (() => true) : matcherOf(f);
    const end = named && truthy(named.get('end'));
    const wantK = named && truthy(named.get('k')), wantKv = named && truthy(named.get('kv')), wantP = named && truthy(named.get('p'));
    const items = end ? arr(v).slice().reverse() : iter(v);
    let i = end ? arr(v).length : -1;
    for (const x of items) { i += end ? -1 : 1; if (test(x)) return wantK ? i : wantKv ? mkList([i, x]) : wantP ? pair(i, x) : x; }
    return Nil;
}
function joinList(v, sep) { const out = []; for (const x of iter(v)) out.push(str(x)); return out.join(sep === undefined ? '' : str(sep)); }
function reverseList(v) { return mkSeq(arr(v).slice().reverse()); }
function sumList(v) { let s = 0; for (const x of iter(v)) s = (x instanceof RJunction) ? junctionOp(j => add(s, j), x, null) : (s instanceof RJunction) ? junctionOp(j => add(j, x), s, null) : add(s, x); return s; }   // a junction element autothreads
function orderNum(r) {
    if (r instanceof REnum) return r.val;
    if (typeof r === 'number') return r;
    if (typeof r === 'boolean') return r ? 1 : 0;
    if (typeof r === 'bigint') return r < 0n ? -1 : r > 0n ? 1 : 0;
    return toFloat(r);
}
function sortList(v, f) {
    const items = arr(v).slice();
    if (f === undefined || f === null) return mkSeq(items.sort(cmpNum));
    const arity = f.arity !== undefined ? f.arity : f.length;
    if (arity >= 2) return mkSeq(items.sort((a, b) => orderNum(f(a, b))));
    const keyed = items.map(x => [f(x), x]);
    keyed.sort((a, b) => cmpNum(a[0], b[0]));
    return mkSeq(keyed.map(p => p[1]));
}
// min/max skip undefined values, as the interpreter does
// .min/.max with :k / :v / :kv / :p answer the position, the value, both or the pair
function minMaxAdv(v, f, isMax, named) {
    if (f === undefined && named.has('by')) f = named.get('by');   // the :by spelling of the mapper
    const a = arr(v); let bi = -1, bk, ties = [];   // every position attaining the extremum, in order
    a.forEach((x, i) => { if (x instanceof RType) return; const k = f ? f(x) : x; const c = bi < 0 ? -1 : (isMax ? -cmpNum(k, bk) : cmpNum(k, bk)); if (c < 0) { bi = i; bk = k; ties = [i]; } else if (c === 0) ties.push(i); });
    const has = (n) => truthy(named.get(n) ?? false);
    if (has('k')) return mkList(ties);
    if (has('kv')) return mkList(ties.flatMap(i => [i, a[i]]));
    if (has('v')) return mkList(ties.map(i => a[i]));
    if (has('p')) return mkList(ties.map(i => new RPair(i, a[i])));
    return bi < 0 ? (isMax ? -Infinity : Infinity) : a[bi];
}
function minOf(v, f) { let best; for (const x of iter(v)) { if (x instanceof RType) continue; if (best === undefined || (f ? cmpNum(f(x), f(best)) < 0 : cmpNum(x, best) < 0)) best = x; } return best === undefined ? Infinity : best; }
function maxOf(v, f) { let best; for (const x of iter(v)) { if (x instanceof RType) continue; if (best === undefined || (f ? cmpNum(f(x), f(best)) > 0 : cmpNum(x, best) > 0)) best = x; } return best === undefined ? -Infinity : best; }
function minmax(v) { const a = arr(v); if (!a.length) return range(Infinity, -Infinity); return range(minOf(a), maxOf(a)); }
function uniqueList(v, f) {
    const seen = new Set(), out = [];
    for (const x of iter(v)) { const k = whichKey(f ? f(x) : x); if (!seen.has(k)) { seen.add(k); out.push(x); } }
    return mkSeq(out);
}
function squishList(v) { const out = []; let prev; for (const x of iter(v)) { if (out.length && eqv(x, prev)) continue; out.push(x); prev = x; } return mkSeq(out); }
function headOf(v, n) {
    if (n === undefined) { const it = iter(v)[Symbol.iterator](); const r = it.next(); return r.done ? Nil : r.value; }
    if (typeof n === 'function') n = n(elemsOf(v));
    if (n instanceof RWhatever) return mkSeq(arr(v).slice());
    const k = Number(toInt(n)); const out = []; if (k <= 0) return mkSeq(out);
    for (const x of iter(v)) { out.push(x); if (out.length >= k) break; }
    return mkSeq(out);
}
function tailOf(v, n) {
    const a = arr(v);
    if (n === undefined) return a.length ? a[a.length - 1] : Nil;
    if (typeof n === 'function') n = n(a.length);
    const k = Number(toInt(n)); return mkSeq(k <= 0 ? [] : a.slice(-k));
}
function keysOf(v) {
    if (v instanceof RType) return mkSeq([]);
    if (v instanceof RHash) return mkSeq(v.keys());
    if (v instanceof RPair) return mkSeq([v.k]);
    if (v instanceof RSetty) return mkSeq(v.keysList());
    if (v instanceof RObj) { const m = v.ty.findUser('keys'); if (m) return m(v); }
    const n = elemsOf(v); return mkSeq(Array.from({ length: n }, (_, i) => i));
}
function valuesOf(v) {
    if (v instanceof RType) return mkSeq([]);
    if (v instanceof RHash) return mkSeq(v.values());
    if (v instanceof RPair) return mkSeq([v.v]);
    if (v instanceof RSetty) return mkSeq(v.valuesList());
    return mkSeq(arr(v).slice());
}
function kvOf(v) {
    if (v instanceof RType) return mkSeq([]);
    if (v instanceof RHash) { const o = []; for (const [k, x] of v.m) o.push(k, x); return mkSeq(o); }
    if (v instanceof RPair) return mkSeq([v.k, v.v]);
    if (v instanceof RSetty) { const o = []; for (const p of v.pairsList()) o.push(p.k, p.v); return mkSeq(o); }
    const o = []; let i = 0; for (const x of iter(v)) o.push(i++, x); return mkSeq(o);
}
function pairsOf(v) {
    if (v instanceof RType) return mkSeq([]);
    if (v instanceof RHash) return mkSeq(v.pairs());
    if (v instanceof RPair) return mkSeq([v]);
    if (v instanceof RSetty) return mkSeq(v.pairsList());
    const o = []; let i = 0; for (const x of iter(v)) o.push(new RPair(i++, x)); return mkSeq(o);
}
function antipairsOf(v) { return mkSeq(arr(pairsOf(v)).map(p => new RPair(p.v, p.k))); }
function invertOf(v) { const o = []; for (const p of arr(pairsOf(v))) { for (const x of (p.v instanceof RList ? p.v.a : itemsOf(p.v))) o.push(new RPair(x, p.k)); } return mkSeq(o); }   // an Iterable value gives one pair per element
function pushTo(a, ...items) {
    items = items.filter(x => !(x instanceof RNamed));   // a named argument contributes nothing
    if (a instanceof RList && a.ty === T.Array) items = items.map(x => (x === Nil || (a.of && !(x instanceof RSlip) && !(x instanceof RList))) ? checkOf(a, x) : x);
    if (a instanceof RHash) {
        const flat = []; for (const it of items) { if (it instanceof RSlip || it instanceof RList) flat.push(...itemsOf(it)); else flat.push(it); }
        for (let i = 0; i < flat.length; i++) {
            const it = flat[i]; let k, v;
            if (it instanceof RPair) { k = it.k; v = it.v; } else if (it instanceof RHash) { for (const [kk, vv] of it.m) pushTo(a, pair(kk, vv)); continue; }
            else { k = it; v = flat[++i]; if (v === undefined) throw new RakuError('Odd number of elements found where hash initializer expected', 'X::Hash::Store::OddNumber'); }
            const kk = hashKey(k);
            if (a.m.has(kk)) { const cur = a.m.get(kk); if (cur instanceof RList && cur.ty === T.Array) cur.a.push(v); else a.m.set(kk, mkArray([cur, v])); } else a.m.set(kk, v);
        }
        return a;
    }
    if (a instanceof RObj) { const m = a.ty.findUser('push'); if (m) return m(a, ...items); }
    if (!(a instanceof RList)) throw new RakuError(`Cannot call push on a ${typeName(a)}`);
    for (const it of items) { if (it instanceof RSlip) a.a.push(...it.a); else a.a.push(it); }
    return a;
}
function appendTo(a, ...items) { if (a instanceof RHash) return pushTo(a, ...items); if (a instanceof RList && a.ty === T.Array) items = items.map(x => (x === Nil || (a.of && !(x instanceof RSlip) && !(x instanceof RList))) ? checkOf(a, x) : x); for (const it of items) { if (it instanceof RNamed) continue; a.a.push(...itemsOf(it)); } return a; }
function unshiftTo(a, ...items) { if (a instanceof RList && a.ty === T.Array) items = items.map(x => (x === Nil || (a.of && !(x instanceof RSlip) && !(x instanceof RList))) ? checkOf(a, x) : x); const add = []; for (const it of items) { if (it instanceof RSlip) add.push(...it.a); else add.push(it); } a.a.unshift(...add); return a; }
function prependTo(a, ...items) { if (a instanceof RList && a.ty === T.Array) items = items.map(x => (x === Nil || (a.of && !(x instanceof RSlip) && !(x instanceof RList))) ? checkOf(a, x) : x); const add = []; for (const it of items) add.push(...itemsOf(it)); a.a.unshift(...add); return a; }
function popFrom(a) { if (a instanceof RObj) { const m = a.ty.findUser('pop'); if (m) return m(a); } if (!(a instanceof RList) || !a.a.length) return failure(new RakuError('Cannot pop from an empty Array', 'X::Cannot::Empty')); return a.a.pop(); }
function shiftFrom(a) { if (a instanceof RObj) { const m = a.ty.findUser('shift'); if (m) return m(a); } if (!(a instanceof RList) || !a.a.length) return failure(new RakuError('Cannot shift from an empty Array', 'X::Cannot::Empty')); return a.a.shift(); }
function spliceArr(a, from, n, ...replArgs) {
    const repl = replArgs.length === 0 ? undefined : replArgs.length === 1 ? (replArgs[0] instanceof RHash ? mkList([replArgs[0]]) : replArgs[0]) : mkList(spliceSlips(replArgs.map(x => x instanceof RHash ? mkList([x]) : x)));   // a bare %h is ONE element
    const f = from === undefined ? 0 : Number(typeof from === 'function' ? from(a.a.length) : toInt(from));
    const k = n === undefined ? a.a.length - f : Number(typeof n === 'function' ? n(a.a.length - f) : toInt(n));   // a `*` count is relative to what is left
    const ins = repl === undefined ? [] : itemsOf(repl);
    if (a.of) for (const x of ins) if (!(x instanceof RType ? x.isa(a.of) : isa(x, a.of))) throw new RakuError(`Type check failed in splice; expected ${a.of.name} but got ${typeName(x)} (${raku(x)})`, 'X::TypeCheck::Splice');
    const removed = a.a.splice(f, k, ...ins);
    return mkArray(removed);
}
function reduceList(f, v) {
    if (v instanceof RType) return Nil;
    if (f.rightAssoc) return reduceOp(f.opName, v);   // .reduce(&[**]) folds from the right
    const it = iter(v)[Symbol.iterator]();
    let r = it.next(); if (r.done) return Nil;
    let acc = r.value;
    for (;;) { r = it.next(); if (r.done) break; acc = f(acc, r.value); }
    return acc;
}
function produceList(f, v) { if (f.rightAssoc) return reduceOp(f.opName, v, true); const out = []; let acc; let first = true; for (const x of iter(v)) { acc = first ? x : f(acc, x); first = false; out.push(acc); } return mkSeq(out); }
function zipLists(lists, f) {
    const as = lists.map(l => arr(l));
    const n = Math.min(...as.map(a => a.length));
    const out = [];
    for (let i = 0; i < n; i++) { const row = as.map(a => a[i]); out.push(f ? row.reduce((x, y) => f(x, y)) : mkList(row)); }
    return mkSeq(out);
}
function crossLists(lists, f) {
    const as = lists.map(l => arr(l));
    let rows = [[]];
    for (const a of as) { const nr = []; for (const r of rows) for (const x of a) nr.push([...r, x]); rows = nr; }
    return mkSeq(rows.map(r => f ? r.reduce((x, y) => f(x, y)) : mkList(r)));
}
function rotateList(v, n) { const a = arr(v).slice(); if (!a.length) return mkSeq(a); let k = Number(toInt(n === undefined ? 1 : n)) % a.length; if (k < 0) k += a.length; return mkSeq(a.slice(k).concat(a.slice(0, k))); }
function rotorList(v, ...specs) {
    let named = null; if (specs.length && specs[specs.length - 1] instanceof RNamed) named = specs.pop().m;
    const partial = named && truthy(named.get('partial'));
    const a = arr(v); const out = []; let i = 0; let si = 0;
    if (!specs.length) return mkSeq(out);
    while (i < a.length) {
        const sp = specs[si % specs.length]; si++;
        let n, gap = 0;
        if (sp instanceof RPair) { n = Number(toInt(sp.k)); gap = Number(toInt(sp.v)); } else n = Number(toInt(sp));
        const chunk = a.slice(i, i + n);
        if (chunk.length < n && !partial) break;
        out.push(mkList(chunk));
        i += n + gap;
        if (i < 0) break;
    }
    return mkSeq(out);
}
function batchList(v, n) { const a = arr(v), k = Number(toInt(n)), out = []; for (let i = 0; i < a.length; i += k) out.push(mkList(a.slice(i, i + k))); return mkSeq(out); }
function pickFrom(v, n) {
    const a = arr(v).slice();
    if (n === undefined) return a.length ? a[Math.floor(rand() * a.length)] : Nil;
    const k = wantAll(n) ? a.length : Math.min(a.length, Number(toInt(n)));
    const out = [];
    for (let i = 0; i < k; i++) { const j = i + Math.floor(rand() * (a.length - i)); [a[i], a[j]] = [a[j], a[i]]; out.push(a[i]); }
    return mkSeq(out);
}
// pick(*), pick(Whatever), roll(*): the `*` term is a curry here, the type object is Whatever
const wantAll = (n) => n instanceof RWhatever || n === T.Whatever || typeof n === 'function' || n === Infinity;
function rollFrom(v, n) {
    const a = arr(v);
    if (n === undefined) return a.length ? a[Math.floor(rand() * a.length)] : Nil;
    if (wantAll(n)) return new RSeq((function* () { for (;;) yield a[Math.floor(rand() * a.length)]; })(), true);   // roll(*): lazy and endless
    const k = Number(toInt(n)); const out = [];
    for (let i = 0; i < k; i++) out.push(a[Math.floor(rand() * a.length)]);
    return mkSeq(out);
}
function classifyList(v, f, as) { const h = new RHash(); for (const x of iter(v)) { const k = hashKey(f(x)); let b = h.m.get(k); if (!b) { b = mkArray([]); h.m.set(k, b); } b.a.push(as ? as(x) : x); } return h; }
function categorizeList(v, f, as) { const h = new RHash(); for (const x of iter(v)) for (const key of itemsOf(f(x))) { const k = hashKey(key); let b = h.m.get(k); if (!b) { b = mkArray([]); h.m.set(k, b); } b.a.push(as ? as(x) : x); } return h; }
function repeatedList(v, f) { const seen = new Set(), out = []; for (const x of iter(v)) { const k = whichKey(f ? f(x) : x); if (seen.has(k)) out.push(x); else seen.add(k); } return mkSeq(out); }
function combinations(v, n) {
    const a = arr(v);
    const sizes = n === undefined ? Array.from({ length: a.length + 1 }, (_, i) => i) : n instanceof RRange ? n.arr().map(Number) : [Number(toInt(n))];
    const out = [];
    for (const k of sizes) {
        if (k > a.length || k < 0) continue;
        const idx = Array.from({ length: k }, (_, i) => i);
        if (k === 0) { out.push(mkList([])); continue; }
        for (;;) {
            out.push(mkList(idx.map(i => a[i])));
            let i = k - 1; while (i >= 0 && idx[i] === a.length - k + i) i--;
            if (i < 0) break;
            idx[i]++; for (let j = i + 1; j < k; j++) idx[j] = idx[j - 1] + 1;
        }
    }
    return mkSeq(out);
}
function permutations(v) {
    const a = arr(v); const out = [];
    const rec = (cur, rest) => { if (!rest.length) { out.push(mkList(cur)); return; } for (let i = 0; i < rest.length; i++) rec([...cur, rest[i]], rest.slice(0, i).concat(rest.slice(i + 1))); };
    rec([], a);
    return mkSeq(out);
}
function listRepeat(v, n) {        // infix:<xx>
    if (n instanceof RWhatever || n === Infinity) return new RSeq((function* () { for (;;) yield typeof v === 'function' ? v() : v; })(), true);
    const k = Number(toInt(n)); const out = [];
    for (let i = 0; i < k; i++) out.push(typeof v === 'function' && v.isThunk ? v() : v);
    return mkList(out);
}
function endOf(v) { return elemsOf(v) - 1; }
function whichKey(v) {
    if (v instanceof RAllo) return typeOf(v).name + '|' + whichKey(v.n) + '|' + whichKey(v.s);
    switch (typeof v) {
        case 'string': return 'Str|' + v;
        case 'number': return (Number.isInteger(v) ? 'Int|' : 'Num|') + v;
        case 'bigint': return 'Int|' + v;
        case 'boolean': return 'Bool|' + (v ? 1 : 0);
        default:
            if (v instanceof RNum) return 'Num|' + v.v;
            if (v instanceof RRat) return 'Rat|' + v.n + '/' + v.d;
            if (v instanceof RComplex) return 'Complex|' + str(v.re) + '|' + str(v.im);
            if (v instanceof RType) return 'Type|' + v.name;
            if (v instanceof REnum) return v.ty.name + '|' + v.key;
            if (v instanceof RPair) return 'Pair|' + whichKey(v.k) + '|' + whichKey(v.v);
            if (v instanceof RList) return v.ty.name + '|' + v.a.map(whichKey).join(',');
            if (v instanceof RObj) { const m = v.ty.findUser('WHICH'); if (m) return str(m(v)); return v.ty.name + '|' + objId(v); }
            if (v instanceof RDate) return v.ty.name + '|' + v.d.getTime();
            if (v instanceof RVersion) return 'Version|' + v.Str();
            return typeName(v) + '|' + objId(v);
    }
}

// --- junctions -------------------------------------------------------------
function junction(kind, v) {
    const items = [];
    for (const x of itemsOf(v)) { if (x instanceof RJunction && x.kind === kind) items.push(...x.items); else items.push(x); }
    return new RJunction(kind, items);
}
function junctionBool(j) {
    const t = j.items.map(truthy);
    switch (j.kind) {
        case 'any': return t.some(x => x);
        case 'all': return t.every(x => x);
        case 'none': return !t.some(x => x);
        case 'one': return t.filter(x => x).length === 1;
    }
    return false;
}
function junctionOp(fn, a, b) {
    if (a instanceof RJunction) return new RJunction(a.kind, a.items.map(x => fn(x, b)));
    return new RJunction(b.kind, b.items.map(y => fn(a, y)));
}
function junctionStr(j) { return j.kind + '(' + j.items.map(str).join(', ') + ')'; }
function junctionGist(j) { return j.kind + '(' + j.items.map(gist).join(', ') + ')'; }
function junctionRaku(j) { return j.kind + '(' + j.items.map(raku).join(', ') + ')'; }

// --- smartmatch ------------------------------------------------------------
// X ~~ (… $_ …): the pattern is computed with X as the topic; a Junction on the left threads the whole test
function withDefault(c, d) { c.dflt = d; return c; }
class RakuItem { constructor(v) { this.v = v; } }   // a value rendered as an item: `$[…]`, `${…}` (raku() only)   // `is default(v)` on an array or hash
function smartmatchWith(v, f) {
    if (v instanceof RJunction) return junctionBool(new RJunction(v.kind, v.items.map(x => truthy(smartmatch(x, f(x))))));
    return smartmatch(v, f(v));
}
function smartmatch(v, pat) {
    if (typeof pat === 'boolean') return pat;
    if (v instanceof RJunction) {   // a Junction topic: a regex answers a Junction of matches, a type or range collapses
        if (pat instanceof RRegex) return new RJunction(v.kind, v.items.map(x => regexMatch(x, pat)));
        if (pat instanceof RType || pat instanceof RRange) return junctionBool(new RJunction(v.kind, v.items.map(x => smartmatch(x, pat))));
    }
    if (pat instanceof RType) return isa(v, pat) || (pat === Nil && v === Nil);
    if (typeof pat === 'function') { if (pat.rtype === T.WhateverCode) return truthy(pat(v)); return truthy(pat(v)); }
    if (pat instanceof RRegex) return regexMatch(v, pat);
    if (pat instanceof RAllo) return isNumeric(v) ? numeq(v, pat.n) : str(v) === pat.s;   // an allomorph matches numerically a Numeric, by string otherwise
    if (pat instanceof RRange) return pat.contains(v);
    if (pat instanceof RJunction) return junctionBool(junctionOp((x, p) => smartmatch(x, p), v, pat));
    if (typeof pat === 'string') return str(v) === pat;
    if (typeof pat === 'number' || typeof pat === 'bigint' || pat instanceof RNum || pat instanceof RRat) {
        if (!isNumeric(v) && typeof v !== 'string') return false;
        try { return numeq(v, pat); } catch (e) { return false; }
    }
    if (pat === Nil) return v === Nil;
    if (pat instanceof RSlip && pat.a.length === 0) return (v instanceof RList || v instanceof RSeq || v instanceof RRange) && arr(v).length === 0;   // X ~~ Empty
    if (pat instanceof RList) {
        if (!(v instanceof RList || v instanceof RSeq || v instanceof RRange)) return false;
        const a = arr(v), b = pat.a; if (a.length !== b.length) return false;
        for (let i = 0; i < a.length; i++) if (!(b[i] instanceof RWhatever) && !smartmatch(a[i], b[i])) return false;
        return true;
    }
    if (pat instanceof RHash) { return v instanceof RHash ? eqv(v, pat) : hexists(pat, v); }
    if (pat instanceof RPair) { if (v instanceof RHash) return truthy(hget(v, pat.k)) === truthy(pat.v); if (v instanceof RPair) return str(v.k) === str(pat.k) && smartmatch(v.v, pat.v); return truthy(mc(v, str(pat.k))) === truthy(pat.v); }   // 3 ~~ :is-prime calls the method; a Pair compares
    if (pat instanceof RSetty) return pat.has(v);
    if (pat instanceof REnum) return v instanceof REnum ? v === pat : (isNumeric(v) ? numeq(v, pat.val) : str(v) === pat.key);
    if (pat instanceof RWhatever) return true;
    if (pat instanceof RObj) { const m = pat.ty.findUser('ACCEPTS'); if (m) return truthy(m(pat, v)); return identical(v, pat); }
    if (pat instanceof RakuError) return v instanceof RakuError && v.type === pat.type;
    if (pat instanceof RVersion) return v instanceof RVersion && v.accepts(pat);   // wildcards and `+`
    return eqv(v, pat);
}

Object.assign(R, { item, decont, bindArray, iterTopic, smartmatchWith, minMaxAdv, withDefault,
    RList, RSeq, RRange, RHash, mkList, mkArray, mkSeq, mkSlip, seqOf, range, upto, mkHash, hashKey, hget, hset, hexists, hdelete, hslice,
    hviv, aviv, assignHash, hashLit, hashFrom, pair, pairKey, pairValue, listItems, arr, iter, iterN, list, arrayLit, itemsOf,
    assignArray, newArray, newHash, flat, slip, spreadArgs, aget, aset, aslice, aexists, adelete, elemsOf,
    mapList, grepList, matcherOf, firstOf, repeatedList, joinList, reverseList, sumList, sortList, orderNum, minOf, maxOf, minmax, uniqueList, squishList,
    headOf, tailOf, keysOf, valuesOf, kvOf, pairsOf, antipairsOf, invertOf, pushTo, appendTo, unshiftTo, prependTo, popFrom, shiftFrom, spliceArr,
    reduceList, produceList, zipLists, crossLists, rotateList, rotorList, batchList, pickFrom, rollFrom, classifyList, categorizeList,
    combinations, permutations, listRepeat, endOf, whichKey, junction, junctionBool, junctionOp, smartmatch, spliceSlips,
});
