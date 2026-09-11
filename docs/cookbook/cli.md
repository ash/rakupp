# Cookbook — a command-line tool, from a script to a binary

A word-frequency counter, grown from a `sub MAIN` into a program with options,
a usage message, a standard-input mode, subcommands and, at the end, a single
executable file that needs no interpreter.

Every program on this page was run under Raku++ on macOS 15 (arm64), and the
output shown is what it printed.

## The whole tool

`MAIN` is the entry point, and its signature is the command line. A positional
parameter is an argument, a named parameter is an option, and the declarator
comments — `#|` above the routine, `#=` after a parameter — are the help text:

```raku
#!/usr/bin/env rakupp

# Without this, a named option is only recognised BEFORE the first positional
# argument: `wordfreq words.txt --top=3` would not match MAIN at all.
my %*SUB-MAIN-OPTS = :named-anywhere;

#| Count the commonest words in a text.
sub MAIN(
    Str  $file = '-',        #= file to read, or "-" for standard input
    Int  :$top = 10,         #= how many words to show
    Int  :$min-length = 1,   #= ignore words shorter than this
    Bool :$json = False,     #= print JSON instead of a table
) {
    my $text;
    if $file eq '-' {
        $text = $*IN.slurp;
    }
    else {
        unless $file.IO.f {
            note "wordfreq: $file: no such file";
            exit 2;
        }
        $text = $file.IO.slurp;
    }

    my $words = bag $text.lc.comb(/ <[\w']>+ /).grep(*.chars >= $min-length);
    my @top   = $words.sort({ -.value, .key }).head($top);

    if $json {
        require JSON::Fast <&to-json>;
        say to-json @top.map({ %( word => .key, count => .value ) });
    }
    else {
        say sprintf('%-14s %4d', .key, .value) for @top;
    }
}
```

The file is [cli/wordfreq.raku](cli/wordfreq.raku). Against a three-line
`words.txt`:

```sh
rakupp wordfreq.raku words.txt --top=5
```

```output
fox               4
the               4
a                 2
dog               2
quick             2
```

Four pieces of the signature are doing work that would otherwise be code.
`Str $file = '-'` makes the argument optional with a default. `Int :$top`
rejects `--top=abc` before the body runs. `Bool :$json` makes `--json` a flag
rather than something that takes a value. And every `#=` becomes a line of the
usage message.

## The usage message is free

Nothing in the program prints it. Run it with `--help`, or with arguments that
do not fit the signature, and Raku assembles it from the signature and the
declarator comments:

```sh
rakupp wordfreq.raku --help
```

```output
Usage:
  wordfreq.raku [--top=<Int>] [--min-length=<Int>] [--json] [<file>] -- Count the commonest words in a text.
  
    [<file>]              file to read, or "-" for standard input [default: '-']
    --top=<Int>           how many words to show [default: 10]
    --min-length=<Int>    ignore words shorter than this [default: 1]
    --json                print JSON instead of a table [default: False]
```

`--help` exits 0. A command line that does not match exits **2**, with the same
text — so a shell script calling this can tell "I asked for help" from "I got
the call wrong".

To write the message yourself, declare a `sub USAGE`; Raku calls it instead of
generating one, and the exit code is unchanged:

```raku
sub USAGE() {
    say "wordfreq [--top=N] FILE";
    say "  FILE   text to read, or - for standard input";
}
```

## Reading standard input

`'-'` as the default means the tool works in a pipeline with no arguments at
all, which is what makes it composable with everything else:

```sh
cat words.txt | rakupp wordfreq.raku --top=3 --min-length=4
```

```output
quick             2
barks             1
brown             1
```

`$*IN.slurp` reads the whole of standard input in one go; `for $*IN.lines { }`
hands the body one line at a time, which is the form to reach for when the
input is a log file rather than three lines of prose.

## Paying for a module only when it is used

`--json` is the only branch that needs a JSON library, and `require` loads one
at run time rather than at compile time:

```raku
require JSON::Fast <&to-json>;
say to-json @top.map({ %( word => .key, count => .value ) });
```

```sh
rakupp wordfreq.raku words.txt --top=2 --json
```

```output
[
  {
    "word": "fox",
    "count": 4
  },
  {
    "word": "the",
    "count": 4
  }
]
```

The `<&to-json>` part is what makes the symbol callable in the surrounding
code; `require JSON::Fast` alone loads the module and imports nothing. The
trade is real: a typo in the module name is now a run-time failure in one
branch instead of a compile-time one. Use it for the heavy optional dependency,
not for everything.

```sh
rakupp install JSON::Fast
```

## Failing usefully

Two conventions, both one line of code. Diagnostics go to standard error, so a
redirected stdout stays clean; and the exit code says what happened:

```raku
note "wordfreq: $file: no such file";
exit 2;
```

```sh
rakupp wordfreq.raku nope.txt ; echo "exit=$?"
```

```output
wordfreq: nope.txt: no such file
exit=2
```

