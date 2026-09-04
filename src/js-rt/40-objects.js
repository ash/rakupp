// Objects: user classes and enums, exceptions, gather/take, scalars, the
// small value classes (Set/Bag/Mix, Capture, Version, IO::Path), and the
// regex shells P3 fills in.

// A container box: `is rw` parameters, `:=` targets, and attribute slots that
// need identity. Generated code reads `.v` where the analysis says so.
class RScalar { constructor(v) { this.v = v; } }

// --- exceptions --------------------------------------------------------------
function die(...args) {
    if (args.length === 1) {
        const a = args[0];
        if (a instanceof RakuError) throw a;
        if (a instanceof RObj && a.ty.isa(T.Exception)) throw a;
        if (a instanceof RType && a.isa(T.Exception)) throw new RakuError(a.name, a.name);
        if (a instanceof RFailure) throw a.err;
    }
    throw new RakuError(args.length ? args.map(str).join('') : 'Died');
}
// sink context: a Failure throws, a lazy Seq is iterated (`@in.map({ … })` as a statement runs)
function sink(v) { if (v instanceof RFailure && !v.handled) throw v.err; if (v instanceof RSeq && !v.done) v.arr(); return v; }
function failure(err) { return new RFailure(err instanceof RakuError ? err : new RakuError(str(err))); }
function fail(...args) {
    if (args.length === 1 && (args[0] instanceof RakuError)) return failure(args[0]);
    return failure(new RakuError(args.length ? args.map(str).join('') : 'Failed'));
}
// Normalize whatever was thrown into an Exception-like value for CATCH / $!.
function exc(e) {
    if (e instanceof RakuError) return e;
    if (e instanceof RObj) return e;
    if (e instanceof RangeError && /call stack/i.test(e.message)) return new RakuError('Maximum call stack size exceeded (raise it with node --stack-size)', 'X::Internal');
    if (e instanceof Error) return new RakuError(e.message, 'X::Internal');
    return new RakuError(String(e), 'X::AdHoc');
}
function isControl(e) { return e instanceof NextCtl || e instanceof LastCtl || e instanceof RedoCtl || e instanceof RetCtl || e instanceof ExitCtl || e instanceof SuccCtl || e instanceof TakeCtl; }
function excMessage(e) { if (e instanceof RakuError) return e.message; if (e instanceof RObj) { const m = e.ty.findUser('message'); if (m) return str(m(e)); const a = e['a_message']; if (a !== undefined) return str(a); } return str(e); }
function excType(e) { if (e instanceof RakuError) return T[e.type] || mkExType(e.type); if (e instanceof RObj) return e.ty; return T.Exception; }
function mkExType(name) { if (!T[name]) { const t = mkType(name, [T.Exception]); return t; } return T[name]; }
function rethrow(e) { throw e; }
function warn(...args) { host.stderr(args.map(str).join('') + '\n'); return true; }

// --- gather / take ----------------------------------------------------------
class TakeCtl { }
const takeStack = [];
function take(v) {
    if (!takeStack.length) throw new RakuError('take without gather');
    takeStack[takeStack.length - 1].push(v);
    return v;
}
// Every take lexically inside: a generator (lazy). Otherwise the body runs
// eagerly with a dynamic collector.
function gather(genFn, lazy) { return new RSeq(genFn(), !!lazy); }
function gatherEager(fn) {
    const buf = [];
    takeStack.push(buf);
    try { fn(); } finally { takeStack.pop(); }
    return mkSeq(buf);
}

