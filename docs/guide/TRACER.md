# When something dies — the tracer

An uncaught error in Raku++ tells you three things: what went wrong, where it
happened, and how the program got there.

```
boom in baz with 2
  in method Foo::baz at t1.raku line 3
      3 |     method baz($x) { die "boom in baz with $x" }
  in method Foo::bar at t1.raku line 2
  in sub helper at t1.raku line 7
  in block <unit> at t1.raku line 10
```

Two rules shape all of it. **The message is line 1, byte for byte** — a script
that reads the first line with `head -1`, or greps for it, keeps working. And
**a frame line is one place you could go and look**: what was running, the file
it was declared in, and the line executing there.

## Reading a trace

Frames run innermost first. The top frame is where the error was raised; the
bottom is the program's own mainline, spelled `block <unit>`.

Each frame names what kind of thing it is — `sub`, `method`, `submethod`,
`regex`, or `block` for a bare one. A method also carries its declaring class:

```
  in method Foo::baz at t1.raku line 3
```

Rakudo prints a bare `in method baz` here. In a program where six classes each
declare a `new`, that frame does not say which `new` ran, so the class goes in.
It sits inside the name, ahead of the ` at `, so anything that parses these
lines by splitting on ` at ` is unaffected.

The frame the error actually came from also shows its source line:

```
      3 |     method baz($x) { die "boom in baz with $x" }
```

One line, for one frame. Every frame would be four times the height for no more
orientation. If the source cannot be read — a compiled `--exe` binary, a module
that has moved, a string that was `EVAL`ed — the excerpt is quietly skipped and
the frame line stands alone.

A typed exception names its type on a line of its own, because that is what you
need in order to write a `CATCH`:

```
No such method 'nonexistent-method' for invocant of type 'Int'
  (X::Method::NotFound)
  in sub g at t3.raku line 1
```

`X::AdHoc` — what a plain `die "message"` raises — is not printed, since it says
nothing the message does not.

Paths are shown the way you would type them. The program appears as you invoked
it, a module under the working directory appears relative to it, and anything
further away appears in full.

### Recursion does not fill the screen

A run of identical frames folds:

```
bottom
  in sub fact at rec.raku line 1
      1 | sub fact($n) { $n == 0 ?? die "bottom" !! 1 + fact($n - 1) }
  in sub fact at rec.raku line 1
  in sub fact at rec.raku line 1
  ... 198 more frames of sub fact
  in block <unit> at rec.raku line 2
```

Very long traces are also capped, with a line saying how many frames were left
out and which knob shows them.

## Errors with two positions

Some errors happen in one place and are noticed in another. Both are printed,
and the frames always lead with the place the error came *from*.

A **`fail`** creates a Failure that lies dormant until something uses the value:

```
bad 5
  in sub inner at f2.raku line 1
      1 | sub inner($x) { fail "bad $x" }
  in sub outer at f2.raku line 2
  in block <unit> at f2.raku line 3

Actually thrown at:
  in block <unit> at f2.raku line 5
```

The first block is where the Failure was made, which is where the error is. The
labelled block is the line that used the value. This layout is Rakudo's.

An exception from inside a **`start`** block reads the same way. The frames are
the worker's, and the label marks the `await` that collected it:

```
in worker
  in sub work at aw.raku line 1
  in block  at aw.raku line 2

Awaited at:
  in block <unit> at aw.raku line 3
```

Rakudo prints this one differently, leading with a heading and indenting the
message inside a second section. Raku++ keeps the message on line 1 and the
frames at the left margin, as it does for every other error.

A **`warn`** gets one frame, the line it was warned from, and the program
carries on:

```
careful
  in sub deep at w2.raku line 1
```

A **syntax error** shows the line it is complaining about:

```
===SORRY!=== Parse error at line 2: Missing required term after infix
      2 | say $x +;
```

## Turning it up, and off

| Knob | Meaning |
|---|---|
| `--ll-exception` | every frame, nothing folded, no limit — Rakudo's flag |
| `RAKUPP_BACKTRACE=full` | the same, as an environment variable |
| `RAKUPP_BACKTRACE=short` | the default |
| `RAKUPP_BACKTRACE=0` | the message alone, no frames anywhere |
| `NO_COLOR` | no ANSI colour |
| `RAKUPP_COLOR=0` / `=1` | force colour off or on |

Colour is used only when standard error is a terminal, and the text is
otherwise identical — the same bytes, minus the escapes. `RAKUPP_BACKTRACE=0`
is the escape hatch for a test whose golden output was recorded before any of
this existed; it silences the frames under `warn` too.

Frames also appear in the machine-readable form. With
`RAKU_EXCEPTIONS_HANDLER=JSON`, an uncaught exception serializes with a
`backtrace` array of `{file, line, subname}` objects.

## From inside a program

A caught exception carries the same information, and reports where it was
**thrown** rather than where you asked:

```raku
sub inner { die "thrown here" }   # line 1
sub outer { inner() }             # line 2
try { outer() }                   # line 3

say $!.backtrace.list[0].subname;  # inner
say $!.backtrace.list[0].line;     # 1
say $!.backtrace.Str;              # the frame lines
```

| | |
|---|---|
| `$!.message` | the message |
| `$!.Str` | the message, alone |
| `$!.gist` | the message followed by the frame lines |
| `$!.backtrace` | the frames, as a `Backtrace` |
| `Backtrace.new` | the chain here and now, without throwing anything |
| `Backtrace.new($n)` | …dropping `$n` innermost frames, so a routine can report its caller |

A `Backtrace` is a list of `BacktraceFrame`s. Each answers `.file` (always the
full path, whatever the printed form shortened it to), `.line`, `.subname`
(`<unit>` for the mainline), and `.code`. The `Backtrace` itself stringifies to
the frame lines through `.Str`, `.gist`, `.nice` and `.concise`, and `.full`
adds the frames the short form hides.

`.gist` and `Backtrace.Str` are deliberately plainer than what the terminal
shows: no excerpt, no type line, no colour. They are strings that programs
print and compare, so the extras belong to the uncaught-error printer alone.

A `rethrow` keeps the original position. So does catching an exception and
throwing it again yourself: the first throw wins, and nothing later overwrites
it.

## What it costs

Nothing, on any program that does not fail. The chain is captured at the moment
of the throw and not before, so ordinary calls and returns are untouched.

Raising an error costs one reference-count bump per live frame, against a throw
that already costs microseconds.

`fail` is the one place with a measurable price, because Failures are created in
bulk — every failed coercion is one, and `"abc".Int` in a loop makes a great
many. Each one records where it was made. On a loop that does nothing else,
that is about 9% of the cost of making the Failure. Code that does something
with its failures will not notice.

## See also

- [CLI.md](CLI.md) — the rest of the command line.
- [LINT.md](LINT.md) — problems found without running the program at all.
