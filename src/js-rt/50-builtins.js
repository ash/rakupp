// Named builtins the emitter resolves by name (R.<name>), and the math library.

function say(...args) { host.stdout(args.map(gist).join('') + '\n'); return true; }
function print(...args) { host.stdout(args.map(str).join('')); return true; }
function put(...args) { host.stdout(args.map(str).join('') + '\n'); return true; }
function note(...args) { host.stderr((args.length ? args.map(gist).join('') : 'Noted') + '\n'); return true; }
function printf(fmtS, ...args) { host.stdout(sprintf(fmtS, ...args)); return true; }
function dd(...args) { for (const a of args) host.stderr(raku(a) + '\n'); return Nil; }
function exit(code) { throw new ExitCtl(code === undefined ? 0 : Number(toInt(code))); }

// --- math ----------------------------------------------------------------------
function fnum(f) { return v => numResult(f(toFloat(v))); }
const sqrt = v => { if (isIntVal(v)) { const b = big(v); if (b >= 0n && b > BIG_MAX_SAFE) { const r = bigSqrt(b); if (r * r === b) return normBig(r); } } const f = toFloat(v); return numResult(Math.sqrt(f)); };
function bigSqrt(n) { if (n < 2n) return n; let x = BigInt(Math.floor(Math.sqrt(Number(n)))); while (x * x > n) x--; while ((x + 1n) * (x + 1n) <= n) x++; return x; }
const sin = fnum(Math.sin), cos = fnum(Math.cos), tan = fnum(Math.tan), asin = fnum(Math.asin), acos = fnum(Math.acos), atan = fnum(Math.atan),
      sinh = fnum(Math.sinh), cosh = fnum(Math.cosh), tanh = fnum(Math.tanh), exp = fnum(Math.exp), cbrt = fnum(Math.cbrt);