// --- user classes -------------------------------------------------------------
// R.class(name, spec) — spec: { parents: [RType], roles: [RType], attrs: [{name, sigil, pub, rw, def, required, type}],
//                              methods: {name: fn(self, ...)}, multis: {name: [[fn, guard]...]}, isRole }
function defClass(name, spec) {
    const parents = (spec.parents && spec.parents.length) ? spec.parents : [T.Any];
    const ty = new RType(name, parents, { roles: spec.roles || [], attrs: spec.attrs || [], isUser: true, isRole: !!spec.isRole });
    // role attributes and methods are composed into the class
    for (const r of ty.roles) {
        for (const a of r.attrs) if (!ty.attrs.some(x => x.name === a.name)) ty.attrs.push(a);
        for (const k of Object.keys(r.methods)) if (!(k in ty.methods)) ty.methods[k] = r.methods[k];
    }
    if (spec.methods) for (const k of Object.keys(spec.methods)) ty.methods[k] = spec.methods[k];
    // accessors for public attributes (a user method of the same name wins)
    for (const a of ty.attrs) {
        if (a.pub && !ty.methods[a.name]) {
            const key = 'a_' + a.name;
            ty.methods[a.name] = a.rw ? accessorRw(key) : function (self) { return self[key]; };
        }
        for (let i = 0; i < (a.handles ? a.handles.length : 0); i++) {
            const mname = a.handles[i], target = (a.handlesTo && a.handlesTo[i]) || mname, key = 'a_' + a.name;
            ty.methods[mname] = function (self, ...args) { return mc(self[key], target, ...args); };
        }
    }
    ty.ctor = function () { };
    ty.allAttrs = function () {         // derived class first, as the interpreter renders them
        const out = [];
        for (const t of this.mro) if (t.attrs) for (const a of t.attrs) if (!out.some(x => x.name === a.name)) out.push(a);
        return out;
    };
    if (name) T[name] = ty;
    return ty;
}
function accessorRw(key) { const f = function (self, ...args) { if (args.length && !(args[0] instanceof RNamed)) self[key] = args[0]; return self[key]; }; f.lvKey = key; return f; }
RType.prototype.allAttrs = function () { return []; };
// Mu.new: build an instance, fill attributes from named args, run BUILD/TWEAK
function construct(ty, ...args) {
    if (!ty.isUser) {
        if (ty === T.Str) return ''; if (ty === T.Int) return 0; if (ty === T.Array) return mkArray(listItems(args.length && !(args[0] instanceof RNamed) ? args[0] : []).slice()); if (ty === T.Hash) return newHash(args.length ? mkList(args) : undefined); if (ty === T.List) return mkList(args.filter(a => !(a instanceof RNamed)));
        if (ty === T.Pair) { const [pos, named] = splitArgs(args); if (pos.length >= 2) return pair(pos[0], pos[1]); const k = named.get('key'), v = named.get('value'); return pair(k === undefined ? Any : k, v === undefined ? Any : v); }
        if (ty === T.Set || ty === T.SetHash || ty === T.Bag || ty === T.BagHash || ty === T.Mix || ty === T.MixHash) return mkSetty(ty, args.filter(a => !(a instanceof RNamed)));
        if (ty === T.Range) { const [pos] = splitArgs(args); return range(pos[0], pos[1]); }
        if (ty === T.Failure) { const [pos] = splitArgs(args); return failure(pos.length ? (pos[0] instanceof RakuError ? pos[0] : new RakuError(str(pos[0]))) : new RakuError('Failed')); }
        if (ty.isa(T.Exception)) { const [pos, named] = splitArgs(args); const m = named.get('message'); return new RakuError(m === undefined ? (pos.length ? str(pos[0]) : ty.name) : str(m), ty.name, named.size ? named : null); }
        if (ty === T.Version) { const [pos] = splitArgs(args); return new RVersion(str(pos[0])); }
        if (ty === T['IO::Path'] || ty === T.IO) { const [pos] = splitArgs(args); return new RIOPath(str(pos[0])); }
        if (ty === T.FatRat || ty === T.Rat) { const [pos] = splitArgs(args); return mkRat(big(pos[0]), big(pos.length > 1 ? pos[1] : 1)); }
        if (ty === T.Int || ty === T.Num || ty === T.Str || ty === T.Bool) { const [pos] = splitArgs(args); return pos.length ? coerce(ty, pos[0]) : (ty === T.Str ? '' : ty === T.Bool ? false : 0); }
        if (ty === T.Nil) return Nil;
        if (ty === T.Any || ty === T.Mu) return new RObj(defClass(null, { parents: [T.Any] }));
        if (ty === T.Capture) { const [pos, named] = splitArgs(args); return new RCapture(pos, new Map(named)); }
        if (ty === T.Date || ty === T.DateTime) return dateNew(ty, args);
        throw new RakuError(`Cannot create a ${ty.name} with .new here`);
    }
    if (ty.isRole) { const anon = defClass(null, { parents: [T.Any], roles: [ty] }); anon.name = ty.name; return construct(anon, ...args); }
    const [pos, named] = splitArgs(args);
    if (pos.length && !ty.findUser('BUILD') && !ty.findUser('new')) throw new RakuError(`Default constructor for '${ty.name}' only takes named arguments`, 'X::Attribute::NoDefault');
    const o = new RObj(ty);
    const used = new Set();
    const buildM = ty.findUser('BUILD');
    for (const a of ty.allAttrs()) {
        const key = 'a_' + a.name;
        let v;
        if ((a.pub || a.built) && named.has(a.name)) { v = named.get(a.name); used.add(a.name); }
        else if (a.def) v = a.def(o);
        else if (a.required && !buildM) throw new RakuError(`The attribute '${a.sigil}!${a.name}' is required, but you did not provide a value for it.`, 'X::Attribute::Required');
        else v = a.sigil === '@' ? mkArray([]) : a.sigil === '%' ? mkHash() : (a.type && T[a.type] ? T[a.type] : Any);
        if (a.sigil === '@' && !(v instanceof RList && v.ty === T.Array)) v = newArray(v);
        else if (a.sigil === '%' && !(v instanceof RHash)) v = newHash(v);
        if (a.type && a.sigil === '$' && v !== Any && !(v instanceof RType) && !isa(v, a.type) && T[a.type]) {
            throw new RakuError(`Type check failed in assignment to ${a.sigil}!${a.name}; expected ${a.type} but got ${typeName(v)} (${raku(v)})`, 'X::TypeCheck::Assignment');
        }
        o[key] = v;
    }
    if (buildM) buildM(o, new RNamed(named));
    const tweak = ty.findUser('TWEAK');
    if (tweak) tweak(o, new RNamed(named));
    return o;
}
function cloneObj(o, ...args) {
    if (o instanceof RObj) {
        const [, named] = splitArgs(args);
        const c = new RObj(o.ty);
        for (const k of Object.keys(o)) if (k !== 'ty') c[k] = o[k];
        for (const [k, v] of named) c['a_' + k] = v;
        return c;
    }
    if (o instanceof RList) return new RList(o.a.slice(), o.ty);
    if (o instanceof RHash) return new RHash(new Map(o.m), o.ty);
    if (o instanceof RSetty) return o.clone();
    return o;
}
function enumType(name, pairs, base) {
    const ty = mkType(name, [base || T.Int], { isEnum: true });
    ty.enumValues = pairs.map(([k, v]) => new REnum(ty, k, v));
    ty.isUser = true;
    return ty;
}
function enumFromKeys(ty, key) { return ty.enumValues.find(e => e.key === key) || Nil; }
function enumFromValue(ty, v) { return ty.enumValues.find(e => numeq(e.val, v)) || Nil; }
function attrGet(o, key) { return o[key]; }