`note` is `say` on `$*ERR`. `exit` ends the program at that point; `END` blocks
still run, so anything that has to happen on the way out belongs in one.

## Subcommands: one `multi MAIN` per verb

A literal string in the signature makes the verb part of the dispatch, so
`git`-style commands need no `given`/`when` at all:

```raku
#| add a note
multi MAIN('add', *@words) { ... }

#| list notes, open ones only unless --all
multi MAIN('list', Bool :$all = False) { ... }

#| mark a note done
multi MAIN('done', Int $n) { ... }
```

The whole program is [cli/notes.raku](cli/notes.raku) — a note keeper in 40
lines. Each candidate gets its own line in the usage message:

```sh
rakupp notes.raku
```

```output
Usage:
  notes.raku add [<words> ...] -- add a note
  notes.raku [--all] list -- list notes, open ones only unless --all
  notes.raku done <n> -- mark a note done
```

A session:

```sh
rakupp notes.raku add buy milk
rakupp notes.raku add write the cookbook
rakupp notes.raku list
rakupp notes.raku done 1
rakupp notes.raku list
rakupp notes.raku list --all
```

```output
added 1: buy milk
added 2: write the cookbook
 1  [ ] buy milk
 2  [ ] write the cookbook
done 1
 2  [ ] write the cookbook
 1  [x] buy milk
 2  [ ] write the cookbook
```

`*@words` is why `add buy milk` needs no quoting — the slurpy takes whatever is
left. `Int $n` on the `done` candidate means `done x` never reaches the body:
it simply does not match, and the reader gets the usage message.

An unknown verb behaves the same way. `notes.raku frobnicate` matches no
candidate, so it prints the usage and exits 2, without a line of argument
checking in the program.

## Turning it into a binary

`--exe` compiles the program to C++ and then to a native executable, which
carries its own runtime and the modules it uses:

```sh
rakupp --exe wordfreq.raku -o wordfreq
```

```output
--exe: embedded 1 module: JSON::Fast
Compiled (native) wordfreq.raku -> wordfreq
```

`JSON::Fast` is pulled in even though it is loaded with `require`: the compiler
follows what the program can reach, not what this run happened to use. The
result behaves the same, including standard input and the exit codes:

```sh
./wordfreq words.txt --top=3
cat words.txt | ./wordfreq --top=2
./wordfreq nope.txt ; echo "exit=$?"
```

```output
fox               4
the               4
a                 2
fox               4
the               4
wordfreq: nope.txt: no such file
exit=2
```

What it buys, measured on this machine over 100 runs of the same command:

| | per run | on disk |
|---|---|---|
| `rakupp wordfreq.raku …` | 3.0 ms | — |
| `./wordfreq …` | 2.6 ms | 9.9 MB |

For a script this size the startup difference is under half a millisecond —
the reason to build the binary is that it runs on a machine with no interpreter
and no module store, not that it is faster. The binary is also where the size
goes: 9.9 MB of runtime for 30 lines of program.

One thing does change. The usage message of a compiled program names the source
file it was built from, with the path it had at build time, and drops the `#|`
description:

```output
Usage:
  /path/to/wordfreq.raku [--top=<Int>] [--min-length=<Int>] [--json] [<file>]
```

If the usage text matters in the shipped tool — and for a tool it does — write
`sub USAGE` and print exactly what you want, which is unaffected.

## Four things that bite

**A named option after a positional argument is not seen.** This is the first
one everybody hits:

```sh
rakupp wordfreq.raku words.txt --top=3     # usage message, exit 2
rakupp wordfreq.raku --top=3 words.txt     # works
```

By default Raku stops treating arguments as options at the first positional, so
`--top=3` is read as a second positional and nothing matches. One line at the
top of the file fixes it for every candidate in the program:

```raku
my %*SUB-MAIN-OPTS = :named-anywhere;
```

**`--top 3` is not `--top=3`.** With a space, `--top` is a flag and `3` is a
positional argument. The value has to be attached with `=`. This is the same
failure as the last one and looks identical — a usage message and exit 2 —
which is why both are worth knowing at once.

**A wrong type is a usage message, not an error.** `--top=abc` does not reach
the body and does not produce a message about `abc`; the candidate simply does
not match, and the reader sees the usage text. The type constraint is the
validation, and the usage text is the error message, so `#=` comments are worth
writing for that reason alone.

**`#|` has to touch the routine.** A declarator block comment documents the
declaration that follows it *immediately*. At the top of a file, above a `my`
or a blank line, it attaches to nothing and the usage message silently loses
the description. Put it on the line above `sub MAIN`.

## What to reach for next

- Inside `sub USAGE`, `$*USAGE` holds the message Raku would have generated, so
  a custom usage can print your own header and then the generated option list
  rather than restating it.
- A parameter can have a short alias: `Int :t(:$top)` accepts `--top=3` and
  `-t=3`.
- `MAIN` can take `*%named` or `*@positionals` to accept what the signature did
  not name, which is how a wrapper passes arguments through to another program.
