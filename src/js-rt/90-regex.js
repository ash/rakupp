// Regexes and grammars (TRANSPILE-PLAN P3). The pattern arrives as a tree the
// emitter serialized at transpile time with the engine's own parser
// (Regex::toJsTree); this is the matcher — the native design ported:
// continuation-passing backtracking, captures pushed before the continuation
// and popped when it fails, whole-grapheme consumption for `.` and classes,
// packrat memoization for ratchet rules. Alternation is first-match with a
// longest-literal-prefix ordering, an approximation of LTM (P3 second half).

class RRegex {
    constructor(tree, adv, src) { this.tree = tree; this.adv = adv || {}; this.root = tree.root; this.src = src; }
}
// A match: `orig` the subject, [from, to) in JS code units, `caps` positional
// captures (RMatch, a list of them, or Nil), `named` name → RMatch | list.
class RMatch {
    constructor(orig, from, to) { this.orig = orig; this.from = from; this.to = to; this.caps = []; this.named = new Map(); this.made = undefined; this.rule = null; }
    Str() { return this.orig.slice(this.from, this.to); }
    pos(i) { const c = this.caps[i]; return c === undefined ? Nil : c; }
    name(k) { const c = this.named.get(k); return c === undefined ? Nil : c; }
}
const namedRegexes = new Map();
function namedRegex(name, rx) { namedRegexes.set(name, rx); return rx; }
function rx(tree, adv, src) { return new RRegex(tree, adv, src); }

// ---- character tests ------------------------------------------------------------
const propCache = new Map();
function propRe(name) {
    let r = propCache.get(name);
    if (r === undefined) {
        r = null;
        for (const cand of [`\\p{${name}}`, `\\p{Script=${name}}`, `\\p{General_Category=${name}}`]) { try { r = new RegExp('^' + cand + '$', 'u'); break; } catch (e) { } }
        propCache.set(name, r);
    }
    return r;
}
const NL_CPS = new Set([0x0A, 0x0B, 0x0C, 0x0D, 0x85, 0x2028, 0x2029]);
function isSpaceCp(cp) { return cp === 0x20 || (cp >= 9 && cp <= 13) || cp === 0x85 || cp === 0xA0 || cp === 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp === 0x2028 || cp === 0x2029 || cp === 0x202F || cp === 0x205F || cp === 0x3000; }
function ccFlag(f, cp, ch) {
    switch (f) {
        case 'a': return cp === 0x5F || (cp < 128 ? ((cp | 32) >= 97 && (cp | 32) <= 122) : propRe('L').test(ch));
        case 'd': return cp < 128 ? (cp >= 48 && cp <= 57) : propRe('Nd').test(ch);
        case 'w': return cp === 0x5F || (cp < 128 ? (((cp | 32) >= 97 && (cp | 32) <= 122) || (cp >= 48 && cp <= 57)) : (propRe('L').test(ch) || propRe('Nd').test(ch)));   // :L ∪ :Nd ∪ '_'
        case 's': return isSpaceCp(cp);
        case 'u': return cp < 128 ? (cp >= 65 && cp <= 90) : propRe('Lu').test(ch);
        case 'l': return cp < 128 ? (cp >= 97 && cp <= 122) : propRe('Ll').test(ch);
        case 'p': return propRe('P').test(ch);   // :P alone — '+', '$', '|' are symbols
        case 'k': return cp < 32 || (cp >= 127 && cp < 160);
        case 'b': return cp === 9 || cp === 32 || (cp > 127 && propRe('Zs').test(ch));
        case 'x': return (cp >= 48 && cp <= 57) || ((cp | 32) >= 97 && (cp | 32) <= 102);
        case 'g': return ccFlag('a', cp, ch) || ccFlag('d', cp, ch) || ccFlag('p', cp, ch);   // <graph> is alnum ∪ punct: symbols and :No are out
        case 'r': return !(cp < 32 || (cp >= 127 && cp < 160));
        case 'n': return NL_CPS.has(cp);
        default: return false;
    }
}
function classFlagsMatch(flags, cp, ch) {
    for (const f of flags) {
        const lc = f.toLowerCase();
        const hit = ccFlag(lc, cp, ch);
        if (f === lc ? hit : !hit) return true;
    }
    return false;
}
// the grapheme that starts at i: [i, end)
function clusterEnd(s, i) {
    const cp = s.codePointAt(i);
    let j = i + (cp > 0xFFFF ? 2 : 1);
    if (cp === 0x0D && s.charCodeAt(j) === 0x0A) return j + 1;
    if (cp < 0x300 && (j >= s.length || s.charCodeAt(j) < 0x300)) return j;   // the ASCII fast path
    // walk while the next codepoint does not start a new cluster
    let prev = gbProp(cp), pictSeq = prev === GB_ExtPict, riRun = prev === GB_RI ? 1 : 0, incbState = incbProp(cp) === 2 ? 1 : 0;
    while (j < s.length) {
        const c2 = s.codePointAt(j), cur = gbProp(c2), ip = incbProp(c2);
        let brk;
        if (prev === GB_CR && cur === GB_LF) brk = false;
        else if (prev === GB_Control || prev === GB_CR || prev === GB_LF) brk = true;
        else if (cur === GB_Control || cur === GB_CR || cur === GB_LF) brk = true;
        else if (prev === GB_L && (cur === GB_L || cur === GB_V || cur === GB_LV || cur === GB_LVT)) brk = false;
        else if ((prev === GB_LV || prev === GB_V) && (cur === GB_V || cur === GB_T)) brk = false;
        else if ((prev === GB_LVT || prev === GB_T) && cur === GB_T) brk = false;
        else if (cur === GB_Extend || cur === GB_ZWJ) brk = false;
        else if (cur === GB_SpacingMark) brk = false;
        else if (prev === GB_Prepend) brk = false;
        else if (incbState === 2 && ip === 2) brk = false;
        else if (pictSeq && prev === GB_ZWJ && cur === GB_ExtPict) brk = false;
        else if (prev === GB_RI && cur === GB_RI && (riRun % 2 === 1)) brk = false;
        else brk = true;
        if (brk) break;
        riRun = (cur === GB_RI) ? riRun + 1 : 0;
        if (cur === GB_ExtPict) pictSeq = true; else if (pictSeq && (cur === GB_Extend || cur === GB_ZWJ)) pictSeq = true; else pictSeq = false;
        if (ip === 2) incbState = 1; else if (incbState >= 1 && ip === 1) incbState = 2; else if (!(incbState >= 1 && ip === 3)) incbState = 0;
        prev = cur;
        j += c2 > 0xFFFF ? 2 : 1;
    }
    return j;
}
function classMatchAt(n, s, pos) {          // → end of the consumed grapheme, or -1
    if (pos >= s.length) return -1;
    const end = clusterEnd(s, pos);
    const g = s.slice(pos, end);
    const cp = s.codePointAt(pos);
    let hit = false;
    if (n.clusters) for (const m of n.clusters) if (m === g) { hit = true; break; }
    if (!hit && n.ranges) { const c = n.icase ? cp : cp; for (let i = 0; i < n.ranges.length; i += 2) { if (cp >= n.ranges[i] && cp <= n.ranges[i + 1]) { hit = true; break; } if (n.icase) { const lc = String.fromCodePoint(cp).toLowerCase().codePointAt(0), uc = String.fromCodePoint(cp).toUpperCase().codePointAt(0); if ((lc >= n.ranges[i] && lc <= n.ranges[i + 1]) || (uc >= n.ranges[i] && uc <= n.ranges[i + 1])) { hit = true; break; } } } }
    if (!hit && n.cp) for (let i = 0; i < n.cp.length; i += 2) if (cp >= n.cp[i] && cp <= n.cp[i + 1]) { hit = true; break; }
    if (!hit && n.flags) hit = classFlagsMatch(n.flags, cp, String.fromCodePoint(cp));
    if (!hit && n.uprop) {
        let name = n.uprop, neg = false;
        if (name[0] === '!') { neg = true; name = name.slice(1); }
        const re = propRe(name);
        const r = re ? re.test(String.fromCodePoint(cp)) : true;
        hit = neg ? !r : r;
    }
    if (n.negFlags && hit && classFlagsMatch(n.negFlags, cp, String.fromCodePoint(cp))) hit = false;
    if (n.negate) hit = !hit;
    return hit ? end : -1;
}
function fold(x) { return x.toUpperCase().toLowerCase(); }
function isWordAt(s, i) { if (i < 0 || i >= s.length) return false; const cp = s.codePointAt(i); return cp === 0x5F || ccFlag('a', cp, String.fromCodePoint(cp)) || ccFlag('d', cp, String.fromCodePoint(cp)); }
function litPrefixLen(n) {            // the leading literal run of a branch (LTM approximation)
    if (!n) return 0;
    if (n.k === 'Lit') return n.lit.length;
    if (n.k === 'Seq') { let t = 0; for (const k of n.kids || []) { if (k.k === 'Lit') t += k.lit.length; else if (k.k === 'Subrule' && k.name === 'ws') continue; else break; } return t; }
    if (n.k === 'Group' && n.kids) return litPrefixLen(n.kids[0]);
    return 0;
}

