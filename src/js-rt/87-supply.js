// Supplies, Suppliers, Channels and react/whenever (TRANSPILE-PLAN P4). One
// thread, cooperative: the event loop runs at the await points — `await`,
// `sleep`, `.receive`, `react`, a Channel's or live Supply's `.list` — and a
// `start` block is a microtask. Taps run synchronously on emit. A `supply {}`
// block runs once per tap with `emit`/`done` bound to that tap through a
// dynamic context; `whenever` subscribes inside a react or supply context and
// `done` finishes the whole context by unwinding the current callback.

const SupplyT = mkType('Supply', [T.Any]);
const SupplierT = mkType('Supplier', [T.Any]);
const SupplierPreservingT = mkType('Supplier::Preserving', [SupplierT]);
const ChannelT = mkType('Channel', [T.Any]);
const TapT = mkType('Tap', [T.Any]);
const VowT = mkType('Vow', [T.Any]);
class DoneCtl { }                        // `done`: ends the enclosing react/supply
let activeStarts = 0, activeTimers = 0;   // is anything still going to happen? (a receive with nobody to send answers Nil)
const pendingWork = () => activeStarts + activeTimers > 0;

class RTap { constructor(closeFn) { this.closeFn = closeFn; this.closed = false; } close() { if (!this.closed) { this.closed = true; if (this.closeFn) this.closeFn(); } return true; } }
class RSupply {
    constructor(producer, derive) { this.producer = producer || null; this.derive = derive || null; this.taps = []; this.doneFlag = false; this.quitErr = null; this.replay = null; this.preserving = false; }
    tap(emitFn, doneFn, quitFn) {
        const t = { emit: emitFn || (() => { }), done: doneFn || null, quit: quitFn || null, tap: null, closed: false, cleanup: null };
        const rt = new RTap(() => { t.closed = true; const i = this.taps.indexOf(t); if (i >= 0) this.taps.splice(i, 1); if (t.cleanup) { const c = t.cleanup; t.cleanup = null; c(); } });
        t.tap = rt;
        if (this.producer) { runProducer(this, this.producer, t); return rt; }
        if (this.derive) { this.derive(t); return rt; }
        this.taps.push(t);
        if (this.replay && this.replay.length) { const r = this.replay; this.replay = null; for (const v of r) { if (t.closed) break; safeEmit(t, v); } }
        if (this.doneFlag && !t.closed) { if (t.done) t.done(); rt.close(); }
        else if (this.quitErr && !t.closed) { if (t.quit) t.quit(this.quitErr); rt.close(); }
        return rt;
    }
    emit(v) { if (this.preserving && !this.taps.length) { (this.replay || (this.replay = [])).push(v); return; } for (const t of this.taps.slice()) if (!t.closed) safeEmit(t, v); }
    done() { this.doneFlag = true; for (const t of this.taps.slice()) if (!t.closed) { if (t.done) t.done(); t.tap.close(); } }
    quit(e) { this.quitErr = e; for (const t of this.taps.slice()) if (!t.closed) { if (t.quit) t.quit(e); t.tap.close(); } }
}
function safeEmit(t, v) { const r = t.emit(v); if (r && typeof r.then === 'function') r.catch(e => { if (!(e instanceof DoneCtl)) reportUncaught(e); }); }
class RSupplier { constructor(preserving) { this.supply = new RSupply(); this.supply.preserving = !!preserving; } }
class RVow { constructor(p) { this.p = p; } }

