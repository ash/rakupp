// Method dispatch. `mc(inv, name, ...args)` finds the method on the invocant's
// type (user methods first, then the core tables below, then Any/Mu), and calls
// it as `m(inv, ...args)`. Named arguments travel as a trailing RNamed.

function M(ty, table) { Object.assign(ty.methods, table); }
// a mapper may be a Code, or a List/Hash indexed by the value
function mapperFn(f) { return typeof f === 'function' ? f : (x) => (f instanceof RHash ? hget(f, x) : aget(f, x)); }
function nm(args) { return args.length && args[args.length - 1] instanceof RNamed ? args[args.length - 1].m : EMPTY_MAP; }
function posArgs(args) { return args.length && args[args.length - 1] instanceof RNamed ? args.slice(0, -1) : args; }
function optArg(args, i) { const p = posArgs(args); return p[i]; }

// ---- Mu / Any: everything answers these ---------------------------------------
M(T.Mu, {
    gist: (s) => gist(s), Str: (s) => str(s), raku: (s) => raku(s), perl: (s) => raku(s),
    say: (s) => say(s), print: (s) => print(s), put: (s) => put(s), note: (s) => note(s),
    defined: (s) => defined(s), Bool: (s) => truthy(s), so: (s) => truthy(s), not: (s) => !truthy(s),
    WHAT: (s) => typeOf(s), WHICH: (s) => whichKey(s), WHERE: (s) => objId(s),
    isa: (s, t) => isa(s, typeof t === 'string' ? t : t), does: (s, t) => isa(s, t), can: (s, n) => { const m = typeOf(s).find(str(n)); return m ? mkList([m]) : mkList([]); },
    new: (s, ...args) => construct(s instanceof RType ? s : typeOf(s), ...args),
    clone: (s, ...args) => cloneObj(s, ...args),
    item: (s) => s, self: (s) => s, sink: (s) => Nil, return: (s) => s,
    ACCEPTS: (s, v) => smartmatch(v, s), 'ACCEPTS': (s, v) => smartmatch(v, s),
    elems: (s) => elemsOf(s), end: (s) => elemsOf(s) - 1, list: (s) => s instanceof RList && s.ty !== T.Seq ? s : mkList(itemsOf(s).slice()), List: (s) => mkList(itemsOf(s).slice()),
    Array: (s) => newArray(s), Seq: (s) => mkSeq(itemsOf(s).slice()), Slip: (s) => mkSlip(itemsOf(s).slice()), flat: (s) => flat(s), eager: (s) => eagerOf(s), lazy: (s) => lazyOf(s), cache: (s) => cacheOf(s),
    'is-lazy': (s) => s instanceof RSeq && s.lazy, iterator: (s) => iter(s)[Symbol.iterator](), hash: (s) => newHash(s), Hash: (s) => newHash(s),
    map: (s, f) => mapList(s, f), grep: (s, f) => grepList(s, f), first: (s, ...a) => firstOf(s, posArgs(a)[0], nm(a)), join: (s, sep) => joinList(s, sep), sort: (s, f) => sortList(s, f), reverse: (s) => reverseList(s),
    sum: (s) => sumList(s), min: (s, ...a) => nm(a).size ? minMaxAdv(s, posArgs(a)[0], false, nm(a)) : minOf(s, posArgs(a)[0]), max: (s, ...a) => nm(a).size ? minMaxAdv(s, posArgs(a)[0], true, nm(a)) : maxOf(s, posArgs(a)[0]), minmax: (s, ...a) => { const f = posArgs(a)[0] ?? nm(a).get('by'); return f ? range(minOf(s, f), maxOf(s, f)) : minmax(s); }, unique: (s, ...a) => uniqueList(s, nm(a).get('as') || nm(a).get('with')), squish: (s) => squishList(s),
    head: (s, n) => headOf(s, n), tail: (s, n) => tailOf(s, n), keys: (s) => keysOf(s), values: (s) => valuesOf(s), kv: (s) => kvOf(s), pairs: (s) => pairsOf(s), antipairs: (s) => antipairsOf(s), invert: (s) => invertOf(s),
    pick: (s, n) => pickFrom(s, n), roll: (s, n) => rollFrom(s, n), reduce: (s, f) => reduceList(f, s), produce: (s, f) => produceList(f, s), classify: (s, ...a) => classifyList(s, posArgs(a)[0], nm(a).get('as')), categorize: (s, ...a) => categorizeList(s, posArgs(a)[0], nm(a).get('as')), repeated: (s, ...a) => repeatedList(s, nm(a).get('as') || nm(a).get('with')),
    combinations: (s, n) => combinations(s, n), permutations: (s) => permutations(s), rotate: (s, n) => rotateList(s, n), rotor: (s, ...sp) => rotorList(s, ...sp), batch: (s, n) => batchList(s, n),
    any: (s) => junction('any', s), all: (s) => junction('all', s), none: (s) => junction('none', s), one: (s) => junction('one', s),
    Set: (s) => toSetty(s, T.Set), SetHash: (s) => toSetty(s, T.SetHash), Bag: (s) => toSetty(s, T.Bag), BagHash: (s) => toSetty(s, T.BagHash), Mix: (s) => toSetty(s, T.Mix), MixHash: (s) => toSetty(s, T.MixHash),
    Numeric: (s) => toNumeric(s), Int: (s) => toInt(toNumeric(s)), Num: (s) => mkNum(toFloat(s)), Rat: (s) => { const n = toNumeric(s); return n instanceof RRat ? n : isIntVal(n) ? mkRat(big(n), 1n) : floatToRat(toFloat(n)); }, Real: (s) => toNumeric(s),
    Stringy: (s) => str(s), chars: (s) => chars(s), uc: (s) => uc(s), lc: (s) => lc(s), tc: (s) => tc(s), tclc: (s) => tclc(s), fc: (s) => fc(s), wordcase: (s) => wordcase(s), flip: (s) => flip(s), trim: (s) => trim(s),
    'trim-leading': (s) => trimLeading(s), 'trim-trailing': (s) => trimTrailing(s), chomp: (s) => chomp(s), chop: (s, n) => chop(s, n), ord: (s) => ord(s), chr: (s) => chr(s), ords: (s) => ords(s), chrs: (s) => chrs(s),
    index: (s, n, st) => strIndex(s, n, st), rindex: (s, n, st) => strRindex(s, n, st), contains: (s, n, st) => contains(s, n, st), 'starts-with': (s, p) => startsWith(s, p), 'ends-with': (s, p) => endsWith(s, p),
    substr: (s, f, l) => substr(s, f, l), split: (s, ...a) => strSplit(s, posArgs(a)[0], posArgs(a)[1], nm(a)), words: (s, n) => words(s, n), lines: (s) => lines(s), comb: (s, p, l) => comb(s, p, l), indent: (s, n) => indent(s, n),
    fmt: (s, f) => fmt(s, f), succ: (s) => inc(s), pred: (s) => dec(s), samecase: (s, p) => samecase(s, p), trans: (s, ...a) => { const [p, named] = splitArgs(a); return strTrans(s, p.length === 1 ? p[0] : mkList(p), named); }, NFC: (s) => strNfc(s), NFD: (s) => strNfd(s), encode: (s) => strEncode(s),
    'is-prime': (s) => isPrime(s), abs: (s) => abs(s), sqrt: (s) => sqrt(s), sign: (s) => sign(s), floor: (s) => floor(s), ceiling: (s) => ceiling(s), round: (s, n) => round(s, n), truncate: (s) => truncate(s),
    exp: (s, b) => b === undefined ? exp(s) : pow(b, s), log: (s, b) => log(s, b), log2: (s) => log2(s), log10: (s) => log10(s), sin: (s) => sin(s), cos: (s) => cos(s), tan: (s) => tan(s), asin: (s) => asin(s), acos: (s) => acos(s), atan: (s) => atan(s), atan2: (s, b) => atan2(s, b), sinh: (s) => sinh(s), cosh: (s) => cosh(s), tanh: (s) => tanh(s), cbrt: (s) => cbrt(s),
    rand: (s) => randNum(s), gcd: (s, b) => gcd(s, b), lcm: (s, b) => lcm(s, b), expmod: (s, e, m) => expmod(s, e, m), polymod: (s, ...m) => polymod(s, ...m), base: (s, b) => toBase(s, b), 'base-repeating': (s, b) => toBase(s, b),
    narrow: (s) => { const n = toNumeric(s); if (n instanceof RRat && n.d === 1n) return normBig(n.n); if (n instanceof RNum) return n.v; if (typeof n === 'number' && !Number.isInteger(n) && Number.isInteger(n)) return n; return n; },
    numerator: (s) => { const n = toNumeric(s); return n instanceof RRat ? normBig(n.n) : n; }, denominator: (s) => { const n = toNumeric(s); return n instanceof RRat ? normBig(n.d) : 1; }, nude: (s) => { const n = toNumeric(s); return n instanceof RRat ? mkList([normBig(n.n), normBig(n.d)]) : mkList([n, 1]); },
    'is-nan': (s) => Number.isNaN(toFloat(s)), 'isNaN': (s) => Number.isNaN(toFloat(s)), 'is-inf': (s) => !Number.isFinite(toFloat(s)) && !Number.isNaN(toFloat(s)),
    IO: (s) => ioPath(s), 'is-int': (s) => isIntVal(toNumeric(s)),
    exists: (s) => false, EXISTS: (s) => false, Capture: (s) => capture(s), Version: (s) => new RVersion(str(s)), Date: (s) => dateNew(T.Date, [s]), DateTime: (s) => dateNew(T.DateTime, [s]),
    'assuming': (s, ...pre) => assumingCall(s, ...pre), 'succ': (s) => inc(s), Failure: (s) => s,
    'push': (s, ...i) => pushTo(s, ...i), 'append': (s, ...i) => appendTo(s, ...i), 'pop': (s) => popFrom(s), 'shift': (s) => shiftFrom(s), 'unshift': (s, ...i) => unshiftTo(s, ...i), 'prepend': (s, ...i) => prependTo(s, ...i), 'splice': (s, ...a) => spliceArr(s, ...a),
    'message': (s) => excMessage(s), 'throw': (s) => { throw s; }, 'rethrow': (s) => { throw s; }, 'resume': (s) => Nil, 'backtrace': (s) => mkList([]), 'payload': (s) => s instanceof RakuError ? (s.payload ? namedHash(s.payload) : mkHash()) : Any,
    'exception': (s) => s instanceof RFailure ? s.err : s, 'handled': (s) => s instanceof RFailure ? s.handled : false, 'Exception': (s) => s instanceof RFailure ? s.err : s,
    'key': (s) => s instanceof RPair ? s.k : Any, 'value': (s) => s instanceof RPair ? s.v : Any, 'kv': (s) => kvOf(s), 'freeze': (s) => s,
    'elem': (s, set) => elem(s, set), 'AT-POS': (s, i) => aget(s, i), 'AT-KEY': (s, k) => hget(s, k), 'EXISTS-KEY': (s, k) => hexists(s, k), 'EXISTS-POS': (s, i) => aexists(s, i), 'DELETE-KEY': (s, k) => hdelete(s, k), 'DELETE-POS': (s, i) => adelete(s, i), 'ASSIGN-POS': (s, i, v) => aset(s, i, v), 'ASSIGN-KEY': (s, k, v) => hset(s, k, v),
    'Order': (s) => order(toFloat(s)), 'Int-ish': (s) => isIntVal(s),
    'starts-with': (s, p) => startsWith(s, p), 'chars': (s) => chars(s), 'codes': (s) => codes(s), 'uniname': (s) => strUniname(s), 'parse-base': (s, b) => parseBase(s, b),
    'sprintf': (s, ...a) => sprintf(s, ...a), 'EVAL': () => EVAL(), 'grab': (s, n) => pickFrom(s, n), 'skip': (s, n) => { const k = n === undefined ? 1 : Number(toInt(n)); return mkSeq(arr(s).slice(k)); },
    'toggle': (s, ...fs) => { const out = []; let on = true, fi = 0; for (const x of iter(s)) { if (fi < fs.length && truthy(fs[fi](x)) !== on) { fi++; on = !on; } if (on) out.push(x); } return mkSeq(out); },
    'deepmap': (s, f) => { const walk = x => (x instanceof RList) ? mkList(x.a.map(walk), x.ty) : f(x); return walk(s); }, 'duckmap': (s, f) => { const walk = x => { try { const r = f(x); if (r instanceof RFailure) throw 0; return r; } catch (e) { return x instanceof RList ? mkList(x.a.map(walk), x.ty) : x; } }; return walk(s); },
    'nodemap': (s, f) => mkList(arr(s).map(f)), 'sum': (s) => sumList(s), 'Bool': (s) => truthy(s), 'are': (s) => { const items = arr(s); if (!items.length) return T.Nil; let t = typeOf(items[0]); for (const x of items) { const u = typeOf(x); while (!u.isa(t)) t = t.parents[0] || T.Mu; } return t; },
    'chunks': (s, n) => batchList(s, n), 'tree': (s) => s, 'antipair': (s) => s instanceof RPair ? pair(s.v, s.k) : Any, 'SET-SELF': (s) => s, 'BUILDALL': (s) => s, 'bless': (s, ...a) => buildObj(s, ...a), 'CREATE': (s) => new RObj(s),
    'perl': (s) => raku(s), 'gistseen': (s) => gist(s), 'iterator-end': () => T.IterationEnd, 'DEFINITE': (s) => defined(s), 'REPR': (s) => 'P6opaque', 'HOW': (s) => typeOf(s), 'WHY': (s) => Nil, 'set': (s, v) => s,
    'seed': (s, v) => s, 'srand': (s) => srand(s), 'exit': (s) => exit(s), 'sleep': (s) => sleep(s), 'now': () => now(), 'time': () => time(),
    'is-integer': (s) => isIntVal(toNumeric(s)), 'msb': (s) => { const b = big(s); return b === 0n ? Nil : b.toString(2).length - 1; }, 'lsb': (s) => { const b = big(s); if (b === 0n) return Nil; let i = 0; let x = b; while ((x & 1n) === 0n) { x >>= 1n; i++; } return i; },
    'Complex': (s) => new RComplex(toFloat(s), 0), 're': (s) => s instanceof RComplex ? mkNum(s.re) : toNumeric(s), 'im': (s) => s instanceof RComplex ? mkNum(s.im) : 0,
    'Range': (s) => range(0, s, false, true), 'succ': (s) => inc(s), 'pred': (s) => dec(s), 'grep-index': (s, f) => { const out = []; let i = 0; const t = matcherOf(f); for (const x of iter(s)) { if (t(x)) out.push(i); i++; } return mkSeq(out); },
    'first-index': (s, f) => { let i = 0; const t = matcherOf(f); for (const x of iter(s)) { if (t(x)) return i; i++; } return Nil; }, 'last-index': (s, f) => { const a = arr(s); const t = matcherOf(f); for (let i = a.length - 1; i >= 0; i--) if (t(a[i])) return i; return Nil; },
    'sort-by': (s, f) => sortList(s, f), 'maxpairs': (s) => { const ps = arr(pairsOf(s)); if (!ps.length) return mkSeq([]); const m = maxOf(ps.map(p => p.v)); return mkSeq(ps.filter(p => cmpNum(p.v, m) === 0)); }, 'minpairs': (s) => { const ps = arr(pairsOf(s)); if (!ps.length) return mkSeq([]); const m = minOf(ps.map(p => p.v)); return mkSeq(ps.filter(p => cmpNum(p.v, m) === 0)); },
    'total': (s) => s instanceof RSetty ? s.total() : sumList(s), 'Slip': (s) => mkSlip(itemsOf(s).slice()), 'STORE': (s, v) => { if (s instanceof RList) assignArray(s, v); else if (s instanceof RHash) assignHash(s, v); return s; },
    'sink': (s) => Nil, 'dd': (s) => dd(s), 'is-prime': (s) => isPrime(s), 'sqrt': (s) => sqrt(s), 'roots': (s, n) => { const k = Number(toInt(n)); const r = Math.pow(toFloat(s), 1 / k); return mkList(Array.from({ length: k }, (_, i) => { const th = 2 * Math.PI * i / k; return new RComplex(r * Math.cos(th), r * Math.sin(th)); })); },
    'Instant': (s) => toNumeric(s), 'Duration': (s) => toNumeric(s), 'unival': (s) => Nil, 'univals': (s) => mkList([]), 'uniprop': (s) => Nil, 'NFKC': (s) => str(s).normalize('NFKC'), 'NFKD': (s) => str(s).normalize('NFKD'), 'encode': (s, ...a) => strEncode(s, ...a), 'decode': (s) => str(s),
    'match': (s, rxo, ...a) => { const named = nm(a); const adv = { g: truthy(named.get('g') || named.get('global')), ex: truthy(named.get('ex') || named.get('exhaustive')), ov: truthy(named.get('ov') || named.get('overlap')) }; return rxMatch(s, (adv.g || adv.ex || adv.ov) ? new RRegex(rxo.tree, { ...rxo.adv, ...adv }) : rxo); },
    'subst': (s, pat, ...a) => { const named = nm(a); const repl = posArgs(a)[0]; const g = truthy(named.get('g') || named.get('global')); if (pat instanceof RRegex) return rxSubst(s, pat, mt => typeof repl === 'function' ? repl(mt) : str(repl), { g }).s; const src = str(s), lit = str(pat); const rep = typeof repl === 'function' ? str(repl(lit)) : str(repl); return g ? src.split(lit).join(rep) : src.replace(lit, () => rep); },
});
function floatToRat(f) {
    if (!Number.isFinite(f)) return f;
    let d = 1n, n = f;
    let k = 0; while (!Number.isInteger(n) && k < 60) { n *= 2; d *= 2n; k++; }
    return ratResult(BigInt(n), d);
}
// numeric-only methods that shouldn't be on Str with those meanings are the same here; Rakudo coerces.
M(T.Int, { Str: (s) => str(s), 'is-prime': (s) => isPrime(s), Rat: (s) => mkRat(big(s), 1n), base: (s, b) => toBase(s, b), 'polymod': (s, ...m) => polymod(s, ...m), 'chr': (s) => chr(s), 'succ': (s) => inc(s), 'pred': (s) => dec(s), 'Range': (s) => range(0, s, false, true), 'sqrt': (s) => sqrt(s), 'Bool': (s) => truthy(s) });
M(T.Bool, { Int: (s) => (s ? 1 : 0), Numeric: (s) => (s ? 1 : 0), Str: (s) => str(s), gist: (s) => str(s), 'key': (s) => str(s), 'value': (s) => (s ? 1 : 0), 'pred': (s) => false, 'succ': (s) => true, 'enums': () => hashFrom([['False', 0], ['True', 1]]), 'pick': (s) => (Math.random() < 0.5), 'Bool': (s) => s, 'not': (s) => !s, 'so': (s) => s });
M(T.Str, { Int: (s) => numFail(() => strInt(s)), Num: (s) => numFail(() => strNum(s)), Rat: (s) => numFail(() => strRat(s)), FatRat: (s) => numFail(() => strRat(s)), Complex: (s) => numFail(() => strToNumeric(s)), Numeric: (s) => numFail(() => strToNumeric(s)), 'Str': (s) => s, 'chars': (s) => chars(s), 'uc': (s) => uc(s), 'lc': (s) => lc(s), 'flip': (s) => flip(s), 'contains': (s, n, st) => contains(s, n, st), 'IO': (s) => ioPath(s), 'succ': (s) => strSucc(s), 'pred': (s) => strPred(s), 'Bool': (s) => truthy(s),
    'unival': (s) => Nil, 'ord': (s) => ord(s), 'trim': (s) => trim(s), 'split': (s, ...a) => strSplit(s, posArgs(a)[0], posArgs(a)[1], nm(a)), 'index': (s, n, st) => strIndex(s, n, st), 'substr': (s, f, l) => substr(s, f, l), 'substr-rw': (s, f, l) => substr(s, f, l),
    'sprintf': (s, ...a) => sprintf(s, ...a), 'starts-with': (s, p) => startsWith(s, p), 'ends-with': (s, p) => endsWith(s, p), 'parse-base': (s, b) => parseBase(s, b), 'is-prime': (s) => isPrime(s), 'x': (s, n) => xrepeat(s, n), 'chomp': (s) => chomp(s), 'comb': (s, p, l) => comb(s, p, l), 'words': (s, n) => words(s, n), 'lines': (s) => lines(s),
    'encode': (s, ...a) => strEncode(s, ...a), 'NFC': (s) => strNfc(s), 'NFD': (s) => strNfd(s), 'succ': (s) => strSucc(s), 'Version': (s) => new RVersion(s), 'path': (s) => ioPath(s), 'Date': (s) => dateNew(T.Date, [s]) });