// --- named arguments -----------------------------------------------------------
function named(pairs) { return new RNamed(new Map(pairs)); }
function splitArgs(args) {
    if (args.length && args[args.length - 1] instanceof RNamed) return [args.slice(0, -1), args[args.length - 1].m];
    return [args, EMPTY_MAP];
}
const EMPTY_MAP = new Map();
function namedArg(m, keys, dflt) {
    for (const k of keys) if (m.has(k)) return m.get(k);
    return dflt;
}
function namedHash(m, skip) {          // *%_ / %named
    const h = new RHash();
    for (const [k, v] of m) if (!skip || !skip.has(k)) h.m.set(k, v);
    return h;
}
function checkNamed(m, allowed, who) {
    for (const k of m.keys()) if (!allowed.includes(k)) throw new RakuError(`Unexpected named argument '${k}' passed${who ? ' to ' + who : ''}`, 'X::AdHoc');
}
function notAny(pname) { throw new RakuError(`Type check failed in binding to parameter '${pname}'; expected Any but got Mu (Mu)`, 'X::TypeCheck::Binding::Parameter'); }
function arityError(who, expected, got) { if (got < expected) tooFew(who, expected, got); tooMany(who, expected, got); }
function tooMany(who, expected, got) { throw new RakuError(`Too many positionals passed${who ? ' to ' + who : ''}; expected ${expected} argument${expected === 1 ? '' : 's'} but got ${got}`, 'X::AdHoc'); }
function tooFew(who, expected, got) { throw new RakuError(`Too few positionals passed${who ? ' to ' + who : ''}; expected ${expected} argument${expected === 1 ? '' : 's'} but got ${got}`, 'X::AdHoc'); }
function typeCheck(v, tname, pname, who) {
    if (v instanceof RJunction) return v;
    if (!isa(v, tname)) throw new RakuError(`Type check failed in binding to parameter '${pname}'; expected ${tname} but got ${typeName(v)} (${raku(v)})`, 'X::TypeCheck::Binding::Parameter');
    return v;
}
function typeMatches(v, tname, def) {       // for multi-dispatch guards; def: 1 = :D, 2 = :U
    if (tname && !isa(v, tname)) return false;
    if (def === 1 && !defined(v)) return false;
    if (def === 2 && defined(v)) return false;
    return true;
}
function noMatch(who, args) { throw new RakuError(`Cannot resolve caller ${who}(${args.filter(a => !(a instanceof RNamed)).map(a => typeName(a) + (defined(a) ? '' : ':U')).join(', ')}); none of these signatures matches`, 'X::Multi::NoMatch'); }

