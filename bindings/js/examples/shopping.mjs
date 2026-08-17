// The JS guide's example: compile a grammar from a .raku file, parse text,
// and move the results into JavaScript. Bun only (bun:ffi). From a checkout:
//
//   RAKUPP_LIB=build/librakupp.dylib bun bindings/js/examples/shopping.mjs
//
// (.so on Linux; with rakupp on PATH the env variable is not needed.)

import { Grammar, ParseError } from "../rakulang.js";

const here = new URL(".", import.meta.url).pathname;
const g = Grammar.fromFile(`${here}../../examples/shopping.raku`,
                           { name: "Shopping", actions: "ShoppingActions" });

const m = g.parse("milk=2\nbread = 1  eggs=12\n"); // a Match, or null if no match

const items = m.get("item");
console.log(items.length, "items");
for (const item of items)                          // lazy: one engine call per leaf
  console.log(item.get("name").str(), "x", item.get("qty").int());

console.log("total, computed in Raku:", m.made()); // ShoppingActions made this

console.log("as plain JS data:", JSON.stringify(m.tree()));

m.close();   // a Match holds a rooted engine value — free it explicitly

// A non-match returns null; strict: true throws a diagnosed error instead.
try {
  g.parse("milk=2\nbread=lots\n", { strict: true });
}
catch (e) {
  if (!(e instanceof ParseError)) throw e;
  console.log(`line ${e.line} column ${e.column} while trying <${e.rule}>`);
}