function log(v, base) { const f = toFloat(v); if (base === undefined) return numResult(Math.log(f)); return numResult(Math.log(f) / Math.log(toFloat(base))); }
function log2(v) { return numResult(Math.log2(toFloat(v))); }
function log10(v) { return numResult(Math.log10(toFloat(v))); }
function atan2(a, b) { return numResult(Math.atan2(toFloat(a), toFloat(b))); }
function floor(v) { const x = toNumeric(v); if (isIntVal(x)) return x; if (x instanceof RRat) { let q = x.n / x.d; if (x.n < 0n && x.n % x.d !== 0n) q -= 1n; return normBig(q); } const f = toFloat(x); return Number.isFinite(f) ? safeInt(Math.floor(f)) : f; }
function ceiling(v) { const x = toNumeric(v); if (isIntVal(x)) return x; if (x instanceof RRat) { let q = x.n / x.d; if (x.n > 0n && x.n % x.d !== 0n) q += 1n; return normBig(q); } const f = toFloat(x); return Number.isFinite(f) ? safeInt(Math.ceil(f)) : f; }
function truncate(v) { const x = toNumeric(v); if (isIntVal(x)) return x; if (x instanceof RRat) return normBig(x.n / x.d); const f = toFloat(x); return Number.isFinite(f) ? safeInt(Math.trunc(f)) : f; }
function safeInt(f) { return Number.isSafeInteger(f) ? f : normBig(BigInt(f)); }
function round(v, scale) {
    const x = toNumeric(v);
    if (scale !== undefined && toFloat(scale) < 0) scale = neg(scale);   // a negative scale rounds like its magnitude
    if (scale === undefined) {
        if (isIntVal(x)) return x;
        if (x instanceof RRat) { // round half up, exactly
            let n = x.n * 2n + x.d; let q = n / (2n * x.d); if (n < 0n && n % (2n * x.d) !== 0n) q -= 1n; return normBig(q);
        }
        const f = toFloat(x); return Number.isFinite(f) ? safeInt(Math.floor(f + 0.5)) : f;
    }
    const s = toNumeric(scale);
    if ((s instanceof RRat || isIntVal(s)) && Number.isFinite(toFloat(x)) && !(s instanceof RRat && s.d === 0n)) {
        const [sn, sd] = asRat(s);          // round(x / s) * s
        const [xn, xd] = asRat(x instanceof RRat || isIntVal(x) ? x : mkRat(BigInt(Math.round(toFloat(x) * 1e12)), 1000000000000n));
        // q = x/s = xn*sd / (xd*sn)
        const qn = xn * sd, qd = xd * sn;
        let n = qn * 2n + qd; let q = n / (2n * qd); if (n < 0n && n % (2n * qd) !== 0n) q -= 1n;
        if (isIntVal(s)) return normBig(q * sn);   // the answer takes the SCALE's type: an Int scale gives an Int
        return ratResult(q * sn, sd);
    }
    const f = toFloat(x), sc = toFloat(s);
    return numResult(Math.floor(f / sc + 0.5) * sc);
}
function sign(v) { const c = numCmp(v, 0); return Number.isNaN(c) ? NaN : c; }
function isPrime(v) {
    const n = toNumeric(v);
    if (!isIntVal(n)) return false;
    if (typeof n === 'bigint') { if (n < 2n) return false; return millerRabin(n); }
    if (n < 2) return false; if (n < 4) return true; if (n % 2 === 0 || n % 3 === 0) return false;
    for (let i = 5; i * i <= n; i += 6) if (n % i === 0 || n % (i + 2) === 0) return false;
    return true;
}
function millerRabin(n) {
    if (n % 2n === 0n) return n === 2n;
    let d = n - 1n, r = 0n; while (d % 2n === 0n) { d /= 2n; r++; }
    const modpow = (b, e, m) => { let res = 1n; b %= m; while (e > 0n) { if (e & 1n) res = res * b % m; b = b * b % m; e >>= 1n; } return res; };
    for (const a of [2n, 3n, 5n, 7n, 11n, 13n, 17n, 19n, 23n, 29n, 31n, 37n]) {
        if (a >= n) break;
        let x = modpow(a, d, n); if (x === 1n || x === n - 1n) continue;
        let comp = true;
        for (let i = 1n; i < r; i++) { x = x * x % n; if (x === n - 1n) { comp = false; break; } }
        if (comp) return false;
    }
    return true;
}
function expmod(b, e, m) { let base = big(b) % big(m), ex = big(e), mod = big(m), res = 1n; if (ex < 0n) throw new RakuError('expmod with negative exponent'); while (ex > 0n) { if (ex & 1n) res = res * base % mod; base = base * base % mod; ex >>= 1n; } return normBig(res); }
function polymod(v, ...mods) {
    let n = big(v); const out = [];
    // the moduli may be a list, and a lazy one (`2**32 xx *`): stop when nothing is left
    const src = mods.length === 1 && (mods[0] instanceof RList || mods[0] instanceof RSeq || mods[0] instanceof RRange) ? mods[0] : mods;
    const lazy = src instanceof RSeq && src.lazy;
    for (const m of iter(src)) {
        if (lazy && n === 0n) break;
        const mm = big(m); if (mm === 0n) throw new RakuError('Attempt to divide by zero using polymod', 'X::Numeric::DivideByZero');
        let r = n % mm; if (r < 0n) r += mm; out.push(normBig(r)); n = (n - r) / mm;
    }
    if (!lazy || n !== 0n) out.push(normBig(n));
    return mkList(out);
}
function factorial(v) { let n = big(v), r = 1n; for (let i = 2n; i <= n; i++) r *= i; return normBig(r); }
function rand() { return host.random(); }
function randNum(v) { return numResult(toFloat(v) * host.random()); }
function srand(seed) { host.srand(Number(toInt(seed))); return seed; }
function roundTo(v, n) { return round(v, n); }
function minmaxOf(...args) { const [pos, named] = splitArgs(args); const src = pos.length === 1 ? pos[0] : mkList(pos); const by = named.get('by'); return by ? range(minOf(src, by), maxOf(src, by)) : minmax(src); }
function min(...args) { const [pos, named] = splitArgs(args); return minOf(pos.length === 1 ? pos[0] : mkList(pos), named.get('by')); }
function max(...args) { const [pos, named] = splitArgs(args); return maxOf(pos.length === 1 ? pos[0] : mkList(pos), named.get('by')); }
function sum(...args) { return sumList(args.length === 1 ? args[0] : mkList(args)); }
function elems(v) { return elemsOf(v); }
function end(v) { return elemsOf(v) - 1; }
function join(sep, ...items) { return joinList(mkList(spliceSlips(items)), sep); }
function reverse(...items) { return reverseList(items.length === 1 ? items[0] : mkList(items)); }
function sort(...args) {
    let f; if (args.length > 1 && typeof args[0] === 'function') f = args.shift();
    return sortList(args.length === 1 ? args[0] : mkList(args), f);
}
function map(f, ...items) { return mapList(items.length === 1 ? items[0] : mkList(items), f); }
function grep(f, ...items) { return grepList(items.length === 1 ? items[0] : mkList(items), f); }
function first(f, ...items) { let named; if (items.length && items[items.length - 1] instanceof RNamed) named = items.pop().m; return firstOf(items.length === 1 ? items[0] : mkList(items), f, named); }
function unique(...items) { return uniqueList(items.length === 1 ? items[0] : mkList(items)); }
function keys(v) { return keysOf(v); }
function values(v) { return valuesOf(v); }
function kv(v) { return kvOf(v); }
function pairs(v) { return pairsOf(v); }
function push(a, ...items) { return pushTo(a, ...items); }
function append(a, ...items) { return appendTo(a, ...items); }
function pop(a) { return popFrom(a); }
function shift(a) { return shiftFrom(a); }
function unshift(a, ...items) { return unshiftTo(a, ...items); }
function prepend(a, ...items) { return prependTo(a, ...items); }
function splice(a, ...rest) { return spliceArr(a, ...rest); }
function zip(...lists) { let named; if (lists.length && lists[lists.length - 1] instanceof RNamed) named = lists.pop().m; return zipLists(lists, named && named.get('with')); }
function roundrobin(...lists) { const as = lists.map(arr); const n = Math.max(0, ...as.map(a => a.length)); const out = []; for (let i = 0; i < n; i++) { const row = []; for (const a of as) if (i < a.length) row.push(a[i]); out.push(mkList(row)); } return mkSeq(out); }
function head(v, n) { return headOf(v, n); }
function tail(v, n) { return tailOf(v, n); }
function defd(v) { return defined(v); }
function flatten(...items) { return flat(items.length === 1 ? items[0] : mkList(items)); }
// pick(N, @list) / roll(N, @list): the sub form takes the count first
const countFirst = (v, n) => n !== undefined && (v instanceof RWhatever || v === T.Whatever || typeof v === 'number' || typeof v === 'bigint') && (n instanceof RList || n instanceof RSeq || n instanceof RRange);
// :16<ff> / :16("ff") — every character a digit of that base, `_` between digits, one radix point; BigInt throughout
function radix(base, sv) {
    base = Number(toInt(base)); const s = str(sv);
    const bad = (why) => { throw new RakuError(`Cannot convert string to number: ${why} in ':${base}<${s}>'`, 'X::Str::Numeric'); };
    let val = 0n, den = 0n; const bb = BigInt(base); let any = false;
    for (let i = 0; i < s.length; i++) {
        const c = s[i];
        if (c === '_') { if (i === 0 || i + 1 === s.length) bad("'_' must be between digits"); continue; }
        if (c === '.') { if (den !== 0n) bad('more than one radix point'); den = 1n; continue; }
        const code = c.charCodeAt(0);
        const d = code >= 48 && code <= 57 ? code - 48 : code >= 97 && code <= 122 ? code - 87 : code >= 65 && code <= 90 ? code - 55 : -1;
        if (d < 0 || d >= base) bad(`base-${base} number must begin with valid digits or '.'`);
        val = val * bb + BigInt(d); if (den !== 0n) den *= bb; any = true;
    }
    if (!any) bad(`base-${base} number must begin with valid digits or '.'`);
    if (den > 1n) return ratResult(val, den);
    return normBig(val);
}
function radixList(base, ...digits) { const bb = BigInt(Number(toInt(base))); let v = 0n; for (const d of digits) { if (d instanceof RNamed) continue; for (const x of itemsOf(d)) v = v * bb + BigInt(toInt(x)); } return normBig(v); }
// val(Str): an allomorph when the string spells a number, else the string; MAIN's arguments come this way
function val(s) { s = str(s); try { if (s.trim() === '') return s; const n = strToNumeric(s); return new RAllo(n, s); } catch (e) { return s; } }
function pick(v, n) { return countFirst(v, n) ? pickFrom(n, v) : pickFrom(v, n); }
function roll(v, n) { return countFirst(v, n) ? rollFrom(n, v) : rollFrom(v, n); }
function categorize(f, v, ...a) { return categorizeList(v, f, nm(a).get("as")); }
function classify(f, v, ...a) { return classifyList(v, f, nm(a).get("as")); }
function reduce(f, ...items) { return reduceList(f, items.length === 1 ? items[0] : mkList(items)); }
function produce(f, ...items) { return produceList(f, items.length === 1 ? items[0] : mkList(items)); }
function anyJ(...items) { return junction('any', items.length === 1 ? items[0] : mkList(items)); }
function allJ(...items) { return junction('all', items.length === 1 ? items[0] : mkList(items)); }
function noneJ(...items) { return junction('none', items.length === 1 ? items[0] : mkList(items)); }
function oneJ(...items) { return junction('one', items.length === 1 ? items[0] : mkList(items)); }
function set(...items) { return mkSetty(T.Set, items); }
function bag(...items) { return mkSetty(T.Bag, items); }
function mix(...items) { return mkSetty(T.Mix, items); }
function infix(op) { return OPS[op]; }
function chrsOf(...v) { return chrs(mkList(v)); }
function ordsOf(v) { return ords(v); }
function ucfirst(s) { return tc(s); }
function slurpB(...args) { return host.slurp(...args); }
function spurtB(...args) { return host.spurt(...args); }
function linesB(...args) { return args.length === 0 || args[0] instanceof RNamed ? host.stdinLines() : lines(args[0]); }
function get() { return host.stdinGet(); }
function prompt(msg) { if (msg !== undefined) host.stdout(str(msg)); host.flush(); return host.stdinGet(); }
function sleep(s) { host.sleep(toFloat(s === undefined ? Infinity : s)); return true; }
function now() { return numResult(host.now()); }
function time() { return Math.floor(host.now()); }
function open(...args) { return host.open(...args); }
function close(h) { return host.close(h); }
function mkdir(...a) { return host.mkdir(...a); }
function rmdir(...a) { return host.rmdir(...a); }
function unlink(...a) { return host.unlink(...a); }
function dir(...a) { return host.dir(...a); }
function chdir(...a) { return host.chdir(...a); }
function shell(...a) { return host.shell(...a); }
function run(...a) { return host.run(...a); }
function ioPath(p) { return p instanceof RIOPath ? p : new RIOPath(str(p)); }
function EVAL() { throw new RakuError('EVAL is not available in transpiled JavaScript'); }

