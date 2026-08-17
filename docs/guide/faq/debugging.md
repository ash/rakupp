# FAQ — when something goes wrong

What Raku++ gives you when a program misbehaves, and how to tell a bug in your
code from a bug in Raku++.

## The tools

```bash
rakupp --lint prog.raku      # static analysis: unused vars, unreachable code, …
rakupp --ast  prog.raku      # the parsed AST, as an indented tree
rakupp --cpp  prog.raku      # the C++ that --exe would generate (add -O)
RAKUPP_DUMPTOKENS=1 rakupp prog.raku   # the lexer's token stream
```

`--lint` is the one to reach for first. It parses without running, so it is
instant and safe on a program you are not sure about:

```
$ rakupp --lint prog.raku
prog.raku:1: note: parameter '$b' is never used [unused-parameter]
prog.raku:2: warning: '$never' is declared but never used [unused-variable]
rakupp --lint: 1 warning, 1 note in prog.raku
```

It exits non-zero if anything was found, so it drops into a pre-commit hook or CI
unchanged. Full rule list: [LINT.md](../LINT.md).

## `die` does not print a backtrace

Today a `die` prints its message and nothing else:

```
$ rakupp -e 'sub f($x) { die "boom: $x" }; f(3)'
boom: 3
```

Rakudo also names the frames (`in sub f at … line 1`). Raku++ does not yet. Until
it does, the practical substitutes are:

**Catch it and print what you know.**

```raku
sub risky($x) { die "boom: $x" }
try { risky(3) }
with $! { say "failed: ", .message, " (", .^name, ")" }
```

**Add your own frame markers** where a stack would have helped:

```raku
sub outer() { note "→ outer"; inner() }
sub inner() { note "→ inner"; die "here" }
```

`note` writes to stderr, so it stays out of your program's output and survives
being piped.

## Telling your bug from ours

Raku++ aims to match Rakudo exactly. When output differs and you have both
installed, that comparison *is* the test:

```bash
rakupp prog.raku > a.txt
raku   prog.raku > b.txt
diff a.txt b.txt
```

A difference is worth reporting — that is the project's whole measure of
correctness. Reduce it first: cut the program down until removing one more line
makes the difference go away. A three-line repro gets fixed; a 300-line program
usually does not.

If it only differs **compiled**, that narrows it a great deal — the code
generator is a separate implementation of the language, so a divergence there is
its own class of bug:

```bash
rakupp prog.raku > a.txt
rakupp --exe prog.raku -o prog && ./prog > b.txt
diff a.txt b.txt
```

## Things that surprise people

**A missing semicolon may not be reported.** Raku++'s parser is more permissive
than Rakudo's about statements running together across lines, so a program Rakudo
rejects may run here. If a program behaves oddly and you suspect a typo, running
it under Rakudo is a quick way to have the syntax checked strictly.

**Output appears all at once from a child process.** `shell('long-command')`
without `:out` buffers and echoes at exit rather than streaming — see
[shell.md](shell.md).

**A one-element list where you expected three.** Almost always itemisation, not a
bug — see [containers.md](containers.md).

## Reporting a bug

Issues: <https://github.com/ash/rakupp/issues>. What makes one immediately
actionable:

- the smallest program that shows it
- what Raku++ printed, and what Rakudo printed
- `rakupp --version` — it reports the commit, the date, the platform and
  the compiler, so that one line covers the whole environment question
- whether it happens interpreted, under `--exe`, or both

---

Back to the [FAQ index](README.md).
