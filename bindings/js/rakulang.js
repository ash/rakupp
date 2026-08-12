// rakulang — Raku grammars for JavaScript/TypeScript, on bun:ffi.
//
// The JS face of the grammar service (docs/dev/plans/GRAMMAR-PLAN.md): the
// grammar stays a .raku file, parsing happens in an embedded Raku++
// interpreter reached through librakupp's C ABI, and the Raku shim it drives
// ships INSIDE the library (rk_grammar_shim) — this package is invocation,
// results and lifetime, nothing else.
//
//   import { Grammar } from "rakulang";
//   const log = Grammar.fromFile("log.raku", { name: "Log", actions: "LogActions" });
//   const m = log.parse(text);                    // a handle, or null
//   for (const line of m.get("line"))             // lazy: one engine call per leaf
//     console.log(line.get("ip").str(), line.get("status").int());
//   const all = m.tree();                          // eager, opt-in (~1.4x the parse)
//   m.close();                                     // a rooted native value — explicit free
//
// Bun only (bun:ffi is built in). One interpreter per process, created on
// first use; one JS thread talks to it (Raku code inside threads freely).
// Matches hold rooted native values: close() them — there is no reliable
// GC hook to lean on, so the Go rule applies here too.

import { dlopen, FFIType, ptr, toArrayBuffer } from "bun:ffi";

const { ptr: P, cstring, i32, i64, f64, u64 } = FFIType;

export class RakuError extends Error {}
export class ParseError extends RakuError {
  constructor(message, diag) {
    super(message);
    this.pos = diag?.pos ?? null;
    this.line = diag?.line ?? null;
    this.column = diag?.col ?? null;
    this.rule = diag?.rule ?? null;
  }
}

const RK_ANY = 0, RK_BOOL = 1, RK_INT = 2, RK_NUM = 3, RK_RAT = 4,
      RK_STR = 5, RK_ARRAY = 6, RK_HASH = 7;

function libraryName() {
  if (process.platform === "darwin") return "librakupp.dylib";
  if (process.platform === "win32") return "rakupp.dll";
  return "librakupp.so";
}

function* candidates(explicit) {
  if (explicit) yield explicit;
  if (process.env.RAKUPP_LIB) yield process.env.RAKUPP_LIB;
  if (process.env.RAKUPP_HOME) yield `${process.env.RAKUPP_HOME}/lib/${libraryName()}`;
  const exe = Bun.which("rakupp");
  if (exe) {
    const real = require("fs").realpathSync(exe);
    for (const d of new Set([exe, real].map(p => p.replace(/\/[^/]+$/, "")))) {
      yield `${d}/../lib/${libraryName()}`;
      yield `${d}/${libraryName()}`;
    }
  }
}

let session = null;

class Session {
  constructor(libPath) {
    let lib = null, tried = [];
    for (const cand of candidates(libPath)) {
      try { lib = dlopen(cand, SYMBOLS); break; }
      catch (e) { tried.push(`${cand}: ${e.message}`); }
    }
    if (!lib)
      throw new RakuError("librakupp not found. Set RAKUPP_LIB, or have rakupp on PATH.\nTried:\n  " + tried.join("\n  "));
    this.f = lib.symbols;
    this.rk = this.f.rk_new(null);
    if (!this.rk) throw new RakuError("rk_new refused: an interpreter is already live in this process");
    this.c = this.f.rk_ctx(this.rk);
    // the returned CString must go back as a Buffer — bun:ffi will not
    // re-marshal its own CString object as a cstring argument
    const shim = this.f.rk_grammar_shim().toString();
    if (this.f.rk_eval(this.rk, Buffer.from(shim + "\0"), null) !== 0)
      throw new RakuError("grammar shim failed to load");
  }

  str(s) {
    // a trailing NUL keeps the buffer non-empty (ptr() refuses empty ones);
    // the length argument excludes it
    const b = Buffer.from(s + "\0", "utf8");
    return this.f.rk_str(this.c, ptr(b), b.byteLength - 1);
  }

  call(name, args) {
    const argv = new BigUint64Array(args.length);
    for (let i = 0; i < args.length; i++) argv[i] = BigInt(args[i]);
    const r = this.f.rk_call(this.c, Buffer.from(name + "\0"), args.length ? ptr(argv) : null, args.length);
    if (!r) {
      const e = this.f.rk_error(this.c);
      const msg = e ? e.toString() : `${name} failed`;
      this.f.rk_clear_error(this.c);
      throw new RakuError(msg);
    }
    return r;
  }

  readStr(v) {
    const lenBuf = new BigUint64Array(1);
    const p = this.f.rk_str_get(this.c, v, ptr(lenBuf));
    const n = Number(lenBuf[0]);
    if (!p || n === 0) return "";
    return new TextDecoder().decode(toArrayBuffer(p, 0, n));
  }

