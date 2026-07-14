// worker.js — runs the Raku.js WebAssembly interpreter off the main thread.
//
// Why a worker: rakupp_run() is a *synchronous* call that runs a whole program
// to completion. On the main thread that freezes the UI — no spinner animates and
// output only appears at the end. In a dedicated worker the main thread stays
// free, so the page can animate a spinner, stream output live (e.g. Game of Life
// frame-by-frame), and terminate a runaway program by killing the worker.

/* global RakuJS */
importScripts('rakujs.js'); // defines the MODULARIZE factory RakuJS on the worker global

const post = (type, extra = {}) => self.postMessage({ type, ...extra });

let Module = null;

// Build a fresh module instance. Its print/printErr stream straight back to the
// main thread as they fire during a run.
function makeModule() {
  return RakuJS({
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
