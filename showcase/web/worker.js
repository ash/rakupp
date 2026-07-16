// worker.js — runs the Raku.js WebAssembly interpreter off the main thread, for
// the browser showcase apps. It loads the same rakujs.js / rakujs.wasm the
// playground builds (rakujs/build.sh), so there is nothing extra to compile:
// point a static server at the repo root and open one of the *.html apps.
//
// Protocol (postMessage): the page sends { type:'run', src } and gets back
//   { type:'ready', version }        once the module has instantiated
//   { type:'out', text, cls }        each chunk the program prints (cls 'err' = stderr)
//   { type:'done', rc, ms }          when a run finishes
//   { type:'runerror', message, deep } on abort / deep recursion
//   { type:'loaderror', message }    if the wasm never loaded

/* global RakuJS */
const BASE = '../../rakujs/playground/';   // where build.sh puts rakujs.js / rakujs.wasm
importScripts(BASE + 'rakujs.js');         // defines the MODULARIZE factory RakuJS

const post = (type, extra = {}) => self.postMessage({ type, ...extra });
let Module = null;

function makeModule() {
  return RakuJS({
    locateFile: p => BASE + p,             // fetch rakujs.wasm next to rakujs.js
    print:    t => post('out', { text: t + '\n', cls: ''    }),
    printErr: t => post('out', { text: t + '\n', cls: 'err' }),
  }).then(m => { Module = m; return m; });
}

let ready = makeModule()
  .then(m => post('ready', { version: m.ccall('rakupp_version', 'string', [], []) }))
  .catch(err => post('loaderror', { message: String(err) }));

self.onmessage = async (e) => {
  if (e.data.type !== 'run') return;
  try { await ready; } catch (_) { /* loaderror already posted */ }
  if (!Module) { post('loaderror', { message: 'module not loaded' }); return; }

  post('start');
  const t0 = performance.now();
  let rc;
  try {
    rc = Module.ccall('rakupp_run', 'number', ['string'], [e.data.src]);
  } catch (err) {
    // A deep-recursion overflow (RangeError) or abort leaves the instance in an
    // unknown state — drop it and rebuild a clean one for the next run.
    Module = null;
    ready = makeModule();
    post('runerror', {
      message: String(err),
      deep: err instanceof RangeError || /call stack/i.test(String(err)),
    });
    return;
  }
  post('done', { rc, ms: Math.round(performance.now() - t0) });
};
