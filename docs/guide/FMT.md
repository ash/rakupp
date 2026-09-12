# `--fmt` — the source formatter

```bash
rakupp --fmt prog.raku            # formatted source to stdout
rakupp --fmt -i prog.raku …       # rewrite in place (-i.bak keeps backups)
rakupp --fmt --check prog.raku …  # name files that would change; exit 1 if any
rakupp --fmt --diff prog.raku …   # …and show what would change
```

The promise is gofmt's: a tool you run on every save without reading its
output. **Zero configuration** — no indent width, no rule toggles, no config
file. One style, because the value of a formatter is the arguing it ends.

## What it does

| rule | |
|---|---|
| **R1** | indentation is `4 × bracket depth`; a *continuation* line is shifted with its statement rather than re-columned, so a chained-method ladder or an aligned argument list keeps its shape |
| **R2** | trailing whitespace goes |
| **R3** | exactly one newline at end of file |
| **R4** | `else` / `elsif` / `orwith` / `without` move to their own line, at the `if`'s indent |
| **R5** | at least one space after a `,` and around a `=>` — a **minimum**, never a maximum: runs of spaces are never shrunk, so hand-aligned tables survive |
| **R6** | runs of three or more blank lines collapse to two; one and two are yours |

Before, and what `rakupp --fmt` prints — every rule above except R6, in one
eight-line program:

```raku
sub classify($n) {                  sub classify($n) {
if $n < 2 {                             if $n < 2 {
return "small";                             return "small";
} else {                                }
my @tags = "big","wide";                else {
return @tags.join(",");                     my @tags = "big", "wide";
}                                           return @tags.join(",");
}                                       }
say classify(1);                    }
                                    say classify(1);
```

Note what did *not* change: the `,` inside `.join(",")` is a string, so R5 left
it alone.

## What it will not do

Raku's whitespace is **semantically significant** — `say (1,2)` and `say(1,2)`
are different programs, `%h<key>` and `%h < key` are different programs, `$x++`
parses and `$x ++` does not. So the formatter edits only the whitespace
*between* classified spans and never the bytes inside one, and a long list of
places is deliberately out of reach:

* inside a string, a regex, a comment, POD, or a heredoc body — those bytes are
  yours, including the indentation of a heredoc's text and of a grammar's rule
  bodies (where whitespace can be significant: a `rule` is `:sigspace`);
* whitespace next to `(` `[` `{` `<`, around `.` and the postfix/prefix
  operators, around `:` in a colonpair, and an unspace `\ `;
* line wrapping, quote normalisation, signature layout and comment reflow are
  not v1 rules at all.

R5 in particular asks the lexer which bytes are a `,` or a `=>` **token**, not
which bytes spell one. `1 <=> 2` and `@a ==> sum()` keep their operators, a
comma inside `<a 1,2 b>` is a word and not punctuation, and a fat arrow glued
to a zip or cross metaoperator (`1,2 X=> 3,4`) is left exactly as written —
there the space is the difference between a metaoperator and a syntax error.

`say( "abc") ;` therefore comes back unchanged: the space after `(` and the one
before `;` are not the formatter's to touch.

## The guarantees

Every run, before a byte is written:

1. **it parses** — a file that does not parse is not formatted (exit 3). There
   is no best-effort tidying of broken code;
2. **it is the same program** — the formatted text is re-parsed and compared to
   the input as a canonical blob with line numbers stripped, so moving lines is
   allowed and changing meaning is not;
3. **it is stable** — formatting the result again changes nothing.

Gates 2 and 3 are backstops that should never fire. If one does, `--fmt` writes
nothing, prints an internal-error banner and exits 5: that is a bug in rakupp,
and the file it happened on is the bug report.

## Exit codes

| code | |
|---|---|
| 0 | done (or, with `--check`/`--diff`, nothing to do) |
| 1 | `--check`/`--diff` found work |
| 3 | the input does not parse |
| 4 | usage |
| 5 | a gate refused — please report it |

## Known limits

* **A file whose syntax comes from a module cannot be formatted.** If a program
  uses an operator its module exports, the parser cannot read it standalone, so
  `--fmt` refuses it exactly as `-c` does. `-I` does not help: the formatter
  parses without loading modules.
* **A regex literal written across several lines is not formatted — the whole
  file is refused.** The scanner colours a regex's *parts* and leaves the rest
  as code, so indentation reaches inside a multi-line `/.../` or a grammar rule
  body, and that changes the pattern. The gate catches it every time and writes
  nothing; the file comes back untouched with exit 5.

  Measured: **11 files of 2,434** over three corpora — this repo (1 of 757),
  raku-corpus (0 of 954) and every installed ecosystem module (10 of 723). All
  eleven are grammar- or regex-heavy modules. It is a known gap, not a mystery:
  don't file it.
