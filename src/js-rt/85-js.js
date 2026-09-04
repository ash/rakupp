// JavaScript interop (`use JS`, TRANSPILE-PLAN P4). Values cross by copy,
// objects cross by identity: a JS::Object is an opaque handle (RJsObj) around
// whatever the host handed us, so identity, prototype methods and the DOM
// survive the crossing — deliberately not a Hash.

const JsObjectT = mkType('JS::Object', [T.Any]);
class RJsObj { constructor(v) { this.v = v; } }

// Raku → JS: Int → number (BigInt past 2^53 stays BigInt), Num → number,
// Str → string, Bool → boolean, Nil → undefined, a type object → null,
// Array/List → a JS array (copy), Hash → a plain object (copy), Pair →
// {key: value}, Code → a function whose arguments are marshalled back.
function toJs(v) {
    switch (typeof v) {
        case 'number': case 'string': case 'boolean': case 'bigint': case 'undefined': return v;
        case 'function': {
            if (v.__js) return v.__js;
            const f = (...a) => toJs(v(...a.map(fromJs)));
            f.__raku = v;
            return f;
        }
        case 'object':
            if (v === null) return null;
            if (v instanceof RJsObj) return v.v;
            if (v === Nil) return undefined;
            if (v instanceof RType) return null;
            if (v instanceof RNum) return v.v;
            if (v instanceof RRat) return ratToFloat(v);
            if (v instanceof RList || v instanceof RSeq || v instanceof RRange) return arr(v).map(toJs);
            if (v instanceof RSlip) return v.a.map(toJs);
            if (v instanceof RHash) { const o = {}; for (const [k, x] of v.m) o[k] = toJs(x); return o; }
            if (v instanceof RPair) { const o = {}; o[str(v.k)] = toJs(v.v); return o; }
            if (v instanceof REnum) return toJs(v.val);
            if (v instanceof RSetty) return v.keysList().map(toJs);
            if (v instanceof RakuError) { const e = new Error(v.message); e.raku = v; return e; }
            if (v instanceof RComplex) return { re: v.re, im: v.im };
            if (v instanceof RDate) return new Date(v.d.getTime());
            if (v instanceof RIOPath) return v.path;
            if (v instanceof RVersion) return v.Str();
            return v;   // a Raku object crosses as itself (identity)
    }
    return v;
}
// JS → Raku: number → Int if integral else Num, string → Str, boolean → Bool,
// null/undefined → Nil, an array → Array, a function → Code, any other object → JS::Object.
function fromJs(v) {
    switch (typeof v) {
        case 'number': return Number.isInteger(v) ? (Number.isSafeInteger(v) ? v : normBig(BigInt(v))) : v;
        case 'string': case 'boolean': return v;
        case 'bigint': return normBig(v);
        case 'undefined': return Nil;
        case 'function': {
            if (v.__raku) return v.__raku;
            const f = (...a) => fromJs(v(...a.map(toJs)));
            f.__js = v; f.rtype = T.Sub;
            return f;
        }
        case 'object':
            if (v === null) return Nil;
            if (Array.isArray(v)) return mkArray(v.map(fromJs));
            if (v instanceof RObj || v instanceof RList || v instanceof RHash || v instanceof RType || v instanceof RJsObj) return v;   // ours, coming back
            if (v instanceof Error && v.raku) return v.raku;
            return new RJsObj(v);
    }
    return new RJsObj(v);
}
// $o.name(args) : call the property when it is a function, else read it
function jsCall(o, name, args) {
    const target = o instanceof RJsObj ? o.v : o;
    const named = args.length && args[args.length - 1] instanceof RNamed ? args.pop().m : null;
    const prop = target == null ? undefined : target[name];
    // a function-valued property is CALLED when arguments are given or the name is
    // lower-case (`JS.fetch(...)`, `$el.focus`); an upper-case name is a constructor
    // or namespace and is READ (`JS.Math`, `JS.Array.from`, `JS.Event.new`)
    const isUpper = name[0] >= 'A' && name[0] <= 'Z';
    if (typeof prop === 'function' && (args.length || !isUpper)) {
        const jsArgs = args.map(toJs);
        if (named) jsArgs.push(Object.fromEntries(Array.from(named, ([k, x]) => [k, toJs(x)])));
        return fromJs(prop.apply(target, jsArgs));
    }
    if (prop === undefined && args.length && name.startsWith('set-')) { target[name.slice(4)] = toJs(args[0]); return args[0]; }
    if (args.length === 1 && !(target != null && name in target)) throw new RakuError(`JS::Object has no property or method '${name}'`, 'X::Method::NotFound');
    return fromJs(prop);
}
function jsGet(o, k) { const target = o instanceof RJsObj ? o.v : o; return fromJs(target == null ? undefined : target[typeof k === 'number' ? k : str(k)]); }
function jsSet(o, k, v) { const target = o instanceof RJsObj ? o.v : o; target[typeof k === 'number' ? k : str(k)] = toJs(v); return v; }
function jsExists(o, k) { const target = o instanceof RJsObj ? o.v : o; return target != null && (str(k) in target); }
function jsNew(ctor, args) { const C = ctor instanceof RJsObj ? ctor.v : ctor; return fromJs(new C(...args.map(toJs))); }
function jsTruthy(o) { return !!o.v; }
function jsStr(o) { try { return String(o.v); } catch (e) { return '[object]'; } }
// EVAL 'literal', :lang<JavaScript>: the emitter inlines the code; this wraps its value
function jsEval(v) { return fromJs(v); }
const JS = new RJsObj(globalThis);

M(JsObjectT, {
    gist: (s) => jsStr(s), Str: (s) => jsStr(s), raku: (s) => 'JS::Object(' + jsStr(s) + ')', Bool: (s) => jsTruthy(s), so: (s) => jsTruthy(s), defined: (s) => s.v != null,
    'AT-KEY': (s, k) => jsGet(s, k), 'ASSIGN-KEY': (s, k, v) => jsSet(s, k, v), 'EXISTS-KEY': (s, k) => jsExists(s, k), 'AT-POS': (s, i) => jsGet(s, Number(toInt(i))), 'ASSIGN-POS': (s, i, v) => jsSet(s, Number(toInt(i)), v),
    'new': (s, ...a) => jsNew(s, a), 'keys': (s) => mkSeq(Object.keys(s.v)), 'elems': (s) => s.v != null && s.v.length !== undefined ? s.v.length : Object.keys(s.v).length,
    'list': (s) => mkList(Array.from(s.v, fromJs)), 'List': (s) => mkList(Array.from(s.v, fromJs)), 'Array': (s) => mkArray(Array.from(s.v, fromJs)), 'Hash': (s) => { const h = new RHash(); for (const k of Object.keys(s.v)) h.m.set(k, fromJs(s.v[k])); return h; },
    'Numeric': (s) => fromJs(Number(s.v)), 'Int': (s) => toInt(fromJs(Number(s.v))), 'Num': (s) => mkNum(Number(s.v)), 'WHAT': (s) => JsObjectT, 'js': (s) => s, 'typeof': (s) => typeof s.v, 'instanceof': (s, c) => s.v instanceof (c instanceof RJsObj ? c.v : c),
    'invoke': (s, ...a) => fromJs(s.v(...a.map(toJs))), 'call': (s, ...a) => fromJs(s.v(...a.map(toJs))),
});
Object.assign(R, { RJsObj, JsObjectT, toJs, fromJs, jsCall, jsGet, jsSet, jsNew, jsEval, JS });
