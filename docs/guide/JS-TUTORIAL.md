# Raku to JavaScript, step by step

How to take a Raku program, turn it into JavaScript, and run it under `bun` or
`node` — worked through three programs of increasing size, ending with a Perl 5
interpreter running in a JavaScript engine.

[JS.md](JS.md) is the reference page for the backend: every flag, the value
table, the exact list of what is inside the core. This page is the walkthrough,
and it answers the four questions people ask first:

| question | short answer |
|---|---|
| Can it convert **anything**? | No. Of the 579 programs in the corpus gate, 162 transpile and agree, 342 are refused by name, 75 disagree. §[Can it convert anything?](#can-it-convert-anything) |
| Does it work with **modules**? | `use SomeModule` in a program: no. A module compiled *for JavaScript importers*: yes. §[Modules](#modules) |
| Does it work with **standard input**? | Yes — `lines`, `get`, `$*IN.lines`, `$*IN.slurp`. §[Standard input](#standard-input) |
| Does it work with **command-line arguments**? | Yes, including `MAIN`, the usage message and Rakudo's argument conventions. §[Command-line arguments](#command-line-arguments) |

Every number and every transcript on this page was produced on one machine
(Darwin 24.6, arm64, `node` v24.20.0, `bun` 1.4.0) with rakupp 3.28.0.

---

## The shortest path

```bash
echo 'say "hello from ", $*VM.name' > hello.raku
rakupp --target=js hello.raku -o hello.js
bun hello.js
```

```
hello from js
```

`-o hello.js` wrote three files:

```
hello.js        the program
hello.js.map    a source map, every generated line back to its Raku line
rakupp-rt.js    the runtime the program imports
```

The runtime comes out of the `rakupp` binary, so a program always gets the
runtime it was compiled against. Ship `hello.js` and `rakupp-rt.js` together, or
use `--standalone` to get one file with the runtime inlined.

Without `-o`, the JavaScript goes to stdout instead.

(`$*VM.name` is `js` there and `cpp` under the interpreter — this engine's two
backends, the way Rakudo's are `moar`, `jvm` and `js`. `$*RAKU.compiler.name` is
`Raku++` either way.)

---

## A program with a command line: factorial

Save this as `factorial.raku`:

```raku
#!/usr/bin/env raku
# factorial — the same program for the interpreter and for JavaScript.

sub fact(Int $n --> Int) { [*] 1 .. $n }

#| Print n! for each number given, or for each line of standard input.
sub MAIN(
    *@numbers,                   #= the numbers to take the factorial of
    Bool :$stdin,                #= read the numbers from standard input instead
    Bool :$digits,               #= print how many digits the answer has
) {
    unless @numbers || $stdin {
        note $*USAGE;
        exit 2;
    }
    for ($stdin ?? lines() !! @numbers) -> $n {
        my $f = fact(+$n);
        say $digits ?? "$n! has {$f.chars} digits" !! "$n! = $f";
    }
}
```

Sixteen lines that exercise most of what a command-line program needs: a typed
sub, a slurpy positional, two `Bool` options, declarator pod,
arbitrary-precision integers, `$*USAGE`, and standard input.

Reading standard input is behind `--stdin` on purpose. A slurpy positional
binds happily to nothing, so a program that falls through to `lines()` when it
has no arguments will sit and block when someone runs it bare to see what it
does — with no prompt to say why. `note $*USAGE; exit 2` is the two lines that
turn that into an answer.

```bash
rakupp --target=js factorial.raku -o factorial.js
```

### Run it

```bash
bun factorial.js 5 10 20
```

```
5! = 120
10! = 3628800
20! = 2432902008176640000
```

`node factorial.js 5 10 20` prints the same thing — with one caveat about
`package.json` that has its own section [below](#choosing-a-host).

### Arbitrary-precision integers survive

Raku's `Int` has no upper bound. In JavaScript the runtime keeps a value as a
number while it is a safe integer and promotes to `BigInt` past 2⁵³, so the
answer is exact rather than `1.7e157`:

```bash
bun factorial.js 100
```

```
100! = 93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000
```

### The usage message

`MAIN`'s usage text is assembled from the signature, and the `#|` above the
routine and the `#=` beside each parameter become the description and the
option list — the same text the interpreter prints:

```bash
bun factorial.js --help
```

```
Usage:
  factorial.js [--stdin] [--digits] [<numbers> ...] -- Print n! for each number given, or for each line of standard input.
  
    [<numbers> ...]    the numbers to take the factorial of
    --stdin            read the numbers from standard input instead
    --digits           print how many digits the answer has
```

That text is also what `$*USAGE` hands the program, which is what the two-line
guard above prints when there is nothing to do.

Only a parameter with a `#=` gets a line in the table; a parameter with a
default shows it, and a one-character option name is spelled with one dash:

```raku
sub MAIN(
    Str  $word,             #= the word to repeat
    Int  :$times = 3,       #= how many times
    Str  :$sep = ', ',      #= what to put between
    Bool :$v,               #= shout it
) { say (($v ?? $word.uc !! $word) xx $times).join($sep) }
```

```
Usage:
  prog.js [--times=<Int>] [--sep=<Str>] [-v] <word>
  
    <word>           the word to repeat
    --times=<Int>    how many times [default: 3]
    --sep=<Str>      what to put between [default: ', ']
    -v               shout it
```

### Options come before positionals

```bash
bun factorial.js --digits 100     # 100! has 158 digits
bun factorial.js 100 --digits     # "--digits" is a POSITIONAL here
```

That is Raku's rule, not the backend's: once `MAIN` has taken a positional
argument, the current token and **everything after it** is positional, verbatim.
The transpiled program follows it, so `100 --digits` passes the literal string
`--digits` to `+$n` and fails the same way the interpreter does. Put options
first, or use `--` to force the boundary.

### Standard input

```bash
printf '10\n20\n' | bun factorial.js --stdin
```

```
10! = 3628800
20! = 2432902008176640000
```

### Proving it agrees

`--verify` runs the program under the interpreter *and* under the JavaScript
host, compares stdout, stderr and the exit status byte for byte, and refuses to
write the output on disagreement:

```bash
rakupp --target=js examples/mandel.raku -o mandel.js --verify
```

```
verified: interpreter and JavaScript agree — emitting mandel.js
```

Three kinds of program it cannot judge, all for the same reason — the two runs
have to be byte-identical:

- one whose output is not deterministic;
- a `use js` program, since the interpreter refuses those by design and so
  cannot be their oracle (they have goldens instead);
- **one that prints its own name.** `factorial.raku` above is one: the usage
  line starts with the program's filename, which is `factorial.raku` under the
  interpreter and `factorial.js` under the host. That is not a disagreement
  about behaviour, but `--verify` cannot tell the difference, so it reports one.
  Any `sub MAIN` that prints usage is in this class.

---

## A compute kernel: mandel.js

`examples/mandel.raku` is 46 lines of nested `loop`s and exact-rational
arithmetic that prints the Mandelbrot set as 75×30 characters.

```bash
rakupp --target=js examples/mandel.raku -o mandel.js
bun mandel.js | head -3
```

```
................::::::::::::::::::::::::::::::::::::::::::::...............
...........::::::::::::::::::::::::::::::::::::::::::::::::::::::..........
........::::::::::::::::::::::::::::::::::,,,,,,,:::::::::::::::::::.......
```

Byte-identical to the interpreter, and to `node mandel.js`. The program itself
is 2.7 KB; the 372 KB is all in `rakupp-rt.js` next to it, and one copy of it
serves every program you compile with the same `rakupp`.

The `0.1`, `1.5` and `0.04` in that program are Raku `Rat`s, not floats, and
they stay exact in JavaScript too — the runtime carries a BigInt numerator and
denominator. That is the reason `mandel` is the slowest of the kernels below,
and the reason its output matches the interpreter's character for character.

### One file, and a browser

```bash
rakupp --target=js --standalone examples/mandel.raku -o mandel.js
```

`--standalone` inlines the runtime: one plain script, no module system to
satisfy, runs under `bun`, `node`, `deno run`, or from a `<script src=…>` tag.
In a page, output goes to `console.log` unless the page replaces
`R.host.writeOut` before `R.main` runs.

---

## A whole interpreter: perl.js

`showcase/perl/perl.raku` is 1,644 lines: a Raku grammar that parses a practical
slice of Perl 5 into an AST, an action class, a tree-walking evaluator, and a
backtracking regex engine written in Raku. If anything is going to find the
edges of the backend, a program of that shape is.

```bash
rakupp --target=js showcase/perl/perl.raku -o perl.js
bun perl.js showcase/perl/examples/fizzbuzz.pl
```

```
1
2
Fizz
4
Buzz
…
```

Perl, running on a Raku interpreter, running in a JavaScript engine.

It is not alone. Every language showcase in the repository transpiles, and
this is how far each one agrees with the interpreter on its own examples:

| showcase | lines | examples byte-identical |
|---|---:|---|
| `showcase/perl` | 1,644 | 6 / 6 |
| `showcase/python` | 1,487 | 5 / 5 |
| `showcase/js` | 2,167 | 11 / 11 |
| `showcase/lisp` | 432 | 2 / 2 |

All six example programs — `fizzbuzz`, `histogram`, `quicksort`, `regex`,
`sieve`, `wordfreq` — produce byte-identical stdout, stderr and exit status
under `rakupp showcase/perl/perl.raku …` and under `bun perl.js …`. That
includes `regex.pl`, which drives the showcase's own regex engine: captures,
captures reused in a substitution's replacement, and a global match in list
context.

### Its own command line still works

`perl.raku`'s `MAIN` is `($file?, *@args, Str :$ast)`, and all three paths
survive the trip:

```bash
bun perl.js prog.pl alpha beta 42    # @ARGV inside the Perl program
bun perl.js --ast=prog.pl            # dump the parse tree
bun perl.js < session.pl             # no file: the REPL, reading stdin
```

(`--ast` prints the tree through `.raku`, and two small rendering differences
show up there — see the divergence list in [JS.md](JS.md#values-in-the-runtime).
The tree itself is the same.)

The REPL path is worth a second look, because it is the whole standard-input
story in one program: with no `$file` the showcase prompts and reads `$*IN`
line by line, and the transcript under `bun` is byte-identical to the
interpreter's.

### Sizes

| file | size |
|---|---:|
| `perl.js` (imports the runtime) | 229 KB |
| `rakupp-rt.js` (beside it) | 372 KB |
| the two together, via `--standalone` | 601 KB |

---

## Can it convert anything?

**No**, and the backend says so rather than guessing. A program outside the core
is refused at transpile time, naming the construct and its line, with exit 5:

```bash
echo 'say EVAL "1 + 2 * 3"' > uses-eval.raku
rakupp --target=js uses-eval.raku -o out.js
```

```
note: EVAL — outside the JavaScript core; --fallback=wasm runs it on the WebAssembly engine instead
```

The measured state of the work is the corpus gate, `rakupp t/js/run.raku`, which
transpiles every program in `t/regression/` and `examples/` and compares each
in-core one against the same binary interpreting it:

```
js gate (bun): 164 in-core and agreeing of 587 programs; 348 refused; 75 disagreeing
```

Three numbers. The last one is the count of open bugs: programs that transpile
but then print something different from what the interpreter prints — which is
why `--verify` exists, and why it is worth using on anything you intend to ship.
The middle one comes with a histogram that *is* the work queue. Its head:

```
    73  a program that runs the rakupp executable ($*EXECUTABLE)
    50  the name '…'
    44  a call to '…'
    25  EVAL
    13  use NativeCall
    11  use nqp
     9  the Test module
     8  a symbolic reference ::(…)
     8  is repr
```

Read that as: the corpus is a *test suite*, so a large slice of it re-runs
`rakupp` itself or loads `Test`, and would be outside the core no matter how
complete the backend got. The honest reading of "can it convert anything" is the
one the three examples above give — an ordinary program that computes, prints,
takes arguments and reads input converts and agrees; a program that reaches for
`EVAL`, NativeCall, nqp, threads or `Proc::Async` does not.

### `--fallback=wasm`

```bash
rakupp --target=js uses-eval.raku -o out.js --fallback=wasm
node out.js
```

Instead of refusing, this writes a small wrapper (about 2 KB) that loads
Raku.js — the interpreter compiled to WebAssembly — and runs the embedded Raku
source through it. It needs `rakujs.js` and `rakujs.wasm` (the
`rakujs-<version>.zip` release asset, built with Node support) next to the
output or named by `$RAKUJS`, which points at the **file**, not the directory:

```bash
RAKUJS=/path/to/rakujs.js node out.js
```

The artifact class changes completely — the wrapper is 2.2 KB but the engine
beside it is about 5 MB of WebAssembly in the Node build, with that build's
recursion cap and no DOM access — which is why it is a flag and not the default.
And it does not rescue every refusal: a program refused for `use SomeModule`
still cannot find that module under WASM, because the WASM engine has no view of
your filesystem. It rescues *self-contained* programs, `EVAL` among them.

---

## Modules

Two different questions hide under one word.

### Can a program that `use`s a module be transpiled?

Not today. Two files — an ordinary module and an ordinary program that uses it:

```raku
# lib/Stats.rakumod
unit module Stats;

sub mean(@xs) is export { @xs.sum / @xs.elems }

sub median(@xs) is export {
    my @s = @xs.sort;
    @s.elems %% 2 ?? (@s[@s.elems div 2 - 1] + @s[@s.elems div 2]) / 2
                  !! @s[@s.elems div 2]
}
```

```raku
# report.raku
use lib 'lib';
use Stats;

sub MAIN(*@n) {
    my @x = @n.map(+*);
    say "mean=", mean(@x), " median=", median(@x);
}
```

It runs under the interpreter:

```bash
rakupp report.raku 3 1 4 1 5 9 2 6     # mean=3.875 median=3.5
```

…and is refused by the JavaScript backend:

```bash
rakupp --target=js report.raku -o report.js
```

```
note: a `use`d module (Stats) — outside the JavaScript core; --fallback=wasm runs it on the WebAssembly engine instead
```

The way around it is the one the browser showcases use: **concatenate the module
into the program** and transpile the result. Drop the `unit module` line and the
`use` lines, and the rest is ordinary Raku:

```bash
{ grep -vE '^\s*unit\s+module' lib/Stats.rakumod
  grep -vE '^\s*use\s+(lib|Stats)\b' report.raku ; } > bundle.raku
rakupp --target=js bundle.raku -o bundle.js
bun bundle.js 3 1 4 1 5 9 2 6          # mean=3.875 median=3.5
```

`showcase/eclipse/tools/build.raku` is the same three steps written properly —
bundle, transpile, inline into a page — and `showcase/fourier` and
`showcase/orbits` reuse it verbatim.

### Can a Raku module be compiled *into* a JavaScript module?

Yes, and this is the direction that works cleanly. `--module` emits an ES module
that runs the file's mainline at import time and exports its subs, classes,
grammars and enums instead of running `MAIN`. Take a module with one of each:

```raku
# lib/Fact.rakumod
unit module Fact;

sub fact(Int $n --> Int) is export { $n <= 1 ?? 1 !! $n * fact($n - 1) }

sub choose(Int $n, Int $k --> Int) is export {
    fact($n) div (fact($k) * fact($n - $k))
}

class Counter is export {
    has $.n is rw = 0;
    method bump($by = 1) { $!n += $by; self }
}
```

```bash
rakupp --target=js --module lib/Fact.rakumod -o fact.js
```

Three files come out: `fact.js`, `fact.d.ts` (TypeScript declarations) and
`rakupp-rt.js`. Then, from ordinary JavaScript:

```js
import { fact, choose, Counter } from './fact.js';

console.log(fact(10), choose(10, 3));       // 3628800 120
const c = Counter.new({ n: 5 });
c.bump(10);
console.log(c.n());                          // 15
```

```bash
bun  drive.mjs     # 3628800 120 / 15
node drive.mjs     # 3628800 120 / 15
```

Values cross as they do under `use js`: numbers, strings, booleans and arrays by
copy, hashes as plain objects, and an object or a Match as a proxy whose
properties call its Raku methods. A name that is not a JavaScript identifier
keeps its Raku spelling: `import { "add-all" as addAll } from './fact.js'`.

The npm shape is those three files plus a manifest; `"type": "module"` is what
lets node load the `.js` as an ES module.

`--module` excludes `--standalone` (a plain script cannot export) and `--verify`
(a module has nothing to run), and `END` blocks do not run.

---

## Standard input

All four spellings work, and give the same bytes under the interpreter and under
the JavaScript host:

| spelling | what it does |
|---|---|
| `lines()` | every line of `$*IN`, lazily |
| `get()` | one line |
| `$*IN.lines` | the same as `lines()`, explicit |
| `$*IN.slurp` | the whole of standard input as one `Str` |

```raku
my $n = 0; my $words = 0;
for lines() -> $l { $n++; $words += $l.words.elems }
say "$n lines, $words words";
```

```bash
printf 'alpha beta\ngamma\n' | bun wc.js      # 2 lines, 3 words
```

One caveat that runs the other way: `$*IN.get` in a `while` loop reads **every**
line under `--target=js` and under Rakudo, but only the first under the rakupp
interpreter. The transpiled program is the correct one, so `--verify` will
report a disagreement on a program written that way — believe the JavaScript.

Files work too, through `IO::Path`, under Node, Bun and Deno. In a browser they
do not exist and say so when used.

---

## Command-line arguments

`@*ARGS` is there, `%*ENV` is there, and `MAIN` is dispatched with the full set
of Rakudo's argument conventions. The transpiled program splits `argv` exactly as
the interpreter does:

| you type | `MAIN` sees |
|---|---|
| `a b` | two positionals |
| `--x=1 a` | `:x(IntStr(1))`, one positional — a numeric value arrives as an allomorph |
| `--v a` | `:v(True)` |
| `--/v a` | `:v(False)` |
| `-n=3 a`, `:n=3 a` | `:n(IntStr(3))` — short forms, `-` or `:` |
| `--x=1 --x=2 a` | `:x([1, 2])` — a repeated option collects every value |
| `-5 a` | `:5(True)` — a leading `-` is an option, digits included |
| `a --x=1` | **two positionals**: `"a"`, `"--x=1"` |
| `-- a --x=1` | two positionals; `--` ends option parsing |
| `-`, `--x=` | a lone dash is positional; an empty value is `IntStr(0, "")` |

A slurpy `*%opts` collects the options that no named parameter claimed; a
slurpy `*@args` collects the leftover positionals; `--help` prints the usage
message and exits 0, and an argument list that binds no candidate prints it and
exits 2.

Multi `MAIN` candidates dispatch by arity, type, literal and `where`, which is
how subcommands are spelled:

```raku
multi MAIN('add', Int $a, Int $b) { say $a + $b }
multi MAIN('list', Bool :$long)   { … }
```

---

## Choosing a host

The generated `.js` is an ES module. That one fact decides most of this section.

- **bun** runs an ES-module `.js` unconditionally, wherever the file sits. It is
  the default host for `--verify` and for the corpus gate for exactly that
  reason, and it is the one to reach for first.
- **node** decides by walking up to the nearest `package.json` and believing it.
  Without a `"type"` field anywhere above your output, an older node refuses the
  `import` and node 24 wraps your program's output in four lines of warning. A
  stray `~/package.json` is enough — this has been reported as "it does not run
  under node" from a home directory that had one. Give it
  `{"type": "module"}`, name the output `.mjs`, or use `--standalone`, which has
  no imports to argue about.
- **deno** runs it; `RAKUPP_JS=deno` points `--verify` at it.
- **a browser** takes `--standalone` output through `<script src=…>`.

### Speed

Wall clock including startup, best of seven, on the machine named at the top:

| kernel | interpreter | node | bun |
|---|---:|---:|---:|
| `say "hi"` | 29 ms | 53 ms | 67 ms |
| fib(29) | 353 ms | 107 ms | 108 ms |
| loopsum (3M) | 288 ms | 83 ms | 110 ms |
| mandel 75×30 | 112 ms | 106 ms | 228 ms |
| factorial 2000 | 30 ms | 57 ms | 78 ms |
| perl.js on `sieve.pl` | 78 ms | 82 ms | 321 ms |

The shape to take away: a JavaScript host costs about 50 ms of startup, so a
small program is slower there than under the interpreter, and a program that
computes is two to four times faster.

Do not read the last column as a fact about bun. **Check your host's
architecture before comparing**:

```bash
node -p "process.arch"        # arm64
bun  -e 'console.log(process.arch)'   # x64  ← running under Rosetta 2
```

The `bun` on this machine is the x86_64 build under Rosetta 2 while `node` is
native arm64, which is most of the gap in the rows that lean on BigInt (`mandel`)
and on strings (`perl.js`). On plain integer work — `fib` — the two agree to
within a millisecond, which is what you would expect.

### Recursion depth

How deep a transpiled sub can recurse depends on the sub *and* on the host.
Measured on

```raku
sub d($n) { $n == 0 ?? 0 !! 1 + d($n - 1) }
```

| | depth |
|---|---:|
| node, default stack | ~5,900 |
| the interpreter | ~27,700 |
| bun, default stack | ~31,900 |
| `node --stack-size=65500` | past 40,000 |

A program that goes past the limit says so and exits 1, rather than crashing:

```
Maximum call stack size exceeded (a deeper recursion than this JavaScript host allows; try node --stack-size=65500)
```

If your program recurses deeply, this is the one place where the choice of host
is worth measuring rather than assuming.

---

## When it goes wrong

Errors come in two kinds, and they behave differently.

**A Raku-level error** — a `die`, a failed type check, `Type Array does not
support associative indexing` — prints the same message the interpreter prints
and exits 1. What you do *not* get is the interpreter's backtrace:

```
# rakupp oops.raku
Type Array does not support associative indexing.
  in sub boom at oops.raku line 5
      5 |     @a<key>;
  in sub depth at oops.raku line 1
  in sub MAIN at oops.raku line 7

# bun oops.js
Type Array does not support associative indexing.
```

**A JavaScript-level error** is an emitter bug, reported as `Internal error`
with exit 3 — and here the source map earns its keep. `-o prog.js` also writes
`prog.js.map`, mapping every generated line back to the Raku line of its
statement:

```bash
node --enable-source-maps prog.js
```

```
Internal error: ReferenceError: u_frob is not defined
    at <anonymous>          (…/proto-multi-as-term.raku:19:1)
    at R.main.mainExit      (…/proto-multi-as-term.raku:19:1)
    at Object.main          (…/rakupp-rt.js:3813:15)
```

Line 19 of the Raku file is `my &p = proto sub frob(|) {*};` — the construct the
emitter got wrong, named directly. `bun` uses the map by itself; a debugger
steps by Raku line.

A recursion deeper than the host allows is reported as such, not as a crash —
see [Recursion depth](#recursion-depth).

The emitted JavaScript is also meant to be read. Subs are functions (`u_` plus
the mangled name), variables are `let`s (`v_`, `a_`, `h_` for `$`, `@`, `%`),
and every value operation is a call into the runtime `R`, so you can find the
operation that went wrong by name:

```js
function u_fib(v_n) {
    let v___2_1 = R.Any;
    if (arguments.length !== 1) R.arityError("fib", 1, arguments.length);
    if (v_n === R.Mu) R.notAny("$n");
    v_n = R.item(v_n);
    return (R.truthy(R.lt(v_n, 2)) ? v_n : R.add((u_fib(R.sub(v_n, 1))), (u_fib(R.sub(v_n, 2)))));
}
```

When a program transpiles but misbehaves, the fastest route is a differential
one: shrink it until one line differs, then run that line under the interpreter,
under `--target=js`, and under Rakudo. Two of the three agreeing tells you which
one to fix — and it is not always the transpiler. `showcase/perl/perl.raku`
carried a `my @caps = %m<caps>` that only worked because the rakupp interpreter
got the single-argument rule wrong there; Rakudo and the JavaScript backend both
read that hash element as one itemized array, which is the correct reading, and
`@(…)` around it is the spelling that works everywhere.

---

## See also

- **[JS.md](JS.md)** — the reference: every flag, the value table, the core's
  exact boundary, `use js` interop, `react`/`supply`/`Channel`, and the browser.
- **[CLI.md](CLI.md#choosing-a-backend)** — `--target=js` next to the other four
  backends.
- **[cookbook/cli.md](../cookbook/cli.md)** — `sub MAIN` at length: options,
  types, subcommands, and the traps.
- **`showcase/eclipse`, `showcase/fourier`, `showcase/orbits`** — the same
  engine driving a terminal program and an interactive page, built by
  `tools/build.raku` in three steps.
