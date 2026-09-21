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
    handlePrint(h, s) { if (h.kind === 'out') { this.stdout(s); if (h.outBuffer === 0) this.flush(); return true; } if (h.kind === 'err') { this.stderr(s); return true; } h.out = (h.out || '') + s; if (h.out.length >= (h.outBuffer === undefined ? 65536 : h.outBuffer)) { this.appendFile(h.path, h.out); h.out = ''; } return true; },   // out-buffer: the size at which writes land; 0 is every write
    handleFlush(h) { if (h && h.kind !== 'out' && h.kind !== 'err' && h.out) { this.appendFile(h.path, h.out); h.out = ''; } if (h && h.kind === 'out') this.flush(); },
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
    host.exit = code => { host.flush(); process.exit(code); };   // the program is over: a live interval or a poll must not keep the process alive
    // errno text the way strerror spells it, so a JS-hosted program's message is
    // the interpreter's message. Anything unmapped keeps node's own wording.
    const errText = (e) => ({ ENOENT: 'No such file or directory', EACCES: 'Permission denied', EISDIR: 'Is a directory', ENOTDIR: 'Not a directory', EEXIST: 'File exists', EROFS: 'Read-only file system' })[e.code] || e.message;
    // Named per operation, as the interpreter names them — X::IO::Open and the
    // rest are engine names parented to X::AdHoc (see EX_PARENT), so `when
    // X::AdHoc` still fires while .^name says which call failed. EEXIST picks
    // its own name the way the interpreter's :createonly / :x arms do.
    const openFailed = (path, e, ty) => new RakuError(`Failed to open file ${path}: ${errText(e)}`,
        e && e.code === 'EEXIST' ? (ty === 'X::IO::Spurt' ? 'X::IO::Exists' : 'X::IO::Exclusive') : (ty || 'X::IO::Open'));
    host.slurp = (p, ...a) => { try { return fs.readFileSync(str(p), 'utf8'); } catch (e) { throw openFailed(str(p), e); } };
    // a write that cannot land answers a Failure, never a quiet true (issue #71)
    host.spurt = (p, content, ...a) => { const named = nm(a); const opts = truthy(named.get('append')) ? { flag: 'a' } : truthy(named.get('createonly')) ? { flag: 'wx' } : {}; try { fs.writeFileSync(str(p), str(content), opts); } catch (e) { return failure(openFailed(str(p), e, 'X::IO::Spurt')); } return true; };
    host.appendFile = (p, s) => { fs.appendFileSync(p, s); };
    host.exists = p => fs.existsSync(p);
    host.isFile = p => { try { return fs.statSync(p).isFile(); } catch (e) { return false; } };
    host.isDir = p => { try { return fs.statSync(p).isDirectory(); } catch (e) { return false; } };
    host.size = p => { try { return fs.statSync(p).size; } catch (e) { return 0; } };
    host.modified = p => { try { return numResult(fs.statSync(p).mtimeMs / 1000); } catch (e) { return 0; } };
    host.absolute = p => pathMod.resolve(p);
    // dir($path = '.', :test): `.` and `..` are candidates only when a test is given; every entry remembers the CWD of the call
    host.dir = (p, ...a) => {
        if (p instanceof RNamed) { a.unshift(p); p = undefined; }
        const d = p === undefined ? '.' : str(p); const named = nm(a); const test = named.get('test');
        let st; try { st = fs.statSync(d); } catch (e) { throw new RakuError(`Failed to get the directory contents of '${d}': ${e.code === 'ENOENT' ? 'no such file or directory' : e.message}`, 'X::IO::Dir'); }
        if (!st.isDirectory()) throw new RakuError(`Failed to get the directory contents of '${d}': not a directory`, 'X::IO::Dir');
        const ents = ['.', '..', ...fs.readdirSync(d).sort()];
        const out = [];
        for (const e of ents) { if (test !== undefined ? !truthy(smartmatch(e, test)) : (e === '.' || e === '..')) continue; out.push(new RIOPath(d === '.' ? e : d.replace(/\/+$/, '') + '/' + e, host.cwd)); }
        return mkSeq(out);
    };
    host.mkdir = p => { fs.mkdirSync(str(p), { recursive: true }); return new RIOPath(str(p)); };
    host.rmdir = p => { fs.rmdirSync(str(p)); return true; };
    host.unlink = p => { try { fs.unlinkSync(str(p)); return true; } catch (e) { return false; } };
    host.copy = (a, b) => { fs.copyFileSync(a, b); return true; };
    host.rename = (a, b) => { fs.renameSync(a, b); return true; };
    host.chdir = p => { process.chdir(str(p)); host.cwd = nodeRequire('path').resolve(host.cwd, str(p)); return new RIOPath(host.cwd); };   // the logical path, not the realpath
    host.open = (p, ...a) => { const named = nm(a); const path = str(p); const w = truthy(named.get('w')) || truthy(named.get('a')) || str(named.get('mode') || '') === 'wo'; const app = truthy(named.get('a')) || truthy(named.get('append')); const h = new RIOHandle(w ? 'file-w' : 'file-r', path); if (named.has('out-buffer')) { const ob = named.get('out-buffer'); h.outBuffer = ob === false ? 0 : ob === true ? 8192 : Number(toInt(ob)); } try { if (fs.statSync(path).isDirectory()) return failure(new RakuError(`'${path}' is a directory, cannot do '.open' on a directory`, 'X::IO::Directory')); } catch (e) { /* absent is not an answer yet: :w may still create it */ } if (w) { try { if (!app) fs.writeFileSync(path, ''); else fs.appendFileSync(path, ''); } catch (e) { return failure(openFailed(path, e)); } h.out = ''; } else { try { h.buf = fs.readFileSync(path, 'utf8'); } catch (e) { return failure(openFailed(path, e)); } } return h; };
    host.close = h => { if (h.kind === 'file-w' && h.out) { fs.appendFileSync(h.path, h.out); h.out = ''; } h.closed = true; return true; };
    host.isTTY = h => h.kind === 'in' ? !!process.stdin.isTTY : h.kind === 'out' ? !!process.stdout.isTTY : h.kind === 'err' ? !!process.stderr.isTTY : false;
    host.shell = (cmd, ...a) => { const cp = nodeRequire('child_process'); host.flush(); const r = cp.spawnSync('/bin/sh', ['-c', str(cmd)], { stdio: 'inherit' }); return mkProc(r.status, str(cmd)); };
    host.run = (...args) => { const cp = nodeRequire('child_process'); const [pos, named] = splitArgs(args); host.flush(); const r = cp.spawnSync(str(pos[0]), pos.slice(1).map(str), { stdio: [truthy(named.get('in')) ? 'pipe' : 'inherit', truthy(named.get('out')) ? 'pipe' : 'inherit', truthy(named.get('err')) ? 'pipe' : 'inherit'], encoding: 'utf8' }); const p = mkProc(r.status, str(pos[0])); p.a_out = new RIOHandle('str'); p.a_out.buf = r.stdout || ''; p.a_err = new RIOHandle('str'); p.a_err.buf = r.stderr || ''; return p; };
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
// $*VM / $*KERNEL / $*DISTRO. The fields live in a Hash, as they do in the
// interpreter; `sysKind` is what lets a METHOD call on one resolve (mc in
// 60-methods.js), so `$*VM.name` works and not only `$*VM<name>`.
function systemic(kind, pairs) { const h = hashFrom(pairs); h.sysKind = kind; return h; }
// The kernel under a JavaScript host is still the operating system's — `js` was
// this engine's own name leaking into a field that answers darwin/linux/win32.
// The VM here IS the JavaScript engine, so its version is the host's.
function hostVersion() {
    if (typeof process !== 'undefined' && process.versions) return process.versions.bun || process.versions.node || '0';
    if (typeof Deno !== 'undefined' && Deno.version) return Deno.version.deno || '0';
    return '0';
}
function hostKernel() {
    const p = typeof process !== 'undefined' && process.platform;
    if (!p) return 'unknown';
    return p === 'win32' ? 'mswin32' : p;   // node's darwin/linux/freebsd already match
}
// …and the DISTRO is the operating system's flavour, which is what Rakudo's
// `macos`/`debian`/`mswin32` name — not the kernel and not the JS host.
function hostDistro() {
    const k = hostKernel();
    return k === 'darwin' ? 'macos' : k;
}
function systemicMethod(h, name, args) {
    const self = k => h.m.get(k);
    const nm = str(self('name'));
    switch (name) {
        case 'name': case 'Str': return nm;
        case 'gist': { const v = self('version'); return h.sysKind === 'VM' ? nm + ' (' + str(v) + ')' : nm; }
        case 'version': return self('version') === undefined ? new RVersion('0') : self('version');
        case 'is-win': return nm === 'mswin32' || nm === 'mingw' || nm === 'msys' || nm === 'cygwin';
        case 'path-sep': return (nm === 'mswin32' || nm === 'mingw' || nm === 'msys' || nm === 'cygwin') ? ';' : ':';
        case 'cpu-cores': { try { return nodeRequire('os').cpus().length || 1; } catch (e) { return 1; } }
        case 'archname': case 'cpu-arch': {
            const a = typeof process !== 'undefined' ? process.arch : 'unknown';
            return name === 'archname' ? a + '-' + nm : a;
        }
    }
    if (h.sysKind !== 'VM') return undefined;
    switch (name) {
        // The VM here is the JavaScript host, which is what `js` names — the same
        // spelling Rakudo's own JS backend uses, and a member of $*RAKU.VMnames.
        case 'auth': return 'Andrew Shitov';
        case 'desc': return 'Raku++ transpiled to JavaScript: the program runs on the host engine (' +
                            host.name + ' ' + hostVersion() + ').';
        // There is no precompilation store on this backend: the program was
        // compiled ahead of time and carries no cache.
        case 'precomp-ext': case 'precomp-target': return '';
        case 'prefix': return '';
        case 'request-garbage-collection': return Nil;   // the host's collector is not ours to drive
        case 'config': return hashFrom([['osname', hostKernel()]]);
    }
    return undefined;
}
function concatBytes(chunks) { let n = 0; for (const c of chunks) n += c.length; const out = new Uint8Array(n); let o = 0; for (const c of chunks) { out.set(c, o); o += c.length; } return out; }
const ProcT = mkType('Proc', [T.Any], { isUser: true, attrs: [{ name: 'exitcode', sigil: '$', pub: true }, { name: 'out', sigil: '$', pub: true }, { name: 'err', sigil: '$', pub: true }] });
ProcT.methods.exitcode = s => s.a_exitcode; ProcT.methods.out = s => s.a_out; ProcT.methods.err = s => s.a_err; ProcT.methods.Bool = s => s.a_exitcode === 0; ProcT.methods.so = s => s.a_exitcode === 0; ProcT.methods.signal = s => 0; ProcT.methods.pid = s => 0;
// Proc.sink — a command that exited unsuccessfully throws when nobody keeps its
// Proc, which is what makes `run @cmd` as a program's last act exit non-zero
// (issue #73). A Proc the caller DOES keep is never sunk, so `my $p = run …;
// $p.exitcode` still reads the code back.
ProcT.methods.sink = s => { if (s.a_exitcode === 0) return Nil; throw new RakuError(`The spawned command '${s.procCmd}' exited unsuccessfully (exit code: ${s.a_exitcode}, signal: 0)`, 'X::Proc::Unsuccessful'); };
function mkProc(code, cmd) { const p = new RObj(ProcT); p.a_exitcode = code === null ? 1 : code; p.a_out = Nil; p.a_err = Nil; p.procCmd = cmd === undefined ? '' : cmd; return p; }
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
        case '$*VM': return systemic('VM', [['name', 'js'], ['version', new RVersion(hostVersion())]]);
        case '$*KERNEL': return systemic('Kernel', [['name', hostKernel()]]);
        case '$*DISTRO': return systemic('Distro', [['name', hostDistro()]]);
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

