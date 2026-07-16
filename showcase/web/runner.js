// runner.js — a tiny main-thread client shared by all three apps.
//
// It runs the Raku.js interpreter directly on the page — no Web Worker and no
// network. The WebAssembly binary is embedded as base64 (lib/rakujs-wasm.js) and
// handed to Emscripten as a data: URL via locateFile, so the "fetch" of the wasm
// resolves against that inline data (data: URLs are fetchable even from file://)
// and nothing touches the network or filesystem: the apps work opened straight
// from disk. rakupp_run() is synchronous; run() returns a Promise for a uniform
// API and to serialise calls.
//
// (Why locateFile and not Module.wasmBinary: the -Oz build tree-shakes the
// wasmBinary incoming-API path out of the glue, but keeps locateFile.)
//
// Globals expected before this file (plain <script src>, which works over
// file://):
//   RakuJS           – the Emscripten MODULARIZE factory   (lib/rakujs.js)
//   RAKUJS_WASM_B64  – the interpreter wasm, base64         (lib/rakujs-wasm.js)

class Raku {
  constructor() {
    this.version = null;
    this._out = '';
    this._err = '';
    this._queue = Promise.resolve();            // serialises run() calls
    this.ready = this._make().then(() => this.version);
  }

  _make() {
    return RakuJS({
      locateFile: () => 'data:application/wasm;base64,' + RAKUJS_WASM_B64,
      print:    t => { this._out += t + '\n'; },
      printErr: t => { this._err += t + '\n'; },
    }).then(m => {
      this._mod = m;
      this.version = m.ccall('rakupp_version', 'string', [], []);
      return m;
    });
  }

  // Run `src` to completion; resolves { out, err, rc, ms, deep }.
  run(src) {
    const result = this._queue.then(() => this._runNow(src));
    this._queue = result.then(() => {}, () => {});   // keep the chain alive on error
    return result;
  }

  async _runNow(src) {
    await this.ready;
    this._out = '';
    this._err = '';
    const t0 = performance.now();
    let rc, deep = false;
    try {
      rc = this._mod.ccall('rakupp_run', 'number', ['string'], [src]);
    } catch (e) {
      // A deep-recursion overflow (RangeError) or abort leaves the instance in an
      // unknown state — rebuild a clean one for the next run.
      deep = e instanceof RangeError || /call stack|unreachable|abort/i.test(String(e));
      this._err += String((e && e.message) || e);
      rc = 1;
      this.ready = this._make();
      await this.ready.catch(() => {});
    }
    return { out: this._out, err: this._err, rc, ms: Math.round(performance.now() - t0), deep };
  }
}

function debounced(fn, ms = 250) {
  let t = null;
  return (...args) => { clearTimeout(t); t = setTimeout(() => fn(...args), ms); };
}

// Emit a Raku statement that binds `varName` to arbitrary user text, using a
// non-interpolating heredoc (so nothing in the text is treated as Raku). The `;`
// goes on the opening line, as heredoc syntax requires; the terminator sits alone
// on its own line. If the text somehow contains the terminator we nudge it.
function rakuHeredoc(varName, text, term = '__RAKU_WEB_INPUT__') {
  let body = String(text);
  if (body.split('\n').some(l => l.trim() === term)) body = body.replaceAll(term, term + '_');
  return `my ${varName} = q:to/${term}/;\n${body}\n${term}\n`;
}
