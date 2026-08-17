// The JS guide's second example: Raku as a library, no grammar involved.
// Evaluate source, call subs with JS values, read results back. Bun only
// (bun:ffi). From a checkout:
//
//   RAKUPP_LIB=build/librakupp.dylib bun bindings/js/examples/calc.mjs
//
// (.so on Linux; with rakupp on PATH the env variable is not needed.)

import { interpreter, RakuError } from "../rakulang.js";
import { readFileSync } from "fs";

const here = new URL(".", import.meta.url).pathname;
const raku = interpreter();

// eval runs source in the mainline scope and keeps it, exactly like the REPL.
console.log("2 + 2 =", raku.eval("2 + 2"));

// So loading a file of subs is just an eval — they stay callable below.
raku.eval(readFileSync(`${here}../../examples/calc.raku`, "utf8"));

// call passes JS values as arguments and returns JS values.
console.log("area(3, 4) =", raku.call("area", 3, 4));

const primes = raku.call("primes-below", 30);        // a Raku list -> a JS Array
console.log("primes below 30:", primes.join(" "));

const s = raku.call("stats", [3, 1, 4, 1, 5, 9, 2, 6]);  // a JS Array -> a Raku list
console.log(`stats: count=${s.count} sum=${s.sum} mean=${s.mean} max=${s.max}`);
                                                     // a Raku hash -> a JS object
console.log("greet:", raku.call("greet", { name: "Ada", age: 36 }));

// Raku integers do not overflow; past 64 bits this one hands back digits.
console.log("30! =", raku.call("factorial", 30));

// A die inside Raku crosses as RakuError, the host's own exception type.
try {
  raku.call("checked-div", 10, 0);
}
catch (e) {
  if (!(e instanceof RakuError)) throw e;
  console.log("died:", e.message);
}