// --- Capture -------------------------------------------------------------------
class RCapture {
    constructor(pos, named) { this.pos = pos; this.named = named || new Map(); }
    gist() { return '\\(' + this.pos.map(raku).concat(Array.from(this.named, ([k, v]) => pairRaku(pair(k, v)))).join(', ') + ')'; }
    raku() { return this.gist(); }
    Str() { return this.pos.map(str).join(' '); }
}
function capture(...args) { const [pos, named] = splitArgs(args); return new RCapture(pos, new Map(named)); }

// --- Set / Bag / Mix -------------------------------------------------------------
class RSetty {
    constructor(ty) { this.ty = ty; this.m = new Map(); }   // whichKey → {v, n}
    isSet() { return this.ty === T.Set || this.ty === T.SetHash; }
    isMix() { return this.ty === T.Mix || this.ty === T.MixHash; }
    add(v, n) {
        const k = whichKey(v); const e = this.m.get(k);
        if (this.isSet()) { if (!e) this.m.set(k, { v, n: true }); return; }
        const cnt = n === undefined ? 1 : n;
        if (e) e.n = add(e.n, cnt); else this.m.set(k, { v, n: cnt });
    }
    get(v) { const e = this.m.get(whichKey(v)); return e ? e.n : (this.isSet() ? false : 0); }
    has(v) { return this.m.has(whichKey(v)); }
    set(k, v) { const kk = whichKey(k); if (this.isSet()) { if (truthy(v)) this.m.set(kk, { v: k, n: true }); else this.m.delete(kk); } else { if (numeq(v, 0)) this.m.delete(kk); else { const e = this.m.get(kk); if (e) e.n = v; else this.m.set(kk, { v: k, n: v }); } } return v; }
    delete(k) { const kk = whichKey(k); const e = this.m.get(kk); this.m.delete(kk); return e ? e.n : (this.isSet() ? false : 0); }
    keysList() { return Array.from(this.m.values(), e => e.v); }
    valuesList() { return Array.from(this.m.values(), e => e.n); }
    pairsList() { return Array.from(this.m.values(), e => pair(e.v, e.n)); }
    listItems() { return this.isSet() ? this.keysList() : this.pairsList(); }
    total() { let t = 0; for (const e of this.m.values()) t = this.isSet() ? t + 1 : add(t, e.n); return t; }
    elems() { return this.m.size; }
    sortedEntries() { return Array.from(this.m.values()).sort((a, b) => cmpNum(a.v, b.v)); }
    Str() { return this.sortedEntries().map(e => this.isSet() ? str(e.v) : str(e.v) + '(' + str(e.n) + ')').join(' '); }
    gist() {
        const ents = this.sortedEntries();
        const body = ents.map(e => this.isSet() ? gist(e.v) : gist(e.v) + '(' + gist(e.n) + ')').join(' ');
        return this.ty.name + '(' + body + ')';
    }
    raku() {
        const ents = this.sortedEntries();
        if (this.isSet()) return this.ty.name + '.new(' + ents.map(e => raku(e.v)).join(',') + ')';
        return '(' + ents.map(e => raku(e.v) + '=>' + raku(e.n)).join(',') + ').' + this.ty.name;
    }
    eqv(o) { if (this.ty !== o.ty || this.m.size !== o.m.size) return false; for (const [k, e] of this.m) { const f = o.m.get(k); if (!f || !eqv(e.n, f.n)) return false; } return true; }
    clone() { const c = new RSetty(this.ty); for (const [k, e] of this.m) c.m.set(k, { v: e.v, n: e.n }); return c; }
}
function mkSetty(ty, src) {
    const s = new RSetty(ty);
    const items = src.length === 1 ? itemsOf(src[0]) : src;
    for (const it of items) {
        if (it instanceof RPair && !(ty === T.Set || ty === T.SetHash)) { s.add(it.k, it.v); continue; }
        if (it instanceof RHash) { for (const [k, v] of it.m) s.add(k, v); continue; }
        if (it instanceof RSetty) { for (const e of it.m.values()) s.add(e.v, e.n); continue; }
        s.add(it);
    }
    return s;
}
function toSetty(v, ty) {
    if (v instanceof RSetty) { if (v.ty === ty) return v; const s = new RSetty(ty); for (const e of v.m.values()) s.add(e.v, e.n); return s; }
    if (v instanceof RHash) { const s = new RSetty(ty); for (const [k, x] of v.m) { if (ty === T.Set || ty === T.SetHash) { if (truthy(x)) s.add(k); } else s.add(k, x); } return s; }
    return mkSetty(ty, [v]);
}
function setOp(op, a, b) {
    const A = a instanceof RSetty ? a : toSetty(a, T.Set), B = b instanceof RSetty ? b : toSetty(b, T.Set);
    const baggy = !A.isSet() || !B.isSet();
    const ty = A.isMix() || B.isMix() ? T.Mix : baggy ? T.Bag : T.Set;
    const out = new RSetty(ty);
    switch (op) {
        case '∪': for (const e of A.m.values()) out.add(e.v, baggy ? (A.isSet() ? 1 : e.n) : undefined);
                  for (const e of B.m.values()) { const cur = out.m.get(whichKey(e.v)); const n = B.isSet() ? 1 : e.n; if (!baggy) out.add(e.v); else if (!cur) out.add(e.v, n); else if (lt(cur.n, n)) cur.n = n; }
                  return out;
        case '∩': for (const e of A.m.values()) if (B.has(e.v)) out.add(e.v, baggy ? (lt(A.isSet() ? 1 : e.n, B.isSet() ? 1 : B.get(e.v)) ? (A.isSet() ? 1 : e.n) : (B.isSet() ? 1 : B.get(e.v))) : undefined);
                  return out;
        case '∖': for (const e of A.m.values()) { if (!baggy) { if (!B.has(e.v)) out.add(e.v); } else { const n = sub(A.isSet() ? 1 : e.n, B.isSet() ? (B.has(e.v) ? 1 : 0) : B.get(e.v)); if (gt(n, 0)) out.add(e.v, n); } }
                  return out;
        case '⊖': for (const e of A.m.values()) if (!B.has(e.v)) out.add(e.v); for (const e of B.m.values()) if (!A.has(e.v)) out.add(e.v); return out;
        case '⊎': for (const e of A.m.values()) out.add(e.v, A.isSet() ? 1 : e.n); for (const e of B.m.values()) out.add(e.v, B.isSet() ? 1 : e.n); if (out.ty === T.Set) { const o2 = new RSetty(T.Bag); for (const e of A.m.values()) o2.add(e.v, 1); for (const e of B.m.values()) o2.add(e.v, 1); return o2; } return out;
    }
    throw new RakuError('unknown set operator ' + op);
}
function setRel(op, a, b) {
    const A = a instanceof RSetty ? a : toSetty(a, T.Set), B = b instanceof RSetty ? b : toSetty(b, T.Set);
    switch (op) {
        case '⊆': for (const e of A.m.values()) if (!B.has(e.v) || (!A.isSet() && lt(B.get(e.v), e.n))) return false; return true;
        case '⊂': return setRel('⊆', A, B) && A.m.size !== B.m.size;
        case '⊇': return setRel('⊆', B, A);
        case '⊃': return setRel('⊂', B, A);
        case '≡': return A.eqv(B) || (A.m.size === B.m.size && Array.from(A.m.keys()).every(k => B.m.has(k)));
    }
    return false;
}
function elem(v, s) { if (s instanceof RSetty) return s.has(v); if (s instanceof RHash) return s.m.has(hashKey(v)); return itemsOf(s).some(x => eqv(x, v) || (isNumeric(x) && isNumeric(v) && numeq(x, v))); }