// Operator table: `[+] @a`, `&infix:<+>`, `.reduce(&[+])`, Z+, X*
const OPS = {
    '+': add, '-': sub, '*': mul, '/': div, '%': mod, '**': pow, 'div': idiv, 'mod': imod, 'gcd': gcd, 'lcm': lcm,
    '~': concat, 'x': xrepeat, 'xx': listRepeat,
    '==': numeq, '!=': numne, '<': lt, '<=': le, '>': gt, '>=': ge, 'eq': seq, 'ne': sne, 'lt': slt, 'le': sle, 'gt': sgt, 'ge': sge,
    '<=>': spaceship, 'cmp': cmp, 'leg': leg, '===': identical, 'eqv': eqv, '=:=': identical,
    '&&': (a, b) => truthy(a) ? b : a, '||': (a, b) => truthy(a) ? a : b, '//': (a, b) => defined(a) ? a : b, 'and': (a, b) => truthy(a) ? b : a, 'or': (a, b) => truthy(a) ? a : b,
    'min': (a, b) => cmpNum(a, b) <= 0 ? a : b, 'max': (a, b) => cmpNum(a, b) >= 0 ? a : b,
    '+&': bitand, '+|': bitor, '+^': bitxor, '+<': shl, '+>': shr,
    '=>': pair, ',': (...a) => mkList(a), '~~': smartmatch, '!~~': (a, b) => !smartmatch(a, b),
    '(elem)': elem, '∈': elem, '(cont)': (a, b) => elem(b, a), '∋': (a, b) => elem(b, a),
    '(|)': (a, b) => setOp('∪', a, b), '∪': (a, b) => setOp('∪', a, b), '(&)': (a, b) => setOp('∩', a, b), '∩': (a, b) => setOp('∩', a, b),
    '(-)': (a, b) => setOp('∖', a, b), '∖': (a, b) => setOp('∖', a, b), '(^)': (a, b) => setOp('⊖', a, b), '⊖': (a, b) => setOp('⊖', a, b), '(+)': (a, b) => setOp('⊎', a, b), '⊎': (a, b) => setOp('⊎', a, b),
    '(<=)': (a, b) => setRel('⊆', a, b), '⊆': (a, b) => setRel('⊆', a, b), '(<)': (a, b) => setRel('⊂', a, b), '⊂': (a, b) => setRel('⊂', a, b),
    '(>=)': (a, b) => setRel('⊇', a, b), '⊇': (a, b) => setRel('⊇', a, b), '(>)': (a, b) => setRel('⊃', a, b), '⊃': (a, b) => setRel('⊃', a, b), '(==)': (a, b) => setRel('≡', a, b), '≡': (a, b) => setRel('≡', a, b),
    '..': (a, b) => range(a, b), '..^': (a, b) => range(a, b, false, true), '^..': (a, b) => range(a, b, true, false), '^..^': (a, b) => range(a, b, true, true),
    '^': (a, b) => range(a, b, false, true),
    '?&': (a, b) => truthy(a) && truthy(b), '?|': (a, b) => truthy(a) || truthy(b), '?^': (a, b) => truthy(a) !== truthy(b),
    'xor': (a, b) => { const ta = truthy(a), tb = truthy(b); return ta !== tb ? (ta ? a : b) : (ta ? Nil : b); },
    '!==': (a, b) => !numeq(a, b), '!eq': (a, b) => !seq(a, b), '!<': (a, b) => !lt(a, b),
    'Z': (a, b) => zipLists([a, b]), 'X': (a, b) => crossLists([a, b]),
    'but': (a, b) => a, 'does': (a, b) => a,
    'o': (f, g) => (...a) => f(g(...a)), '∘': (f, g) => (...a) => f(g(...a)),
    'before': (a, b) => cmpNum(a, b) < 0, 'after': (a, b) => cmpNum(a, b) > 0, 'minmax': (a, b) => !defined(a) ? range(b, b) : !defined(b) ? range(a, a) : cmpNum(a, b) <= 0 ? range(a, b) : range(b, a), '!===': (a, b) => !identical(a, b),
    'unicmp': (a, b) => leg(a, b), 'coll': (a, b) => leg(a, b), '!eqv': (a, b) => !eqv(a, b), '!=:=': (a, b) => !identical(a, b),
    '!(elem)': (a, b) => !elem(a, b), '∉': (a, b) => !elem(a, b), '!(cont)': (a, b) => !elem(b, a), '∌': (a, b) => !elem(b, a),
    '~&': (a, b) => strBitOp(a, b, (x, y) => x & y), '~|': (a, b) => strBitOp(a, b, (x, y) => x | y), '~^': (a, b) => strBitOp(a, b, (x, y) => x ^ y),
};
function strBitOp(a, b, f) { const x = str(a), y = str(b); const n = Math.max(x.length, y.length); let o = ''; for (let i = 0; i < n; i++) o += String.fromCharCode(f(x.charCodeAt(i) || 0, y.charCodeAt(i) || 0)); return o; }
const OP_IDENTITY = { '+': 0, '-': 0, '*': 1, '~': '', '&&': true, '||': false, 'and': true, 'or': false, 'min': Infinity, 'max': -Infinity, '+|': 0, '+&': -1, '+^': 0, 'gcd': 0, 'lcm': 1, '?|': false, '?&': true, '?^': false, 'xor': false, '**': 1, '(|)': undefined };
function opFn(op) { let f = OPS[op]; if (!f && op[0] === '!' && OPS[op.slice(1)]) { const g = OPS[op.slice(1)]; f = (a, b) => !truthy(g(a, b)); }   // [!after]: the negated operator
    if (!f && op[0] === 'R' && OPS[op.slice(1)]) { const g = OPS[op.slice(1)]; f = (a, b) => g(b, a); }   // [R//]: the reversed operator
    if (!f) throw new RakuError(`operator ${op} is not in the JS core yet`); f.opName = op; f.rightAssoc = RIGHT_OPS.has(op); return f; }
