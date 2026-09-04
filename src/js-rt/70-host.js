// The host adapter — one interface, chosen at load: Node/Bun/Deno, or a
// browser (main thread or Web Worker). Everything that touches stdio, the
// filesystem, the clock, the environment or the process goes through `host`.

const host = {
    name: 'unknown', outBuf: '', argv: [], env: new Map(), cwd: '.', program: '',
    stdout(s) { this.outBuf += s; if (this.outBuf.length > 65536) this.flush(); },
    stderr(s) { this.flush(); this.writeErr(s); },
    flush() { if (this.outBuf) { this.writeOut(this.outBuf); this.outBuf = ''; } },
    writeOut(s) { console.log(s.replace(/\n$/, '')); },
    writeErr(s) { console.error(s.replace(/\n$/, '')); },
    exit(code) { },
    random() { return Math.random(); },
    srand(seed) { let x = seed >>> 0 || 1; this.random = () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; },
    now() { return Date.now() / 1000; },
    sleep(sec) { if (!Number.isFinite(sec)) sec = 1e9; const end = Date.now() + sec * 1000; if (typeof SharedArrayBuffer !== 'undefined' && typeof Atomics !== 'undefined') { try { Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, sec * 1000); return; } catch (e) { } } while (Date.now() < end) { } },
    // stdin: read whole, then hand out lines
    stdinText: null, stdinPos: 0,
    readStdin() { return ''; },
    stdinAll() { if (this.stdinText === null) { this.stdinText = this.readStdin(); this.stdinPos = 0; } return this.stdinText; },
    stdinGet() { const t = this.stdinAll(); if (this.stdinPos >= t.length) return Nil; let e = t.indexOf('\n', this.stdinPos); if (e < 0) e = t.length; const line = t.slice(this.stdinPos, e); this.stdinPos = e + 1; return line.endsWith('\r') ? line.slice(0, -1) : line; },
    stdinLines() { const t = this.stdinAll(); const rest = t.slice(this.stdinPos); this.stdinPos = t.length; return lines(rest); },
    stdinSlurp() { const t = this.stdinAll(); const rest = t.slice(this.stdinPos); this.stdinPos = t.length; return rest; },
    noFs(what) { throw new RakuError(`${what} needs a filesystem; this host (${this.name}) has none — run the program under node, bun or deno`); },
    slurp(p, ...a) { return this.noFs('slurp'); }, spurt() { return this.noFs('spurt'); }, exists() { return false; }, isFile() { return false; }, isDir() { return false; }, size() { return 0; },
    open() { return this.noFs('open'); }, close(h) { return true; }, dir() { return this.noFs('dir'); }, mkdir() { return this.noFs('mkdir'); }, rmdir() { return this.noFs('rmdir'); }, unlink() { return this.noFs('unlink'); }, copy() { return this.noFs('copy'); }, rename() { return this.noFs('rename'); }, modified() { return 0; }, absolute(p) { return p; }, chdir() { return this.noFs('chdir'); },
    shell() { return this.noFs('shell'); }, run() { return this.noFs('run'); },
    handleGet(h) { if (h.kind === 'in') return this.stdinGet(); if (h.pos >= h.buf.length) return Nil; let e = h.buf.indexOf('\n', h.pos); if (e < 0) e = h.buf.length; const line = h.buf.slice(h.pos, e); h.pos = e + 1; return line; },
    handleLines(h) { if (h.kind === 'in') return this.stdinLines(); const rest = h.buf.slice(h.pos); h.pos = h.buf.length; return lines(rest); },
    handleSlurp(h) { if (h.kind === 'in') return this.stdinSlurp(); const rest = h.buf.slice(h.pos); h.pos = h.buf.length; return rest; },
    handleGetc(h) { if (h.kind === 'in') { const t = this.stdinAll(); if (this.stdinPos >= t.length) return Nil; return t[this.stdinPos++]; } if (h.pos >= h.buf.length) return Nil; return h.buf[h.pos++]; },
    handleEof(h) { if (h.kind === 'in') return this.stdinPos >= this.stdinAll().length; return h.pos >= h.buf.length; },
    handlePrint(h, s) { if (h.kind === 'out') { this.stdout(s); return true; } if (h.kind === 'err') { this.stderr(s); return true; } h.out = (h.out || '') + s; if (h.out.length > 65536) { this.appendFile(h.path, h.out); h.out = ''; } return true; },
    isTTY(h) { return false; },
    appendFile() { return this.noFs('write'); },
};
const IS_NODE = typeof process !== 'undefined' && process.versions && (process.versions.node || process.versions.bun);
const IS_DENO = typeof Deno !== 'undefined';
// `require` is the CommonJS global (a plain script, Bun) or the createRequire the
// ES-module sidecar defines at its top; a browser has neither and needs neither.
const nodeRequire = typeof require === 'function' ? require : (typeof process !== 'undefined' && process.getBuiltinModule) ? (m => process.getBuiltinModule(m)) : null;
if (IS_NODE && nodeRequire) {
    host.name = process.versions.bun ? 'bun' : 'node';
    const fs = nodeRequire('fs');
    const pathMod = nodeRequire('path');
    host.argv = process.argv.slice(2);
    host.program = process.argv[1] || '';
    host.env = new Map(Object.entries(process.env));
    host.cwd = process.cwd();
    host.writeOut = s => { try { fs.writeSync(1, s); } catch (e) { if (e.code === 'EAGAIN') { host.writeOut(s); } else if (e.code !== 'EPIPE') throw e; } };
    host.writeErr = s => { try { fs.writeSync(2, s); } catch (e) { if (e.code !== 'EPIPE') throw e; } };
    host.readStdin = () => { try { return fs.readFileSync(0, 'utf8'); } catch (e) { return ''; } };
    host.exit = code => { host.flush(); process.exitCode = code; };
    host.slurp = (p, ...a) => { try { return fs.readFileSync(str(p), 'utf8'); } catch (e) { throw new RakuError(`Failed to open file ${str(p)}: ${e.code === 'ENOENT' ? 'No such file or directory' : e.message}`, 'X::IO::DoesNotExist'); } };
    host.spurt = (p, content, ...a) => { const named = nm(a); const opts = truthy(named.get('append')) ? { flag: 'a' } : truthy(named.get('createonly')) ? { flag: 'wx' } : {}; fs.writeFileSync(str(p), str(content), opts); return true; };
    host.appendFile = (p, s) => { fs.appendFileSync(p, s); };
    host.exists = p => fs.existsSync(p);
    host.isFile = p => { try { return fs.statSync(p).isFile(); } catch (e) { return false; } };
    host.isDir = p => { try { return fs.statSync(p).isDirectory(); } catch (e) { return false; } };
    host.size = p => { try { return fs.statSync(p).size; } catch (e) { return 0; } };
    host.modified = p => { try { return numResult(fs.statSync(p).mtimeMs / 1000); } catch (e) { return 0; } };
    host.absolute = p => pathMod.resolve(p);
    host.dir = (p, ...a) => { const d = p === undefined ? '.' : str(p); const named = nm(a); const test = named.get('test'); let ents; try { ents = fs.readdirSync(d); } catch (e) { throw new RakuError(`Failed to get the directory contents of '${d}': ${e.message}`); } ents.sort(); const out = []; for (const e of ents) { if (test !== undefined && !truthy(smartmatch(e, test))) continue; if (test === undefined && (e === '.' || e === '..')) continue; out.push(new RIOPath(d === '.' ? e : d.replace(/\/+$/, '') + '/' + e)); } return mkSeq(out); };
    host.mkdir = p => { fs.mkdirSync(str(p), { recursive: true }); return new RIOPath(str(p)); };
    host.rmdir = p => { fs.rmdirSync(str(p)); return true; };
    host.unlink = p => { try { fs.unlinkSync(str(p)); return true; } catch (e) { return false; } };
    host.copy = (a, b) => { fs.copyFileSync(a, b); return true; };
    host.rename = (a, b) => { fs.renameSync(a, b); return true; };
    host.chdir = p => { process.chdir(str(p)); host.cwd = process.cwd(); return new RIOPath(host.cwd); };
    host.open = (p, ...a) => { const named = nm(a); const path = str(p); const w = truthy(named.get('w')) || truthy(named.get('a')) || str(named.get('mode') || '') === 'wo'; const app = truthy(named.get('a')) || truthy(named.get('append')); const h = new RIOHandle(w ? 'file-w' : 'file-r', path); if (w) { if (!app) fs.writeFileSync(path, ''); h.out = ''; } else { try { h.buf = fs.readFileSync(path, 'utf8'); } catch (e) { throw new RakuError(`Failed to open file ${path}: ${e.code === 'ENOENT' ? 'No such file or directory' : e.message}`, 'X::IO::DoesNotExist'); } } return h; };
    host.close = h => { if (h.kind === 'file-w' && h.out) { fs.appendFileSync(h.path, h.out); h.out = ''; } h.closed = true; return true; };
    host.isTTY = h => h.kind === 'in' ? !!process.stdin.isTTY : h.kind === 'out' ? !!process.stdout.isTTY : h.kind === 'err' ? !!process.stderr.isTTY : false;
    host.shell = (cmd, ...a) => { const cp = nodeRequire('child_process'); host.flush(); const r = cp.spawnSync('/bin/sh', ['-c', str(cmd)], { stdio: 'inherit' }); return mkProc(r.status); };
    host.run = (...args) => { const cp = nodeRequire('child_process'); const [pos, named] = splitArgs(args); host.flush(); const r = cp.spawnSync(str(pos[0]), pos.slice(1).map(str), { stdio: [truthy(named.get('in')) ? 'pipe' : 'inherit', truthy(named.get('out')) ? 'pipe' : 'inherit', truthy(named.get('err')) ? 'pipe' : 'inherit'], encoding: 'utf8' }); const p = mkProc(r.status); p.a_out = new RIOHandle('str'); p.a_out.buf = r.stdout || ''; p.a_err = new RIOHandle('str'); p.a_err.buf = r.stderr || ''; return p; };
} else if (IS_DENO) {
    host.name = 'deno';
    host.argv = Deno.args.slice();
    host.env = new Map(Object.entries(Deno.env.toObject()));
    host.cwd = Deno.cwd();
    const enc = new TextEncoder();
    host.writeOut = s => Deno.stdout.writeSync(enc.encode(s));
    host.writeErr = s => Deno.stderr.writeSync(enc.encode(s));
    host.readStdin = () => { const chunks = []; const buf = new Uint8Array(65536); for (;;) { const n = Deno.stdin.readSync(buf); if (n === null) break; chunks.push(buf.slice(0, n)); } return new TextDecoder().decode(concatBytes(chunks)); };
    host.exit = code => { host.flush(); if (code) Deno.exit(code); };
    host.slurp = p => Deno.readTextFileSync(str(p));
    host.spurt = (p, c, ...a) => { const named = nm(a); Deno.writeTextFileSync(str(p), str(c), { append: truthy(named.get('append')) }); return true; };
    host.exists = p => { try { Deno.statSync(p); return true; } catch (e) { return false; } };
    host.isFile = p => { try { return Deno.statSync(p).isFile; } catch (e) { return false; } };
    host.isDir = p => { try { return Deno.statSync(p).isDirectory; } catch (e) { return false; } };
} else {
    host.name = typeof importScripts === 'function' ? 'worker' : typeof document !== 'undefined' ? 'browser' : 'unknown';
    if (typeof console !== 'undefined') { host.writeOut = s => console.log(s.replace(/\n$/, '')); host.writeErr = s => console.error(s.replace(/\n$/, '')); }
}
function concatBytes(chunks) { let n = 0; for (const c of chunks) n += c.length; const out = new Uint8Array(n); let o = 0; for (const c of chunks) { out.set(c, o); o += c.length; } return out; }
const ProcT = mkType('Proc', [T.Any], { isUser: true, attrs: [{ name: 'exitcode', sigil: '$', pub: true }, { name: 'out', sigil: '$', pub: true }, { name: 'err', sigil: '$', pub: true }] });
ProcT.methods.exitcode = s => s.a_exitcode; ProcT.methods.out = s => s.a_out; ProcT.methods.err = s => s.a_err; ProcT.methods.Bool = s => s.a_exitcode === 0; ProcT.methods.so = s => s.a_exitcode === 0; ProcT.methods.signal = s => 0; ProcT.methods.pid = s => 0;
function mkProc(code) { const p = new RObj(ProcT); p.a_exitcode = code === null ? 1 : code; p.a_out = Nil; p.a_err = Nil; return p; }
const STDIN = new RIOHandle('in', ''), STDOUT = new RIOHandle('out', ''), STDERR = new RIOHandle('err', '');