M(T.Num, { Int: (s) => toInt(s), Str: (s) => str(s), Rat: (s) => floatToRat(toFloat(s)), 'round': (s, n) => round(s, n), 'Num': (s) => s, 'Bool': (s) => truthy(s), 'is-nan': (s) => Number.isNaN(toFloat(s)), 'isNaN': (s) => Number.isNaN(toFloat(s)), 'exp': (s) => exp(s), 'abs': (s) => abs(s) });
M(T.Rat, { Int: (s) => s.d === 0n ? failure(new RakuError('Attempt to divide by zero when coercing Rational to Int', 'X::Numeric::DivideByZero')) : toInt(s), Str: (s) => str(s), Num: (s) => mkNum(ratToFloat(s)), 'Rat': (s) => s, 'FatRat': (s) => s, 'Bool': (s) => truthy(s), 'nude': (s) => mkList([normBig(s.n), normBig(s.d)]), 'numerator': (s) => normBig(s.n), 'denominator': (s) => normBig(s.d), 'isNaN': (s) => false, 'floor': (s) => floor(s), 'round': (s, n) => round(s, n) });
M(T.List, {
    elems: (s) => s.a.length, end: (s) => s.a.length - 1, Str: (s) => str(s), gist: (s) => s.gist(), raku: (s) => s.raku(), Bool: (s) => s.a.length > 0, Int: (s) => s.a.length, Numeric: (s) => s.a.length, list: (s) => s, List: (s) => s.ty === T.List ? s : mkList(s.a.slice()),
    Array: (s) => s.ty === T.Array ? s : mkArray(s.a.slice()), 'is-lazy': (s) => false, 'push': (s, ...i) => pushTo(s, ...i), 'append': (s, ...i) => appendTo(s, ...i), 'pop': (s) => popFrom(s), 'shift': (s) => shiftFrom(s), 'unshift': (s, ...i) => unshiftTo(s, ...i), 'prepend': (s, ...i) => prependTo(s, ...i), 'splice': (s, ...a) => spliceArr(s, ...a),
    'AT-POS': (s, i) => aget(s, i), 'ASSIGN-POS': (s, i, v) => aset(s, i, v), 'EXISTS-POS': (s, i) => aexists(s, i), 'DELETE-POS': (s, i) => adelete(s, i), 'Slip': (s) => mkSlip(s.a.slice()), 'Seq': (s) => mkSeq(s.a.slice()), 'flat': (s) => flat(s), 'eager': (s) => s, 'sort': (s, f) => sortList(s, f), 'join': (s, sep) => joinList(s, sep),
    'Hash': (s) => newHash(s), 'hash': (s) => newHash(s), 'Bag': (s) => toSetty(s, T.Bag), 'Set': (s) => toSetty(s, T.Set), 'Mix': (s) => toSetty(s, T.Mix), 'BagHash': (s) => toSetty(s, T.BagHash), 'SetHash': (s) => toSetty(s, T.SetHash), 'MixHash': (s) => toSetty(s, T.MixHash),
    'clone': (s) => new RList(s.a.slice(), s.ty), 'iterator': (s) => s.a[Symbol.iterator](), 'of': (s) => s.of || T.Mu, 'default': (s) => s.dflt === undefined ? Any : s.dflt, 'is-lazy': (s) => !!s.src, 'Capture': (s) => new RCapture(s.a.slice()), 'shape': (s) => mkList([s.a.length]), 'keys': (s) => keysOf(s), 'sum': (s) => sumList(s), 'map': (s, f) => mapList(s, f),
});
M(T.Seq, { elems: (s) => s.elems(), Bool: (s) => !s.isEmpty(), gist: (s) => s.gist(), raku: (s) => s.raku(), 'is-lazy': (s) => s.lazy, list: (s) => s.list(), List: (s) => s.list(), cache: (s) => s instanceof RSeq ? s.list() : mkList(s.arr()), eager: (s) => s instanceof RSeq ? s.cache() : s, Array: (s) => newArray(s), 'lazy': (s) => lazyOf(s), 'Str': (s) => str(s), 'Seq': (s) => s, 'iterator': (s) => s[Symbol.iterator](), 'Slip': (s) => mkSlip(s.arr().slice()), 'clone': (s) => s });
M(T.Range, { elems: (s) => s.elemsOrInf(), min: (s) => s.min(), max: (s) => s.max(), 'excludes-min': (s) => s.exFrom, 'excludes-max': (s) => s.exTo, bounds: (s) => mkList([s.from, s.to]), Str: (s) => s.Str(), gist: (s) => s.gist(), raku: (s) => s.raku(), Bool: (s) => s.elemsOrInf() !== 0,
    list: (s) => mkList(s.arr()), List: (s) => mkList(s.arr()), Array: (s) => mkArray(s.arr()), reverse: (s) => s.reverse(), 'is-lazy': (s) => s.isInfinite(), 'infinite': (s) => s.isInfinite(), 'is-int': (s) => s.isIntRange(), sum: (s) => { if (s.isInfinite()) return Infinity; if (s.isIntRange() && !s.isInfinite()) { const lo = s.lo(), hi = s.hi(); if (lt(hi, lo)) return 0; return idiv(mul(add(lo, hi), add(sub(hi, lo), 1)), 2); } return sumList(s); },
    'int-bounds': (s) => mkList([s.lo(), s.hi()]), 'minmax': (s) => mkList([s.min(), s.max()]), 'iterator': (s) => s[Symbol.iterator](), 'ACCEPTS': (s, v) => s.contains(v), 'Int': (s) => s.elemsOrInf(), 'Numeric': (s) => s.elemsOrInf(), 'pick': (s, n) => pickFrom(s, n), 'roll': (s, n) => rollFrom(s, n), 'rand': (s) => rangeRand(s), 'Seq': (s) => new RSeq(s[Symbol.iterator](), s.isInfinite()), 'first': (s, ...a) => firstOf(s, posArgs(a)[0], nm(a)) });