// Splitting @*ARGS into positionals and named args — the port of the
// interpreter's rtMainArgs, whose CHECK ORDER is observable:
//   1. a bare `--` is consumed and the whole rest is positional;
//   2. otherwise, once a positional has been taken, the current token AND the
//      whole rest are positional VERBATIM — `prog a --x=1` passes the literal
//      string "--x=1", which is Rakudo's rule and not a common one;
//   3. option spellings: `--foo`, and the single-dash/colon short forms (`-v`,
//      `-n=3`, `:n=3`); `--/k`, `-/k`, `:/k` negate. A lone `-` or `:` is
//      positional. `-5` is therefore the named `:5` — as in Rakudo.
// A repeated option collects EVERY value into one named arg, in order.
function mainArgs(argv) {
    const pos = [], named = new Map();
    const addNamed = (k, v) => { const cur = named.get(k); if (cur === undefined) named.set(k, [v]); else cur.push(v); };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--') { for (i++; i < argv.length; i++) pos.push(argValue(argv[i])); break; }
        if (pos.length) { for (; i < argv.length; i++) pos.push(argValue(argv[i])); break; }
        if (a.length > 1 && (a[0] === '-' || a[0] === ':')) {
            const rest = a[0] === ':' ? a.slice(1) : a[1] === '-' ? a.slice(2) : a.slice(1);
            if (rest.length) {
                if (rest[0] === '/') { addNamed(rest.slice(1), false); continue; }
                const eq = rest.indexOf('=');
                if (eq >= 0) { addNamed(rest.slice(0, eq), argValue(rest.slice(eq + 1))); continue; }
                addNamed(rest, true);
                continue;
            }
        }
        pos.push(argValue(a));
    }
    const one = new Map();
    for (const [k, vs] of named) one.set(k, vs.length === 1 ? vs[0] : mkArray(vs));
    return { pos, named: one };
}