// Dynamic variables the core provides
function dynVar(name) {
    switch (name) {
        case '@*ARGS': return ARGS;
        case '%*ENV': return ENV;
        case '$*PROGRAM-NAME': return host.program;
        case '$*PROGRAM': return new RIOPath(host.program);
        case '$*CWD': return new RIOPath(host.cwd);
        case '$*IN': return STDIN;
        case '$*OUT': return STDOUT;
        case '$*ERR': return STDERR;
        case '$*EXECUTABLE': return new RIOPath(host.name);
        case '$*EXECUTABLE-NAME': return host.name;
        case '$*PID': return typeof process !== 'undefined' ? process.pid : 0;
        case '$*TMPDIR': return new RIOPath('/tmp');
        case '$*HOME': return new RIOPath(host.env.get('HOME') || '');
        case '$*USER': return host.env.get('USER') || '';
        case '$*RAKU': return hashFrom([['name', 'Raku'], ['version', new RVersion('6.d')]]);
        case '$*PERL': return hashFrom([['name', 'Raku']]);
        case '$*VM': return hashFrom([['name', 'js'], ['version', new RVersion('1')]]);
        case '$*KERNEL': return hashFrom([['name', 'js']]);
        case '$*DISTRO': return hashFrom([['name', host.name]]);
        case '$*COLLATION': return Nil;
        case '$*RAKUDO_MODULE_DEBUG': return false;
        case '$*USAGE': return usageText || '';
        case '$*SCHEDULER': return Nil;
        case '$*THREAD': return Nil;
        case '$*DEFAULT-READ-ELEMS': return 65536;
        case '$*INIT-INSTANT': return numResult(startTime);
        case '$*REPO': return Nil;
        case '$*DISTRIBUTION': return Nil;
    }
    throw new RakuError(`Dynamic variable ${name} not found`, 'X::Dynamic::NotFound');
}
let ARGS = null, ENV = null, usageText = '', startTime = 0;
let endBlocks = [];
function atEnd(f) { endBlocks.push(f); }