// ---- the matcher --------------------------------------------------------------------
class RxState {
    constructor(s, rx, ctx) {
        this.s = s; this.rx = rx; this.ctx = ctx || {};   // ctx: grammar (RType), actions, lexical
        this.caps = []; this.capReps = new Map(); this.named = new Map();
        this.startPos = 0; this.capFrom = -1; this.capTo = -1; this.steps = 0; this.ratchet = !!rx.tree.ratchet; this.curSym = null;
        this.memo = ctx && ctx.memo || new Map();
    }
}
const STEP_LIMIT = 8000000;
function m(n, st, pos, k) {
    if (++st.steps > STEP_LIMIT) throw new RakuError('regex backtracking limit exceeded');
    const s = st.s;
    switch (n.k) {
        case 'Lit': {
            const L = n.lit.length;
            if (n.icase) {   // full case folding: ß ~ ss, so the subject is consumed grapheme by grapheme until the folds agree
                const want = fold(n.lit);
                if (s.slice(pos, pos + L).toLowerCase() === n.lit.toLowerCase() && fold(s.slice(pos, pos + L)) === want) return k(pos + L);
                let got = '', i = pos;
                while (got.length < want.length && i < s.length) { const e = clusterEnd(s, i); got += fold(s.slice(i, e)); i = e; }
                return got === want ? k(i) : false;
            }
            if (n.imark) { const a = s.slice(pos, pos + L); if (a.normalize('NFD').replace(/\p{M}/gu, '') !== n.lit.normalize('NFD').replace(/\p{M}/gu, '')) return false; return k(pos + L); }
            if (s.startsWith(n.lit, pos)) return k(pos + L);
            return false;
        }
        case 'Any': { if (pos >= s.length) return false; return k(clusterEnd(s, pos)); }
        case 'Class': { const e = classMatchAt(n, s, pos); return e < 0 ? false : k(e); }
        case 'Seq': {
            const kids = n.kids || [];
            const go = (i, p) => i === kids.length ? k(p) : m(kids[i], st, p, (q) => go(i + 1, q));
            return go(0, pos);
        }
        case 'Alt': {
            const kids = n.kids || [];
            if (n.firstMatch || n.classCombo) { for (const kid of kids) if (m(kid, st, pos, k)) return true; return false; }
            let order = n._order;
            if (!order) { order = kids.map((kid, i) => [litPrefixLen(kid), i]).sort((a, b) => b[0] - a[0] || a[1] - b[1]).map(x => kids[x[1]]); n._order = order; }
            for (const kid of order) if (m(kid, st, pos, k)) return true;
            return false;
        }
        case 'Conj': {
            const kids = n.kids || [];
            const go = (i, p) => {
                if (i === kids.length - 1) return m(kids[i], st, p, (q) => k(q));
                return m(kids[i], st, p, (q) => go(i + 1, p));
            };
            return kids.length ? go(0, pos) : k(pos);
        }
        case 'Rep': return rep(n, st, pos, k);
        case 'Group': return group(n, st, pos, k);
        case 'AnchorStart': {
            if (n.multiline || n.p5Line) { if (pos === 0 || s[pos - 1] === '\n') return (n.p5Line && pos === s.length && pos > 0) ? false : k(pos); return false; }
            return pos === 0 ? k(pos) : false;
        }
        case 'AnchorEnd': {
            if (n.multiline) { if (pos === s.length || s[pos] === '\n') return k(pos); return false; }
            if (n.absEnd) return pos === s.length ? k(pos) : false;
            return (pos === s.length || (pos === s.length - 1 && s[pos] === '\n')) ? k(pos) : false;
        }
        case 'WBLeft': return (isWordAt(s, pos) && !isWordAt(s, pos - 1)) ? k(pos) : false;
        case 'WBRight': return (!isWordAt(s, pos) && isWordAt(s, pos - 1)) ? k(pos) : false;
        case 'Nop': return k(pos);
        case 'Subrule': return subrule(n, st, pos, k);
        case 'Look': {
            const inner = n.kids[0];
            if (!n.behind) {
                const st2 = new RxState(s, st.rx, st.ctx); st2.steps = st.steps; st2.named = new Map(st.named); st2.caps = st.caps.slice();
                const ok = m(inner, st2, pos, () => true);
                st.steps = st2.steps;
                return (ok !== !!n.negate) ? k(pos) : false;
            }
            // lookbehind: some start at or before pos whose match ends exactly at pos
            let ok = false;
            for (let start = pos; start >= 0 && !ok; start--) {
                const st2 = new RxState(s, st.rx, st.ctx);
                if (m(inner, st2, start, (q) => q === pos)) ok = true;
                if (pos - start > 256) break;
            }
            return (ok !== !!n.negate) ? k(pos) : false;
        }
        case 'Code': {
            const cur = cursorMatch(st, pos);
            if (n.runOnly) { n.fn(cur); return k(pos); }
            const r = truthy(n.fn(cur));
            return (r !== !!n.negate) ? k(pos) : false;
        }
        case 'VarMatch': return varMatch(n, st, pos, k);
        case 'CapStart': { const saved = st.capFrom; st.capFrom = pos; if (k(pos)) return true; st.capFrom = saved; return false; }
        case 'CapEnd': { const saved = st.capTo; st.capTo = pos; if (k(pos)) return true; st.capTo = saved; return false; }
        case 'CondRef': throw new RakuError('a Perl 5 conditional group is not in the JS core');
        default: throw new RakuError('unknown regex node ' + n.k);
    }
}
function isSingleChar(n) { return n.k === 'Any' || n.k === 'Class' || (n.k === 'Lit' && !n.icase && !n.imark); }
function stepSingle(n, s, pos) {
    if (n.k === 'Lit') return s.startsWith(n.lit, pos) ? pos + n.lit.length : -1;
    if (n.k === 'Any') return pos < s.length ? clusterEnd(s, pos) : -1;
    return classMatchAt(n, s, pos);
}
function rep(n, st, pos, k) {
    const s = st.s, kid = n.kids[0];
    let mn = n.min, mx = n.max;
    if (n.repCode) { const r = n.repCode(cursorMatch(st, pos)); if (r instanceof RRange) { mn = Number(toInt(r.lo())); mx = r.isInfinite() ? -1 : Number(toInt(r.hi())); } else { mn = mx = Number(toInt(r)); } }
    const finish = (p, count) => {
        if (n.sepTrail && n.sep && count > 0) { if (m(n.sep, st, p, (q) => k(q))) return true; }
        return k(p);
    };
    const matchOne = (p, count, kk) => {
        if (count > 0 && n.sep) return m(n.sep, st, p, (q) => m(kid, st, q, (r) => r === p && count > 0 ? false : kk(r)));
        return m(kid, st, p, (r) => (r === p && !st.rx.tree.p5) ? false : kk(r));
    };
    // possessive / ratchet: take as many as possible, never give back
    if (!n.frugal && (n.possessive || st.ratchet)) {
        let p = pos, count = 0;
        for (;;) {
            if (mx >= 0 && count >= mx) break;
            let np = -1;
            matchOne(p, count, (q) => { np = q; return true; });
            if (np < 0) break;
            p = np; count++;
        }
        if (count < mn) return false;
        return finish(p, count);
    }
    // iterative greedy for a deterministic single atom without a separator
    if (!n.frugal && !n.sep && isSingleChar(kid)) {
        const stops = [pos]; let p = pos;
        for (;;) {
            if (mx >= 0 && stops.length - 1 >= mx) break;
            const q = stepSingle(kid, s, p);
            if (q < 0 || q === p) break;
            stops.push(q); p = q;
        }
        for (let i = stops.length - 1; i >= mn; i--) if (finish(stops[i], i)) return true;
        return false;
    }
    if (!n.frugal) {
        const more = (p, count) => {
            if (mx < 0 || count < mx) { if (matchOne(p, count, (q) => more(q, count + 1))) return true; }
            return count >= mn ? finish(p, count) : false;
        };
        return more(pos, 0);
    }
    const lazy = (p, count) => {
        if (count >= mn && finish(p, count)) return true;
        if (mx < 0 || count < mx) return matchOne(p, count, (q) => lazy(q, count + 1));
        return false;
    };
    return lazy(pos, 0);
}
function group(n, st, pos, k) {
    const kid = n.kids[0];
    if (n.cap === undefined && !n.capName) return m(kid, st, pos, k);
    const idx = n.cap;
    const nested = n.nestNames;
    const savedNamed = nested ? st.named : null;
    if (nested) st.named = new Map();
    return m(kid, st, pos, (q) => {
        // record the capture, then continue; undo on failure
        const span = new RMatch(st.s, pos, q);
        if (nested) { span.named = namedFromFrame(st.named, st.rx.tree); st.named = savedNamed; }
        let undo;
        if (idx !== undefined && idx >= 0) {
            if (n.listCap) { let reps = st.capReps.get(idx); if (!reps) { reps = []; st.capReps.set(idx, reps); } reps.push(span); undo = () => reps.pop(); }
            else { const prev = st.caps[idx]; st.caps[idx] = span; undo = () => { st.caps[idx] = prev; }; }
        }
        let undoName;
        if (n.capName) undoName = addNamed(st, n.capName, span);
        if (k(q)) return true;
        if (undo) undo();
        if (undoName) undoName();
        if (nested) st.named = new Map();
        return false;
    }) || (nested ? (st.named = savedNamed, false) : false);
}
function addNamed(st, name, match) {
    let list = st.named.get(name);
    if (!list) { list = []; st.named.set(name, list); }
    list.push(match);
    return () => { list.pop(); if (!list.length) st.named.delete(name); };
}
// the match so far, for code blocks and assertions ($/ inside a regex)
function cursorMatch(st, pos) {
    const cur = new RMatch(st.s, st.startPos, pos);
    cur.st = st;
    finishMatch(cur, st, st.rx.tree);
    return cur;
}
const BUILTIN_RULES = {
    ws(s, pos) { let p = pos; while (p < s.length && isSpaceCp(s.codePointAt(p))) p += s.codePointAt(p) > 0xFFFF ? 2 : 1; if (p === pos && isWordAt(s, pos - 1) && isWordAt(s, pos)) return -1; return p; },
    wb(s, pos) { return isWordAt(s, pos - 1) !== isWordAt(s, pos) ? pos : -1; },
    ww(s, pos) { return isWordAt(s, pos - 1) && isWordAt(s, pos) ? pos : -1; },
    alpha: 'a', digit: 'd', alnum: 'ad', ident: null, space: 's', blank: 'b', upper: 'u', lower: 'l', punct: 'p', xdigit: 'x', cntrl: 'k', graph: 'g', print: 'r', word: 'w',
};
function builtinRule(name, s, pos) {
    const b = BUILTIN_RULES[name];
    if (b === undefined) return undefined;
    if (typeof b === 'function') return b(s, pos);
    if (name === 'ident') { if (pos >= s.length) return -1; let cp = s.codePointAt(pos); if (!(cp === 0x5F || ccFlag('a', cp, String.fromCodePoint(cp)))) return -1; let p = clusterEnd(s, pos); while (p < s.length) { cp = s.codePointAt(p); if (!(cp === 0x5F || ccFlag('a', cp, String.fromCodePoint(cp)) || ccFlag('d', cp, String.fromCodePoint(cp)))) break; p = clusterEnd(s, p); } return p; }
    if (pos >= s.length) return -1;
    const cp = s.codePointAt(pos);
    return classFlagsMatch(b, cp, String.fromCodePoint(cp)) ? clusterEnd(s, pos) : -1;
}
// a grammar's rule by name, through its inheritance chain
function findRule(ty, name) {
    if (!ty) return null;
    for (const t of ty.mro) if (t.rules && t.rules[name]) return t.rules[name];
    return null;
}
// the candidates of a proto: `name:sym<x>` / `name:x` rules, most-derived class first, declaration order
function protoCandidates(ty, name) {
    const out = [], seen = new Set();
    for (const t of ty.mro) {
        if (!t.rules) continue;
        for (const key of Object.keys(t.rules)) {
            if (!key.startsWith(name + ':') || seen.has(key)) continue;
            seen.add(key);
            let sym = key.slice(name.length + 1);
            const mm = /^sym<(.*)>$/.exec(sym) || /^sym«(.*)»$/.exec(sym);
            if (mm) sym = mm[1];
            out.push({ name: key, rule: t.rules[key], sym });
        }
    }
    return out;
}
function subrule(n, st, pos, k) {
    const s = st.s;
    const name = n.name;
    const capKey = n.alias || name;
    const capture = !n.noCapture;
    const record = (sub, q) => {
        if (!capture) return k(q);
        const undo = addNamed(st, capKey, sub);
        let undo2 = null;
        if (n.alias && !n.aliasDotted && n.alias !== name && name[0] !== '$' && name[0] !== '@') undo2 = addNamed(st, name, sub);
        if (k(q)) return true;
        undo(); if (undo2) undo2();
        return false;
    };
    if (name[0] === '$' || name[0] === '@') {   // <$var> / <@var>: the value is the pattern — a Regex, or a string parsed as one
        const v = n.fn ? n.fn() : Nil;
        const rec = n.alias ? record : (sub, q) => k(q);
        const one = (x, cont) => { const rx = x instanceof RRegex ? x : rxFromString(str(x), !!(n.icase || st.rx.tree.icase)); return callRule({ rx, kind: rx.tree.ratchet ? 'token' : 'regex' }, capKey, { k: 'Subrule', name: capKey }, st, pos, cont); };
        if (n.lit) {   // a bare @array: its strings, literally, longest first
            const xs = (v instanceof RList || v instanceof RSeq) ? arr(v) : [v];
            const alts = xs.map(str).sort((a, b) => b.length - a.length);
            for (const a of alts) if (s.startsWith(a, pos) && rec(new RMatch(s, pos, pos + a.length), pos + a.length)) return true;
            return false;
        }
        if (v instanceof RList || v instanceof RSeq) { const xs = arr(v); const ordered = xs.every(x => !(x instanceof RRegex)) ? xs.map(str).sort((a, b) => b.length - a.length) : xs; for (const x of ordered) if (one(x, rec)) return true; return false; }   // longest alternative first
        return one(v, rec);
    }
    if (name === 'sym') { const sym = st.curSym; if (sym == null || !s.startsWith(sym, pos)) return false; const sub = new RMatch(s, pos, pos + sym.length); return record(sub, pos + sym.length); }
    if (n.rec) return callRule({ rx: st.rx, kind: st.rx.tree.ratchet ? 'token' : 'regex' }, name, n, st, pos, record);
    if (n.inline) return callRule({ rx: new RRegex(n.inline), kind: 'regex' }, name, n, st, pos, record);
    // lexical `my regex NAME`, then the grammar's own rules, then a method, then the builtins
    const lex = namedRegexes.get(name);
    const rule = st.ctx.grammar ? findRule(st.ctx.grammar, name) : null;
    if (rule && !rule.proto) return callRule(rule, name, n, st, pos, record);
    if (st.ctx.grammar) {
        const cands = protoCandidates(st.ctx.grammar, name);
        if (cands.length) {
            for (const c of cands) {
                const ok = callRule(c.rule, c.name, n, st, pos, (sub, q) => { sub.rule = c.name; sub.actualRule = c.name; return record(sub, q); }, c.sym);
                if (ok) return true;
            }
            return false;
        }
    }
    if (lex) return callRule({ rx: lex, kind: lex.tree.ratchet ? 'token' : 'regex' }, name, n, st, pos, record);
    if (st.ctx.grammar) {
        const meth = st.ctx.grammar.findUser(name);
        if (meth) {
            const cursor = cursorMatch(st, pos);
            cursor.ctx = st.ctx; cursor.pos = pos; cursor.st = st;
            const args = n.argsFn ? n.argsFn(cursor) : [];
            const r = meth(cursor, ...args);
            if (r instanceof RMatch) return record(r, r.to);
            if (r instanceof RType || r === false) return false;
            return record(new RMatch(s, pos, pos), pos);
        }
    }
    const e = builtinRule(name, s, pos);
    if (e !== undefined) { if (e < 0) return false; if (!capture || name === 'ws') return k(e); return record(new RMatch(s, pos, e), e); }
    throw new RakuError(`No such method '${name}' for invocant of type '${st.ctx.grammar ? st.ctx.grammar.name : 'Match'}'`, 'X::Method::NotFound');
}
// Call a rule (a compiled RRegex with a kind) at pos: its own capture frame; a
// ratchet rule commits to its first match and is memoized per (rule, pos).
function callRule(rule, name, n, st, pos, record, sym) {
    const rx = rule.rx;
    const ratchet = !!rx.tree.ratchet;
    const memoKey = ratchet && !n.argsFn ? rx : null;
    if (memoKey) {
        let byPos = st.memo.get(memoKey);
        if (byPos && byPos.has(pos)) { const hit = byPos.get(pos); return hit ? record(hit, hit.to) : false; }
    }
    const st2 = new RxState(st.s, rx, st.ctx);
    st2.steps = st.steps; st2.startPos = pos; st2.memo = st.memo; if (sym !== undefined) st2.curSym = sym;
    if (n.argsFn) st2.args = n.argsFn(cursorMatch(st, pos));
    let result = null;
    if (ratchet) {
        let end = -1;
        const ok = m(rx.root, st2, pos, (q) => { end = q; return true; });
        st.steps = st2.steps;
        if (ok) {
            const sub = new RMatch(st.s, st2.capFrom >= 0 ? st2.capFrom : pos, st2.capTo >= 0 ? st2.capTo : end);
            finishMatch(sub, st2, rx.tree); sub.rule = name;
            fireAction(st, name, sub);
            result = sub;
        }
        if (memoKey) { let byPos = st.memo.get(memoKey); if (!byPos) { byPos = new Map(); st.memo.set(memoKey, byPos); } byPos.set(pos, result); }
        return result ? record(result, result.to) : false;
    }
    // a backtrackable regex rule: threaded through the continuation
    const ok = m(rx.root, st2, pos, (q) => {
        const sub = new RMatch(st.s, st2.capFrom >= 0 ? st2.capFrom : pos, st2.capTo >= 0 ? st2.capTo : q);
        finishMatch(sub, st2, rx.tree); sub.rule = name;
        fireAction(st, name, sub);
        return record(sub, q);
    });
    st.steps = st2.steps;
    return ok;
}
function fireAction(st, name, sub) {
    const actions = st.ctx.actions;
    if (!actions || actions === Nil) return;
    const ty = actions instanceof RType ? actions : typeOf(actions);
    let meth = ty.findUser ? ty.findUser(name) : null;
    if (!meth && name.includes(':')) meth = ty.findUser(name.slice(0, name.indexOf(':')));
    if (meth) meth(actions, sub);
}
function varMatch(n, st, pos, k) {
    const s = st.s;
    let v;
    if (n.name.startsWith('$<')) { const nm = n.name.slice(2, -1); const l = st.named.get(nm); if (!l || !l.length) return false; v = l[l.length - 1].Str(); }
    else if (/^\$\d+$/.test(n.name)) { const c = st.caps[Number(n.name.slice(1))]; if (!c) return false; v = c.Str(); }
    else if (n.fn) v = n.fn();
    else return false;
    if (v instanceof RRegex) return callRule({ rx: v, kind: v.tree.ratchet ? 'token' : 'regex' }, '', { noCapture: true }, st, pos, (sub, q) => k(q));
    if (v instanceof RList || v instanceof RSeq) { const alts = arr(v).map(str).sort((a, b) => b.length - a.length); for (const a of alts) if (s.startsWith(a, pos) && k(pos + a.length)) return true; return false; }
    const lit = str(v);
    return s.startsWith(lit, pos) ? k(pos + lit.length) : false;
}
// fill a match's captures from a frame: positional (list-valued under a quantifier),
// named (a list when repeated or declared so), `%<name>=` hashes
function finishMatch(mt, st, tree) {
    if (st.made !== undefined) mt.made = st.made;
    const caps = [];
    const listCaps = tree.listCaps || [];
    for (let i = 0; i < tree.ncaps; i++) {
        if (listCaps.includes(i)) caps.push(mkList((st.capReps.get(i) || []).slice()));
        else caps.push(st.caps[i] === undefined ? Nil : st.caps[i]);
    }
    while (caps.length && caps[caps.length - 1] === Nil) caps.pop();
    mt.caps = caps;
    mt.named = namedFromFrame(st.named, tree);
    return mt;
}
function namedFromFrame(frame, tree) {
    const named = new Map();
    const listNames = tree.listNames || [], hashNames = tree.hashNames || [];
    for (const [name, list] of frame) {
        if (hashNames.includes(name)) { const h = new RHash(); for (const x of list) h.m.set(x.Str(), x.Str()); named.set(name, h); }
        else if (list.length > 1 || listNames.includes(name)) named.set(name, mkList(list.slice()));
        else named.set(name, list[0]);
    }
    for (const name of listNames) if (!named.has(name)) named.set(name, mkList([]));
    return named;
}
// ---- the surface ------------------------------------------------------------------------
// `self.rule` / `self.method` inside a grammar method running as a subrule: the cursor stands for the grammar
function cursorCall(cur, name, args) {
    const g = cur.ctx && cur.ctx.grammar; if (!g || !cur.st) return undefined;
    const n = { k: 'Subrule', name, noCapture: 1 }; if (args.length) n.argsFn = () => args;
    let out = null;
    const rec = (sub, q) => { out = sub; return true; };
    const rule = findRule(g, name);
    if (rule && !rule.proto) { callRule(rule, name, n, cur.st, cur.pos, rec); return out || Nil; }
    const cands = protoCandidates(g, name);
    if (cands.length) { for (const c of cands) if (callRule(c.rule, c.name, n, cur.st, cur.pos, rec, c.sym)) return out; return Nil; }
    const meth = g.findUser(name);
    if (meth) return meth(cur, ...args);
    return undefined;
}
// the end of a match of rxo anchored at pos, or -1
function matchEndAt(s, rxo, pos) { const st = new RxState(s, rxo, null); st.startPos = pos; let end = -1; m(rxo.root, st, pos, (q) => { end = q; return true; }); return end; }
// A regex built from a STRING at run time (`<$p>` with $p a Str): the subset of
// the syntax a pattern string carries — literals, quotes, \d\w\s\n\t\h and their
// negations, `.`, anchors, word boundaries, ( ) and [ ] groups, <[…]> classes,
// <name> assertions, quantifiers with % separators, | and || alternation, :i.
const rxStrCache = new Map();
function rxFromString(src, ic) {
    const key = ic ? src + '\u0001i' : src;
    let rx = rxStrCache.get(key);
    if (!rx) { rx = new RRegex(parseRxString(src, ic)); rxStrCache.set(key, rx); }
    return rx;
}
function parseRxString(src, ic) {
    let i = 0, ncaps = 0, icase = !!ic;
    const bad = (what) => { throw new RakuError(`a regex built from a string at run time uses ${what}, which is outside the JavaScript core: ${src}`); };
    const ws = () => { for (;;) { while (i < src.length && /\s/.test(src[i])) i++; if (src[i] === '#') { while (i < src.length && src[i] !== '\n') i++; continue; } break; } };
    const hexEsc = () => { let h = ''; if (src[i] === '[') { i++; while (i < src.length && src[i] !== ']') h += src[i++]; i++; } else { while (/[0-9a-fA-F]/.test(src[i] || '')) h += src[i++]; } return parseInt(h, 16); };
    const cls = (neg) => {   // after '<[' or '<-['
        const node = { k: 'Class', ranges: [], flags: '' }; if (neg) node.negate = 1; if (icase) node.icase = 1;
        for (;;) {
            if (i >= src.length) bad('an unterminated character class');
            if (src[i] === ']' && src[i + 1] === '>') { i += 2; break; }
            if (/\s/.test(src[i])) { i++; continue; }
            let cp;
            if (src[i] === '\\') { const e = src[i + 1]; i += 2; if ('dwsn'.includes(e)) { node.flags += e; continue; } if (e === 't') cp = 9; else if (e === 'x') cp = hexEsc(); else cp = e.codePointAt(0); }
            else { cp = src.codePointAt(i); i += cp > 0xFFFF ? 2 : 1; }
            let hi = cp;
            if (src[i] === '.' && src[i + 1] === '.') { i += 2; while (/\s/.test(src[i] || '')) i++; hi = src.codePointAt(i); i += hi > 0xFFFF ? 2 : 1; }
            node.ranges.push(cp, hi);
        }
        if (!node.ranges.length) delete node.ranges;
        if (!node.flags) delete node.flags;
        return node;
    };
    const lit = (s) => { const n = { k: 'Lit', lit: s }; if (icase) n.icase = 1; return n; };
    const atom = () => {
        const c = src[i];
        if (c === '(') { i++; const idx = ncaps++; const kid = alt(); if (src[i] !== ')') bad('an unbalanced parenthesis'); i++; return { k: 'Group', cap: idx, kids: [kid] }; }
        if (c === '[') { i++; const kid = alt(); if (src[i] !== ']') bad('an unbalanced bracket'); i++; return { k: 'Group', kids: [kid] }; }
        if (c === '<') {
            if (src.startsWith('<[', i)) { i += 2; return cls(false); }
            if (src.startsWith('<-[', i)) { i += 3; return cls(true); }
            if (src.startsWith('<<', i)) { i += 2; return { k: 'WBLeft' }; }
            const close = src.indexOf('>', i); if (close < 0) bad('an unterminated <…> assertion');
            let body = src.slice(i + 1, close); i = close + 1;
            if (body[0] === '?' || body[0] === '!') {
                const look = { k: 'Look' }; if (body[0] === '!') look.negate = 1; body = body.slice(1);
                if (body.startsWith('before ')) look.kids = [parseRxString(body.slice(7)).root];
                else if (body.startsWith('after ')) { look.behind = 1; look.kids = [parseRxString(body.slice(6)).root]; }
                else look.kids = [{ k: 'Subrule', name: body, noCapture: 1 }];
                return look;
            }
            const node = { k: 'Subrule' };
            if (body[0] === '.') { node.noCapture = 1; body = body.slice(1); }
            const eq = body.indexOf('='); if (eq > 0) { node.alias = body.slice(0, eq); body = body.slice(eq + 1); }
            if (!/^[\w:-]+$/.test(body)) bad(`the assertion <${body}>`);
            node.name = body; return node;
        }
        if (c === "'" || c === '"') { const close = src.indexOf(c, i + 1); if (close < 0) bad('an unterminated quote'); const s = src.slice(i + 1, close).replace(/\\(.)/g, '$1'); i = close + 1; return lit(s); }
        if (c === '\\') {
            const e = src[i + 1]; i += 2;
            const lower = e.toLowerCase();
            const flagOf = { d: 'd', w: 'w', s: 's', n: 'n', h: 'b' }[lower];
            if (flagOf) { const n = { k: 'Class', flags: flagOf }; if (e !== lower) n.negate = 1; return n; }
            if (e === 't') return lit('\t');
            if (e === 'x') return lit(String.fromCodePoint(hexEsc()));
            if (/[A-Za-z0-9]/.test(e)) bad(`the escape \\${e}`);
            return lit(e);
        }
        if (c === '.') { i++; return { k: 'Any' }; }
        if (c === '^') { i++; if (src[i] === '^') { i++; return { k: 'AnchorStart', multiline: 1 }; } return { k: 'AnchorStart' }; }
        if (c === '$') { i++; if (src[i] === '$') { i++; return { k: 'AnchorEnd', multiline: 1 }; } if (/[\w<]/.test(src[i] || '')) bad('a variable'); return { k: 'AnchorEnd' }; }
        if (src.startsWith('>>', i)) { i += 2; return { k: 'WBRight' }; }
        if (c === '«') { i++; return { k: 'WBLeft' }; }
        if (c === '»') { i++; return { k: 'WBRight' }; }
        const cp = src.codePointAt(i), ch = String.fromCodePoint(cp);
        if (/[\p{L}\p{N}_]/u.test(ch)) { i += ch.length; return lit(ch); }
        bad(`the character '${ch}'`);
    };
    const quant = (a) => {
        ws();
        const c = src[i];
        let node = null;
        if (src.startsWith('**', i)) {
            i += 2; ws();
            const mm = /^(\d+)(?:\s*(\.\.)\s*(\d+|\*))?/.exec(src.slice(i)); if (!mm) bad('a ** quantifier of this form'); i += mm[0].length;
            node = { k: 'Rep', min: Number(mm[1]), max: mm[2] ? (mm[3] === '*' ? -1 : Number(mm[3])) : Number(mm[1]), kids: [a] };
        } else if (c === '*' || c === '+' || c === '?') { i++; node = { k: 'Rep', min: c === '+' ? 1 : 0, max: c === '?' ? 1 : -1, kids: [a] }; }
        if (!node) return a;
        if (src[i] === '?') { i++; node.frugal = 1; } else if (src[i] === ':') { i++; node.possessive = 1; } else if (src[i] === '!') i++;
        ws();
        if (src[i] === '%') { i++; if (src[i] === '%') { i++; node.sepTrail = 1; } ws(); node.sep = atom(); }
        return node;
    };
    const seq = () => {
        const kids = [];
        for (;;) { ws(); if (i >= src.length || src[i] === '|' || src[i] === ')' || src[i] === ']' || src[i] === '&') break; kids.push(quant(atom())); }
        return kids.length === 1 ? kids[0] : { k: 'Seq', kids };
    };
    const alt = () => {
        const kids = [seq()];
        let first = false;
        while (src[i] === '|') { if (src[i + 1] === '|') { i += 2; first = true; } else i++; kids.push(seq()); }
        if (src[i] === '&') bad('a conjunction');
        if (kids.length === 1) return kids[0];
        const n = { k: 'Alt', kids }; if (first) n.firstMatch = 1; return n;
    };
    ws();
    for (;;) {   // leading adverbs
        if (src.startsWith(':i', i) && !/\w/.test(src[i + 2] || '')) { icase = true; i += 2; ws(); continue; }
        if (src.startsWith(':ignorecase', i)) { icase = true; i += 11; ws(); continue; }
        if (src[i] === ':') bad('an adverb'); break;
    }
    const root = alt();
    if (i < src.length) bad(`the character '${src[i]}'`);
    const tree = { root, ncaps }; if (icase) tree.icase = 1;
    return tree;
}
function runSearch(s, rxo, ctx, startPos) {
    const st = new RxState(s, rxo, ctx);
    for (let start = startPos; start <= s.length; start++) {
        st.caps = []; st.capReps = new Map(); st.named = new Map(); st.startPos = start; st.capFrom = -1; st.capTo = -1;
        let end = -1;
        let ok;
        try { ok = m(rxo.root, st, start, (q) => { end = q; return true; }); }
        catch (e) { throw e; }
        if (ok) {
            const mt = new RMatch(s, st.capFrom >= 0 ? st.capFrom : start, st.capTo >= 0 ? st.capTo : end);
            finishMatch(mt, st, rxo.tree);
            mt.end = end;   // where the scan continues (not the `<(` trimmed .from)
            return mt;
        }
        if (start < s.length) { const cp = s.codePointAt(start); if (cp > 0xFFFF) start++; }
    }
    return null;
}
function allMatches(s, rxo, ctx, overlap) {
    const out = [];
    let pos = 0;
    while (pos <= s.length) {
        const mt = runSearch(s, rxo, ctx, pos);
        if (!mt) break;
        out.push(mt);
        pos = overlap ? mt.from + 1 : (mt.end > mt.from ? mt.end : mt.end + 1);
    }
    return out;
}
function rxMatch(subject, rxo, ctx) {
    if (subject instanceof RJunction) return new RJunction(subject.kind, subject.items.map(x => rxMatch(x, rxo, ctx)));   // autothreads
    const s = str(subject);
    if (rxo.adv.g || rxo.adv.ov) return mkList(allMatches(s, rxo, ctx, !!rxo.adv.ov));
    if (rxo.adv.ex) { const out = []; for (let start = 0; start <= s.length; start++) { const st = new RxState(s, rxo, ctx); st.startPos = start; m(rxo.root, st, start, (q) => { const mt = new RMatch(s, start, q); finishMatch(mt, new RxState(s, rxo, ctx), rxo.tree); out.push(mt); return false; }); } return mkList(out); }
    const mt = runSearch(s, rxo, ctx, 0);
    return mt || Nil;
}
// s///: {s: the new string, m: the Match (or their list under :g)}
// .subst-mutate: { s: the new string, m: the Match (or their list under :g, or Nil) }
function substMutate(s, pat, ...a) {
    const named = nm(a); const repl = posArgs(a)[0]; const g = truthy(named.get('g') ?? named.get('global') ?? false);
    if (pat instanceof RRegex) { const r = rxSubst(str(s), pat, mt => typeof repl === 'function' ? repl(mt) : str(repl), { g }); return { s: r.s, m: r.m }; }
    const src = str(s), lit = str(pat);
    if (!src.includes(lit)) return { s: src, m: Nil };
    const rep = typeof repl === 'function' ? str(repl(lit)) : str(repl);
    return { s: g ? src.split(lit).join(rep) : src.replace(lit, () => rep), m: lit };
}
function rxSubst(subject, rxo, replFn, opts) {
    const s = str(subject);
    const global = !!(rxo.adv.g || (opts && opts.g));
    const ms = global ? allMatches(s, rxo, null, false) : (() => { const one = runSearch(s, rxo, null, 0); return one ? [one] : []; })();
    if (!ms.length) return { s, m: global ? mkList([]) : Nil };
    let out = '', last = 0;
    for (const mt of ms) { out += s.slice(last, mt.from) + str(replFn(mt)); last = mt.to; }
    out += s.slice(last);
    return { s: out, m: global ? mkList(ms) : ms[0] };
}
function regexMatch(v, rxo) { const mt = runSearch(str(v), rxo, null, 0); return mt ? mt : Nil; }   // a failed match is Nil, as `~~` answers
function regexComb(s, rxo, limit) { const ms = allMatches(s, rxo, null, false); const out = ms.map(mt => mt.Str()); return mkList(limit !== undefined ? out.slice(0, Number(toInt(limit))) : out); }
function regexSplit(s, rxo, limit, named) {
    const ms = allMatches(s, rxo, null, false);
    const v = named && truthy(named.get('v')), kk = named && truthy(named.get('k')), kv = named && truthy(named.get('kv')), p = named && truthy(named.get('p'));
    const out = []; let last = 0, pieces = 0;
    for (const mt of ms) {
        if (pieces >= limOf(limit) - 1) break;
        out.push(s.slice(last, mt.from)); pieces++;
        if (v) out.push(mt); else if (kk) out.push(0); else if (kv) out.push(0, mt); else if (p) out.push(new RPair(0, mt));   // the separator is always the 0th
        last = mt.to;
    }
    out.push(s.slice(last));
    return out;
}
function make(mt, v) { if (mt instanceof RMatch) { mt.made = v; if (mt.st) mt.st.made = v; } return v; }   // an inline `{ make … }` lands on the match being built
function matchAt(mt, i) { return mt instanceof RMatch ? mt.pos(i) : Nil; }
function matchNamed(mt, k) { return mt instanceof RMatch ? mt.name(k) : Nil; }
// grapheme offsets for .from/.to/.pos
function gOff(s, i) { return isAscii(s) ? i : graphemes(s.slice(0, i)).length; }
function matchGist(mt, depth) {
    let out = '｢' + mt.Str() + '｣';
    const entries = [];
    mt.caps.forEach((c, i) => { if (c instanceof RList) { for (const x of c.a) entries.push([x.from, String(i), x]); } else if (c instanceof RMatch) entries.push([c.from, String(i), c]); });
    for (const [k, v] of mt.named) { if (v instanceof RList) { for (const x of v.a) entries.push([x.from, k, x]); } else if (v instanceof RMatch) entries.push([v.from, k, v]); else entries.push([0, k, v]); }
    entries.sort((a, b) => a[0] - b[0]);
    const pad = ' '.repeat(depth + 1);
    for (const [, k, v] of entries) out += '\n' + pad + k + ' => ' + (v instanceof RMatch ? matchGist(v, depth + 1) : gist(v));
    return out;
}
function matchRaku(mt) {
    const list = mt.caps.map(c => c instanceof RMatch ? matchRaku(c) : raku(c)).join(', ');
    const hash = Array.from(mt.named, ([k, v]) => strLit(k) + ' => ' + (v instanceof RMatch ? matchRaku(v) : raku(v))).join(', ');
    return 'Match.new(:orig(' + strLit(mt.orig) + '), :from(' + gOff(mt.orig, mt.from) + '), :pos(' + gOff(mt.orig, mt.to) + ')' + (list ? ', :list((' + list + '))' : '') + (hash ? ', :hash(Map.new((' + hash + ')))' : '') + ')';
}
function matchList(mt) { return mkList(mt.caps.slice()); }
function matchHash(mt) { const h = new RHash(); for (const [k, v] of mt.named) h.m.set(k, v); return h; }
// Grammar.parse(str, :rule, :actions, :args)
function grammarParse(ty, subject, args, subparse) {
    const [pos, named] = splitArgs(args);
    const s = str(pos[0]);
    const ruleName = named.has('rule') ? str(named.get('rule')) : 'TOP';
    const actions = named.get('actions');
    const rule = findRule(ty, ruleName);
    if (!rule) throw new RakuError(`No such method '${ruleName}' for invocant of type '${ty.name}'`, 'X::Method::NotFound');
    const ctx = { grammar: ty, actions: actions === undefined ? null : actions, memo: new Map() };
    if (rule.proto) {   // parsing from a proto: the top-level match is the winning candidate's
        const st = new RxState(s, new RRegex({ root: { k: 'Subrule', name: ruleName }, ncaps: 0 }), ctx);
        let found = null;
        const ok = subrule({ k: 'Subrule', name: ruleName }, st, 0, (q) => { if (!subparse && q !== s.length) return false; found = st.named.get(ruleName)[0]; return true; });
        if (!ok) return Nil;
        return found;
    }
    const st = new RxState(s, rule.rx, ctx);
    st.startPos = 0;
    let end = -1;
    const ok = m(rule.rx.root, st, 0, (q) => { if (!subparse && q !== s.length) return false; end = q; return true; });
    if (!ok) return Nil;
    const mt = new RMatch(s, st.capFrom >= 0 ? st.capFrom : 0, st.capTo >= 0 ? st.capTo : end);
    finishMatch(mt, st, rule.rx.tree); mt.rule = ruleName;
    if (ctx.actions) { const aty = ctx.actions instanceof RType ? ctx.actions : typeOf(ctx.actions); const meth = aty.findUser ? aty.findUser(ruleName) : null; if (meth) meth(ctx.actions, mt); }
    return mt;
}
mkType('Match', [T.Capture]);
M(T.Match, {
    Str: (s) => s.Str(), gist: (s) => matchGist(s, 0), raku: (s) => matchRaku(s), Bool: (s) => true, so: (s) => true, defined: (s) => true,
    from: (s) => gOff(s.orig, s.from), to: (s) => gOff(s.orig, s.to), pos: (s) => gOff(s.orig, s.to), orig: (s) => s.orig, target: (s) => s.orig,
    prematch: (s) => s.orig.slice(0, s.from), postmatch: (s) => s.orig.slice(s.to), made: (s) => s.made === undefined ? Nil : s.made, ast: (s) => s.made === undefined ? Nil : s.made, make: (s, v) => make(s, v),
    list: (s) => matchList(s), List: (s) => matchList(s), Slip: (s) => mkSlip(matchList(s).a.slice()), hash: (s) => matchHash(s), Hash: (s) => matchHash(s), elems: (s) => s.caps.length,
    keys: (s) => mkSeq(s.caps.map((_, i) => i).concat(Array.from(s.named.keys()))), values: (s) => mkSeq(s.caps.concat(Array.from(s.named.values())).flatMap(c => c instanceof RList ? c.a : [c])),   // a quantified capture contributes each sub-match
    kv: (s) => { const o = []; s.caps.forEach((c, i) => o.push(i, c)); for (const [k, v] of s.named) o.push(k, v); return mkSeq(o); },
    pairs: (s) => { const o = []; s.caps.forEach((c, i) => o.push(pair(i, c))); for (const [k, v] of s.named) o.push(pair(k, v)); return mkSeq(o); },
    caps: (s) => { const o = []; s.caps.forEach((c, i) => { if (c instanceof RList) for (const x of c.a) o.push(pair(i, x)); else if (c instanceof RMatch) o.push(pair(i, c)); }); for (const [k, v] of s.named) { if (v instanceof RList) for (const x of v.a) o.push(pair(k, x)); else if (v instanceof RMatch) o.push(pair(k, v)); } return mkSeq(o.sort((a, b) => a.v.from - b.v.from)); },
    chunks: (s) => { const o = []; let last = s.from; const caps = arr(mc(s, 'caps')); for (const p of caps) { if (p.v.from > last) o.push(pair('~', new RMatch(s.orig, last, p.v.from))); o.push(p); last = p.v.to; } if (last < s.to) o.push(pair('~', new RMatch(s.orig, last, s.to))); return mkSeq(o); },
    'AT-POS': (s, i) => s.pos(Number(toInt(i))), 'AT-KEY': (s, k) => s.name(str(k)), 'EXISTS-KEY': (s, k) => s.named.has(str(k)), 'EXISTS-POS': (s, i) => Number(toInt(i)) < s.caps.length,
    Int: (s) => toInt(strToNumeric(s.Str())), Numeric: (s) => strToNumeric(s.Str()), Num: (s) => mkNum(toFloat(strToNumeric(s.Str()))), chars: (s) => chars(s.Str()), 'Match': (s) => s, 'WHAT': (s) => T.Match, 'rule': (s) => s.rule || Nil, 'succeeded': (s) => true,
    'iterator': (s) => s.caps[Symbol.iterator](), 'join': (s, sep) => joinList(mkList(s.caps), sep), 'map': (s, f) => mapList(mkList(s.caps), f), 'first': (s, ...a) => firstOf(mkList(s.caps), posArgs(a)[0], nm(a)), 'sort': (s, f) => sortList(mkList(s.caps), f), 'grep': (s, f) => grepList(mkList(s.caps), f),
});
const regexGist = (s) => 'rx/' + (s.src === undefined ? '…' : s.src) + '/';
M(T.Regex, { gist: regexGist, Str: regexGist, raku: regexGist, ACCEPTS: (s, v) => { const mt = runSearch(str(v), s, null, 0); return mt ? mt : Nil; }, Bool: (s) => true, defined: (s) => true, 'WHAT': (s) => T.Regex, 'match': (s, v) => rxMatch(v, s) });
Object.assign(R, { RRegex, RMatch, rx, isMatch: (v) => v instanceof RMatch, isRegex: (v) => v instanceof RRegex, isFailure: (v) => v instanceof RFailure, substMutate, cursorCall, matchEndAt, rxFromString, namedRegex, rxMatch, rxSubst, regexMatch, regexComb, regexSplit, make, matchAt, matchNamed, grammarParse, allMatches, runSearch, matchGist });
