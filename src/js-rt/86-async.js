// `start`, `await` and Promise (TRANSPILE-PLAN P4): concurrency, not
// parallelism. A Raku Promise is an RPromise over a JS Promise; `start` runs
// its block on the microtask queue; the emitter colours every routine that
// awaits as `async` and awaits its calls, so `await` here is JavaScript's.

const PromiseStatusT = enumType('PromiseStatus', [['Planned', 0], ['Kept', 1], ['Broken', 2]]);
const [Planned, Kept, Broken] = PromiseStatusT.enumValues;
class RPromise {
    constructor() {
        this.status = Planned; this.value = undefined; this.cause = undefined;
        this.p = new Promise((res, rej) => { this._res = res; this._rej = rej; });
        this.p.catch(() => { });   // a broken promise nobody awaits is not an unhandled rejection
    }
    keep(v) { if (this.status !== Planned) throw new RakuError('Promise is already ' + this.status.key.toLowerCase(), 'X::Promise::Vowed'); this.status = Kept; this.value = v; this._res(v); return this; }
    break_(e) { if (this.status !== Planned) throw new RakuError('Promise is already ' + this.status.key.toLowerCase(), 'X::Promise::Vowed'); this.status = Broken; this.cause = e instanceof RakuError ? e : exc(e); this._rej(this.cause); return this; }
    // .result: the value once kept; the cause thrown once broken; otherwise it would block
    result() {
        if (this.status === Kept) return this.value;
        if (this.status === Broken) throw this.cause;
        throw new RakuError('Cannot get the result of a Promise that is still planned without awaiting it (the JavaScript host cannot block)', 'X::Promise::Planned');
    }
}
function mkPromise() { return new RPromise(); }
function promiseKept(v) { const p = new RPromise(); p.keep(v); return p; }
function promiseBroken(e) { const p = new RPromise(); p.break_(e === undefined ? new RakuError('Died') : (e instanceof RakuError ? e : new RakuError(str(e)))); return p; }
// start { … }: the block runs once the current synchronous code yields
function start(fn) {
    const p = new RPromise();
    Promise.resolve().then(() => fn()).then(v => p.keep(v), e => { if (isControl(e)) e = new RakuError('control exception escaped a start block'); p.break_(e); });
    return p;
}
// await X: a Promise's value (or its cause, thrown); a list of them → their values; a JS thenable → its value
async function awaitP(x) {
    if (x instanceof RPromise) return await x.p;
    if (x instanceof RJsObj && x.v && typeof x.v.then === 'function') return fromJs(await x.v);
    if (x && typeof x.then === 'function') return fromJs(await x);
    if (x instanceof RList || x instanceof RSeq) return mkList(await Promise.all(arr(x).map(awaitP)));
    return x;
}
function promiseIn(secs) { const p = new RPromise(); setTimeout(() => p.keep(true), Math.max(0, toFloat(secs) * 1000)); return p; }
function promiseAllof(ps) { const p = new RPromise(); const list = arr(ps).map(x => x instanceof RPromise ? x.p : Promise.resolve(toJs(x))); Promise.allSettled(list).then(() => p.keep(true)); return p; }
function promiseAnyof(ps) { const p = new RPromise(); const list = arr(ps).map(x => x instanceof RPromise ? x.p : Promise.resolve(toJs(x))); let done = false; for (const q of list) q.then(() => { if (!done) { done = true; p.keep(true); } }, () => { if (!done) { done = true; p.keep(true); } }); return p; }
// .then(&cb): a new Promise kept with cb's value, cb receiving the settled original
function promiseThen(p, cb) {
    const out = new RPromise();
    p.p.then(() => cb(p), () => cb(p)).then(v => out.keep(v), e => out.break_(e));
    return out;
}
M(T.Promise, {
    keep: (s, v) => s.keep(v === undefined ? true : v), 'break': (s, e) => s.break_(e), result: (s) => s.result(), status: (s) => s.status, cause: (s) => s.status === Broken ? s.cause : Nil,
    then: (s, cb) => promiseThen(s, cb), Bool: (s) => s.status !== Planned, so: (s) => s.status !== Planned, gist: (s) => 'Promise.new', Str: (s) => 'Promise', raku: (s) => 'Promise.new',
    'is-kept': (s) => s.status === Kept, 'is-broken': (s) => s.status === Broken, 'is-planned': (s) => s.status === Planned,
    'await': (s) => s.result(), 'sink': (s) => Nil, 'WHAT': (s) => T.Promise, defined: (s) => true,
});
Object.assign(TYPE_METHODS, {
    'in': (t, secs) => t === T.Promise ? promiseIn(secs) : Nil,
    'kept': (t, v) => promiseKept(v === undefined ? true : v), 'broken': (t, e) => promiseBroken(e),
    'allof': (t, ...ps) => promiseAllof(ps.length === 1 ? ps[0] : mkList(ps)), 'anyof': (t, ...ps) => promiseAnyof(ps.length === 1 ? ps[0] : mkList(ps)),
    'start': (t, fn) => start(fn),
});
Object.assign(R, { RPromise, mkPromise, promiseKept, promiseBroken, start, awaitP, promiseIn, PromiseStatusT, Planned, Kept, Broken });