// The MAIN protocol: pos/named from @*ARGS, then dispatch. `sig` describes
// the candidates: [{fn, params:[{name, named, slurpy, optional, hasDefault, type, isBool}]}]
function runMain(cands, argv) {
    const pos = [], named = new Map();
    let onlyPos = false;
    for (const a of argv) {
        if (!onlyPos && a === '--') { onlyPos = true; continue; }
        if (!onlyPos && a.startsWith('--') && a.length > 2) {
            const eq = a.indexOf('=');
            if (eq > 0) named.set(a.slice(2, eq), argValue(a.slice(eq + 1)));
            else if (a.startsWith('--/')) named.set(a.slice(3), false);
            else if (a.startsWith('--no-')) named.set(a.slice(5), false);
            else named.set(a.slice(2), true);
            continue;
        }
        if (!onlyPos && a.startsWith('-') && a.length > 1 && !/^-\d/.test(a)) {
            const eq = a.indexOf('=');
            if (eq > 0) named.set(a.slice(1, eq), argValue(a.slice(eq + 1)));
            else if (a.startsWith('-/')) named.set(a.slice(2), false);
            else named.set(a.slice(1), true);
            continue;
        }
        pos.push(argValue(a));
    }
    for (const c of cands) {
        const r = bindMain(c, pos, named);
        if (r) return r.fn(...r.args);
    }
    if (named.has('help') && cands.length) { host.stdout(usage(cands) + '\n'); return 0; }
    host.stderr(usage(cands) + '\n');
    return 2;
}
function argValue(s) { return s; }   // an IntStr-like allomorph is what Rakudo hands MAIN; strings numify on use here
function bindMain(c, pos, named) {
    const args = [];
    let pi = 0;
    const usedNamed = new Set();
    for (const p of c.params) {
        if (p.named) {
            if (named.has(p.name)) { let v = named.get(p.name); if (p.isBool && typeof v === 'string') v = truthy(v); if (p.type === 'Int' && typeof v === 'string') { const n = strToNumeric(v); if (!isIntVal(n)) return null; v = n; } args.push(pair(p.name, v)); usedNamed.add(p.name); }
            else if (p.slurpy) { const h = new RHash(); for (const [k, v] of named) if (!usedNamed.has(k)) { h.m.set(k, v); usedNamed.add(k); } args.push(pair(p.name, h)); }
            else if (!p.optional && !p.hasDefault) return null;
            continue;
        }
        if (p.slurpy) { args.push(mkList(pos.slice(pi))); pi = pos.length; continue; }
        if (pi >= pos.length) { if (p.optional || p.hasDefault) continue; return null; }
        let v = pos[pi++];
        if (p.type && (p.type === 'Int' || p.type === 'Num' || p.type === 'Numeric' || p.type === 'Real' || p.type === 'Rat')) { let n; try { n = strToNumeric(v); } catch (e) { return null; } if (p.type === 'Int' && !isIntVal(n)) return null; v = n; }
        else if (p.type === 'Bool') v = truthy(v);
        if (p.lit !== undefined && str(v) !== p.lit) return null;
        args.push(v);
    }
    if (pi < pos.length) return null;
    for (const k of named.keys()) if (!usedNamed.has(k)) return null;
    // named args are passed as Pairs → convert to RNamed
    const posArgs2 = args.filter(a => !(a instanceof RPair && c.params.some(p => p.named && p.name === a.k)));
    const nmap = new Map(); for (const a of args) if (a instanceof RPair && c.params.some(p => p.named && p.name === a.k)) nmap.set(a.k, a.v);
    return { fn: c.fn, args: nmap.size ? [...posArgs2, new RNamed(nmap)] : posArgs2 };
}
function usage(cands) {
    const prog = host.program.split('/').pop() || 'prog';
    const lines = ['Usage:'];
    for (const c of cands) {
        const named = [], pos = [];
        for (const p of c.params) {
            if (p.named) { const t = p.isBool ? '' : '[=' + (p.type || 'Any') + ']'; named.push('[--' + p.name + t + ']'); continue; }
            if (p.slurpy) { pos.push('[<' + p.name + '> ...]'); continue; }
            if (p.lit !== undefined) { pos.push(p.lit); continue; }
            const n = '<' + p.name + '>';
            pos.push(p.optional || p.hasDefault ? '[' + n + ']' : n);
        }
        const parts = named.concat(pos);
        lines.push('  ' + prog + (parts.length ? ' ' + parts.join(' ') : ''));
    }
    return lines.join('\n');
}

