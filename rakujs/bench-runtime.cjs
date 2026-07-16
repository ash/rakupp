// bench-runtime.cjs — time Raku.js under Node or Bun on the same kernels as
// ../tools/run-bench.raku (7 runs, first discarded, min of the remaining 6).
//
// Needs a Node-target build (the shipped playground bundle is web,worker-only):
//
//     RAKUJS_ENV=node RAKUJS_OUT=/tmp/rakujs-node rakujs/build.sh
//     node --stack-size=6000 rakujs/bench-runtime.cjs /tmp/rakujs-node/rakujs.js
//     bun rakujs/bench-runtime.cjs /tmp/rakujs-node/rakujs.js
//
// Node's default JS stack overflows on fib(29) — under -fexceptions C++
// recursion consumes the host JS stack (see README) — hence --stack-size.
// Bun's JavaScriptCore default stack handles it as-is.
const path = require('path');
const fs = require('fs');

const modPath = path.resolve(process.argv[2] ?? './rakujs.js');
const factory = require(modPath);
const BENCH = path.join(__dirname, '..', 'tools', 'bench');
const kernels = ['loopsum','fib','strcat','arrayops','sortnums','regex','hash','bigint'];
const RUNS = 7;

(async () => {
  const results = {};
  const t0 = performance.now();
  let mod = await factory({ print: () => {}, printErr: () => {} });
  results.__module_init_ms = Math.round(performance.now() - t0);
  results.__version = mod.ccall('rakupp_version', 'string', [], []);

  for (const k of kernels) {
    const src = fs.readFileSync(path.join(BENCH, k + '.raku'), 'utf8');
    const times = [];
    let err = null;
    for (let i = 0; i < RUNS; i++) {
      let rc, ms;
      try {
        const r0 = performance.now();
        rc = mod.ccall('rakupp_run', 'number', ['string'], [src]);
        ms = performance.now() - r0;
      } catch (e) {
        // Stack overflow/abort leaves the instance in an unknown state —
        // rebuild a fresh one (same recovery as playground/worker.js).
        err = (e instanceof RangeError || /call stack/i.test(String(e))) ? 'STACK-OVERFLOW' : 'ERROR: ' + e;
        mod = await factory({ print: () => {}, printErr: () => {} });
        break;
      }
      if (rc !== 0) { err = 'ERROR rc=' + rc; break; }
      if (i > 0) times.push(ms);
    }
    results[k] = err ?? Math.round(Math.min(...times) * 10) / 10;
    console.error(`# ${k}: ${results[k]}`);
  }
  console.log(JSON.stringify(results));
})();