// ---- the dynamic context: a running supply body or react block -------------
const ctxStack = [];
function withCtx(ctx, fn) { ctxStack.push(ctx); try { return fn(); } finally { ctxStack.pop(); } }
function finishCtx(ctx) {
    if (ctx.finished) return;
    ctx.finished = true;
    for (const s of ctx.subs.slice()) s.close();
    ctx.subs = [];
    if (ctx.kind === 'react') ctx.resolve(Nil);
    else { const t = ctx.t; if (!t.closed) { if (t.done) t.done(); t.tap.close(); } }
}
function failCtx(ctx, e) {
    if (ctx.finished) return;
    ctx.finished = true;
    for (const s of ctx.subs.slice()) s.close();
    if (ctx.kind === 'react') ctx.reject(e);
    else { const t = ctx.t; if (!t.closed) { if (t.quit) t.quit(e instanceof RakuError ? e : exc(e)); t.tap.close(); } }
}
function maybeDone(ctx) { if (!ctx.finished && ctx.bodyDone && ctx.pending === 0) finishCtx(ctx); }
function runBody(ctx, body) {   // the body of a react or supply block, sync or async; DoneCtl ends it
    let r;
    try { r = withCtx(ctx, body); }
    catch (e) { if (e instanceof DoneCtl) { finishCtx(ctx); return; } if (isControl(e)) { failCtx(ctx, new RakuError('control exception escaped a react/supply block')); return; } failCtx(ctx, e); return; }
    const after = () => { ctx.bodyDone = true; maybeDone(ctx); };
    if (r && typeof r.then === 'function') r.then(after, e => { if (e instanceof DoneCtl) finishCtx(ctx); else failCtx(ctx, e); });
    else after();
}
function runProducer(sup, producer, t) {
    const ctx = { kind: 'supply', t, subs: [], pending: 0, bodyDone: false, finished: false };
    t.cleanup = () => { ctx.finished = true; for (const s of ctx.subs.slice()) s.close(); };
    runBody(ctx, producer);
}
function react(body) {
    return new Promise((resolve, reject) => {
        const ctx = { kind: 'react', subs: [], pending: 0, bodyDone: false, finished: false, resolve, reject };
        runBody(ctx, body);
    });
}
function supplyBlock(body) { return new RSupply(body); }
function currentCtx() { return ctxStack.length ? ctxStack[ctxStack.length - 1] : null; }
function emitVal(v) { const ctx = currentCtx(); if (!ctx) throw new RakuError('emit without a supply block'); if (ctx.kind === 'supply' && !ctx.t.closed) safeEmit(ctx.t, v); return v; }
function done() { const ctx = currentCtx(); if (!ctx) throw new RakuError('done without a react or supply block'); throw new DoneCtl(); }