// [op] LIST — the reduce metaoperator, with chaining for comparison ops
const CHAIN_OPS = new Set(['==', '!=', '<', '<=', '>', '>=', 'eq', 'ne', 'lt', 'le', 'gt', 'ge', '===', 'eqv', '=:=', '~~', 'before', 'after']);
const isChainOp = (op) => CHAIN_OPS.has(op[0] === '!' ? op.slice(1) : op);   // [!after] chains like [after]
const RIGHT_OPS = new Set(['**', '=>', 'xx', 'x']);
function reduceOp(op, v, triangle) {
    if (op === 'R,' && triangle) return mkSeq(arr(v).map((_, i, items) => mkList(items.slice(0, i + 1).reverse())));   // [\R,]: each prefix reversed
    if (op[0] === 'R' && !OPS[op] && OPS[op.slice(1)]) return reduceOp(op.slice(1), mkList(arr(v).slice().reverse()), triangle);   // [R-] 1,2,3 is 3-2-1: the list reversed, folded left ([\R-]: 3 1 0)
    const items = arr(v);
    const f = opFn(op);
    if (triangle) {
        if (!items.length) return mkSeq([]);
        if (op === ',') return mkSeq(items.map((_, i) => mkList(items.slice(0, i + 1))));   // [\,]: the flat prefixes
        if (isChainOp(op)) { const out = [true]; let acc = true; for (let i = 1; i < items.length; i++) { acc = acc && truthy(f(items[i - 1], items[i])); out.push(acc); } return mkSeq(out); }   // [\!eq] 1,2,3: True, then each prefix
        if (RIGHT_OPS.has(op)) {   // [\**] 2,3,4 → (4 81 2**81): the folds from the right, in that order
            const out = []; let acc = items[items.length - 1]; out.push(acc);
            for (let i = items.length - 2; i >= 0; i--) { acc = f(items[i], acc); out.push(acc); }
            return mkSeq(out);
        }
        const out = []; let acc = items[0]; out.push(acc);
        for (let i = 1; i < items.length; i++) { acc = f(acc, items[i]); out.push(acc); }
        return mkSeq(out);
    }
    if (v instanceof RRange && v.isInfinite()) { if (op === '+' || op === '*') return Infinity; throw new RakuError('Cannot reduce an infinite Range'); }
    if (!items.length) { if (op in OP_IDENTITY && OP_IDENTITY[op] !== undefined) return OP_IDENTITY[op]; return Nil; }
    if (isChainOp(op)) { for (let i = 0; i + 1 < items.length; i++) if (!truthy(f(items[i], items[i + 1]))) return false; return true; }
    if (op === '<=>' || op === 'cmp' || op === 'leg') { if (items.length === 1) return Nil; let acc = f(items[0], items[1]); for (let i = 2; i < items.length && (acc instanceof REnum && acc.val === 0); i++) acc = f(items[i - 1], items[i]); return acc; }
    if (op === ',') return mkList(items.slice());
    if (RIGHT_OPS.has(op)) { let acc = items[items.length - 1]; for (let i = items.length - 2; i >= 0; i--) acc = f(items[i], acc); return acc; }
    if (op === '..' || op === '..^') return items.length === 2 ? f(items[0], items[1]) : Nil;
    let acc = items[0];
    for (let i = 1; i < items.length; i++) acc = f(acc, items[i]);
    return acc;
}
// Zop / Xop metaoperators
function zipOp(op, a, b) { return zipLists([a, b], opFn(op)); }
function crossOp(op, a, b) { return crossLists([a, b], opFn(op)); }
// hyper operators: >>op<<, <<op>>, >>op>>, <<op<<
function hyperOp(op, a, b, dwimL, dwimR) {
    const f = opFn(op);
    const la = a instanceof RList || a instanceof RSeq || a instanceof RRange, lb = b instanceof RList || b instanceof RSeq || b instanceof RRange;
    if (a instanceof RHash && b instanceof RHash) { const h = new RHash(); const keys = new Set([...a.m.keys(), ...b.m.keys()]); for (const k of keys) { if (!dwimL && !dwimR && !(a.m.has(k) && b.m.has(k))) continue; h.m.set(k, f(a.m.has(k) ? a.m.get(k) : Any, b.m.has(k) ? b.m.get(k) : Any)); } return h; }
    if (a instanceof RHash) { const h = new RHash(); for (const [k, v] of a.m) h.m.set(k, f(v, b)); return h; }
    if (b instanceof RHash) { const h = new RHash(); for (const [k, v] of b.m) h.m.set(k, f(a, v)); return h; }
    if (la && lb) {
        const x = arr(a), y = arr(b);
        let n = Math.max(x.length, y.length);
        if (!dwimL && !dwimR && x.length !== y.length) throw new RakuError(`Lists on either side of non-dwimmy hyperop of infix:<${op}> are not of the same length while recursing\nleft: ${x.length} elements, right: ${y.length} elements`, 'X::HyperOp::NonDWIM');
        if (dwimL && !dwimR) n = y.length; if (dwimR && !dwimL) n = x.length;
        const out = []; for (let i = 0; i < n; i++) out.push(hyperOp(op, x[i % x.length], y[i % y.length], dwimL, dwimR));
        return a.ty === T.Array || a instanceof RList ? mkList(out, a instanceof RList ? a.ty : T.List) : mkList(out);
    }
    if (la) { const x = arr(a); return mkList(x.map(e => hyperOp(op, e, b, dwimL, dwimR)), a instanceof RList ? a.ty : T.List); }
    if (lb) { const y = arr(b); return mkList(y.map(e => hyperOp(op, a, e, dwimL, dwimR)), b instanceof RList ? b.ty : T.List); }
    return f(a, b);
}
function hyperPrefix(op, a) { const f = { '-': neg, '!': not, '?': so, '~': str, '+': numify, 'abs': abs }[op] || (x => x); if (a instanceof RList || a instanceof RSeq || a instanceof RRange) return mkList(arr(a).map(x => hyperPrefix(op, x)), a instanceof RList ? a.ty : T.List); return f(a); }
function assumingCall(f, ...pre) { const g = (...rest) => f(...pre, ...rest); g.arity = Math.max(0, (f.arity ?? f.length) - pre.length); return g; }
// the `...` sequence operator
function seqOp(items, endv, exclusive) {
    const lazy = endv instanceof RWhatever || endv === Infinity;
    const starts = items.slice();
    let genFn = null;
    if (starts.length && typeof starts[starts.length - 1] === 'function') genFn = starts.pop();
    const endTest = typeof endv === 'function' ? (x => truthy(endv(x))) : lazy ? (() => false) : (x => smartmatch(x, endv) || (isNumeric(x) && isNumeric(endv) && numeq(x, endv)));
    // deduce the rule from the start values
    if (!genFn) {
        const n = starts.length;
        if (n >= 3 && starts.every(isNumeric)) {
            const d1 = sub(starts[1], starts[0]), d2 = sub(starts[2], starts[1]);
            if (numeq(d1, d2)) genFn = x => add(x, d1);
            else if (!numeq(starts[0], 0) && !numeq(starts[1], 0) && numeq(div(starts[1], starts[0]), div(starts[2], starts[1]))) { const r = div(starts[1], starts[0]); genFn = x => { const y = mul(x, r); return (y instanceof RRat && y.d === 1n) ? normBig(y.n) : y; }; }
            else throw new RakuError('Unable to deduce arithmetic or geometric sequence from: ' + starts.map(str).join(',') + '. Did you try to use a Whatever (*) without a block?');
        } else if (n === 2 && starts.every(isNumeric)) { const d = sub(starts[1], starts[0]); genFn = x => add(x, d); }
        else if (n === 1 && isNumeric(starts[0])) { genFn = !lazy && typeof endv !== 'function' && isNumeric(endv) && lt(endv, starts[0]) ? (x => sub(x, 1)) : (x => add(x, 1)); }
        else if (n >= 1 && typeof starts[n - 1] === 'string') genFn = x => strSucc(x);
        else throw new RakuError('Unable to deduce arithmetic or geometric sequence from: ' + starts.map(str).join(','));
    }
    const arity = genFn.arity !== undefined ? genFn.arity : genFn.length;
    const wantsAll = genFn.slurpy;
    // A numeric sequence stops when it PASSES the endpoint, not only when it
    // lands on it: `1, 3 ...^ 2` is `(1)`, because 3 is already past 2. Without
    // this the step steps straight over the limit and the sequence never ends.
    const numericEnd = !lazy && typeof endv !== 'function' && isNumeric(endv);
    const crossed = (p, v) => numericEnd && p !== undefined && isNumeric(p) && isNumeric(v) &&
        ((lt(p, endv) && gt(v, endv)) || (gt(p, endv) && lt(v, endv)));
    return new RSeq((function* () {
        const hist = [];
        let prev;
        for (const s of starts) {
            if (endTest(s)) { if (!exclusive) yield s; return; }
            if (crossed(prev, s)) return;
            yield s; hist.push(s); prev = s;
        }
        for (;;) {
            let nx;
            if (wantsAll) nx = genFn(...hist);
            else if (arity <= 0) nx = genFn();
            else nx = genFn(...hist.slice(-arity));
            if (nx instanceof RSlip) { for (const x of nx.a) { if (endTest(x)) { if (!exclusive) yield x; return; } if (crossed(prev, x)) return; yield x; hist.push(x); prev = x; } continue; }
            if (endTest(nx)) { if (!exclusive) yield nx; return; }
            if (crossed(prev, nx)) return;
            yield nx; hist.push(nx); prev = nx;
            if (hist.length > 64) hist.splice(0, hist.length - 64);
        }
    })(), lazy);
}
function lazyOf(v) { if (v instanceof RSeq) { v.lazy = true; return v; } if (v instanceof RRange) return new RSeq(v[Symbol.iterator](), true); return v; }
function eagerOf(v) { if (v instanceof RSeq) return v.cache(); return v; }
function cacheOf(v) { if (v instanceof RSeq) return v.list(); if (v instanceof RRange) return mkList(v.arr()); return v; }