  toJs(v) {
    switch (this.f.rk_type(this.c, v)) {
      case RK_ANY: return null;
      case RK_BOOL: return this.f.rk_truthy(this.c, v) !== 0;
      case RK_INT: return Number(this.f.rk_int_get(this.c, v));
      case RK_NUM: case RK_RAT: return this.f.rk_num_get(this.c, v);
      case RK_ARRAY: {
        const n = Number(this.f.rk_elems(this.c, v));
        const out = new Array(n);
        for (let i = 0; i < n; i++) out[i] = this.toJs(this.f.rk_at_pos(this.c, v, i));
        return out;
      }
      case RK_HASH: {
        const n = Number(this.f.rk_elems(this.c, v));
        const out = {};
        for (let i = 0; i < n; i++) {
          const lenBuf = new BigUint64Array(1);
          const kp = this.f.rk_key_at(this.c, v, i, ptr(lenBuf));
          const key = new TextDecoder().decode(toArrayBuffer(kp, 0, Number(lenBuf[0])));
          out[key] = this.toJs(this.f.rk_val_at(this.c, v, i));
        }
        return out;
      }
      default: return this.readStr(v); // RK_STR / RK_OTHER stringify
    }
  }
}

const SYMBOLS = {
  rk_new:      { args: [P], returns: P },
  rk_free:     { args: [P], returns: FFIType.void },
  rk_ctx:      { args: [P], returns: P },
  rk_eval:     { args: [P, cstring, P], returns: i32 },
  rk_last_error: { args: [P], returns: cstring },
  rk_grammar_shim: { args: [], returns: cstring },
  rk_call:     { args: [P, cstring, P, u64], returns: P },
  rk_error:    { args: [P], returns: cstring },
  rk_clear_error: { args: [P], returns: FFIType.void },
  rk_root:     { args: [P, P], returns: P },
  rk_unroot:   { args: [P, P], returns: FFIType.void },
  rk_type:     { args: [P, P], returns: i32 },
  rk_truthy:   { args: [P, P], returns: i32 },
  rk_int_get:  { args: [P, P], returns: i64 },
  rk_num_get:  { args: [P, P], returns: f64 },
  rk_str_get:  { args: [P, P, P], returns: P },
  rk_elems:    { args: [P, P], returns: u64 },
  rk_at_pos:   { args: [P, P, u64], returns: P },
  rk_key_at:   { args: [P, P, u64, P], returns: P },
  rk_val_at:   { args: [P, P, u64], returns: P },
  rk_int:      { args: [P, i64], returns: P },
  rk_str:      { args: [P, P, u64], returns: P },
  rk_array:    { args: [P], returns: P },
  rk_push:     { args: [P, P, P], returns: FFIType.void },
};

export function interpreter(libPath) {
  if (!session) session = new Session(libPath);
  return session;
}

class Node {
  #match; #steps;
  constructor(match, steps) { this.#match = match; this.#steps = steps; }

  get(key) { return new Node(this.#match, [...this.#steps, key]); }
  at(i)    { return new Node(this.#match, [...this.#steps, i]); }

  #walk(op) {
    const S = session;
    const path = S.f.rk_array(S.c);
    for (const st of this.#steps)
      S.f.rk_push(S.c, path, typeof st === "number" ? S.f.rk_int(S.c, BigInt(st)) : S.str(String(st)));
    return S.call("rk-match-walk", [this.#match.handle, path, S.str(op)]);
  }

  str()    { return session.readStr(this.#walk("str")); }
  int()    { return Number(session.f.rk_int_get(session.c, this.#walk("int"))); }
  num()    { return session.f.rk_num_get(session.c, this.#walk("num")); }
  truthy() { return session.f.rk_truthy(session.c, this.#walk("bool")) !== 0; }
  isList() { return session.f.rk_truthy(session.c, this.#walk("islist")) !== 0; }
  get length() { return Number(session.f.rk_int_get(session.c, this.#walk("elems"))); }
  tree()   { return session.toJs(this.#walk("tree")); }
  made()   { return session.toJs(this.#walk("made")); }

  *[Symbol.iterator]() {
    if (this.isList()) {
      const n = this.length;
      for (let i = 0; i < n; i++) yield this.at(i);
    }
    else if (this.truthy()) yield this;
  }
}

export class Grammar {
  #id; #label;

  static fromSource(source, { name = "", actions = "" } = {}) {
    const S = interpreter();
    const id = S.call("rk-grammar-compile", [S.str(source), S.str(name), S.str(actions)]);
    const g = new Grammar();
    g.#id = Number(S.f.rk_int_get(S.c, id));
    g.#label = name || "<anonymous>";
    return g;
  }

  static fromFile(path, opts = {}) {
    const src = require("fs").readFileSync(path, "utf8");
    const g = Grammar.fromSource(src, opts);
    g.#label = path;
    return g;
  }

  parse(text, { rule = "", strict = false } = {}) {
    const S = interpreter();
    const raw = S.call("rk-grammar-parse", [S.f.rk_int(S.c, BigInt(this.#id)), S.str(text), S.str(rule)]);
    if (S.f.rk_type(S.c, raw) === RK_ANY) {
      if (strict) {
        const d = S.toJs(S.call("rk-grammar-diagnosis", [S.str(text)]));
        if (d)
          throw new ParseError(
            `${this.#label}: no match — failed at line ${d.line} column ${d.col} while trying <${d.rule}>`, d);
        throw new ParseError(`${this.#label}: no match`);
      }
      return null;
    }
    // the root Match: a Node over a holder owning the rooted native value
    const holder = { handle: S.f.rk_root(S.c, raw) };
    const node = new Node(holder, []);
    node.close = () => {
      if (holder.handle) { S.f.rk_unroot(S.c, holder.handle); holder.handle = null; }
    };
    return node;
  }
}