M(T.Hash, { elems: (s) => s.m.size, Bool: (s) => s.m.size > 0, Str: (s) => str(s), gist: (s) => s.gist(), raku: (s) => s.raku(), keys: (s) => mkSeq(s.keys()), values: (s) => mkSeq(s.values()), kv: (s) => kvOf(s), pairs: (s) => mkSeq(s.pairs()), antipairs: (s) => antipairsOf(s), invert: (s) => invertOf(s),
    'AT-KEY': (s, k) => hget(s, k), 'ASSIGN-KEY': (s, k, v) => hset(s, k, v), 'EXISTS-KEY': (s, k) => hexists(s, k), 'DELETE-KEY': (s, k) => hdelete(s, k), 'push': (s, ...i) => pushTo(s, ...i), 'append': (s, ...i) => pushTo(s, ...i), 'sort': (s, f) => sortList(s, f), 'list': (s) => mkList(s.pairs()), 'List': (s) => mkList(s.pairs()), 'Array': (s) => mkArray(s.pairs()),
    'classify-list': (s, f, ...l) => { const mf = mapperFn(f); const src = l.length === 1 ? l[0] : mkList(l); for (const x of iter(src)) { const k = hashKey(mf(x)); let b = s.m.get(k); if (!b) { b = mkArray([]); s.m.set(k, b); } b.a.push(x); } return s; },
    'categorize-list': (s, f, ...l) => { const mf = mapperFn(f); const src = l.length === 1 ? l[0] : mkList(l); for (const x of iter(src)) for (const key of itemsOf(mf(x))) { const k = hashKey(key); let b = s.m.get(k); if (!b) { b = mkArray([]); s.m.set(k, b); } b.a.push(x); } return s; },
    'Hash': (s) => s, 'hash': (s) => s, 'clone': (s) => new RHash(new Map(s.m), s.ty), 'Map': (s) => s, 'Set': (s) => toSetty(s, T.Set), 'Bag': (s) => toSetty(s, T.Bag), 'Mix': (s) => toSetty(s, T.Mix), 'SetHash': (s) => toSetty(s, T.SetHash), 'BagHash': (s) => toSetty(s, T.BagHash), 'MixHash': (s) => toSetty(s, T.MixHash), 'iterator': (s) => s.pairs()[Symbol.iterator](),
    'Int': (s) => s.m.size, 'Numeric': (s) => s.m.size, 'min': (s, ...a) => nm(a).size ? minMaxHash(s, false, nm(a)) : minMaxHash(s, false, null), 'max': (s, ...a) => nm(a).size ? minMaxHash(s, true, nm(a)) : minMaxHash(s, true, null), 'grep': (s, f) => grepList(s, f), 'map': (s, f) => mapList(s, f), 'first': (s, ...a) => firstOf(s, posArgs(a)[0], nm(a)), 'of': (s) => s.of || T.Mu, 'keyof': (s) => T.Str, 'default': (s) => s.dflt === undefined ? Any : s.dflt, 'sum': (s) => sumList(mkList(s.values())), 'join': (s, sep) => joinList(mkList(s.pairs()), sep) });
