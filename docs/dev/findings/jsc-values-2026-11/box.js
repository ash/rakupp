// dot over arrays whose storage is boxed JSValues (a string sits past the loop's end,
// so the array is ArrayWithContiguous, not ArrayWithDouble), against unboxed storage.
function dot(a, b, n) { let s = 0; for (let i = 0; i < n; i++) s += a[i] * b[i]; return s; }
function mk(boxed) { const n = 1000, a = [], b = [];
  for (let i = 0; i < n; i++) { a.push(i * 0.5); b.push(1 / (i + 1)); }
  if (boxed) { a.push("x"); b.push("y"); } return [a, b]; }
const [a, b] = mk(process.argv[2] === "boxed");
const t0 = performance.now();
let t = 0; for (let r = 0; r < 50000; r++) t += dot(a, b, 1000);
console.log(process.argv[2], ((performance.now() - t0) * 1e6 / 50e6).toFixed(3), t);