// whenever SRC { … }: subscribe inside the current context; `done` in the body ends the
// context, `last` closes this subscription; LAST/QUIT phasers are the tap's done/quit
function whenever(src, fn, phasers) {
    const ctx = currentCtx();
    if (!ctx) throw new RakuError('whenever outside the lexical scope of a react/supply block');
    const sup = toSupply(src);
    ctx.pending++;
    let tap = null, left = false;
    const leave = () => { if (left) return; left = true; ctx.pending--; if (tap) { const i = ctx.subs.indexOf(tap); if (i >= 0) ctx.subs.splice(i, 1); } maybeDone(ctx); };
    const run = (cb, v) => {
        ctxStack.push(ctx);
        try { const r = cb(v); if (r && typeof r.then === 'function') r.catch(e => { if (e instanceof DoneCtl) finishCtx(ctx); else if (e instanceof LastCtl) { if (tap) tap.close(); leave(); } else if (!(e instanceof NextCtl)) failCtx(ctx, e); }); }
        catch (e) { if (e instanceof DoneCtl) { finishCtx(ctx); return; } if (e instanceof LastCtl) { if (tap) tap.close(); leave(); return; } if (e instanceof NextCtl) return; if (isControl(e)) throw e; failCtx(ctx, e); }
        finally { ctxStack.pop(); }
    };
    tap = sup.tap(
        v => { if (!ctx.finished) run(fn, v); },
        () => { if (phasers && phasers.last && !ctx.finished) run(phasers.last, Nil); leave(); },
        e => { if (phasers && phasers.quit) { run(phasers.quit, e); leave(); } else { failCtx(ctx, e); } });
    if (!left) ctx.subs.push(tap);
    return tap;
}
function toSupply(src) {
    if (src instanceof RSupply) return src;
    if (src instanceof RSupplier) return src.supply;
    if (src instanceof RChannel) return src.Supply();
    if (src instanceof RPromise) return new RSupply(null, (t) => { src.p.then(v => { if (!t.closed) { safeEmit(t, v); if (t.done) t.done(); t.tap.close(); } }, e => { if (!t.closed) { if (t.quit) t.quit(e); t.tap.close(); } }); });
    if (src instanceof RJsObj && src.v && typeof src.v.then === 'function') return toSupply(new RPromise()) && (() => { const p = new RPromise(); src.v.then(v => p.keep(fromJs(v)), e => p.break_(e)); return toSupply(p); })();
    if (src instanceof RList || src instanceof RSeq || src instanceof RRange || src instanceof RSlip) return supplyFromList(arr(src));
    throw new RakuError(`Cannot whenever a ${typeName(src)}`);
}
function supplyFromList(items) { return new RSupply(null, (t) => { for (const v of items) { if (t.closed) return; safeEmit(t, v); } if (!t.closed) { if (t.done) t.done(); t.tap.close(); } }); }
function supplyInterval(secs, delay) {
    const ms = Math.max(1, toFloat(secs) * 1000), first = delay === undefined ? 0 : toFloat(delay) * 1000;
    return new RSupply(null, (t) => {
        let n = 0, iv = null;
        const tick = () => { if (t.closed) return; safeEmit(t, n++); };
        activeTimers++;
        const to = setTimeout(() => { tick(); iv = setInterval(tick, ms); }, first);
        t.cleanup = () => { clearTimeout(to); if (iv) clearInterval(iv); activeTimers--; };
    });
}
// a derived supply: tap the source, transform, forward; closing the tap closes the source's
function derived(src, onEmit, onDone) {
    return new RSupply(null, (t) => {
        const inner = src.tap(
            v => onEmit(v, t),
            () => { if (t.closed) return; if (onDone) onDone(t); if (!t.closed) { if (t.done) t.done(); t.tap.close(); } },
            e => { if (t.closed) return; if (t.quit) t.quit(e); t.tap.close(); });
        t.cleanup = () => inner.close();
    });
}
const finishTap = (t) => { if (!t.closed) { if (t.done) t.done(); t.tap.close(); } };
function supplyHead(src, n) { const k = n === undefined ? 1 : Number(toInt(n)); return new RSupply(null, (t) => { let c = 0; if (k <= 0) { finishTap(t); return; } const inner = src.tap(v => { if (t.closed) return; c++; safeEmit(t, v); if (c >= k) { finishTap(t); } }, () => finishTap(t), e => { if (!t.closed) { if (t.quit) t.quit(e); t.tap.close(); } }); t.cleanup = () => inner.close(); }); }
function supplyLines(src, chomp) {
    return new RSupply(null, (t) => {
        let buf = '';
        const inner = src.tap(v => { buf += str(v); for (;;) { const i = buf.indexOf('\n'); if (i < 0) break; const line = buf.slice(0, i + 1); buf = buf.slice(i + 1); if (t.closed) return; safeEmit(t, chomp ? line.slice(0, -1) : line); } },
            () => { if (t.closed) return; if (buf !== '') { safeEmit(t, buf); buf = ''; } finishTap(t); },
            e => { if (!t.closed) { if (t.quit) t.quit(e); t.tap.close(); } });
        t.cleanup = () => inner.close();
    });
}
function supplyWords(src) {
    return new RSupply(null, (t) => {
        let buf = '';
        const inner = src.tap(v => { buf += str(v); const parts = buf.split(/\s+/); buf = parts.pop(); for (const w of parts) { if (w !== '' && !t.closed) safeEmit(t, w); } },
            () => { if (t.closed) return; if (buf.trim() !== '') safeEmit(t, buf.trim()); finishTap(t); },
            e => { if (!t.closed) { if (t.quit) t.quit(e); t.tap.close(); } });
        t.cleanup = () => inner.close();
    });
}
// collect a supply: synchronously for an on-demand one, as a Promise kept on done for a live one
function supplyList(s) {
    const out = []; let finished = false, err = null;
    const tap = s.tap(v => out.push(v), () => { finished = true; }, e => { err = e; finished = true; });
    if (finished) { if (err) throw err; return mkList(out); }
    const p = new RPromise();
    tap.closeFn2 = null;
    s.tap(v => { }, () => { }, () => { });   // (no-op: the first tap already collects)
    const check = () => { if (finished) { if (err) p.break_(err); else p.keep(mkList(out)); return; } setTimeout(check, 5); };
    check();
    return p;
}
function supplyPromise(s) { const p = new RPromise(); let last = Nil; s.tap(v => { last = v; }, () => { if (p.status === Planned) p.keep(last); }, e => { if (p.status === Planned) p.break_(e); }); return p; }
function supplyChannel(s) { const ch = new RChannel(); s.tap(v => ch.send(v), () => ch.close(), e => ch.fail(e)); return ch; }