// The program entry: install the host, run, catch what the interpreter's main
// catches, flush, and leave an exit code.
function main(body, opts) {
    startTime = host.now();
    ARGS = mkArray(host.argv.slice());
    ENV = new RHash(new Map(host.env));
    let code = 0;
    const finish = (r) => {
        if (typeof r === 'number' && Number.isInteger(r) && opts && opts.mainExit) code = r;
        try { for (let i = endBlocks.length - 1; i >= 0; i--) endBlocks[i](); } catch (e) { code = reportUncaught(e); }
        host.flush();
        host.exit(code);
        return code;
    };
    let r;
    try { r = body(); }
    catch (e) { code = reportUncaught(e); return finish(undefined); }
    // a coloured program (one that awaits) hands back a Promise: finish when it settles
    if (r && typeof r.then === 'function') return r.then(v => finish(v), e => { code = reportUncaught(e); return finish(undefined); });
    return finish(r);
}
function reportUncaught(e) {
    if (e instanceof ExitCtl) return e.code;
    if (e instanceof LastCtl) { host.stderr('last without loop construct\n'); return 1; }
    if (e instanceof NextCtl) { host.stderr('next without loop construct\n'); return 1; }
    if (e instanceof RedoCtl) { host.stderr('redo without loop construct\n'); return 1; }
    if (e instanceof RetCtl) { return 0; }
    if (e instanceof RakuError) { host.stderr(e.message + '\n'); return 1; }
    if (e instanceof RObj) { host.stderr(excMessage(e) + '\n'); return 1; }
    if (e instanceof RFailure) { host.stderr(e.err.message + '\n'); return 1; }
    if (e instanceof RangeError && /call stack/i.test(e.message)) { host.stderr('Maximum call stack size exceeded (a deeper recursion than this JavaScript host allows; try node --stack-size=65500)\n'); return 1; }
    host.stderr('Internal error: ' + (e && e.stack ? e.stack : String(e)) + '\n');
    return 3;
}
function setUsage(s) { usageText = s; }
function envGet(k) { const v = host.env.get(k); return v === undefined ? Any : v; }

Object.assign(R, { host, dynVar, atEnd, runMain, main, reportUncaught, setUsage, envGet, STDIN, STDOUT, STDERR, mkProc, usage });
