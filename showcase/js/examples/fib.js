// Recursion, closures and memoization.
function fib(n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}
console.log("fib(15) =", fib(15));

// The memoized version keeps its cache in a closure and handles
// numbers far beyond what the naive one could reach.
const fastFib = (function () {
  const memo = {};
  return function f(n) {
    if (n < 2) { return n; }
    if (memo[n] === undefined) { memo[n] = f(n - 1) + f(n - 2); }
    return memo[n];
  };
})();

for (const n of [30, 50, 78]) {
  console.log(`fastFib(${n}) = ${fastFib(n)}`);
}
