# FAQ

Short, task-shaped answers to questions people actually ask — "how do I …",
"why does this print twice", "does Rakudo do the same".

These are not a tutorial and not a reference. [RECIPES.md](../RECIPES.md) has
runnable one-liners by topic; [REFERENCE.md](../REFERENCE.md) has every operator,
sub and method. This is for the questions in between, where the answer is a
paragraph and a caveat rather than a signature.

**Every snippet is run on both Raku++ and Rakudo and produces identical output.**
Where the two genuinely differ, the page says so and explains why — those
differences are the most useful thing here, since they are what a Rakudo user
trips over.

## Pages

- **[shell.md](shell.md)** — running external commands: `run` vs `shell`,
  capturing output, feeding input, exit codes, and what a `Proc` shows you.
- **[buffering.md](buffering.md)** — why output does not appear when you
  expect it: `.out-buffer` and `.flush`, the three places output can sit (your
  own handles, a file you opened, a child's stdout), the child's own buffer
  that you cannot set from outside, and how to tell which one is holding it.
- **[background-processes.md](background-processes.md)** — `Proc::Async`:
  fire-and-forget processes that outlive the program (and where Raku's own
  documentation stands on that), when `.start` spawns, taps, `bind-stdin`
  pipelines, `.kill`, and reading the exit status.
- **[containers.md](containers.md)** — "why does my list have one element?":
  itemisation, `$(…)` vs `[…]`, when you need `@(…)`, and passing a list to a
  routine.
- **[modules.md](modules.md)** — installing and finding modules: `zef install`
  or `rakupp install` and then just `use` it, the places searched (the failure
  message is the list), pointing `-I` at a store somewhere else, what the
  SHA-named files are, why "Could not find Foo" is usually a distribution name,
  and running a module you are still writing.
- **[compiling.md](compiling.md)** — turning a program into a binary: `--exe`
  vs `--aot` vs `--bundle`, what `-O` buys, and why `--exe` needs a C++ compiler
  on the machine that runs it.
- **[optimizer.md](optimizer.md)** — why `-O` is not the default: what a
  default owes every program, the measured case where `-O` loses, and how a
  pass graduates into the default when it stops being speculation.
- **[performance.md](performance.md)** — "my program is slow": what compiling
  does and does not speed up, with measured numbers, and the things that are slow
  in any Raku.
- **[garbage-collection.md](garbage-collection.md)** — there isn't one:
  `shared_ptr` refcounting, what that buys (a 1.5 MB floor, no stop-the-world
  pause) and what it costs (cycles are never reclaimed, and the free is on your
  clock), when `DESTROY` actually runs, why a dropped filehandle is not closed,
  and how to tell a leak from a materialised list.
- **[debugging.md](debugging.md)** — when something goes wrong: `--lint`,
  `--ast`, `--cpp`, telling your bug from ours, and what to put in a report.
- **[differences.md](differences.md)** — where Raku++ and Rakudo differ, in both
  directions: what Raku++ does that Rakudo does not, where Rakudo is ahead, and
  the handful you will actually run into.
- **[6e.md](6e.md)** — what the 6.e language revision adds to 6.d, and what
  `use v6.e.PREVIEW` actually turns on: new syntax, subs and methods, the
  behaviour changes that bite, the new compile-time errors — each with both
  outputs, plus where Raku++ matches and where it does not.
- **[hand-written.md](hand-written.md)** — "hand-written lexer and parser"
  vs. written by a human: what the compiler term of art claims (no parser
  generator — the sense GCC, Clang and Go use of themselves, with receipts),
  and who wrote this code.

## Adding one

Answer a question someone actually asked, in the shape they asked it. Verify
every snippet against both engines before it goes in — a FAQ that is wrong is
worse than no FAQ, because it is what people copy. If Raku++ and Rakudo differ,
say so plainly rather than quietly writing to whichever one is convenient.