// The MAIN protocol: pos/named from @*ARGS, then dispatch. `sig` describes
// the candidates: [{fn, params:[{name, named, slurpy, optional, hasDefault, type, isBool}]}]
function runMain(cands, argv, sinkResult) {
    const { pos, named } = mainArgs(argv);
    // `$*USAGE` is readable INSIDE MAIN, not just printed when nothing binds —
    // a program that wants to refuse its own argument list does `note $*USAGE`.
    usageText = usage(cands);
    for (const c of cands) {
        const r = bindMain(c, pos, named);
        if (!r) continue;
        const v = r.fn(...r.args);
        // The PROGRAM's MAIN has its value SUNK (Rakudo): a Failure detonates, a
        // Proc that exited unsuccessfully throws (issue #73), and an Int MAIN
        // happens to return is NOT the exit code — only the protocol's own 0/2
        // leaves here as a number. A coloured MAIN hands back a Promise, which
        // main() is already waiting on: sink what it settles to. Called as a
        // MODULE export (exportMain) there is a caller, so the value flows out.
        if (!sinkResult) return v;
        if (v && typeof v.then === 'function') return v.then(x => { sink(x); return null; });
        sink(v);
        return null;
    }
    if (named.has('help') && cands.length) { host.stdout(usage(cands) + '\n'); return 0; }
    host.stderr(usage(cands) + '\n');
    return 2;
}
// A string default is shown QUOTED, so `[default: '.']` cannot be read as
// punctuation of the sentence around it — as the interpreter renders it.
function defaultGist(thunk) { try { const d = thunk(); return typeof d === 'string' ? "'" + d + "'" : gist(d); } catch (e) { return ''; } }
// The IntStr-like allomorph Rakudo hands MAIN (`Int $n` accepts it, `say $n`
// prints the spelling), with the command line's own rule ahead of it: the words
// naming Bool's two values arrive as the Bool itself, which is what lets
// `--tls=True` bind the `Bool :$tls` a program wrote for `--tls`. The rule is
// about the spelling and not the parameter, so `--tls=1` and `--tls=yes` stay
// Str and do NOT bind that Bool. The interpreter's rtMainArgs is the twin of
// this, and says there why the wider enum rule Rakudo reaches these through is
// not implemented in either. See issue #95.
function argValue(s) {
    if (s === 'True'  || s === 'Bool::True')  return true;
    if (s === 'False' || s === 'Bool::False') return false;
    return val(s);
}
function bindMain(c, pos, named) {
    const args = [];
    let pi = 0;
    const usedNamed = new Set();
    const nmap = new Map();
    for (const p of c.params) {
        if (p.named) {
            // A `Bool` named takes a Bool and nothing else: `--tls`, `--/tls` and
            // the `--tls=True` / `--tls=False` spellings argValue already turned
            // into one. Anything else — `--tls=yes`, or the list a repeated
            // `--tls=…` collects — does not bind, and the usage message is printed.
            // The mirror of that is a `Str` named refusing the Bool those two
            // words became: `--a=True` does not bind a `Str :$a`.
            if (named.has(p.name)) { let v = named.get(p.name); if (p.isBool && typeof v !== 'boolean') return null; if (p.type === 'Str' && typeof v === 'boolean') return null; if (p.type === 'Int' && typeof v === 'string') { const n = strToNumeric(v); if (!isIntVal(n)) return null; v = n; } nmap.set(p.name, v); usedNamed.add(p.name); }
            // `*%opts` takes the leftover options ONE BY ONE: it is the slurpy
            // that collects them, so they arrive as the named arguments they
            // are, not as a single `:opts(%h)`.
            else if (p.slurpy) { for (const [k, v] of named) if (!usedNamed.has(k)) { nmap.set(k, v); usedNamed.add(k); } }
            else if (!p.optional && !p.hasDefault) return null;
            continue;
        }
        // An EMPTY positional slurpy contributes no argument: the emitted body
        // reads it as `_pos.slice(n)`, which is empty either way, and pushing
        // one would fill the slot of an optional positional that took no
        // argument — `prog` with no arguments handed `$file?` an empty list
        // (defined!) instead of leaving it Any. A non-empty slurpy can only
        // follow filled positionals, so no hole is ever needed.
        if (p.slurpy) { const rest = pos.slice(pi); pi = pos.length; if (rest.length) args.push(mkList(rest)); continue; }
        if (pi >= pos.length) { if (p.optional || p.hasDefault) continue; return null; }
        let v = pos[pi++];
        if (p.type && (p.type === 'Int' || p.type === 'Num' || p.type === 'Numeric' || p.type === 'Real' || p.type === 'Rat')) { let n; try { n = strToNumeric(str(v)); } catch (e) { return null; } if (p.type === 'Int' && !isIntVal(n)) return null; if (!(v instanceof RAllo)) v = n; }   // the allomorph stays: `Int $n` sees an IntStr
        // A positional `Bool $x` takes `True`/`False` and no other spelling, and
        // conversely those two words do not bind a `Str $p` — by the time binding
        // looks at them they are a Bool, not a string.
        else if (p.type === 'Bool' && typeof v !== 'boolean') return null;
        else if (p.type === 'Str' && typeof v === 'boolean') return null;
        if (p.lit !== undefined && str(v) !== p.lit) return null;
        args.push(v);
    }
    if (pi < pos.length) return null;
    for (const k of named.keys()) if (!usedNamed.has(k)) return null;
    return { fn: c.fn, args: nmap.size ? [...args, new RNamed(nmap)] : args };
}
function usage(cands) {
    const prog = host.program.split('/').pop() || 'prog';
    const lines = ['Usage:'];
    const opts = [];          // the `#=` option list, in declaration order
    for (const c of cands) {
        const named = [], pos = [];
        for (const p of c.params) {
            const doc = (label) => { if (p.pod) opts.push({ label, desc: p.pod, def: p.dflt ? defaultGist(p.dflt) : '' }); };
            if (p.lit !== undefined) { pos.push(p.lit); continue; }
            if (p.named && !p.slurpy) {
                // A one-character name is a SHORT option: `-x`, not `--x`. An
                // untyped one takes `[=Any]`, a typed one `=<Type>`, and a Bool
                // takes nothing at all because its presence is the value. A
                // required named (`:$x!`) prints without the outer brackets.
                let label = (p.name.length === 1 ? '-' : '--') + p.name;
                if (p.type === 'Bool') { }
                else if (!p.type) label += '[=Any]';
                else label += '=<' + p.type + '>';
                named.push(p.optional ? '[' + label + ']' : label);
                doc(label);
                continue;
            }
            if (p.slurpy) { const label = '[<' + p.name + '> ...]'; pos.push(label); doc(label); continue; }
            const n = '<' + p.name + '>';
            const label = p.optional || p.hasDefault ? '[' + n + ']' : n;
            pos.push(label); doc(label);
        }
        const parts = named.concat(pos);
        lines.push('  ' + prog + (parts.length ? ' ' + parts.join(' ') : '') + (c.pod ? ' -- ' + c.pod : ''));
    }
    if (opts.length) {
        // …then the documented parameters, aligned, as the interpreter lays them out
        lines.push('  ');
        const w = opts.reduce((m, o) => Math.max(m, o.label.length), 0);
        for (const o of opts) lines.push('    ' + o.label + ' '.repeat(w - o.label.length + 4) + o.desc + (o.def === '' ? '' : ' [default: ' + o.def + ']'));
    }
    return lines.join('\n');
}