M(T.Pair, { key: (s) => s.k, value: (s) => s.v, kv: (s) => mkList([s.k, s.v]), keys: (s) => mkList([s.k]), values: (s) => mkList([s.v]), pairs: (s) => mkList([s]), antipair: (s) => pair(s.v, s.k), invert: (s) => { const v = s.v instanceof RScalar ? s.v.v : s.v; return mkSeq(v instanceof RList ? v.a.map(x => pair(x, s.k)) : [pair(v, s.k)]); }, Str: (s) => str(s), gist: (s) => pairGist(s), raku: (s) => pairRaku(s), Bool: (s) => true, 'freeze': (s) => s, 'Hash': (s) => hashFrom([[hashKey(s.k), s.v]]), 'hash': (s) => hashFrom([[hashKey(s.k), s.v]]), 'elems': (s) => 1, 'Numeric': (s) => 1, 'Int': (s) => 1, 'AT-KEY': (s, k) => hget(s, k), 'EXISTS-KEY': (s, k) => hexists(s, k), 'ACCEPTS': (s, v) => smartmatch(v, s), 'Map': (s) => hashFrom([[hashKey(s.k), s.v]]) });
M(T.Whatever, { gist: () => '*', raku: () => '*', Str: () => '*' });
M(T.Junction, { gist: (s) => junctionGist(s), Str: (s) => junctionStr(s), raku: (s) => junctionRaku(s), Bool: (s) => junctionBool(s), 'defined': (s) => new RJunction(s.kind, s.items.map(defined)), 'so': (s) => junctionBool(s), 'not': (s) => !junctionBool(s), 'eigenstates': (s) => mkList(s.items), 'THREAD': (s, f) => new RJunction(s.kind, s.items.map(f)) });
M(T.Exception, { message: (s) => excMessage(s), Str: (s) => excMessage(s), gist: (s) => excMessage(s), throw: (s) => { throw s; }, rethrow: (s) => { throw s; }, resume: (s) => Nil, 'backtrace': (s) => mkList([]), 'payload': (s) => s.payload ? namedHash(s.payload) : mkHash(), 'Bool': (s) => true, 'defined': (s) => true, 'WHAT': (s) => excType(s), 'raku': (s) => raku(s), 'Failure': (s) => failure(s), 'fail': (s) => failure(s), 'name': (s) => excType(s).name });
M(T.Failure, { Bool: (s) => { s.handled = true; return false; }, defined: (s) => { s.handled = true; return false; }, exception: (s) => s.err, message: (s) => s.err.message, handled: (s) => s.handled, Str: (s) => { throw s.err; }, gist: (s) => '(HANDLED) ' + s.err.message, throw: (s) => { throw s.err; }, 'so': (s) => { s.handled = true; return false; }, 'not': (s) => { s.handled = true; return true; }, 'sink': (s) => { throw s.err; }, 'Numeric': (s) => { throw s.err; }, 'Int': (s) => { throw s.err; }, 'self': (s) => { throw s.err; }, 'elems': (s) => { throw s.err; }, 'list': (s) => { throw s.err; }, 'Exception': (s) => s.err, 'WHAT': (s) => T.Failure, 'gistseen': (s) => gist(s), 'perl': (s) => raku(s.err), 'raku': (s) => raku(s.err), 'mess': (s) => s.err.message, 'payload': (s) => s.err.payload ? namedHash(s.err.payload) : mkHash() });
M(T.Setty, { elems: (s) => s.m.size, total: (s) => s.total(), keys: (s) => mkSeq(s.keysList()), values: (s) => mkSeq(s.valuesList()), kv: (s) => kvOf(s), pairs: (s) => mkSeq(s.pairsList()), list: (s) => mkList(s.listItems()), List: (s) => mkList(s.listItems()), Str: (s) => s.Str(), gist: (s) => s.gist(), raku: (s) => s.raku(), Bool: (s) => s.m.size > 0,
    'AT-KEY': (s, k) => s.get(k), 'EXISTS-KEY': (s, k) => s.has(k), 'ASSIGN-KEY': (s, k, v) => s.set(k, v), 'DELETE-KEY': (s, k) => s.delete(k), 'grab': (s, n) => { const r = pickFrom(mkList(s.keysList()), n); for (const x of (n === undefined ? [r] : arr(r))) s.delete(x); return r; }, 'grabpairs': (s, n) => { const ps = pickFrom(mkList(s.pairsList()), n); for (const p of (n === undefined ? [ps] : arr(ps))) s.delete(p.k); return ps; },
    'pick': (s, n) => pickFrom(mkList(s.keysList()), n), 'roll': (s, n) => rollFrom(mkList(s.keysList()), n), 'Set': (s) => toSetty(s, T.Set), 'Bag': (s) => toSetty(s, T.Bag), 'Mix': (s) => toSetty(s, T.Mix), 'SetHash': (s) => toSetty(s, T.SetHash), 'BagHash': (s) => toSetty(s, T.BagHash), 'MixHash': (s) => toSetty(s, T.MixHash), 'Hash': (s) => { const h = new RHash(); for (const e of s.m.values()) h.m.set(str(e.v), e.n); return h; }, 'hash': (s) => { const h = new RHash(); for (const e of s.m.values()) h.m.set(str(e.v), e.n); return h; },
    'clone': (s) => s.clone(), 'minpairs': (s) => { const ps = s.pairsList(); if (!ps.length) return mkSeq([]); const m = minOf(ps.map(p => p.v)); return mkSeq(ps.filter(p => cmpNum(p.v, m) === 0)); }, 'maxpairs': (s) => { const ps = s.pairsList(); if (!ps.length) return mkSeq([]); const m = maxOf(ps.map(p => p.v)); return mkSeq(ps.filter(p => cmpNum(p.v, m) === 0)); }, 'antipairs': (s) => mkSeq(s.pairsList().map(p => pair(p.v, p.k))), 'invert': (s) => mkSeq(s.pairsList().map(p => pair(p.v, p.k))), 'Int': (s) => s.m.size, 'Numeric': (s) => s.m.size, 'sort': (s, f) => sortList(mkList(s.listItems()), f), 'map': (s, f) => mapList(mkList(s.listItems()), f), 'grep': (s, f) => grepList(mkList(s.listItems()), f), 'iterator': (s) => s.listItems()[Symbol.iterator](), 'first': (s, ...a) => firstOf(mkList(s.listItems()), posArgs(a)[0], nm(a)), 'sum': (s) => sumList(mkList(s.listItems())), 'join': (s, sep) => joinList(mkList(s.listItems()), sep), 'elem': (s, v) => s.has(v), 'add': (s, ...v) => { for (const x of v) s.add(x); return s; } });
