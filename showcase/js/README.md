# js — a JavaScript/TypeScript interpreter

A working interpreter for a practical slice of JavaScript — with enough
TypeScript on top that everyday `.ts` files run unchanged. One Raku grammar
parses both languages into an AST; a tree-walking evaluator runs it. TypeScript
is handled the way `tsc` handles it: type annotations, interfaces, type aliases
and generics are parsed and erased, while enums (the one TS construct with
runtime output) become real objects.

```sh
build/rakupp showcase/js/js.raku showcase/js/examples/fizzbuzz.js
build/rakupp showcase/js/js.raku showcase/js/examples/bank.ts
build/rakupp showcase/js/js.raku --ast=file.js     # dump the parsed AST
build/rakupp showcase/js/js.raku --asi=file.js     # show semicolons ASI inserts
build/rakupp showcase/js/js.raku                   # no file → a REPL
```

Semicolons are optional — a newline ends a statement wherever JavaScript's
automatic semicolon insertion (ASI) would; see [Semicolons (ASI)](#semicolons-asi).

Ten example programs live in [`examples/`](examples/):

| File | Shows off |
|---|---|
| `fizzbuzz.js`    | data-driven rules instead of an if-chain |
| `fib.js`         | recursion, closures, memoization |
| `closures.js`    | currying, compose, private counters, `once`, memoize |
| `quicksort.js`   | quicksort + mergesort, array methods, non-destructive sorts |
| `wordcount.js`   | strings and objects — count, rank, report |
| `gameoflife.js`  | Conway's Game of Life on a wrap-around grid, nested loops |
| `bank.ts`        | classes, inheritance, `super`, enums, generics, param properties |
| `roman.ts`       | tuple types, `Record`, `[v, s]` destructuring, both-way conversion |
| `shapes.ts`      | an interface + a class hierarchy with polymorphic dispatch |
| `calculator.ts`  | a tokenizer + recursive-descent parser, in the language being parsed |
| `bits.js`        | bitwise ops, `switch` with fall-through, optional chaining `?.` |

The seven `.js` examples produce byte-identical output under `node` and under
`js.raku` — including `0.1 + 0.2` printing `0.30000000000000004`, `-7 % 3`
being `-1`, and default `sort()` ordering numbers as strings. (`console.log`
of a long array is the one place they differ: node wraps arrays past six
elements into a grid, so the sort demo joins its arrays for display.)

## What runs

**JavaScript.** `let`/`const`/`var` (with multiple declarators),
functions, closures, arrow functions (expression and block bodies, default
parameters), `if`/`else`, `while`, `do`/`while`, classic `for`, `for…of`
(arrays, strings, and `[k, v]` destructuring for `Object.entries`),
`switch`/`case`/`default` with fall-through, `break`/`continue`, `return`,
ternary, `&&`/`||`/`??` with short-circuit, `==`/`===` with JS coercion rules,
`+` as concat-or-add, `**`, bitwise `& | ^ ~ << >> >>>`, `typeof`/`void`/`delete`,
`instanceof`, `in`, `x++`/`x--`, compound assignment (arithmetic, bitwise, and
logical `&&=`/`||=`/`??=`), optional chaining `?.` (`a?.b`, `a?.[i]`, `a?.()`)
with short-circuit, template literals with `${…}`, array and object literals
(shorthand and method properties), `this`, classes with fields, `extends`,
`super(...)`/`super.m(...)`, static members, `new`,
`throw`/`try`/`catch`/`finally`, IIFEs, and `//` and `/* */` comments.

**TypeScript.** Type annotations on variables, parameters, returns and fields;
`interface` and `type` declarations; generic functions and generic call sites
(`identity<string>("x")`); union types; optional (`x?`) and `readonly`/access
modifiers; constructor parameter properties (`constructor(public r: number)`,
which really assign); `as` casts and `x!` non-null assertions (erased);
`enum` with auto and explicit values plus reverse mapping (`Color[10]`).

**Built-ins.** `console.log/error/warn`; `Math` (floor, ceil, round, trunc,
abs, sign, sqrt, pow, min, max, random, log, exp, hypot, PI, E);
`JSON.stringify` (with indent); `Object.keys/values/entries`;
`Array.isArray/from`; `Number(...)`, `String(...)`, `Boolean(...)`,
`parseInt` (with radix), `parseFloat`, `isNaN`, `toFixed`, `toString(radix)`;
array methods (push, pop, shift, unshift, slice, indexOf, includes, join, map,
filter, forEach, reduce, find, findIndex, some, every, concat, reverse, sort,
flat); string methods (toUpperCase, toLowerCase, trim, charAt, charCodeAt,
indexOf, includes, startsWith, endsWith, slice, substring, split, repeat,
replace, replaceAll, padStart, padEnd, concat); `new Error(msg)` with
`.message`.

## What doesn't

Not implemented: regex literals, labels, getters/setters, object/array
destructuring outside `for…of`, spread/rest, `async`/`await`, promises,
generators, modules (`import`/`export`), `Symbol`, `Map`/`Set`, prototypes
(`Object.create`, `.prototype`), `JSON.parse`. `instanceof` covers user classes
plus `Object`/`Array` (there's no global prototype chain otherwise). Keywords
are not valid identifiers.

## Semicolons (ASI)

Semicolons are optional — a newline ends a statement wherever JavaScript's
[automatic semicolon insertion](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Lexical_grammar#automatic_semicolon_insertion)
would end one. This is a preprocessing pass (`insert-asi`, alongside comment
stripping) that inserts a real `;` at each statement-ending line break, since
the grammar can't see newlines directly. It follows JS's own heuristic: end the
statement at a line break *unless* the break is a continuation — inside `(`/`[`,
inside an object literal, after a trailing operator, or before a leading
continuation token (`.`, a binary operator, `)`, `else`…). It handles the
subtle cases — method chains split across lines, `return`↵`expr` (which becomes
`return; expr;`, per the "restricted production" rule), do-while, Allman braces,
and object-vs-block `{` — but it is a heuristic, not the full ECMAScript
algorithm, so pathological code (`a = b`↵`(c).d()`, which JS keeps as one call)
follows JS by *not* inserting there. All six `.js` examples run identically with
or without semicolons. See the transform with:

```sh
build/rakupp showcase/js/js.raku --asi=file.js     # print the semicolon-inserted source
```

[`ASI.md`](ASI.md) is a full writeup: why the grammar can't see newlines, the
decision rule and token alphabet, and the tricky cases (object-vs-block braces,
IIFEs, `enum`, do-while, `else`) that each took a correction.

## Semantics worth knowing

* Every number is an IEEE double, like JS. `1/0` is `Infinity`, `0/0` is
  `NaN`, `%` truncates toward zero, integers print without a decimal point.
* Truthiness, `==` coercion, string comparison with `<`, and `+`
  concat-or-add all follow JS rules.
* `sort()` without a comparator sorts as strings; with `(a, b) => a - b` it
  sorts numerically. Both mutate, like JS.
* Objects preserve insertion order (a `JSObject` keeps its own key list —
  a plain Raku `Hash` wouldn't).
* `this` is bound at the call site for functions and methods; arrows inherit
  it lexically. `return`, `break`, `continue` and `throw` unwind as typed
  Raku exceptions.

## Speed

It's a tree-walking interpreter written in an interpreted language: naive
`fib(15)` (1973 calls) takes about 1.5 s under rakupp on an M-class laptop;
the other three examples run in well under 0.1 s. Function bodies that end in
a lone `return` skip the exception-based unwind path, which cut about a third
off call-heavy code.

`rakupp --exe` compiles it to a native binary that runs all four examples
except `fib.js`'s deep memoized calls: the native binary currently has a
smaller usable recursion budget than the interpreter (JS recursion deeper
than ~15 levels hits it), so `fastFib(78)` only completes under the
interpreter.

## Implementation notes

The pipeline is the same as the lisp and json showcases, scaled up: a
`grammar` (~120 lines) with a precedence ladder of nine levels feeds an
actions class that builds hash-based AST nodes, and `eval-stmt`/`eval-expr`
walk them. Two pre-passes run before parsing, since ASI is a token-stream
transform, not a grammar-shaped problem (see [`ASI.md`](ASI.md)): comments are
blanked out with newlines preserved, then `insert-asi` inserts real semicolons
at statement-ending line breaks. A few rakupp-specific workarounds
are marked with comments in the source: quantified captures are read through a
list assignment before indexing, every `CATCH` carries a `default { .rethrow }`,
and value keywords like `null` are classified in the ident action because
proto-rule dispatch is longest-match.
