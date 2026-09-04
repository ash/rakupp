// Strings: graphemes (UAX #29, the same rule chain as src/Unicode.cpp over the
// same generated tables), and the Str methods the core covers.

// --- grapheme-cluster breaks ------------------------------------------------
const GB_Other = 0, GB_CR = 1, GB_LF = 2, GB_Control = 3, GB_Extend = 4, GB_ZWJ = 5, GB_RI = 6, GB_Prepend = 7,
      GB_SpacingMark = 8, GB_L = 9, GB_V = 10, GB_T = 11, GB_LV = 12, GB_LVT = 13, GB_ExtPict = 14;
function gbProp(cp) {
    if (cp < 0x300) {
        if (cp === 0x0D) return GB_CR;
        if (cp === 0x0A) return GB_LF;
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return GB_Control;
        if (cp === 0xA9 || cp === 0xAE) return GB_ExtPict;
        if (cp === 0xAD) return GB_Control;
        return GB_Other;
    }
    if (cp === 0x200D) return GB_ZWJ;
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return GB_RI;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0xA960 && cp <= 0xA97C)) return GB_L;
    if ((cp >= 0x1160 && cp <= 0x11A7) || (cp >= 0xD7B0 && cp <= 0xD7C6)) return GB_V;
    if ((cp >= 0x11A8 && cp <= 0x11FF) || (cp >= 0xD7CB && cp <= 0xD7FB)) return GB_T;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return ((cp - 0xAC00) % 28 === 0) ? GB_LV : GB_LVT;
    let lo = 0, hi = GBRANGE.length / 3;
    while (lo < hi) {
        const mid = (lo + hi) >> 1;
        const s = GBRANGE[mid * 3], e = GBRANGE[mid * 3 + 1];
        if (cp < s) hi = mid;
        else if (cp > e) lo = mid + 1;
        else switch (GBRANGE[mid * 3 + 2]) {
            case 1: return GB_Extend;
            case 2: return GB_SpacingMark;
            case 3: return GB_Control;
            case 4: return GB_Prepend;
            case 5: return GB_ExtPict;
            default: return GB_Other;
        }
    }
    return GB_Other;
}
function incbProp(cp) {
    let lo = 0, hi = INCB.length / 3;
    while (lo < hi) {
        const mid = (lo + hi) >> 1;
        const s = INCB[mid * 3], e = INCB[mid * 3 + 1];
        if (cp < s) hi = mid; else if (cp > e) lo = mid + 1; else return INCB[mid * 3 + 2];
    }
    return 0;
}
// Split into grapheme clusters. ASCII strings are their own characters.
function graphemes(s) {
    if (isAscii(s)) return s.split('');
    const out = [];
    let start = 0, i = 0;
    const n = s.length;
    let first = s.codePointAt(0);
    let prev = gbProp(first), pictSeq = prev === GB_ExtPict, riRun = prev === GB_RI ? 1 : 0,
        incbState = incbProp(first) === 2 ? 1 : 0;
    i = first > 0xFFFF ? 2 : 1;
    while (i < n) {
        const cp = s.codePointAt(i);
        const cur = gbProp(cp), ip = incbProp(cp);
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
        if (brk) { out.push(s.slice(start, i)); start = i; }
        riRun = (cur === GB_RI) ? (brk ? 1 : riRun + 1) : 0;
        if (cur === GB_ExtPict) pictSeq = true;
        else if (!brk && pictSeq && (cur === GB_Extend || cur === GB_ZWJ)) pictSeq = true;
        else pictSeq = false;
        if (brk) incbState = (ip === 2) ? 1 : 0;
        else if (ip === 2) incbState = 1;
        else if (incbState >= 1 && ip === 1) incbState = 2;
        else if (!(incbState >= 1 && ip === 3)) incbState = 0;
        prev = cur;
        i += cp > 0xFFFF ? 2 : 1;
    }
    out.push(s.slice(start));
    return out;
}
function chars(s) { s = str(s); return isAscii(s) ? s.length : graphemes(s).length; }
function codes(s) { let n = 0; for (const _ of str(s)) n++; return n; }
function substr(s, from, len) {
    s = str(s);
    const g = isAscii(s) ? null : graphemes(s);
    const total = g ? g.length : s.length;
    let f = typeof from === 'function' ? toInt(from(total)) : toInt(from);
    if (f < 0) f += total;
    if (f > total) throw new RakuError(`Start argument to substr out of range. Is: ${f}, should be in 0..${total}; use *-${-f + total} if you want to index relative to the end`, 'X::OutOfRange');
    let l = len === undefined ? total - f : (typeof len === 'function' ? toInt(len(total)) : toInt(len));
    if (l < 0) l = 0;
    return g ? g.slice(f, f + l).join('') : s.substr(f, l);
}
function flip(s) { s = str(s); return isAscii(s) ? s.split('').reverse().join('') : graphemes(s).reverse().join(''); }
function uc(s) { return str(s).toUpperCase(); }
function lc(s) { return str(s).toLowerCase(); }
function tc(s) { s = str(s); if (s === '') return s; const g = graphemes(s); return g[0].toUpperCase() + g.slice(1).join(''); }
function tclc(s) { return tc(lc(s)); }
function wordcase(s) { return str(s).replace(/[^\s]+/g, w => tclc(w)); }
function fc(s) { return str(s).toLowerCase(); }
function trim(s) { return str(s).replace(/^\s+|\s+$/g, ''); }
function trimLeading(s) { return str(s).replace(/^\s+/, ''); }
function trimTrailing(s) { return str(s).replace(/\s+$/, ''); }
function chomp(s) { s = str(s); return s.endsWith('\n') ? s.slice(0, -1) : s; }
function chop(s, n = 1) { s = str(s); const g = graphemes(s); return g.slice(0, Math.max(0, g.length - toInt(n))).join(''); }
function ord(s) { s = str(s); return s === '' ? Nil : s.codePointAt(0); }
function chr(n) { return String.fromCodePoint(Number(toInt(n))); }
function ords(s) { return mkList(Array.from(str(s), c => c.codePointAt(0))); }
function chrs(l) { return arr(l).map(chr).join(''); }
function strIndex(s, needle, start) {
    s = str(s); needle = str(needle);
    if (isAscii(s)) { const i = s.indexOf(needle, start === undefined ? 0 : toInt(start)); return i < 0 ? Nil : i; }
    const g = graphemes(s), ng = graphemes(needle);
    const from = start === undefined ? 0 : toInt(start);
    outer: for (let i = from; i + ng.length <= g.length; i++) {
        for (let j = 0; j < ng.length; j++) if (g[i + j] !== ng[j]) continue outer;
        return i;
    }
    return Nil;
}
function strRindex(s, needle, start) {
    s = str(s); needle = str(needle);
    const i = s.lastIndexOf(needle, start === undefined ? Infinity : toInt(start));
    if (i < 0) return Nil;
    return isAscii(s) ? i : graphemes(s.slice(0, i)).length;
}
function contains(s, needle, start) {
    if (needle instanceof RJunction) return junctionOp(x => contains(s, x, start), needle, null);
    return str(s).indexOf(str(needle), start === undefined ? 0 : toInt(start)) >= 0;
}
function startsWith(s, p) { return str(s).startsWith(str(p)); }
function endsWith(s, p) { return str(s).endsWith(str(p)); }
function strSplit(s, sep, limit, named) {
    s = str(s);
    const skipEmpty = named && truthy(named.get('skip-empty'));
    let parts;
    if (sep instanceof RRegex) parts = regexSplit(s, sep, limit);
    else if (sep instanceof RList) {
        const seps = sep.arr().map(str).filter(x => x !== '').sort((a, b) => b.length - a.length);
        parts = []; let cur = '';
        for (let i = 0; i < s.length;) {
            let hit = null;
            for (const sp of seps) if (s.startsWith(sp, i)) { hit = sp; break; }
            if (hit) { parts.push(cur); cur = ''; i += hit.length; } else { cur += s[i]; i++; }
        }
        parts.push(cur);
    } else {
        const d = str(sep);
        if (d === '') parts = ['', ...graphemes(s), ''];
        else parts = s.split(d);
        if (limit !== undefined && limit !== null && !(limit instanceof RWhatever)) {
            const l = toInt(limit);
            if (l > 0 && parts.length > l) { const head = parts.slice(0, l - 1); head.push(parts.slice(l - 1).join(d)); parts = head; }
        }
    }
    if (skipEmpty) parts = parts.filter(p => p !== '');
    return mkList(parts);
}
function words(s) { s = str(s).trim(); return mkList(s === '' ? [] : s.split(/\s+/)); }
function lines(s) { s = str(s); if (s === '') return mkList([]); const l = s.split('\n'); if (l[l.length - 1] === '') l.pop(); return mkList(l.map(x => x.endsWith('\r') ? x.slice(0, -1) : x)); }
function comb(s, pat, limit) {
    s = str(s);
    if (pat === undefined) return mkList(graphemes(s));
    if (pat instanceof RRegex) return regexComb(s, pat, limit);
    if (typeof pat === 'number' || typeof pat === 'bigint') {
        const n = Number(pat), g = graphemes(s), out = [];
        for (let i = 0; i < g.length; i += n) out.push(g.slice(i, i + n).join(''));
        return mkList(out);
    }
    const p = str(pat), out = [];
    if (p === '') return mkList(graphemes(s));
    let i = 0; while ((i = s.indexOf(p, i)) >= 0) { out.push(p); i += p.length; if (limit !== undefined && out.length >= toInt(limit)) break; }
    return mkList(out);
}
function indent(s, n) {
    const k = toInt(n);
    if (k >= 0) { const pad = ' '.repeat(k); return str(s).split('\n').map(l => l === '' ? l : pad + l).join('\n'); }
    return str(s).split('\n').map(l => l.replace(new RegExp('^\\s{0,' + (-k) + '}'), '')).join('\n');
}
function strRepeatList(s, n) { return xrepeat(s, n); }
function strJoin(l, sep) { return arr(l).map(str).join(sep === undefined ? '' : str(sep)); }
function strEncode(s) { return mkList(Array.from(new TextEncoder().encode(str(s)))); }
function unival(s) { return Nil; }
function isNumericStr(s) { try { strToNumeric(s); return true; } catch (e) { return false; } }
function strParseNumeric(s, what) {
    const n = strToNumeric(str(s));
    return n;
}
function strInt(s) { return toInt(strToNumeric(str(s))); }
function strNum(s) { const n = strToNumeric(str(s)); return typeof n === 'number' && !Number.isInteger(n) ? n : mkNum(toFloat(n)); }
function strRat(s) { const n = strToNumeric(str(s)); return n instanceof RRat ? n : mkRat(big(toInt(n)), 1n); }
function succ(v) { return inc(v); }
function pred(v) { return dec(v); }
function strNfc(s) { return str(s).normalize('NFC'); }
function strNfd(s) { return str(s).normalize('NFD'); }
function samecase(s, pat) {
    const a = graphemes(str(s)), p = graphemes(str(pat));
    return a.map((c, i) => { const q = p[Math.min(i, p.length - 1)]; return q === q.toUpperCase() && q !== q.toLowerCase() ? c.toUpperCase() : q === q.toLowerCase() && q !== q.toUpperCase() ? c.toLowerCase() : c; }).join('');
}
function strTrans(s, pairs) {
    s = str(s);
    const map = new Map();
    const ps = pairs instanceof RList ? pairs.arr() : [pairs];
    for (const p of ps) {
        if (!(p instanceof RPair)) continue;
        const from = expandTrans(p.k), to = expandTrans(p.v);
        for (let i = 0; i < from.length; i++) map.set(from[i], to.length ? to[Math.min(i, to.length - 1)] : '');
    }
    let out = '';
    for (const g of graphemes(s)) out += map.has(g) ? map.get(g) : g;
    return out;
}
function expandTrans(v) {
    if (v instanceof RList) return v.arr().flatMap(expandTrans);
    if (v instanceof RRange) return arr(v).map(str);
    return graphemes(str(v));
}
// sprintf — the subset the corpus uses: %s %d %i %u %f %e %g %x %X %o %b %c %% with flags, width, precision, `*`
function sprintf(fmt, ...args) {
    fmt = str(fmt);
    let ai = 0;
    const next = () => { if (ai >= args.length) throw new RakuError('Your printf-style directives specify ' + (ai + 1) + ' arguments, but ' + args.length + ' argument' + (args.length === 1 ? ' was' : 's were') + ' supplied', 'X::Str::Sprintf::Directives::Count'); return args[ai++]; };
    let out = '';
    for (let i = 0; i < fmt.length; i++) {
        const c = fmt[i];
        if (c !== '%') { out += c; continue; }
        i++;
        if (fmt[i] === '%') { out += '%'; continue; }
        let flags = '';
        while ('-+ 0#'.includes(fmt[i])) flags += fmt[i++];
        let width = '';
        if (fmt[i] === '*') { width = String(toInt(next())); i++; } else while (fmt[i] >= '0' && fmt[i] <= '9') width += fmt[i++];
        let prec = null;
        if (fmt[i] === '.') { i++; prec = ''; if (fmt[i] === '*') { prec = String(toInt(next())); i++; } else while (fmt[i] >= '0' && fmt[i] <= '9') prec += fmt[i++]; if (prec === '') prec = '0'; }
        while ('hlqLV'.includes(fmt[i])) i++;
        const conv = fmt[i];
        let s;
        const left = flags.includes('-'), zero = flags.includes('0') && !left, plus = flags.includes('+'), space = flags.includes(' '), alt = flags.includes('#');
        const signed = (neg, body) => neg ? '-' + body : plus ? '+' + body : space ? ' ' + body : body;
        const padNum = (sign, body) => {
            const w = width === '' ? 0 : parseInt(width, 10);
            if (zero && sign.length + body.length < w) body = '0'.repeat(w - sign.length - body.length) + body;
            return sign + body;
        };
        switch (conv) {
            case 's': s = str(next()); if (prec !== null) s = graphemes(s).slice(0, parseInt(prec, 10)).join(''); break;
            case 'd': case 'i': case 'u': {
                const v = toInt(toNumeric(next()));
                let b = big(v); const neg = b < 0n; if (neg) b = -b;
                let body = b.toString();
                if (prec !== null) while (body.length < parseInt(prec, 10)) body = '0' + body;
                s = padNum(neg ? '-' : plus ? '+' : space ? ' ' : '', body);
                break;
            }
            case 'f': case 'F': { const v = toFloat(next()); const p = prec === null ? 6 : parseInt(prec, 10); const neg = v < 0 || Object.is(v, -0); let body = Number.isFinite(v) ? Math.abs(v).toFixed(p) : numToStr(Math.abs(v)); s = padNum(neg ? '-' : plus ? '+' : space ? ' ' : '', body); break; }
            case 'e': case 'E': { const v = toFloat(next()); const p = prec === null ? 6 : parseInt(prec, 10); const neg = v < 0; let body = Math.abs(v).toExponential(p).replace(/e([+-])(\d)$/, 'e$10$2'); if (conv === 'E') body = body.toUpperCase(); s = padNum(neg ? '-' : plus ? '+' : space ? ' ' : '', body); break; }
            case 'g': case 'G': { const v = toFloat(next()); const p = prec === null ? 6 : Math.max(1, parseInt(prec, 10)); const neg = v < 0; let body = fmtG(Math.abs(v), p); if (conv === 'G') body = body.toUpperCase(); s = padNum(neg ? '-' : plus ? '+' : space ? ' ' : '', body); break; }
            case 'x': case 'X': case 'o': case 'b': case 'B': {
                let b = big(toInt(toNumeric(next()))); const neg = b < 0n; if (neg) b = -b;
                const base = conv === 'o' ? 8 : (conv === 'b' || conv === 'B') ? 2 : 16;
                let body = b.toString(base); if (conv === 'X') body = body.toUpperCase();
                if (prec !== null) while (body.length < parseInt(prec, 10)) body = '0' + body;
                const pre = alt && b !== 0n ? (conv === 'o' ? '0' : conv === 'x' ? '0x' : conv === 'X' ? '0X' : '0b') : '';
                s = padNum((neg ? '-' : '') + pre, body);
                break;
            }
            case 'c': s = chr(next()); break;
            default: throw new RakuError(`Directive ${conv} not applicable`, 'X::Str::Sprintf::Directives::Unsupported');
        }
        const w = width === '' ? 0 : parseInt(width, 10);
        const cl = chars(s);
        if (cl < w) s = left ? s + ' '.repeat(w - cl) : ' '.repeat(w - cl) + s;
        out += s;
    }
    if (ai < args.length) throw new RakuError('Your printf-style directives specify ' + ai + ' argument' + (ai === 1 ? '' : 's') + ', but ' + args.length + ' argument' + (args.length === 1 ? ' was' : 's were') + ' supplied', 'X::Str::Sprintf::Directives::Count');
    return out;
}
function fmt(v, f) { return sprintf(f === undefined ? '%s' : f, v); }
// Str.Int / .Num etc. for the base-N methods
function parseBase(s, base) {
    s = str(s).replace(/_/g, '');
    let neg = false; if (s[0] === '-') { neg = true; s = s.slice(1); } else if (s[0] === '+') s = s.slice(1);
    const b = Number(toInt(base));
    const digits = '0123456789abcdefghijklmnopqrstuvwxyz';
    let n = 0n; const bb = BigInt(b);
    for (const ch of s.toLowerCase()) {
        const d = digits.indexOf(ch);
        if (d < 0 || d >= b) throw new RakuError(`Cannot convert string to number: base-${b} number must begin with valid digits or '.' in '${s}'`, 'X::Str::Numeric');
        n = n * bb + BigInt(d);
    }
    return normBig(neg ? -n : n);
}
function toBase(v, base) {
    const b = Number(toInt(base)), x = toNumeric(v);
    if (x instanceof RRat || x instanceof RNum || (typeof x === 'number' && !Number.isInteger(x))) {
        const f = toFloat(x);
        let ip = Math.trunc(f), fp = Math.abs(f - ip);
        let s = big(ip).toString(b).toUpperCase();
        if (fp > 0) { s += '.'; for (let i = 0; i < 10 && fp > 0; i++) { fp *= b; const d = Math.floor(fp); s += d.toString(b).toUpperCase(); fp -= d; } }
        return s;
    }
    return big(x).toString(b).toUpperCase();
}
function strUniname(s) { return Nil; }

Object.assign(R, {
    graphemes, chars, codes, substr, flip, uc, lc, tc, tclc, wordcase, fc, trim, trimLeading, trimTrailing, chomp, chop,
    ord, chr, ords, chrs, strIndex, strRindex, contains, startsWith, endsWith, strSplit, words, lines, comb, indent,
    strJoin, strEncode, strInt, strNum, strRat, succ, pred, strNfc, strNfd, samecase, strTrans, sprintf, fmt,
    parseBase, toBase, isNumericStr, strToNumeric,
});