M(T.Version, { Str: (s) => s.Str(), gist: (s) => 'v' + s.Str(), raku: (s) => /^\d/.test(s.Str()) ? 'v' + s.Str() : "Version.new('" + s.Str() + "')", parts: (s) => mkList(s.parts), 'ACCEPTS': (s, v) => v instanceof RVersion && v.accepts(s), 'Bool': (s) => true });
M(T.Capture, { list: (s) => mkList(s.pos), hash: (s) => namedHash(s.named), elems: (s) => s.pos.length, gist: (s) => s.gist(), raku: (s) => s.raku(), Str: (s) => s.Str(), 'AT-POS': (s, i) => aget(s, i), 'AT-KEY': (s, k) => hget(s, k), 'keys': (s) => mkList(Array.from({ length: s.pos.length }, (_, i) => i).concat(Array.from(s.named.keys()))), 'Capture': (s) => s, 'Bool': (s) => s.pos.length > 0 || s.named.size > 0 });
M(T.Complex, { re: (s) => mkNum(s.re), im: (s) => mkNum(s.im), Str: (s) => s.Str(), gist: (s) => s.Str(), raku: (s) => '<' + s.Str() + '>', abs: (s) => numResult(Math.hypot(s.re, s.im)), 'polar': (s) => mkList([numResult(Math.hypot(s.re, s.im)), numResult(Math.atan2(s.im, s.re))]), 'conj': (s) => new RComplex(s.re, -s.im), 'Complex': (s) => s, 'Bool': (s) => s.re !== 0 || s.im !== 0, 'sqrt': (s) => { const r = Math.hypot(s.re, s.im); const re = Math.sqrt((r + s.re) / 2), im = Math.sign(s.im || 1) * Math.sqrt((r - s.re) / 2); return new RComplex(re, im); }, 'reals': (s) => mkList([mkNum(s.re), mkNum(s.im)]), 'Numeric': (s) => s, 'narrow': (s) => s.im === 0 ? mkNum(s.re) : s });
M(T.Date, { Str: (s) => s.Str(), gist: (s) => s.Str(), raku: (s) => s.raku(), Int: (s) => s.ty === T.Date ? s.daycount() : Math.floor(s.d.getTime() / 1000), year: (s) => s.d.getUTCFullYear(), month: (s) => s.d.getUTCMonth() + 1, day: (s) => s.d.getUTCDate(), 'day-of-month': (s) => s.d.getUTCDate(), 'day-of-week': (s) => (s.d.getUTCDay() + 6) % 7 + 1, 'day-of-year': (s) => Math.floor((s.d - Date.UTC(s.d.getUTCFullYear(), 0, 1)) / 86400000) + 1, 'days-in-month': (s) => new Date(Date.UTC(s.d.getUTCFullYear(), s.d.getUTCMonth() + 1, 0)).getUTCDate(), 'is-leap-year': (s) => { const y = s.d.getUTCFullYear(); return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0; }, later: (s, ...a) => { const n = nm(a); const d = new Date(s.d); if (n.has('days') || n.has('day')) d.setUTCDate(d.getUTCDate() + Number(toInt(n.get('days') ?? n.get('day')))); if (n.has('months') || n.has('month')) d.setUTCMonth(d.getUTCMonth() + Number(toInt(n.get('months') ?? n.get('month')))); if (n.has('years') || n.has('year')) d.setUTCFullYear(d.getUTCFullYear() + Number(toInt(n.get('years') ?? n.get('year')))); if (n.has('hours') || n.has('hour')) d.setUTCHours(d.getUTCHours() + Number(toInt(n.get('hours') ?? n.get('hour')))); if (n.has('minutes') || n.has('minute')) d.setUTCMinutes(d.getUTCMinutes() + Number(toInt(n.get('minutes') ?? n.get('minute')))); if (n.has('seconds') || n.has('second')) d.setUTCSeconds(d.getUTCSeconds() + Number(toInt(n.get('seconds') ?? n.get('second')))); return new RDate(s.ty, d); }, earlier: (s, ...a) => { const n = nm(a); const m2 = new Map(); for (const [k, v] of n) m2.set(k, neg(v)); return mc(s, 'later', new RNamed(m2)); }, 'succ': (s) => { const d = new Date(s.d); d.setUTCDate(d.getUTCDate() + 1); return new RDate(s.ty, d); }, 'pred': (s) => { const d = new Date(s.d); d.setUTCDate(d.getUTCDate() - 1); return new RDate(s.ty, d); }, 'daycount': (s) => Math.floor(s.d.getTime() / 86400000) + 40587, 'Date': (s) => new RDate(T.Date, new Date(Date.UTC(s.d.getUTCFullYear(), s.d.getUTCMonth(), s.d.getUTCDate()))), 'DateTime': (s) => new RDate(T.DateTime, new Date(s.d)), hour: (s) => s.d.getUTCHours(), minute: (s) => s.d.getUTCMinutes(), second: (s) => s.d.getUTCSeconds(), 'posix': (s) => Math.floor(s.d.getTime() / 1000), 'Instant': (s) => numResult(s.d.getTime() / 1000), 'yyyy-mm-dd': (s) => mc(s, 'Date').Str(), 'hh-mm-ss': (s) => s.Str().slice(11, 19), 'Numeric': (s) => s.numeric(), 'truncated-to': (s, u) => { const d = new Date(s.d); const un = str(u); if (un === 'month') d.setUTCDate(1); if (un === 'year') { d.setUTCMonth(0); d.setUTCDate(1); } if (un === 'day' || un === 'month' || un === 'year') d.setUTCHours(0, 0, 0, 0); return new RDate(s.ty, d); }, 'in-timezone': (s) => s, 'utc': (s) => s, 'local': (s) => s, 'timezone': (s) => 0, 'offset': (s) => 0, 'week-number': (s) => { const d = new Date(Date.UTC(s.d.getUTCFullYear(), s.d.getUTCMonth(), s.d.getUTCDate())); const day = d.getUTCDay() || 7; d.setUTCDate(d.getUTCDate() + 4 - day); const y0 = new Date(Date.UTC(d.getUTCFullYear(), 0, 1)); return Math.ceil(((d - y0) / 86400000 + 1) / 7); }, 'weekday-of-month': (s) => Math.floor((s.d.getUTCDate() - 1) / 7) + 1, 'clone': (s, ...a) => { const n = nm(a); const d = new Date(s.d); if (n.has('year')) d.setUTCFullYear(Number(toInt(n.get('year')))); if (n.has('month')) d.setUTCMonth(Number(toInt(n.get('month'))) - 1); if (n.has('day')) d.setUTCDate(Number(toInt(n.get('day')))); return new RDate(s.ty, d); }, 'Bool': (s) => true, 'ACCEPTS': (s, v) => v instanceof RDate && v.d.getTime() === s.d.getTime() });
Object.assign(T.DateTime.methods, T.Date.methods);
M(T['IO::Path'], { Str: (s) => s.path, gist: (s) => strLit(s.path) + '.IO', raku: (s) => 'IO::Path.new(' + strLit(s.path) + ')', IO: (s) => s, 'Bool': (s) => true, absolute: (s) => host.absolute(s.path), relative: (s, ...a) => { const base = posArgs(a).length ? str(posArgs(a)[0]) : host.cwd; const abs = host.absolute(s.path); const b = host.absolute(base).replace(/\/+$/, '') + '/'; return abs.startsWith(b) ? abs.slice(b.length) : s.path; }, basename: (s) => s.path.replace(/\/+$/, '').split('/').pop(), extension: (s) => { const b = s.path.split('/').pop(); const i = b.lastIndexOf('.'); return i > 0 ? b.slice(i + 1) : ''; }, parent: (s) => { const p = s.path.replace(/\/+$/, ''); const i = p.lastIndexOf('/'); return new RIOPath(i < 0 ? '.' : i === 0 ? '/' : p.slice(0, i)); }, dirname: (s) => { const p = s.path.replace(/\/+$/, ''); const i = p.lastIndexOf('/'); return i < 0 ? '.' : i === 0 ? '/' : p.slice(0, i); }, add: (s, p) => new RIOPath(s.path.replace(/\/+$/, '') + '/' + str(p)), child: (s, p) => new RIOPath(s.path.replace(/\/+$/, '') + '/' + str(p)), 'e': (s) => host.exists(s.path), 'f': (s) => host.isFile(s.path), 'd': (s) => host.isDir(s.path), 'r': (s) => host.exists(s.path), 'w': (s) => host.exists(s.path), 'x': (s) => host.exists(s.path), 's': (s) => host.size(s.path), 'z': (s) => host.size(s.path) === 0, 'slurp': (s, ...a) => host.slurp(s.path, ...a), 'spurt': (s, ...a) => host.spurt(s.path, ...a), 'lines': (s, ...a) => lines(host.slurp(s.path)), 'words': (s) => words(host.slurp(s.path)), 'open': (s, ...a) => host.open(s.path, ...a), 'dir': (s, ...a) => host.dir(s.path, ...a), 'mkdir': (s) => host.mkdir(s.path), 'rmdir': (s) => host.rmdir(s.path), 'unlink': (s) => host.unlink(s.path), 'copy': (s, t) => host.copy(s.path, str(t)), 'rename': (s, t) => host.rename(s.path, str(t)), 'move': (s, t) => host.rename(s.path, str(t)), 'modified': (s) => host.modified(s.path), 'resolve': (s) => new RIOPath(host.absolute(s.path)), 'is-absolute': (s) => s.path.startsWith('/'), 'is-relative': (s) => !s.path.startsWith('/'), 'chars': (s) => chars(s.path), 'parts': (s) => hashFrom([['basename', s.path.split('/').pop()], ['dirname', s.path.includes('/') ? s.path.slice(0, s.path.lastIndexOf('/')) : '.'], ['volume', '']]), 'SPEC': (s) => T.IO, 'path': (s) => s.path, 'touch': (s) => host.spurt(s.path, '', new RNamed(new Map([['append', true]]))), 'ACCEPTS': (s, v) => str(v) === s.path, 'succ': (s) => { const i = s.path.lastIndexOf('/') + 1, dir = s.path.slice(0, i), base = s.path.slice(i), d = base.lastIndexOf('.'); return new RIOPath(dir + (d > 0 ? strSucc(base.slice(0, d)) + base.slice(d) : strSucc(base)), s.cwd); }, 'comb': (s, ...a) => comb(host.slurp(s.path), ...a), 'split': (s, ...a) => strSplit(host.slurp(s.path), ...a) });
M(T['IO::Handle'], { Str: (s) => s.path || '', gist: (s) => 'IO::Handle<' + (s.path || '') + '>', get: (s) => host.handleGet(s), lines: (s) => host.handleLines(s), slurp: (s) => host.handleSlurp(s), 'slurp-rest': (s) => host.handleSlurp(s), print: (s, ...a) => host.handlePrint(s, a.map(str).join('')), say: (s, ...a) => host.handlePrint(s, a.map(gist).join('') + '\n'), put: (s, ...a) => host.handlePrint(s, a.map(str).join('') + '\n'), printf: (s, f, ...a) => host.handlePrint(s, sprintf(f, ...a)), 'print-nl': (s) => host.handlePrint(s, '\n'), 'close': (s) => host.close(s), 'eof': (s) => host.handleEof(s), 'flush': (s) => { host.handleFlush(s); host.flush(); return true; }, 'opened': (s) => !s.closed, 'words': (s) => words(host.handleSlurp(s)), 'comb': (s, ...a) => comb(host.handleSlurp(s), ...a), 'getc': (s) => host.handleGetc(s), 'read': (s, n) => strEncode(host.handleSlurp(s)), 'write': (s, b) => host.handlePrint(s, chrs(b)), 'nl-in': (s) => '\n', 'nl-out': (s) => '\n', 'path': (s) => new RIOPath(s.path || ''), 'IO': (s) => new RIOPath(s.path || ''), 't': (s) => host.isTTY(s), 'encoding': (s) => 'utf8', 'Bool': (s) => true, 'Supply': (s) => mkList(arr(host.handleLines(s))), 'seek': (s, p) => { s.pos = Number(toInt(p)); return true; }, 'tell': (s) => s.pos, 'lock': (s) => true, 'unlock': (s) => true, 'spurt': (s, ...a) => { host.handlePrint(s, str(posArgs(a)[0])); if (truthy(nm(a).get('close'))) host.close(s); return true; } });

