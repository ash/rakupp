// The perf-review-2026-11 kernels as plain untyped JS, scaled: bun ks.js <kernel> <S>
// Prints the result and "kernel ns-per-op" (time measured inside the process,
// so start-up is excluded but JIT compilation is included).
const k = process.argv[2], S = +(process.argv[3] || 1);
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
function sum(a, b) { return a + b; }
class P { constructor(x, y) { this.x = x; this.y = y; } sum() { return this.x + this.y; } }
class Body {
  constructor(x, y, vx, vy, m) { this.x = x; this.y = y; this.vx = vx; this.vy = vy; this.m = m; }
  advance(dt) { this.x += dt * this.vx; this.y += dt * this.vy; }
}
const t0 = performance.now();
let out, ops;
switch (k) {
  case "intloop": { let n = 20000000 * S, s = 0, i = 0; while (i < n) { s = (s + i * i) % 1000003; i++; } out = s; ops = n; break; }
  case "intloop32": { let n = 20000000 * S, s = 0, i = 0; while (i < n) { s = (s + (i & 1023) * (i & 1023)) % 1000003; i++; } out = s; ops = n; break; }
  case "numloop": { let n = 20000000 * S, s = 0, sg = 1; for (let q = 0; q < n; q++) { s += sg / (2 * q + 1); sg = -sg; } out = s * 4; ops = n; break; }
  case "dot": { const n = 1000; const a = [], b = []; for (let i = 0; i < n; i++) { a.push(i * 0.5); b.push(1 / (i + 1)); }
    let t = 0; for (let r = 0; r < 5000 * S; r++) { let s = 0; for (let i = 0; i < n; i++) s += a[i] * b[i]; t += s; } out = t; ops = 5000 * S * n; break; }
  case "matmul": { const n = 120; const a = [], b = [], c = new Array(n * n).fill(0);
    for (let i = 0; i < n * n; i++) { a.push((i % 7) * 0.25); b.push((i % 5) * 0.5); }
    for (let r = 0; r < S; r++)
      for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) { let s = 0; for (let q = 0; q < n; q++) s += a[i * n + q] * b[q * n + j]; c[i * n + j] = s; }
    let t = 0; for (let i = 0; i < n * n; i++) t += c[i]; out = t; ops = S * n * n * n; break; }
  case "stencil": { const n = 2000; const u = new Array(n).fill(0); u[0] = 1; u[n - 1] = 1; const v = u.slice();
    for (let r = 0; r < 400 * S; r++) { for (let i = 1; i < n - 1; i++) v[i] = 0.5 * (u[i - 1] + u[i + 1]); for (let i = 1; i < n - 1; i++) u[i] = 0.5 * (v[i - 1] + v[i + 1]); }
    let t = 0; for (let i = 0; i < n; i++) t += u[i]; out = t; ops = 400 * S * 2 * (n - 2); break; }
  case "fib": { for (let r = 0; r < S; r++) out = fib(27); ops = S * 635621; break; }
  case "subcall": { let s = 0; for (let i = 0; i < 3000000 * S; i++) s += sum(i, 2); out = s; ops = 3000000 * S; break; }
  case "meth": { const p = new P(1, 2); let s = 0; for (let i = 0; i < 3000000 * S; i++) s += p.sum(); out = s; ops = 3000000 * S; break; }
  case "nbody": { const B = []; for (let i = 0; i < 5; i++) B.push(new Body(i * 1, i * 0.5, 0, 0, 1 + i));
    for (let st = 0; st < 20000 * S; st++) {
      for (let i = 0; i < 5; i++) { const bi = B[i]; for (let j = i + 1; j < 5; j++) { const bj = B[j];
        const dx = bi.x - bj.x, dy = bi.y - bj.y, d2 = dx * dx + dy * dy + 0.01, mag = 0.001 / (d2 * Math.sqrt(d2));
        bi.vx -= dx * bj.m * mag; bi.vy -= dy * bj.m * mag; bj.vx += dx * bi.m * mag; bj.vy += dy * bi.m * mag; } }
      for (const b of B) b.advance(0.001); }
    let e = 0; for (const p of B) e += 0.5 * p.m * (p.vx * p.vx + p.vy * p.vy); out = e; ops = 20000 * S; break; }
  default: throw new Error("kernel?");
}
const t1 = performance.now();
console.log(`${k} ${((t1 - t0) * 1e6 / ops).toFixed(3)} ${out}`);