// ---- Channel ----------------------------------------------------------------
class RChannel {
    constructor() { this.q = []; this.waiters = []; this.closedFlag = false; this.err = null; this.closedP = new RPromise(); this.supplies = []; }
    send(v) {
        if (this.closedFlag) throw new RakuError('Cannot send a message on a closed channel', 'X::Channel::SendOnClosed');
        if (this.waiters.length) { this.waiters.shift().res(v); return true; }
        if (this.supplies.length) { for (const s of this.supplies) s.emit(v); return true; }
        this.q.push(v); return true;
    }
    receive() {   // a Promise: the next value, or the closed error; nobody left to send → Nil
        if (this.q.length) return promiseKept(this.q.shift());
        if (this.closedFlag) return promiseBroken(this.err || new RakuError('Cannot receive a message on a closed channel', 'X::Channel::ReceiveOnClosed'));
        const p = new RPromise();
        const w = { res: v => p.keep(v), rej: e => p.break_(e) };
        this.waiters.push(w);
        // is anyone still going to send? Decided a macrotask later, once the microtasks of a
        // start block that just finished have run; while timers or start blocks are live we wait
        const poll = () => { if (p.status !== Planned) return; if (!pendingWork()) { const i = this.waiters.indexOf(w); if (i >= 0) this.waiters.splice(i, 1); p.keep(Nil); return; } setTimeout(poll, 10); };
        setTimeout(poll, 0);
        return p;
    }
    poll() { return this.q.length ? this.q.shift() : Nil; }
    close() { if (this.closedFlag) return true; this.closedFlag = true; for (const w of this.waiters.splice(0)) w.rej(new RakuError('Cannot receive a message on a closed channel', 'X::Channel::ReceiveOnClosed')); for (const s of this.supplies) s.done(); this.closedP.keep(true); return true; }
    fail(e) { this.err = e instanceof RakuError ? e : exc(e); this.close(); }
    list() {   // everything until close: synchronous once closed, else a Promise kept at close (or when nobody can send)
        if (this.closedFlag) return mkList(this.q.splice(0));
        const p = new RPromise(); const out = this.q.splice(0);
        const s = this.Supply(); s.tap(v => out.push(v), () => { if (p.status === Planned) p.keep(mkList(out)); }, e => { if (p.status === Planned) p.break_(e); });
        const poll = () => { if (p.status !== Planned) return; if (!pendingWork()) { p.keep(mkList(out)); return; } setTimeout(poll, 10); };
        setTimeout(poll, 0);
        return p;
    }
    Supply() { const s = new RSupply(); this.supplies.push(s); const q = this.q.splice(0); if (q.length) s.replay = q; if (this.closedFlag) s.doneFlag = true; return s; }
}

// ---- sleep: the event loop runs while we wait ----------------------------------
function sleepP(secs) { const ms = secs === undefined ? 1e9 : Math.max(0, toFloat(secs) * 1000); activeTimers++; return new Promise(res => setTimeout(() => { activeTimers--; res(true); }, ms)); }
const startCounted = (fn) => { activeStarts++; const p = start(fn); p.p.then(() => { activeStarts--; }, () => { activeStarts--; }); return p; };

