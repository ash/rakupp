// Functions that make functions.

// Currying: turn f(a, b, c) into f(a)(b)(c).
function curry3(f) {
  return a => b => c => f(a, b, c);
}
const volume = curry3((l, w, h) => l * w * h);
console.log("volume 2x3x4:", volume(2)(3)(4));

// compose(f, g)(x) === f(g(x)).
const compose = (f, g) => x => f(g(x));
const inc = x => x + 1;
const double = x => x * 2;
console.log("compose(inc, double)(10):", compose(inc, double)(10));

// A counter that keeps its state completely private.
function makeCounter(start) {
  let n = start;
  return {
    next: () => { n += 1; return n; },
    reset: () => { n = start; },
    value: () => n,
  };
}
const c = makeCounter(10);
console.log("counter:", c.next(), c.next(), c.next());
c.reset();
console.log("after reset:", c.value());

// once: run a function a single time, then cache and return that result.
function once(f) {
  let called = false;
  let cached;
  return x => {
    if (!called) { cached = f(x); called = true; }
    return cached;
  };
}
const init = once(seed => { console.log("  (initializing with", seed + ")"); return seed * 2; });
console.log("first call:", init(21));
console.log("second call:", init(99));

// memoize any single-argument function with a closure-held cache.
function memoize(f) {
  const cache = {};
  return n => {
    if (cache[n] === undefined) { cache[n] = f(n); }
    return cache[n];
  };
}
let calls = 0;
const slowSquare = memoize(n => { calls++; return n * n; });
console.log("squares:", slowSquare(4), slowSquare(4), slowSquare(5));
console.log("actual computations:", calls);
