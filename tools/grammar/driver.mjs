// The JS half of the byte-identical gate: the same walk as driver.py,
// through bindings/js/rakulang.js on bun:ffi. Run by grammar-smoke.raku;
// outputs are compared byte for byte.
//
//   bun tools/grammar/driver.mjs <librakupp-path> <grammar-file> <input-file>

import { Grammar, interpreter, ParseError } from "../../bindings/js/rakulang.js";
import { readFileSync } from "fs";

const [lib, grammarFile, inputFile] = process.argv.slice(2);

function canon(x) {
  if (Array.isArray(x)) return "[" + x.map(canon).join(",") + "]";
  if (x !== null && typeof x === "object")
    return "{" + Object.keys(x).sort().map(k => k + ":" + canon(x[k])).join(",") + "}";
  return String(x);
}

const rakuBool = b => (b ? "True" : "False");

interpreter(lib);
const g = Grammar.fromFile(grammarFile, { name: "Log", actions: "LogActions" });

const m = g.parse(readFileSync(inputFile, "utf8"));
if (!m) { console.error("gate: the log corpus did not parse"); process.exit(1); }

const lines = m.get("line");
let out = [];
out.push(`lines ${lines.length}`);
out.push(`islist ${rakuBool(lines.isList())}`);

for (const line of lines) out.push(`${line.get("ip").str()} ${line.get("status").str()}`);

out.push(`made ${m.get("line").at(0).get("size").made()}`);
out.push(`req.str ${m.get("line").at(42).get("req").str()}`);
out.push(`size.int ${m.get("line").at(42).get("size").int()}`);
out.push(`missing ${rakuBool(m.get("nope").truthy())}`);
out.push(`tree ${canon(m.get("line").at(999).tree())}`);

const one = g.parse('7.7.7.7 - - [x] "GET / HTTP/1.1" 200 5\n', { rule: "line" });
out.push(`rule-parse ${one.get("status").str()}`);

const bad = g.parse("this is not a log line");
out.push(`failed-parse ${bad ? "Match" : "None"}`);

try {
  g.parse("this is not a log line", { strict: true });
  out.push("diag none");
} catch (e) {
  if (!(e instanceof ParseError)) throw e;
  out.push(`diag line ${e.line} col ${e.column} rule ${e.rule}`);
}

console.log(out.join("\n"));