// The program entry: install the host, run, catch what the interpreter's main
// catches, flush, and leave an exit code.
// --module: the mainline runs at import time; its exceptions reach the importer as
// JavaScript errors; the returned table is what the module exports
function moduleInit(body) {
    startTime = host.now();
    ARGS = mkArray(host.argv.slice());
    ENV = new RHash(new Map(host.env));
    let r;
    try { r = body(); } catch (e) { host.flush(); throw toJs(e); }
    if (r && typeof r.then === 'function') return r.then(v => { host.flush(); return v; }, e => { host.flush(); throw toJs(e); });
    host.flush();
    return Promise.resolve(r);
}
// what crosses OUT of an exported routine: values by copy, objects and matches
// as proxies whose properties call the Raku methods
const unwrapped = new WeakMap();
function exportVal(v) {
    if (v instanceof RObj || v instanceof RMatch) return wrapObj(v);
    if (v instanceof RList || v instanceof RSeq) return arr(v).map(exportVal);
    if (v instanceof RHash) { const o = {}; for (const [k, x] of v.m) o[k] = exportVal(x); return o; }
    if (v instanceof RPair) { const o = {}; o[str(v.k)] = exportVal(v.v); return o; }
    if (v instanceof RSlip) return v.a.map(exportVal);
    return toJs(v);
}
function wrapObj(o) {
    const p = new Proxy(o, {
        get(t, k) {
            if (typeof k !== 'string') return t[k];
            if (k === 'then') return undefined;
            if (k === 'raku') return t;
            if (k === 'toString') return () => str(t);
            if (k === 'toJSON') return () => exportVal(t instanceof RMatch ? t.Str() : t);
            if (t instanceof RMatch) {   // captures are properties: m.k, m[0], m.made
                if (/^\d+$/.test(k)) return exportVal(t.pos(Number(k)));
                if (t.named.has(k)) return exportVal(t.name(k));
                if (k === 'made' || k === 'ast') return exportVal(t.made === undefined ? Nil : t.made);   // a value, not a method
            }
            if (can(t, k)) return (...a) => outCall(() => mc(t, k, ...inArgs(a)));
            if (k in t) { const x = t[k]; return typeof x === 'function' ? x.bind(t) : exportVal(x); }
            return undefined;
        },
        has(t, k) { return (typeof k === 'string' && can(t, k)) || k in t; },
    });
    unwrapped.set(p, o);
    return p;
}
// arguments coming IN from JavaScript: a trailing plain object is the named arguments; a
// Raku exception on the way out is a JavaScript Error carrying it as `.raku`
const isPlainObj = (x) => x !== null && typeof x === 'object' && (Object.getPrototypeOf(x) === Object.prototype || Object.getPrototypeOf(x) === null);
function inArgs(a) {
    const out = a.map(fromJs);
    if (a.length && isPlainObj(a[a.length - 1])) out[out.length - 1] = named(Object.entries(a[a.length - 1]).map(([k, v]) => [k, fromJs(v)]));
    return out;
}
function outCall(f) { try { return exportVal(f()); } catch (e) { throw toJs(e); } finally { host.flush(); } }   // `say` output leaves with the call
function exportFn(fn, name) { const f = (...a) => outCall(() => fn(...inArgs(a))); Object.defineProperty(f, 'name', { value: name }); return f; }
function exportMain(cands) { return (...argv) => outCall(() => runMain(cands, argv.map(String))); }
function exportType(T) {
    const C = (...a) => outCall(() => construct(T, ...inArgs(a)));
    Object.defineProperty(C, 'name', { value: T.name });
    C.new = C; C.type = T;
    if (T.mro.some(t => t.rules)) {
        C.parse = (s, o) => outCall(() => { const a = inArgs(o ? [s, o] : [s]); return grammarParse(T, a[0], a, false); });
        C.subparse = (s, o) => outCall(() => { const a = inArgs(o ? [s, o] : [s]); return grammarParse(T, a[0], a, true); });
    }
    if (T.isEnum && T.enumValues) for (const e of T.enumValues) C[e.key] = e;
    return C;
}
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
    // A `return` whose routine is no longer on the stack — a block holding a
    // `return` that is stored and called after the routine that made it has
    // gone. The emitter tags the RetCtl with that routine's token and the
    // routine's own `try` catches it while it is live; reaching HERE means
    // nothing did. This used to answer 0, so the program stopped silently and
    // reported success — which is how six `return`s inside the JavaScript
    // showcase's Array methods went unnoticed. Both the interpreter and Rakudo
    // say this, and exit non-zero.
    if (e instanceof RetCtl) { host.stderr('Attempt to return outside of any Routine\n'); return 1; }
    if (e instanceof RakuError) { host.stderr(e.message + '\n'); return 1; }
    if (e instanceof RObj) { host.stderr(excMessage(e) + '\n'); return 1; }
    if (e instanceof RFailure) { host.stderr(e.err.message + '\n'); return 1; }
    if (e instanceof RangeError && /call stack/i.test(e.message)) { host.stderr('Maximum call stack size exceeded (a deeper recursion than this JavaScript host allows; try node --stack-size=65500)\n'); return 1; }
    host.stderr('Internal error: ' + (e && e.stack ? e.stack : String(e)) + '\n');
    return 3;
}
function setUsage(s) { usageText = s; }
function envGet(k) { const v = host.env.get(k); return v === undefined ? Any : v; }

Object.assign(R, { host, dynVar, atEnd, runMain, main, module: moduleInit, exportFn, exportMain, exportType, exportVal, wrapObj, reportUncaught, setUsage, envGet, STDIN, STDOUT, STDERR, mkProc, usage });