Object.assign(R, {
    say, print, put, note, printf, dd, exit, sqrt, sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, exp, cbrt, log, log2, log10, atan2,
    floor, ceiling, truncate, round, sign, 'is-prime': isPrime, expmod, polymod, factorial, rand, randNum, srand, min, max, sum, elems, end, join, reverse, sort, map, grep, first,
    unique, keys, values, kv, pairs, push, append, pop, shift, unshift, prepend, splice, zip, roundrobin, head, tail, defined: defd, item, flat: flatten, pick, roll,
    categorize, classify, opFn, val, allo, '__radix': radix, '__radix-list': radixList, reduce, produce, any: anyJ, all: allJ, none: noneJ, one: oneJ, set, bag, mix, chrs: chrsOf, ords: ordsOf, ucfirst, slurp: slurpB, spurt: spurtB,
    lines: linesB, get, prompt, sleep, now, time, open, close, mkdir, rmdir, unlink, dir, chdir, shell, run, ioPath, EVAL, OPS, opFn, reduceOp, zipOp, crossOp, hyperOp, hyperPrefix,
    assumingCall, seqOp, lazyOf, eagerOf, cacheOf, chars, ord, chr, uc, lc, tc, tclc, flip, trim, chomp, chop, substr, index: strIndex, rindex: strRindex, split: (sep, s, ...a) => strSplit(s, sep, ...a), words, comb,
    sprintf, abs, gcd, lcm, not, so, gist, raku, str, numify, truncate, 'trim-leading': trimLeading, 'trim-trailing': trimTrailing, samecase, indent, fc, wordcase, minmax: minmaxOf, 'is-prime': isPrime, warn, die, fail, take,
    unival, exists: (v) => defined(v), squish: (...items) => squishList(items.length === 1 ? items[0] : mkList(items)),
    'rotor': (v, ...specs) => { const nm_ = specs.filter(x => x instanceof RNamed), ps = specs.filter(x => !(x instanceof RNamed)); const isL = x => x instanceof RList || x instanceof RSeq || x instanceof RRange; return (ps.length && !isL(v) && isL(ps[ps.length - 1])) ? rotorList(ps[ps.length - 1], v, ...ps.slice(0, -1), ...nm_) : rotorList(v, ...specs); }, 'batch': (v, n) => batchList(v, n), combinations: (v, n) => combinations(v, n), permutations: (v) => permutations(v),
    hash: (...a) => hashLit(a), list: list, slip, elem, cross: (...l) => crossLists(l), 'roundrobin': roundrobin, antipairs: antipairsOf, invert: invertOf, 'succ': succ, 'pred': pred, chdir, 'lazy': lazyOf, 'eager': eagerOf, cache: cacheOf,
    'DateTime': T.DateTime, 'Date': T.Date, capture, 'infix': infix,
});