// --- Version ---------------------------------------------------------------------
class RVersion {
    constructor(s) { this.s = str(s).replace(/^v/, ''); this.parts = this.s.split('.').map(p => /^\d+$/.test(p) ? Number(p) : p); }
    Str() { return this.s; }
    cmp(o) { const n = Math.max(this.parts.length, o.parts.length); for (let i = 0; i < n; i++) { const a = this.parts[i] ?? 0, b = o.parts[i] ?? 0; if (a === '*' || b === '*') continue; if (typeof a === 'number' && typeof b === 'number') { if (a !== b) return a < b ? -1 : 1; } else { const c = strCmp(String(a), String(b)); if (c) return c; } } return 0; }
}
// --- Date / DateTime (the small subset) -------------------------------------------
class RDate {
    constructor(ty, d) { this.ty = ty; this.d = d; }
    Str() { const d = this.d; const p = n => String(n).padStart(2, '0'); const ymd = d.getUTCFullYear() + '-' + p(d.getUTCMonth() + 1) + '-' + p(d.getUTCDate()); if (this.ty === T.Date) return ymd; return ymd + 'T' + p(d.getUTCHours()) + ':' + p(d.getUTCMinutes()) + ':' + p(d.getUTCSeconds()) + 'Z'; }
    raku() { const d = this.d; if (this.ty === T.Date) return 'Date.new(' + d.getUTCFullYear() + ',' + (d.getUTCMonth() + 1) + ',' + d.getUTCDate() + ')'; return 'DateTime.new(' + [d.getUTCFullYear(), d.getUTCMonth() + 1, d.getUTCDate(), d.getUTCHours(), d.getUTCMinutes(), d.getUTCSeconds()].join(',') + ')'; }
    daycount() { return Math.floor(this.d.getTime() / 86400000) + 40587; }   // Modified Julian Day
    numeric() { return this.ty === T.Date ? this.daycount() : this.d.getTime() / 1000; }
}
function dateNew(ty, args) {
    const [pos, named] = splitArgs(args);
    if (pos.length === 1) return new RDate(ty, new Date(typeof pos[0] === 'string' ? pos[0] + (ty === T.Date ? 'T00:00:00Z' : '') : toFloat(pos[0]) * 1000));
    if (pos.length >= 3) return new RDate(ty, new Date(Date.UTC(Number(pos[0]), Number(pos[1]) - 1, Number(pos[2]), Number(pos[3] || 0), Number(pos[4] || 0), Number(pos[5] || 0))));
    const g = k => named.has(k) ? Number(toInt(named.get(k))) : 0;
    return new RDate(ty, new Date(Date.UTC(g('year'), (named.has('month') ? g('month') : 1) - 1, named.has('day') ? g('day') : 1, g('hour'), g('minute'), g('second'))));
}
// --- IO shells; the host adapter (70-host.js) does the work ---------------------------
class RIOPath { constructor(path) { this.path = path; } }
class RIOHandle { constructor(kind, path) { this.kind = kind; this.path = path; this.buf = ''; this.pos = 0; this.eof = false; this.lines = null; } }
// --- Regex shells (P3) ---------------------------------------------------------------
class RRegex { constructor(tree, src) { this.tree = tree; this.src = src; } }
class RMatch { constructor() { this.ok = false; } pos() { return Nil; } named() { return Nil; } }
function regexMatch() { throw new RakuError('regexes are not in the JS core yet (P3)'); }
function regexSplit() { throw new RakuError('regexes are not in the JS core yet (P3)'); }
function regexComb() { throw new RakuError('regexes are not in the JS core yet (P3)'); }

Object.assign(R, {
    RScalar, die, failure, fail, sink, exc, isControl, excMessage, excType, mkExType, rethrow, warn, TakeCtl, take, gather, gatherEager,
    defClass, construct, cloneObj, enumType, enumFromKeys, enumFromValue, attrGet, named, splitArgs, namedArg, namedHash, checkNamed,
    tooMany, tooFew, arityError, notAny, typeCheck, typeMatches, noMatch, RCapture, capture, RSetty, mkSetty, toSetty, setOp, setRel, elem,
    RVersion, RDate, dateNew, RIOPath, RIOHandle, RRegex, RMatch, EMPTY_MAP,
});