// type-object methods (Int.new, Str.new, Date.today, ...)
const TYPE_METHODS = {
    today: (t) => new RDate(T.Date, new Date(new Date().setUTCHours(0, 0, 0, 0))),
    now: (t) => new RDate(T.DateTime, new Date()),
    enums: (t) => { const h = new RHash(); for (const e of t.enumValues || []) h.m.set(e.key, e.val); return h; },
    pick: (t, n) => pickFrom(mkList(t.enumValues || []), n), roll: (t, n) => rollFrom(mkList(t.enumValues || []), n),
    keys: (t) => mkSeq((t.enumValues || []).map(e => e.key)), values: (t) => mkSeq((t.enumValues || []).map(e => e.val)), kv: (t) => mkSeq((t.enumValues || []).flatMap(e => [e.key, e.val])), pairs: (t) => mkSeq((t.enumValues || []).map(e => pair(e.key, e.val))), elems: (t) => (t.enumValues || []).length,
    'from-posix': (t, n) => new RDate(t, new Date(toFloat(n) * 1000)),
    'new-from-daycount': (t, n) => new RDate(t, new Date((Number(toInt(n)) - 40587) * 86400000)),
    'Str': (t) => t === T.IterationEnd ? 'IterationEnd' : '', 'Numeric': (t) => 0, 'Int': (t) => t === T.Bool ? 0 : 0,
    'Range': (t) => t === T.Int ? range(-Infinity, Infinity) : Nil,
    'perl': (t) => t === Nil ? 'Nil' : t.name, 'raku': (t) => t === Nil ? 'Nil' : t.name, 'gist': (t) => t === Nil ? 'Nil' : '(' + t.name + ')', 'WHAT': (t) => t, 'name': (t) => t.name, 'shortname': (t) => t.name.split('::').pop(),
    'methods': (t) => mkList(Object.keys(t.methods)), 'attributes': (t) => mkList(t.attrs.map(a => a.sigil + (a.pub ? '.' : '!') + a.name)), 'mro': (t) => mkList(t.mro), 'parents': (t) => mkList(t.parents), 'roles': (t) => mkList(t.roles), 'isa': (t, o) => t.isa(typeof o === 'string' ? T[o] : o), 'does': (t, o) => t.isa(typeof o === 'string' ? T[o] : o),
    'can': (t, n) => { const m = t.find(str(n)); return m ? mkList([m]) : mkList([]); }, 'ver': (t) => Nil, 'auth': (t) => '', 'api': (t) => '', 'WHY': (t) => Nil, 'Bool': (t) => false, 'defined': (t) => false, 'so': (t) => false, 'not': (t) => true, 'is-lazy': (t) => false,
    'ACCEPTS': (t, v) => isa(v, t) || (t === Nil && v === Nil), 'elems': (t) => 1, 'chars': (t) => 0,
    'Failure': (t) => t, 'DEFINITE': (t) => false, 'succ': (t) => 1, 'pred': (t) => -1, 'IO': (t) => new RIOPath(''), 'HOW': (t) => t, 'find_method': (t, n) => t.find(str(n)) || Nil, 'lookup': (t, n) => t.find(str(n)) || Nil,
    'add_method': (t, n, f) => { t.methods[str(n)] = f; t.cache = Object.create(null); return f; }, 'compose': (t) => t, 'archetypes': (t) => t, 'is_composed': (t) => true,
};
function mc(inv, name, ...args) {
    // a JS::Object: its own property or method first, then the handle's methods
    if (inv instanceof RJsObj) {
        const target = inv.v;
        if (target != null && (name in Object(target))) return jsCall(inv, name, args);
        const m = JsObjectT.methods[name]; if (m) return m(inv, ...args);
        return jsCall(inv, name, args);
    }
    // a JavaScript function read as a property (`JS.Array`, `JS.Event`): its own
    // properties and `.new` dispatch on the function itself
    if (typeof inv === 'function' && inv.__js) {
        if (name === 'new') return jsNew(inv.__js, args);
        if (name in inv.__js) return jsCall(inv.__js, name, args);
    }
    if (inv instanceof RJunction && name === 'defined') return junctionBool(new RJunction(inv.kind, inv.items.map(x => defined(x))));   // .defined threads, then collapses
    if (inv instanceof RJunction && name === 'DEFINITE') return true;   // the junction itself is an instance
    if (inv instanceof RJunction && name !== 'gist' && name !== 'Str' && name !== 'raku' && name !== 'Bool' && name !== 'so' && name !== 'not' && name !== 'defined' && name !== 'WHAT' && name !== 'eigenstates') {
        return new RJunction(inv.kind, inv.items.map(x => mc(x, name, ...args)));
    }
    if (inv instanceof RAllo) {   // an allomorph: the numeric half's methods first (IntStr is Int is Str), then Str's
        switch (name) {
            case 'WHAT': return typeOf(inv); case 'raku': case 'perl': return raku(inv); case 'gist': return inv.s; case 'Str': return inv.s; case 'WHICH': return whichKey(inv);
            case 'Numeric': case 'Real': return inv.n; case 'Bool': case 'so': return truthy(inv.n); case 'defined': return true;
        }
        const mn = typeOf(inv.n).find(name); if (mn) return mn(inv.n, ...args);
        const ms = T.Str.find(name); if (ms) return ms(inv.s, ...args);
        throw new RakuError(`No such method '${name}' for invocant of type '${typeOf(inv).name}'`, 'X::Method::NotFound');
    }
    const ty = typeOf(inv);
    if (inv instanceof RType) {
        // a type object: its own class's methods for the meta-ish ones, else the table above
        if (name === 'new') return construct(inv, ...args);
        if ((name === 'parse' || name === 'subparse' || name === 'parsefile') && inv.mro.some(t => t.rules)) {
            if (name === 'parsefile') { const [pos, named] = splitArgs(args); return grammarParse(inv, host.slurp(str(pos[0])), named.size ? [new RNamed(named)] : [], false); }
            return grammarParse(inv, args[0], args, name === 'subparse');
        }
        const um = inv.isUser && inv.find(name);
        if (um) return um(inv, ...args);
        const tm = TYPE_METHODS[name];
        if (tm) return tm(inv, ...args);
        if (inv === T.Any || inv === T.Mu) { const m = T.Mu.methods[name]; if (m) return m(inv, ...args); }
        if (inv.isEnum && inv.enumValues) { const e = inv.enumValues.find(x => x.key === name); if (e) return e; }
        const cm = inv.find(name);
        if (cm) return cm(inv, ...args);
        throw new RakuError(`No such method '${name}' for invocant of type '${inv.name}'`, 'X::Method::NotFound');
    }
    if (inv instanceof REnum) { const em = ENUM_METHODS[name]; if (em) return em(inv, ...args); const um = ty.findUser(name); if (um) return um(inv, ...args); return mc(inv.val, name, ...args); }
    if (inv instanceof RObj && (name === 'parse' || name === 'subparse') && inv.ty.mro.some(t => t.rules)) return grammarParse(inv.ty, args[0], args, name === 'subparse');   // Grammar.new.parse
    if (inv instanceof RakuError && inv.payload && Object.prototype.hasOwnProperty.call(inv.payload, name)) return inv.payload[name];   // an exception's own fields (.method, .typename, …)
    const m = ty.find(name);
    if (m) return m(inv, ...args);
    if (inv instanceof RMatch && inv.ctx) { const r = cursorCall(inv, name, args); if (r !== undefined) return r; }   // self.rule inside a grammar method
    if (typeof inv === 'function') { const cm = CODE_METHODS[name]; if (cm) return cm(inv, ...args); }
    if (inv instanceof RSlip) return mc(mkList(inv.a), name, ...args);
    if (inv instanceof RScalar) return mc(inv.v, name, ...args);
    if (inv instanceof RObj && inv.ty.isa(T.Exception)) { const em = T.Exception.methods[name]; if (em) return em(inv, ...args); }
    if (inv instanceof RObj) { const fb = inv.ty.findUser('FALLBACK'); if (fb) return fb(inv, name, ...args); }
    throw new RakuError(`No such ${name[0] === '!' ? 'private ' : ''}method '${name}' for invocant of type '${ty.name}'`, 'X::Method::NotFound', { method: name.replace(/^!/, ''), typename: ty.name, private: name[0] === '!' });
}
// .?name
function mcMaybe(inv, name, ...args) {
    const ty = typeOf(inv);
    if (inv instanceof RType) { if (name === 'new' || TYPE_METHODS[name] || inv.find(name)) return mc(inv, name, ...args); return Nil; }
    if (ty.find(name) || (typeof inv === 'function' && CODE_METHODS[name]) || (inv instanceof REnum && ENUM_METHODS[name])) return mc(inv, name, ...args);
    return Nil;
}
function can(inv, name) { const ty = typeOf(inv); return !!(ty.find(name) || (inv instanceof RType && (TYPE_METHODS[name] || name === 'new'))); }
// $obj."$name"()
function mcDyn(inv, name, ...args) { return mc(inv, str(name), ...args); }
// >>.method
function numFail(f) { try { return f(); } catch (e) { if (e instanceof RakuError && e.type === 'X::Str::Numeric') return new RFailure(e); throw e; } }
// Rakudo's `is nodal` methods: a hyper applies them to a nested list as a whole instead of descending
const NODAL = new Set(['elems', 'end', 'keys', 'values', 'kv', 'pairs', 'antipairs', 'invert', 'join', 'sort', 'reverse', 'rotate', 'flat', 'list', 'List', 'Slip', 'Seq', 'Array', 'head', 'tail', 'first', 'unique', 'squish', 'sum', 'min', 'max', 'minmax', 'Bag', 'Set', 'Mix', 'BagHash', 'SetHash', 'MixHash', 'hash', 'Hash', 'Capture', 'pick', 'roll', 'classify', 'categorize', 'combinations', 'permutations', 'rotor', 'batch', 'produce', 'reduce', 'grep', 'map', 'is-lazy', 'eager', 'lazy', 'cache', 'iterator', 'Bool', 'so', 'not', 'defined', 'WHAT', 'WHICH']);
function hyperMethod(inv, name, ...args) {
    if (inv instanceof RHash) { const h = new RHash(); for (const [k, v] of inv.m) h.m.set(k, mc(v, name, ...args)); return h; }
    return mkList(spliceSlips(arr(inv).map(x => ((x instanceof RList || x instanceof RSeq) && !NODAL.has(name)) ? hyperMethod(x, name, ...args) : mc(x, name, ...args))), inv instanceof RList ? inv.ty : T.List);   // ».m descends into nested lists unless the method is nodal (elems, join, sort …); a Slip result splices
}
const CODE_METHODS = {
    arity: (f) => f.arity !== undefined ? f.arity : f.length, count: (f) => f.count !== undefined ? f.count : (f.arity !== undefined ? f.arity : f.length),
    name: (f) => f.rname || '', signature: (f) => new RSig(f), assuming: (f, ...pre) => assumingCall(f, ...pre), 'WHAT': (f) => f.rtype || T.Block, 'gist': (f) => gist(f), 'raku': (f) => raku(f), 'Str': (f) => str(f),
    'Bool': () => true, 'defined': () => true, 'so': () => true, 'not': () => false, 'call': (f, ...a) => f(...a), 'CALL-ME': (f, ...a) => f(...a), 'clone': (f) => f, 'candidates': (f) => mkList(f.candidates || [f]), 'cando': (f) => mkList([f]), 'of': (f) => T.Mu, 'returns': (f) => T.Mu, 'is-lazy': () => false, 'elems': () => 1, 'list': (f) => mkList([f]), 'item': (f) => f, 'WHICH': (f) => 'Code|' + objId(f), 'ACCEPTS': (f, v) => truthy(f(v)), 'package': (f) => T.Any, 'file': (f) => '', 'line': (f) => 0, 'multi': (f) => !!f.candidates, 'WHY': () => Nil, 'map': (f, g) => mapList(mkList([f]), g), 'join': (f, s) => str(f), 'wrap': (f, w) => { const orig = f; const wrapped = (...a) => w(...a); return wrapped; },
};
const ENUM_METHODS = {
    key: (e) => e.key, value: (e) => e.val, kv: (e) => mkList([e.key, e.val]), pair: (e) => pair(e.key, e.val), Str: (e) => str(e), gist: (e) => e.key, raku: (e) => raku(e), Int: (e) => toInt(e.val), Numeric: (e) => e.val, 'enums': (e) => { const h = new RHash(); for (const x of e.ty.enumValues) h.m.set(x.key, x.val); return h; },
    succ: (e) => inc(e), pred: (e) => dec(e), 'WHAT': (e) => e.ty, 'defined': () => true, 'Bool': (e) => e.ty === T.Bool ? !!e.val : true, 'so': (e) => truthy(e), 'not': (e) => !truthy(e), 'ACCEPTS': (e, v) => smartmatch(v, e), 'pick': (e) => e, 'WHICH': (e) => whichKey(e), 'name': (e) => e.key, 'keys': (e) => mkList([e.key]), 'values': (e) => mkList([e.val]), 'Bool': (e) => truthy(e), 'isa': (e, t) => isa(e, t),
};
Object.assign(R, { mc, mcMaybe, mcDyn, can, hyperMethod, M, TYPE_METHODS, CODE_METHODS, ENUM_METHODS, floatToRat });
// .indices(needle, :overlap) — grapheme offsets of every occurrence; .slice(indices) picks by position
// :i / :ignorecase and :m / :ignoremark on the substring searches: compare graphemes with the marks stripped and/or case folded
function foldG(g, ic, im) { let x = g; if (im) x = x.normalize('NFD').replace(/\p{M}+/gu, ''); if (ic) x = x.toLowerCase(); return x; }
function foldOpts(a) { const [pos, named] = splitArgs(a); return [pos, truthy(named.get('i') ?? named.get('ignorecase') ?? false), truthy(named.get('m') ?? named.get('ignoremark') ?? false), named]; }
function gIndexFold(s, needle, from, ic, im, all, overlap) {
    const g = graphemes(str(s)).map(x => foldG(x, ic, im)), n = graphemes(str(needle)).map(x => foldG(x, ic, im)); const out = [];
    outer: for (let i = from; i + n.length <= g.length; i++) { for (let j = 0; j < n.length; j++) if (g[i + j] !== n[j]) continue outer; if (!all) return i; out.push(i); if (!overlap && n.length) i += n.length - 1; }
    return all ? out : -1;
}
const FRAC = /^(\d*)\u2044(\d+)$/;
function unival(ch) { const g = str(ch); if (g === '') return NaN; const c = String.fromCodePoint(g.codePointAt(0)); if (!/\p{N}/u.test(c)) return NaN; if (/\p{Nd}/u.test(c)) return Number(foldDigits(c)); const k = c.normalize('NFKC'); let m; if (/^\d+$/.test(k)) return Number(k); if ((m = FRAC.exec(k))) return ratResult(BigInt(m[1] || '0'), BigInt(m[2])); return NaN; }
M(T.Str, {
    'contains': (s, ...a) => { const [pos, ic, im] = foldOpts(a); if (!ic && !im) return contains(s, pos[0], pos[1]); return gIndexFold(s, pos[0], pos.length > 1 ? Number(toInt(pos[1])) : 0, ic, im, false) >= 0; },
    'index': (s, ...a) => { const [pos, ic, im] = foldOpts(a); if (!ic && !im) return strIndex(s, pos[0], pos[1]); const i = gIndexFold(s, pos[0], pos.length > 1 ? Number(toInt(pos[1])) : 0, ic, im, false); return i < 0 ? Nil : i; },
    'indices': (s, ...a) => { const [pos, ic, im, named] = foldOpts(a); return mkList(gIndexFold(s, pos[0], pos.length > 1 ? Number(toInt(pos[1])) : 0, ic, im, true, truthy(named.get('overlap') ?? false))); },
    'starts-with': (s, ...a) => { const [pos, ic, im] = foldOpts(a); if (!ic && !im) return startsWith(s, pos[0]); const g = graphemes(str(s)).map(x => foldG(x, ic, im)), n = graphemes(str(pos[0])).map(x => foldG(x, ic, im)); return n.length <= g.length && n.every((x, j) => g[j] === x); },
    'ends-with': (s, ...a) => { const [pos, ic, im] = foldOpts(a); if (!ic && !im) return endsWith(s, pos[0]); const g = graphemes(str(s)).map(x => foldG(x, ic, im)), n = graphemes(str(pos[0])).map(x => foldG(x, ic, im)); const off = g.length - n.length; return off >= 0 && n.every((x, j) => g[off + j] === x); },
    'unival': (s) => unival(s), 'univals': (s) => mkList(graphemes(str(s)).map(unival)),
});
const collateOf = (s) => mkSeq(arr(s).slice().sort((a, b) => str(a).localeCompare(str(b), 'en')));
M(T.List, { 'collate': collateOf }); M(T.Seq, { 'collate': collateOf, 'slice': (s, ...idx) => sliceOf(s, ...idx) });
const sliceOf = (s, ...idx) => { const a = arr(s); return mkList(idx.flatMap(i => (i instanceof RList || i instanceof RSeq || i instanceof RRange) ? arr(i) : [i]).map(i => a[Number(toInt(i))] ?? Nil)); };
M(T.List, { 'slice': sliceOf }); M(T.Range, { 'slice': sliceOf });
M(T.Range, { 'in-range': (s, v) => { if (!s.contains(v)) throw new RakuError(`Value out of range. Is: ${gist(v)}, should be in ${s.gist()}`, 'X::OutOfRange'); return true; } });
M(T.Pair, { 'fmt': (s, f) => sprintf(f === undefined ? '%s\t%s' : f, s.k, s.v) });   // .fmt on a Pair formats key and value
M(T['IO::Path'], { 'CWD': (s) => s.cwd === undefined ? host.cwd : s.cwd });
// a byte list decodes; Setty conversions
M(T.List, { 'decode': (s, enc) => { const bytes = Uint8Array.from(arr(s).map(b => Number(toInt(b)))); const e = enc === undefined ? 'utf-8' : str(enc).toLowerCase(); try { return new TextDecoder(e === 'ascii' ? 'utf-8' : e).decode(bytes); } catch (x) { return new TextDecoder().decode(bytes); } } });
for (const t of [T.Set, T.SetHash, T.Bag, T.BagHash, T.Mix, T.MixHash]) M(t, { 'Setty': (s) => s.ty === T.Set || s.ty === T.SetHash ? s : toSetty(s, T.Set), 'Baggy': (s) => s.ty === T.Bag || s.ty === T.BagHash ? s : toSetty(s, T.Bag), 'Mixy': (s) => s.ty === T.Mix || s.ty === T.MixHash ? s : toSetty(s, T.Mix) });
M(T.Date, { 'first-date-in-month': (s) => new RDate(T.Date, new Date(Date.UTC(s.d.getUTCFullYear(), s.d.getUTCMonth(), 1))), 'last-date-in-month': (s) => new RDate(T.Date, new Date(Date.UTC(s.d.getUTCFullYear(), s.d.getUTCMonth() + 1, 0))) });
// calendar components shared by Date and DateTime
const isLeap = (y) => (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
const dateParts = {
    'day-of-week': (s) => { const d = s.d.getUTCDay(); return d === 0 ? 7 : d; },
    'day-of-year': (s) => Math.floor((Date.UTC(s.d.getUTCFullYear(), s.d.getUTCMonth(), s.d.getUTCDate()) - Date.UTC(s.d.getUTCFullYear(), 0, 1)) / 86400000) + 1,
    'days-in-year': (s) => isLeap(s.d.getUTCFullYear()) ? 366 : 365,
    'days-in-month': (s) => new Date(Date.UTC(s.d.getUTCFullYear(), s.d.getUTCMonth() + 1, 0)).getUTCDate(),
    'is-leap-year': (s) => isLeap(s.d.getUTCFullYear()),
    'weekday-of-month': (s) => Math.floor((s.d.getUTCDate() - 1) / 7) + 1,
    'yyyy-mm-dd': (s) => s.Str().slice(0, 10),
};
M(T.Date, dateParts); M(T.DateTime, dateParts);
M(T.Instant, { 'to-posix': (s) => mkList([toFloat(s), false]) });
// %h.classify-list(&mapper, @items) / .categorize-list: fill the hash in place; an Array mapper looks the item up
M(T.Hash, {
    'classify-list': (h, f, ...items) => { const key = f instanceof RList ? (x => aget(f, Number(toInt(x)))) : f; for (const x of spliceSlips(items.flatMap(i => i instanceof RList || i instanceof RSeq || i instanceof RRange ? arr(i) : [i]))) classifyInto(h, key(x), x); return h; },
    'categorize-list': (h, f, ...items) => { const key = f instanceof RList ? (x => aget(f, Number(toInt(x)))) : f; for (const x of spliceSlips(items.flatMap(i => i instanceof RList || i instanceof RSeq || i instanceof RRange ? arr(i) : [i]))) for (const kk of itemsOf(key(x))) classifyInto(h, kk, x); return h; },
});
// a list key nests: %h.classify-list([['a','b']], …) → {a => {b => [...]}}
function classifyInto(h, key, x) {
    const path = key instanceof RList ? key.a : [key];
    let cur = h;
    for (let i = 0; i + 1 < path.length; i++) { const k = hashKey(path[i]); let nx = cur.m.get(k); if (!(nx instanceof RHash)) { nx = new RHash(); cur.m.set(k, nx); } cur = nx; }
    const k = hashKey(path[path.length - 1]); let b = cur.m.get(k); if (!(b instanceof RList)) { b = mkArray([]); cur.m.set(k, b); } b.a.push(x);
}
const dateMore = {
    'day-fraction': (s) => ratResult(BigInt(s.d.getUTCHours() * 3600 + s.d.getUTCMinutes() * 60 + s.d.getUTCSeconds()), 86400n),
    'offset': (s) => 0, 'offset-in-minutes': (s) => 0, 'offset-in-hours': (s) => 0, 'timezone': (s) => 0, 'utc': (s) => s, 'local': (s) => s,
    'whole-second': (s) => s.d.getUTCSeconds(), 'hh-mm-ss': (s) => s.Str().slice(11, 19),
    'julian-date': (s) => numResult(s.d.getTime() / 86400000 + 2440587.5), 'modified-julian-date': (s) => numResult(s.d.getTime() / 86400000 + 40587),
    'earlier': (s, ...a) => { const [pos, named] = splitArgs(a); const negs = new Map(); for (const [k, v] of named) negs.set(k, neg(v)); const later = s.ty.methods.later || T.Date.methods.later || T.DateTime.methods.later; return later(s, ...pos, new RNamed(negs)); },
};
M(T.Date, dateMore); M(T.DateTime, dateMore);
// Range.rand: Rakudo's refusals, message for message
function rangeRand(s) {
    if (!isNumeric(s.from) || !isNumeric(s.to)) throw new RakuError('Can only get a random value on Real values, did you mean .pick?');
    const a = toFloat(s.from), b = toFloat(s.to);
    if (!Number.isFinite(a) || !Number.isFinite(b)) throw new RakuError('Impossible to get a random number from an infinite range', 'X::Range::Rand::InvalidEndpoints');
    if (a === b) throw new RakuError('Impossible to generate random numbers for a range where endpoints are equal', 'X::Range::Rand::InvalidEndpoints');
    if (a > b) throw new RakuError(`Impossible to get a random number from range containing no values.\nThe sequence (...) operator supports descension between ${str(s.from)} and ${str(s.to)},\nbut for a random number between ${str(s.from)} and ${str(s.to)}, (${str(s.to)}..${str(s.from)}).rand is\nlikely to be functionally equivalent to what was meant by (${str(s.from)}..${str(s.to)}).rand`, 'X::Range::Rand::InvalidEndpoints');
    return numResult(a + rand() * (b - a));
}
M(T['IO::Handle'], { 'out-buffer': (s) => s.outBuffer === undefined ? (s.kind === 'out' || s.kind === 'err' ? 0 : 8192) : s.outBuffer });   // a file is buffered by default; the standard streams are not
// a Hash's min/max go by value: the pair, or with :v the value, :k the key, :kv both, :p the pair
function minMaxHash(h, isMax, named) {
    const ps = arr(pairsOf(h)); let best = null, ties = [];
    const byValue = named && named.size;   // the adverbs compare values; the bare form compares whole pairs
    for (const p of ps) { const c = best ? (byValue ? (isMax ? cmpNum(p.v, best.v) : -cmpNum(p.v, best.v)) : (isMax ? cmpNum(p, best) : -cmpNum(p, best))) : 1; if (c > 0) { best = p; ties = [p]; } else if (c === 0) ties.push(p); }
    const has = (n) => named && truthy(named.get(n) ?? false);
    if (!best) return (named && named.size) ? mkList([]) : (isMax ? -Infinity : Infinity);   // empty: no positions to answer; the bare form is the identity
    if (has('v')) return mkList(ties.map(p => p.v)); if (has('k')) return mkList(ties.map(p => p.k)); if (has('kv')) return mkList(ties.flatMap(p => [p.k, p.v])); if (has('p')) return mkList(ties);
    return best;   // the bare form: the pair (whole pairs compare by value first)
}
M(T.Hash, { 'max': (s, ...a) => minMaxHash(s, true, nm(a)), 'min': (s, ...a) => minMaxHash(s, false, nm(a)) });
for (const t of [T.Set, T.SetHash, T.Bag, T.BagHash, T.Mix, T.MixHash]) M(t, { 'max': (s, ...a) => minMaxHash(s, true, nm(a)), 'min': (s, ...a) => minMaxHash(s, false, nm(a)) });
