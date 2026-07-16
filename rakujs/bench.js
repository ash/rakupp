// bench.js — measure Raku.js against the same kernels as ../tools/run-bench.raku.
//
// Serve the REPO ROOT (so /tools/bench/*.raku is fetchable) and open the
// playground from it:
//
//     cd .. && python3 -m http.server 8000
//     open http://localhost:8000/rakujs/playground/index.html
//
// then paste this whole file into the browser console. Results land in
// window.__bench (poll it; status flips to 'done'). Timing happens inside the
// worker around the synchronous rakupp_run() ccall — same policy as the native
// harness: 7 runs, first discarded as warm-up, minimum of the remaining 6.
window.__bench = { status: 'running', results: {}, progress: '' };
(async () => {
  const B = window.__bench;
  const kernels = ['loopsum','fib','strcat','arrayops','sortnums','regex','hash','bigint'];
  const RUNS = 7;

  // Fresh worker; time module init (the wasm-world analog of cold start).
  const t0 = performance.now();
  const w = new Worker('worker.js');
  const initMs = await new Promise((res, rej) => {
    w.onmessage = e => { if (e.data.type === 'ready') res(performance.now() - t0);
                         if (e.data.type === 'loaderror') rej(e.data.message); };
  });
  B.results.__module_init_ms = Math.round(initMs);

  const runOnce = src => new Promise((res, rej) => {
    w.onmessage = e => {
      if (e.data.type === 'done') res(e.data.ms);
      if (e.data.type === 'runerror') rej(e.data.message);
    };
    w.postMessage({ type: 'run', src });
  });

  for (const k of kernels) {
    const src = await (await fetch('/tools/bench/' + k + '.raku')).text();
    const times = [];
    try {
      for (let i = 0; i < RUNS; i++) {
        const ms = await runOnce(src);
        if (i > 0) times.push(ms);
        B.progress = k + ' run ' + (i + 1) + '/' + RUNS;
      }
      B.results[k] = Math.min(...times);
    } catch (err) {
      B.results[k] = 'ERROR: ' + err;
    }
  }
  w.terminate();
  B.status = 'done';
  console.table(B.results);
})().catch(e => { window.__bench.status = 'failed: ' + e; });