// ---- methods ----------------------------------------------------------------
M(SupplyT, {
    tap: (s, ...a) => { const [pos, named] = splitArgs(a); const dn = named.get('done'), qt = named.get('quit'); return s.tap(pos[0] || (() => { }), dn ? () => dn() : null, qt ? (e) => qt(e) : null); },
    act: (s, f) => s.tap(f), emit: (s, v) => { s.emit(v); return v; }, done: (s) => { s.done(); return true; }, quit: (s, e) => { s.quit(e instanceof RakuError ? e : new RakuError(str(e))); return true; },
    map: (s, f) => derived(s, (v, t) => safeEmit(t, f(v))), grep: (s, f) => derived(s, (v, t) => { if (truthy(matcherOf(f)(v))) safeEmit(t, v); }),
    'do': (s, f) => derived(s, (v, t) => { f(v); safeEmit(t, v); }), head: (s, n) => supplyHead(s, n), first: (s) => supplyHead(s, 1),
    skip: (s, n) => { let k = n === undefined ? 1 : Number(toInt(n)); return derived(s, (v, t) => { if (k > 0) k--; else safeEmit(t, v); }); },
    unique: (s) => { const seen = new Set(); return derived(s, (v, t) => { const k = whichKey(v); if (!seen.has(k)) { seen.add(k); safeEmit(t, v); } }); },
    squish: (s) => { let last, has = false; return derived(s, (v, t) => { if (!has || !eqv(v, last)) safeEmit(t, v); last = v; has = true; }); },
    lines: (s, ...a) => supplyLines(s, !(nm(a).has('chomp') && !truthy(nm(a).get('chomp')))), words: (s) => supplyWords(s),
    batch: (s, ...a) => { const n = Number(toInt(nm(a).get('elems') ?? posArgs(a)[0] ?? 1)); let buf = []; return derived(s, (v, t) => { buf.push(v); if (buf.length >= n) { safeEmit(t, mkArray(buf)); buf = []; } }, (t) => { if (buf.length) safeEmit(t, mkArray(buf)); buf = []; }); },
    'on-close': (s, f) => derived(s, (v, t) => safeEmit(t, v), (t) => f()),
    list: (s) => supplyList(s), List: (s) => supplyList(s), Promise: (s) => supplyPromise(s), wait: (s) => supplyPromise(s), Channel: (s) => supplyChannel(s), Supply: (s) => s,
    gist: (s) => 'Supply.new', Str: (s) => 'Supply', raku: (s) => 'Supply.new', WHAT: (s) => SupplyT, defined: (s) => true, Bool: (s) => true, live: (s) => !s.producer && !s.derive,
});
M(SupplierT, { emit: (s, v) => { s.supply.emit(v); return v; }, done: (s) => { s.supply.done(); return true; }, quit: (s, e) => { s.supply.quit(e instanceof RakuError ? e : new RakuError(str(e))); return true; }, Supply: (s) => s.supply, gist: (s) => 'Supplier.new', Str: (s) => 'Supplier', raku: (s) => 'Supplier.new', WHAT: (s) => s.supply.preserving ? SupplierPreservingT : SupplierT, defined: (s) => true });
M(ChannelT, {
    send: (s, v) => s.send(v), receive: (s) => s.receive(), poll: (s) => s.poll(), close: (s) => s.close(), closed: (s) => s.closedP, fail: (s, e) => { s.fail(e); return true; },
    list: (s) => s.list(), Supply: (s) => s.Supply(), gist: (s) => 'Channel.new', Str: (s) => 'Channel', raku: (s) => 'Channel.new', WHAT: (s) => ChannelT, defined: (s) => true, Bool: (s) => true,
});
M(TapT, { close: (s) => s.close(), WHAT: (s) => TapT, gist: (s) => 'Tap.new', defined: (s) => true });
M(VowT, { keep: (s, v) => s.p.keep(v === undefined ? true : v), 'break': (s, e) => s.p.break_(e === undefined ? new RakuError('Died') : e), WHAT: (s) => VowT });
M(T.Promise, { vow: (s) => new RVow(s) });
M(T.Any, { emit: (s) => emitVal(s) });
Object.assign(TYPE_METHODS, {
    'from-list': (t, ...a) => t === SupplyT ? supplyFromList(spliceSlips(a.filter(x => !(x instanceof RNamed)))) : Nil,
    'interval': (t, secs, delay) => t === SupplyT ? supplyInterval(secs, delay) : Nil,
});
Object.assign(R, { RSupply, RSupplier, RChannel, RTap, DoneCtl, SupplyT, SupplierT, ChannelT, react, supplyBlock, whenever, emit: emitVal, done, sleepP, startCounted, toSupply, supplyFromList });
R.start = startCounted; TYPE_METHODS.start = (t, fn) => startCounted(fn);   // a start block counts as pending work while it runs
