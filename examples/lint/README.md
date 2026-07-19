# Linter demos

Raku++ ships a static analyzer. `rakupp --lint FILE` parses a program and
reports likely mistakes **without running it**, then exits non-zero if it found
any warnings (notes alone still exit 0). Full rule reference:
[docs/LINT.md](../../docs/LINT.md).

Each file here isolates one rule; [`kitchen-sink.raku`](kitchen-sink.raku) trips
several at once, and [`clean.raku`](clean.raku) is written to pass with nothing
reported (it exercises the interpolation / closure / topic-variable idioms a
naive linter tends to false-positive on).

```
$ rakupp --lint kitchen-sink.raku
kitchen-sink.raku:7: note: parameter '$currency' is never used [unused-parameter]
kitchen-sink.raku:10: note: 'return' as the final statement is redundant; the block's last value is returned automatically [redundant-return]
kitchen-sink.raku:17: warning: code after an unconditional flow statement on line 16 is never reached [unreachable-code]
kitchen-sink.raku:20: warning: lexical routine '&format-tax' is never called [unused-routine]
kitchen-sink.raku:26: warning: '$note' is declared but never used [unused-variable]
kitchen-sink.raku:28: warning: numeric '==' compares the string literal "free"; use eq/ne/lt/gt for string comparison [numeric-cmp-of-string]
kitchen-sink.raku:33: warning: '$seen' is assigned to itself [self-assignment]
kitchen-sink.raku:35: warning: condition is a constant; the branch always runs [constant-condition]
rakupp --lint: 6 warnings, 2 notes in kitchen-sink.raku
```

| File | Rule | Severity |
|------|------|----------|
| [unused-variable.raku](unused-variable.raku) | `unused-variable` — a `my`/`state` lexical that is never read | warning |
| [unused-routine.raku](unused-routine.raku) | `unused-routine` — a lexical `sub` that is never called | warning |
| [redeclaration.raku](redeclaration.raku) | `redeclaration` — a second `my` of the same name in one scope | warning |
| [unreachable-code.raku](unreachable-code.raku) | `unreachable-code` — a statement after `return`/`last`/`next`/`die`/`exit` | warning |
| [self-assignment.raku](self-assignment.raku) | `self-assignment` — `$x = $x` | warning |
| [constant-condition.raku](constant-condition.raku) | `constant-condition` — `if`/`unless` on a literal | warning |
| [numeric-cmp-of-string.raku](numeric-cmp-of-string.raku) | `numeric-cmp-of-string` — `==`/`<`/… against a non-numeric string literal | warning |
| [unused-parameter.raku](unused-parameter.raku) | `unused-parameter` — a signature parameter that is never used | note |
| [redundant-return.raku](redundant-return.raku) | `redundant-return` — an explicit `return` as a block's last statement | note |

**Notes vs. warnings.** *Notes* are advisory (an unused parameter is often a
deliberate callback/dispatch signature; a trailing `return` is a style choice),
so they don't affect the exit code. *Warnings* are things that are almost always
a bug. The rules are deliberately conservative — every one of these programs is
otherwise valid Raku, and the analyzer stays silent on the dynamic constructs
(string interpolation, `EVAL`, symbolic references) where it can't be sure.
