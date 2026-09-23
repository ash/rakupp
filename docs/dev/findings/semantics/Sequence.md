# The sequence operator `...` — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Rakudo/SEQUENCE.rakumod`
(367 lines, read in full) and the `infix:<...>`, `...^`, `^...`, `^...^`
candidates in `src/core.c/operators.rakumod` (lines 134–207). The Range
candidates of `..` are in [Range.md](Range.md); the Seq object itself in
[List-Array.md](List-Array.md). Oracle: Homebrew Rakudo v2026.08 on macOS.
Compared against Raku++ 4.0.1-84-ga4291988 (build-arm64, 2026-09-23).
Format and legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes all 7 files of
`S03-sequence`; Raku++ passes 1 (`arity0.t`). The rules below are what the
other six test: what the right-hand list means, which seeds are checked
against the endpoint, how a step or ratio is deduced and from which seeds,
where a numeric sequence stops, what a generator receives and may return,
how strings walk, and how chains like `1 ... 5 ... 1` join. 13 of
the 29 items are neither fully documented nor fully asserted by Roast.

In the outputs a `|` stands for a newline (the probes were run through
`tr '\n' '|'`); two outputs are the text of an uncaught exception, which is
the observation.

## A. Shape of the operator

### SQ-01  Four spellings, the ellipsis, first and last exclusion      D:yes R:yes V:spec
`...` and `…` are the same routine; `...^` drops the element that matched
the endpoint, `^...` drops the first element of the whole result, `^...^`
both. The result is always a Seq. `1 ... 1` is `(1,)`, and each exclusion
of it is empty.
```
say (1 ... 5).^name, " ", (1 ... 5).raku, " ", (1 ...^ 5).raku, " ", (1 ^... 5).raku, " ", (1 ^...^ 5).raku, " ", (1 … 3).raku, " ", (1 …^ 3).raku, " ", (1 ^… 3).raku, " ", (1 ^…^ 3).raku, " ", (5 ... 1).raku, " ", (5 ...^ 1).raku, " ", (5 ^...^ 1).raku, " ", (1 ... 1).raku, " ", (1 ...^ 1).raku, " ", (1 ^... 1).raku, " ", (&infix:<…> === &infix:<...>), " ", (1 ... 3).WHAT.raku
# rakudo 2026.08: Seq (1, 2, 3, 4, 5).Seq (1, 2, 3, 4).Seq (2, 3, 4, 5).Seq (2, 3, 4).Seq (1, 2, 3).Seq (1, 2).Seq (2, 3).Seq (2,).Seq (5, 4, 3, 2, 1).Seq (5, 4, 3, 2).Seq (4, 3, 2).Seq (1,).Seq ().Seq ().Seq True Seq
```
rakupp 4.0.1-84: differs — `&infix:<…>` is a separate routine object (`===` is False); everything else matches.

### SQ-02  Laziness                                                  D:partial R:yes V:spec
An endpoint of `*`, `Inf` or `∞` (also via a variable) makes the Seq
lazy; a numeric endpoint, a code endpoint and a generator with a numeric
endpoint give a non-lazy Seq, even though a code endpoint cannot be
reified early. `.elems` of a lazy sequence is a Failure `X::Cannot::Lazy`
(action `.elems`); `.gist` of it is `(...)`; a lazy sequence assigned to
an array keeps the array lazy; `map` and `grep` over it stay lazy.
```
say (1 ... *).is-lazy, " ", (1 ... Inf).is-lazy, " ", (1 ... ∞).is-lazy, " ", (1 ... 5).is-lazy, " ", (1 ... { $_ > 3 }).is-lazy, " ", (1, { $_ + 1 } ... *).is-lazy, " ", (1, { $_ + 1 } ... 5).is-lazy, " ", (1, 2, 3 ... *).is-lazy, " ", do { my $w = *; (1 ... $w).is-lazy }, " ", do { my $e = Inf; (1 ... $e).is-lazy }, " ", (1 ... *)[^4].raku, " ", (1 ... Inf).head(3).raku, " ", (1 ... *).elems.raku, " ", (1 ... *).gist, " ", do { my @a = 1 ... *; @a.is-lazy ~ " " ~ @a[4] }, " ", (1 ... *).map(* * 2).is-lazy, " ", (1 ... *).grep(* %% 3).head(2).List.raku
# rakudo 2026.08: True True True False False True False True True True (1, 2, 3, 4) (1, 2, 3).Seq Failure.new(exception => X::Cannot::Lazy.new(action => ".elems", what => ""), backtrace => Backtrace.new) (...) True 5 True (3, 6)
```
rakupp 4.0.1-84: differs — a generator with a numeric endpoint is reported lazy (`(1, { $_ + 1 } ... 5).is-lazy` True), and the `.elems` Failure prints as a hash.

### SQ-03  The right-hand list: endpoint first, the rest appended      D:no R:yes V:spec
The first element of the right-hand list is the endpoint; every further
element is appended after the sequence as it is, whatever its type. A
scalar holding a list is iterated the same way (`$l = (3, 9)` gives the
endpoint 3 and the tail 9). A bare parenthesised list on the right is
**one** endpoint, a List, which never smartmatches a number, so the
sequence runs on; likewise a List on the left is the seeds, one per
element.
```
say (1 ... 3, 10, 20).raku, " ", (1 ... 3, 'x').raku, " ", (1 ... 10, 4).raku, " ", (1 ... "3", "z").raku, " ", ('aa' ... 'cc', 'zz').raku, " ", (1 ... 3..5, 8).raku, " ", do { my $e = 4; (1 ... $e).raku }, " ", do { my $l = (3, 9); (1 ... $l).raku }, " ", (1 ... (3, 9)).head(5).List.raku, " ", ((1, 2) ... (5, 6)).head(4).List.raku, " ", (1 ... 5 ... 1, 10).raku
# rakudo 2026.08: (1, 2, 3, 10, 20).Seq (1, 2, 3, "x").Seq (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 4).Seq (1, 2, 3, "z").Seq ("aa", "ab", "ac", "ba", "bb", "bc", "ca", "cb", "cc", "zz").Seq (1, 2, 3, 8).Seq (1, 2, 3, 4).Seq (1, 2, 3, 9).Seq (1, 2, 3, 9) (1, 2, 3, 4) (1, 2, 3, 4, 5, 4, 3, 2, 1, 10).Seq
```
rakupp 4.0.1-84: differs — `'aa' ... 'cc', 'zz'` walks every two-letter string instead of the cross product; a Range endpoint counts down to 0 (`1 ... 3..5, 8` gives `(1, 0, 8)`); a scalar holding `(3, 9)` and a bare `(3, 9)` both stop at 2.

### SQ-04  Empty sides and a Failure endpoint                          D:no R:partial V:spec
An empty right-hand list throws `X::Cannot::Empty` (action "get sequence
endpoint"); an empty left-hand list throws `X::Cannot::Empty` (action "get
sequence start value") at reification. A Failure as endpoint throws the
exception it wraps. `X::Cannot::Empty` has `.action` and `.what`.
```
say (try (1 ... ()).eager.raku) // $!.^name ~ ":" ~ $!.message, " ", (try (1 ... Empty).eager.raku) // $!.^name, " ", (try (() ... 5).eager.raku) // $!.^name ~ ":" ~ $!.message, " ", (try (() ... *).eager.raku) // $!.^name, " ", (try (1 ... fail("f")).eager.raku) // $!.^name ~ ":" ~ $!.message, " ", X::Cannot::Empty.^mro.map(*.^name).raku, " ", (try X::Cannot::Empty.new(:action<a>, :what<b>).message) // $!.^name
# rakudo 2026.08: X::Cannot::Empty:Cannot get sequence endpoint from an empty list (use * or :!elems instead?) X::Cannot::Empty X::Cannot::Empty:Cannot get sequence start value from an empty list X::Cannot::Empty X::AdHoc:f ("X::Cannot::Empty", "Exception", "Any", "Mu").Seq Cannot a from an empty b
```
rakupp 4.0.1-84: differs — `1 ... ()` gives `(1, 0)`, `() ... 5` gives `()`, and a bare `fail` at the top level dies ("Attempt to return outside of any Routine"), which killed the line there.

### SQ-05  The endpoint is smartmatched                                D:partial R:partial V:spec
A non-code endpoint stops the sequence at the first value it `ACCEPTS`:
a Str matches by string equality (`1 ... "3"` stops at 3, `'1' ... 3`
walks strings), a Regex by match, a Range by containment, `any` by
autothreading. A type object, `Nil`, `True` and `False` all accept the
first value, so the result is the first seed alone; `Str` as the endpoint
of an Int sequence never matches. A non-integral numeric endpoint is a
bound only: `1 ... 3.5` gives 1, 2, 3 and `1 ... 0.5` gives `(1,)`. The
endpoint's type never changes the element types (`1, 2 ... 4e0` is Ints).
```
say (1 ... "3").raku, " ", ('1' ... 3).raku, " ", (1 ... /3/).raku, " ", ("aa" ... /ac/).raku, " ", (1 ... 3..5).raku, " ", (1 ... any(3, 5)).raku, " ", (1 ... any(1, 2)).is-lazy, " ", (1 ... all(1, 2)).head(3).List.raku, " ", (1 ... Int).raku, " ", (1 ... Str).head(4).List.raku, " ", (try ('a' ... Str).head(3).List.raku) // $!.^name, " ", (1 ... Any).head(4).List.raku, " ", (1 ... Nil).head(4).List.raku, " ", (1 ... True).head(4).List.raku, " ", (1 ... False).head(4).List.raku, " ", (1 ... 3.5).raku, " ", (1 ... 2.5).raku, " ", (1 ... 1.5).raku, " ", (1 ... 0.5).raku, " ", (1, 2, 4 ... 4.5).raku, " ", (1, 2, 4 ... 4.0).raku, " ", (1, 2, 4 ... 4.0).map(*.^name).raku, " ", (1, 2 ... 4e0).raku, " ", (1, 2 ... 4e0).map(*.^name).raku
# rakudo 2026.08: (1, 2, 3).Seq ("1", "2", "3").Seq (1, 2, 3).Seq ("aa", "ab", "ac").Seq (1, 2, 3, 4, 5).Seq (1, 2, 3).Seq False (1, 2, 3) (1,).Seq (1, 2, 3, 4) ("a",) (1,) (1,) (1,) (1, 2, 3, 4) (1, 2, 3).Seq (1, 2).Seq (1,).Seq (1,).Seq (1, 2, 4).Seq (1, 2, 4).Seq ("Int", "Int", "Int").Seq (1, 2, 3, 4).Seq ("Int", "Int", "Int", "Int").Seq
```
rakupp 4.0.1-84: differs — a non-numeric, non-string endpoint is treated as 0 (`1 ... /3/`, `1 ... Int`, `1 ... Any`, `1 ... Nil`, `1 ... False` all give `(1, 0)`), `1 ... any(3, 5)` stops at 2, `'1' ... 3` produces Nums after the first string.

### SQ-06  A `none` junction endpoint is taken for infinity            D:no R:no V:bug
`1 ... none(1, 2)` is a lazy, endless sequence: the endpoint is compared
with `Inf` by `===`, and `none(...) === Inf` autothreads to True, so the
endpoint is replaced by "infinite" and never consulted. The language's
intent (an endpoint smartmatched against each value) would stop at 3.
Recorded so that nobody imitates it.
```
say (1 ... none(1, 2)).is-lazy, " ", (1 ... none(1, 2)).head(5).List.raku, " ", (1 ... none(5)).head(3).List.raku, " ", (none(1, 2) === Inf), " ", (any(1, 2) === Inf)
# rakudo 2026.08: True (1, 2, 3, 4, 5) (1, 2, 3) none(False, False) any(False, False)
```
rakupp 4.0.1-84: differs — not lazy, gives `(1, 2)`; neither Rakudo's behaviour nor the intended one.

### SQ-07  Seeds are checked against the endpoint first               D:yes R:yes V:spec
Every seed is tested against the endpoint before any deduction: the
sequence ends at the first seed that matches, later seeds are dropped, and
with `...^` the matching seed is dropped too. A code endpoint with arity n
is consulted as soon as n seeds exist and receives the last n values.
```
say (1, 2, 3 ... 2).raku, " ", (1, 2, 3 ...^ 2).raku, " ", (1, 2, 3 ^... 2).raku, " ", (1, 2, 4 ...^ 4).raku, " ", (1, 2, 3 ... 1).raku, " ", (1, 2, 3 ...^ 1).raku, " ", (1, 2, 3 ... { $_ == 2 }).raku, " ", (1, 2, 3 ...^ { $_ == 2 }).raku, " ", (1, 2, 3, 4, 5 ... { $^a + $^b == 3 }).raku, " ", (1, 2, 3, 4, 5 ... { $^a + $^b + $^c == 9 }).raku, " ", (1, 2, 3, 4, 5 ...^ { $^a + $^b + $^c == 9 }).raku, " ", (1, 1, 1 ... 1).raku, " ", (5, 4 ... 5).raku, " ", (1, 2, 4, 7 ... 2).raku, " ", (1, 2, 4, 7 ... 7).raku, " ", (1, 2, 4, 7 ...^ 7).raku, " ", (2, 2, 2 ... 2).raku
# rakudo 2026.08: (1, 2).Seq (1,).Seq (2,).Seq (1, 2).Seq (1,).Seq ().Seq (1, 2).Seq (1,).Seq (1, 2).Seq (1, 2, 3, 4).Seq (1, 2, 3).Seq (1,).Seq (5,).Seq (1, 2).Seq (1, 2, 4, 7).Seq (1, 2, 4).Seq (2,).Seq
```
rakupp 4.0.1-84: differs — deduction runs before the seeds are checked, so `1, 2, 4, 7 ... 2` throws `X::Sequence::Deduction` instead of giving `(1, 2)`; the line died there.

### SQ-08  Generator position, generator alone, more than three seeds  D:partial R:yes V:spec
A code object among the seeds is the generator and ends the seed list:
values after it are ignored. A generator with no seeds is called with no
arguments (`{ 7 } ... *` is 7, 7, 7, …). When more than three seeds are
given, all but the last three are emitted as they are and only the last
three are used to deduce the step; deduction failure names those three.
```
say (1, { $_ + 10 }, 99, 98 ... 31).raku, " ", ({ 7 } ... *).head(3).List.raku, " ", (-> { 42 } ... *).head(3).List.raku, " ", (1, -> { 42 } ... *).head(4).List.raku, " ", (1, 5, 2, 4, 6 ... 12).raku, " ", (9, 8, 7, 1, 2, 3 ... 6).raku, " ", (1, 2, 3, 4, 5, 6 ... 8).raku, " ", (1, 2, 3, 4, 5, 6 ...^ 8).raku, " ", (1, 2, 4, 8, 16 ... 4).raku, " ", (1, 2, 4, 8, 16 ... 3).head(7).List.raku, " ", (1, 2, 4, 8, 16, 32 ...^ 64).raku, " ", (1, 2, 4, 8, 16 ...^ 16).raku, " ", (try (1, 3, 5, 7, 9, 12 ... 24).raku) // $!.^name ~ ":" ~ $!.from, " ", (try (1, 2, 3, 4, 5, 8 ... 20).raku) // $!.^name ~ ":" ~ $!.from, " ", (try (1, 2, 4, 7, 11 ... 16).raku) // $!.^name ~ ":" ~ $!.from
# rakudo 2026.08: (1, 11, 21, 31).Seq (7, 7, 7) (42, 42, 42) (1, 42, 42, 42) (1, 5, 2, 4, 6, 8, 10, 12).Seq (9, 8, 7, 1, 2, 3, 4, 5, 6).Seq (1, 2, 3, 4, 5, 6, 7, 8).Seq (1, 2, 3, 4, 5, 6, 7).Seq (1, 2, 4).Seq (1, 2) (1, 2, 4, 8, 16, 32).Seq (1, 2, 4, 8).Seq X::Sequence::Deduction:7,9,12 X::Sequence::Deduction:4,5,8 X::Sequence::Deduction:4,7,11
```
rakupp 4.0.1-84: differs — a generator that is not the last left-hand element makes deduction fail over the whole list ("1,sub { ... },99,98"); the line died there.

## B. Deducing a step

### SQ-09  Three numeric seeds: arithmetic, constant, geometric         D:yes R:yes V:spec
Three Real seeds with equal differences give an arithmetic sequence (a
difference of 0 is a constant sequence); otherwise, if none is zero and
the ratios are equal, a geometric one, whose ratio is an Int when it is a
whole number and a Rat otherwise, so `81, 27, 9 ... 1` continues in Rats.
```
say (1, 3, 5 ... 11).raku, " ", (1, 3, 5 ... 12).raku, " ", (1, 3, 5 ... *).head(5).List.raku, " ", (10, 8, 6 ... 1).raku, " ", (10, 8, 6 ... -1).raku, " ", (1, 1, 1 ... *).head(4).List.raku, " ", (0, 0, 0 ... *).head(3).List.raku, " ", (0, 0, 0 ... 0).raku, " ", (1, 2, 4 ... 32).raku, " ", (1, 2, 4 ... 33).raku, " ", (1, 2, 4 ... *).head(6).map(*.^name).raku, " ", (1, 3, 9 ... 100).raku, " ", (81, 27, 9 ... 1).raku, " ", (81, 27, 9 ... 2).raku, " ", (4, 2, 1 ... *).head(5).List.raku, " ", (4, 2, 1 ... *).head(5).map(*.^name).raku, " ", (1, 1/2, 1/4 ... 1/32).raku, " ", (1, 0.5, 0.25 ... 0.03).raku, " ", (1, 1.5, 2.25 ... 4).raku, " ", (2, 1, 1/2 ... 1/8).raku, " ", (2, 1, 1/2 ... 1/8).map(*.^name).raku
# rakudo 2026.08: (1, 3, 5, 7, 9, 11).Seq (1, 3, 5, 7, 9, 11).Seq (1, 3, 5, 7, 9) (10, 8, 6, 4, 2).Seq (10, 8, 6, 4, 2, 0).Seq (1, 1, 1, 1) (0, 0, 0) (0,).Seq (1, 2, 4, 8, 16, 32).Seq (1, 2, 4, 8, 16, 32).Seq ("Int", "Int", "Int", "Int", "Int", "Int").Seq (1, 3, 9, 27, 81).Seq (81, 27.0, 9.0, 3.0, 1.0).Seq (81, 27.0, 9.0, 3.0).Seq (4, 2.0, 1.0, 0.5, 0.25) ("Int", "Rat", "Rat", "Rat", "Rat").Seq (1, 0.5, 0.25, 0.125, 0.0625, 0.03125).Seq (1, 0.5, 0.25, 0.125, 0.0625, 0.03125).Seq (1, 1.5, 2.25, 3.375).Seq (2, 1.0, 0.5, 0.25, 0.125).Seq ("Int", "Rat", "Rat", "Rat", "Rat").Seq
```
rakupp 4.0.1-84: differs — the seeds of a Rat-ratio sequence are emitted as given instead of regenerated (`(81, 27, 9, 3.0)`, see SQ-15), and `0, 0, 0 ... 0` gives three zeros instead of one.

### SQ-10  X::Sequence::Deduction                                     D:partial R:yes V:spec
When neither rule fits, the sequence throws `X::Sequence::Deduction` at
the point of reification, not at construction: the Seq is created and
even reports `is-lazy`. Its `.from` is the three seeds joined by commas;
`.new` without `:from` has a different message. A zero seed rules out the
geometric rule (`0, 2, 8` and `1, 0, 0` fail). Seeds already emitted
before the failure are lost with the exception.
```
say (try (1, 2, 4, 7 ... *).head(5).List.raku) // $!.^name ~ ":" ~ $!.from ~ ":" ~ $!.message, " ", (try (2, 4, 7 ... *).head(5).List.raku) // $!.^name ~ ":" ~ $!.from, " ", (try (0, 2, 8 ... *).head(5).List.raku) // $!.^name ~ ":" ~ $!.from, " ", (try (1, 0, 0 ... *).head(5).List.raku) // $!.^name ~ ":" ~ $!.from, " ", (try (0, 1, 3 ... *).head(5).List.raku) // $!.^name ~ ":" ~ $!.from, " ", (try (1, 2, 3, 5 ... 9).raku) // $!.^name ~ ":" ~ $!.from, " ", (try (1, 1, 2 ... *).head(5).List.raku) // $!.^name ~ ":" ~ $!.from, " ", (try (1, 2, 4, 7 ... 8).raku) // $!.^name ~ ":" ~ $!.from, " ", (1, 2, 4, 7 ... *).is-lazy, " ", ((1, 2, 4, 7 ... *) ~~ Seq), " ", X::Sequence::Deduction.new(:from("1,2,7")).message, " ", X::Sequence::Deduction.new.message, " ", X::Sequence::Deduction.^mro.map(*.^name).raku, " ", X::Sequence::Deduction.new(:from("a")).from.raku
# rakudo 2026.08: X::Sequence::Deduction:2,4,7:Unable to deduce arithmetic or geometric sequence from: 2,4,7|Did you really mean '..'? X::Sequence::Deduction:2,4,7 X::Sequence::Deduction:0,2,8 X::Sequence::Deduction:1,0,0 X::Sequence::Deduction:0,1,3 X::Sequence::Deduction:2,3,5 X::Sequence::Deduction:1,1,2 X::Sequence::Deduction:2,4,7 True True Unable to deduce arithmetic or geometric sequence from: 1,2,7|Did you really mean '..'? Unable to deduce sequence for some unfathomable reason ("X::Sequence::Deduction", "Exception", "Any", "Mu").Seq "a"
```
rakupp 4.0.1-84: differs — `.from` lists every seed rather than the last three, and `.message` of a hand-built exception is undefined.

### SQ-11  Two numeric seeds                                          D:yes R:yes V:spec
Two Real seeds give an arithmetic sequence with their difference, a zero
difference being constant. Rats and Nums keep their kind; `1, 1/2 ... -1`
walks in Rats through 0.
```
say (1, 2 ... 10).raku, " ", (1, 3 ... 10).raku, " ", (1, 3 ... 11).raku, " ", (10, 8 ... 1).raku, " ", (10, 8 ... 0).raku, " ", (1, 1 ... *).head(3).List.raku, " ", (1, 1 ... 1).raku, " ", (1, 1.5 ... 3).raku, " ", (1e0, 2e0 ... 4).raku, " ", (0.1, 0.2 ... 0.5).raku, " ", (1/3, 2/3 ... 1).raku, " ", (1, 1/2 ... -1).raku, " ", (1, 2 ... 5.5).raku, " ", (1, 2 ... 5/2).raku
# rakudo 2026.08: (1, 2, 3, 4, 5, 6, 7, 8, 9, 10).Seq (1, 3, 5, 7, 9).Seq (1, 3, 5, 7, 9, 11).Seq (10, 8, 6, 4, 2).Seq (10, 8, 6, 4, 2, 0).Seq (1, 1, 1) (1,).Seq (1, 1.5, 2.0, 2.5, 3.0).Seq (1e0, 2e0, 3e0, 4e0).Seq (0.1, 0.2, 0.3, 0.4, 0.5).Seq (<1/3>, <2/3>, 1.0).Seq (1, 0.5, 0.0, -0.5, -1.0).Seq (1, 2, 3, 4, 5).Seq (1, 2).Seq
```
rakupp 4.0.1-84: differs — `1, 1 ... 1` gives `(1, 1)` instead of stopping at the first seed.

### SQ-12  One numeric seed                                           D:yes R:yes V:spec
One Real seed steps by 1 toward a Real endpoint, up or down, and steps up
forever toward `*`. The seed's type is kept: `1.5 ... 4` gives Rats,
`1e0 ... 3` Nums, and the endpoint's type is irrelevant.
```
say (1 ... 0).raku, " ", (1 ... -2).raku, " ", (0 ... -1).raku, " ", (1.5 ... 4).raku, " ", (1.5 ... 4.5).raku, " ", (1e0 ... 3).raku, " ", (1 ... 3e0).raku, " ", (1 ... 4).map(*.^name).raku, " ", (1 ... 4.0).map(*.^name).raku, " ", (1.0 ... 4).map(*.^name).raku, " ", (1 ... 0).Bool, " ", (1 ... 5)[10].raku
# rakudo 2026.08: (1, 0).Seq (1, 0, -1, -2).Seq (0, -1).Seq (1.5, 2.5, 3.5).Seq (1.5, 2.5, 3.5, 4.5).Seq (1e0, 2e0, 3e0).Seq (1, 2, 3).Seq ("Int", "Int", "Int", "Int").Seq ("Int", "Int", "Int", "Int").Seq ("Rat", "Rat", "Rat", "Rat").Seq True Nil
```
rakupp 4.0.1-84: differs — `1.5 ... 4` continues in Nums (`2.5e0`), and `1.0 ... 4` gives Rat, Num, Num, Num.

### SQ-13  When the seeds already lie beyond the endpoint              D:no R:partial V:quirk
For a deduced arithmetic step the direction is the sign of the step, and
an ascending sequence whose first seed is already above the endpoint (or a
descending one below it) produces **nothing at all**: the seeds are
dropped, not emitted. A zero step counts as descending, so `1, 1 ... 2`
and `1, 1, 1 ... 2` are empty. Extra seeds beyond the last three are
still emitted (`1, 2, 4, 5, 6 ... 3` is `(1, 2)`, asserted by
`S03-sequence/basic.t`). Recorded as observed; the natural rule would
emit the seeds or run forever.
```
say (5, 6 ... 3).raku, " ", (5, 6 ... 5).raku, " ", (5, 6 ... 6).raku, " ", (5, 6 ... 7).raku, " ", (5, 4 ... 7).raku, " ", (5, 4 ... 4).raku, " ", (1, 2 ... 1).raku, " ", (2, 1 ... 3).raku, " ", (1, 2, 3 ... 0).raku, " ", (3, 2, 1 ... 10).raku, " ", (1, 1 ... 2).head(4).List.raku, " ", (1, 1, 1 ... 2).head(5).List.raku, " ", (2, 2 ... 1).head(5).List.raku, " ", (2, 2, 2 ... 1).head(5).List.raku, " ", (2, 2, 2 ... 5).head(5).List.raku, " ", (1, 2, 4, 5, 6 ... 3).head(6).List.raku
# rakudo 2026.08: ().Seq (5,).Seq (5, 6).Seq (5, 6, 7).Seq ().Seq (5, 4).Seq (1,).Seq ().Seq ().Seq ().Seq () () (2, 2, 2, 2, 2) (2, 2, 2, 2, 2) () (1, 2)
```
rakupp 4.0.1-84: differs — a constant sequence toward a different endpoint runs forever (`1, 1 ... 2` gives 1, 1, 1, …); the beyond-endpoint cases match.

### SQ-14  Where a geometric sequence stops                            D:no R:partial V:spec
A ratio above 1 stops before the first value above the endpoint, a ratio
between 0 and 1 before the first value below it, and a negative ratio
before the first value whose magnitude crosses the endpoint's magnitude,
so `1, -2, 4 ... 100` and `... -100` both end at 64. A ratio of −1 never
crosses and runs forever. A seed already past the bound gives the first
seed alone.
```
say (1, -2, 4 ... 100).raku, " ", (1, -2, 4 ... -100).raku, " ", (1, -1, 1 ... *).head(5).List.raku, " ", (1, -1, 1 ... 5).head(6).List.raku, " ", (2, 4, 8 ... 3).raku, " ", (2, 4, 8 ... 2).raku, " ", (8, 4, 2 ... 8).raku, " ", (8, 4, 2 ... 3).raku, " ", (1, 2, 4 ... 3).raku
# rakudo 2026.08: (1, -2, 4, -8, 16, -32, 64).Seq (1, -2, 4, -8, 16, -32, 64).Seq (1, -1, 1, -1, 1) (1, -1, 1, -1, 1, -1) (2,).Seq (2,).Seq (8,).Seq (8, 4.0).Seq (1, 2).Seq
```
rakupp 4.0.1-84: differs — only in the regenerated-seed type (`8, 4, 2 ... 3` gives `(8, 4)` for Rakudo's `(8, 4.0)`).

### SQ-15  Only the first seed is emitted as given                     D:no R:partial V:spec
With two or three numeric seeds the later seeds are not emitted; they are
recomputed from the first seed by the deduced step, so their type follows
the arithmetic: `1, 1.5 ... 3` is Int then Rats, `1.0, 2 ... 3` is all
Rats, `1e0, 2 ... 3` is all Nums, and `81, 27, 9 ... 1` is Int then Rats.
```
say (1, 1.5 ... 3).map(*.^name).raku, " ", (1.0, 2 ... 3).map(*.^name).raku, " ", (1e0, 2 ... 3).raku, " ", (1, 2e0 ... 3).map(*.^name).raku, " ", (1, 2 ... 3e0).map(*.^name).raku, " ", (1, 2 ... 3).map(*.^name).raku, " ", (1, 2 ... 5/2).map(*.^name).raku, " ", (1, 1.5, 2 ... 3).raku, " ", (1, 1.5, 2.25 ... 4).map(*.^name).raku, " ", (1, 2, 4 ... 4/1).map(*.^name).raku, " ", (1, 2, 4 ... 4).map(*.^name).raku, " ", (1, 2 ... 4).map(*.^name).raku, " ", (81, 27, 9 ... 1).map(*.^name).raku
# rakudo 2026.08: ("Int", "Rat", "Rat", "Rat", "Rat").Seq ("Rat", "Rat", "Rat").Seq (1e0, 2e0, 3e0).Seq ("Int", "Num", "Num").Seq ("Int", "Int", "Int").Seq ("Int", "Int", "Int").Seq ("Int", "Int").Seq (1, 1.5, 2.0, 2.5, 3.0).Seq ("Int", "Rat", "Rat", "Rat").Seq ("Int", "Int", "Int").Seq ("Int", "Int", "Int").Seq ("Int", "Int", "Int", "Int").Seq ("Int", "Rat", "Rat", "Rat", "Rat").Seq
```
rakupp 4.0.1-84: differs — the seeds are emitted as written (`1.0, 2 ... 3` is Rat, Int, Rat; `1e0, 2 ... 3` is `(1e0, 2, 3e0)`).

## C. Generators and code endpoints

### SQ-16  What a generator receives                                   D:yes R:yes V:spec
The generator is called with the last n values, n being its `count`: a
WhateverCode `* + *` and a two-parameter block get two, `{ $_ * 2 }` one,
a parameterless block none, a slurpy block every value produced so far
(`{ @_.sum }` gives 1, 2, 3, 6, 12, 24). An unnamed `$` parameter skips a
position. Its results are checked against the endpoint like any value.
```
say (1, * + 1 ... 5).raku, " ", (1, * * 2 ... 64).raku, " ", (1, * * 2 ... 65).head(8).List.raku, " ", (1, 1, * + * ... *).head(8).List.raku, " ", (1, 1, -> $a, $b { $a + $b } ... 21).raku, " ", (1, 1, -> $a, $b { $a + $b } ...^ 21).raku, " ", (1, 1, * + * ...^ * >= 100).raku, " ", (5, { $_ * 2 } ... 40).raku, " ", (5, { $_ * 2 } ... 41).head(5).List.raku, " ", (1, 1, 1, -> $a, $b, $ { $a + $b } ... *)[3..10].raku, " ", (1, 2, { @_.sum } ... *).head(6).List.raku, " ", (1, 2, -> *@a { @a.tail * 2 } ... *).head(5).List.raku, " ", (1, 2, 3, { $^a * $^b * $^c } ... *).head(6).List.raku, " ", (1, 2, sub { [*] @_[*-1], @_ + 1 } ... 720).raku
# rakudo 2026.08: (1, 2, 3, 4, 5).Seq (1, 2, 4, 8, 16, 32, 64).Seq (1, 2, 4, 8, 16, 32, 64, 128) (1, 1, 2, 3, 5, 8, 13, 21) (1, 1, 2, 3, 5, 8, 13, 21).Seq (1, 1, 2, 3, 5, 8, 13).Seq (1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89).Seq (5, 10, 20, 40).Seq (5, 10, 20, 40, 80) (2, 2, 3, 4, 5, 7, 9, 12) (1, 2, 3, 6, 12, 24) (1, 2, 4, 8, 16) (1, 2, 3, 6, 36, 648) (1, 2, 6, 24, 120, 720).Seq
```
rakupp 4.0.1-84: matches.

### SQ-17  Fewer seeds than the generator needs                        D:partial R:no V:spec
A generator that needs more arguments than there are seeds throws (an
`X::AdHoc` about too few positionals) when the first generated value is
demanded; the Seq itself is constructed.
```
say (try (1, * + * ... *).head(4).List.raku) // $!.^name, " ", (try (1, -> $a, $b { $a + $b } ... *).head(4).List.raku) // $!.^name, " ", (try (1, -> $a, $b, $c { 0 } ... 5).raku) // $!.^name, " ", (try (1, 2, -> $a, $b, $c { 0 } ... 5).raku) // $!.^name
# rakudo 2026.08: X::AdHoc X::AdHoc X::AdHoc X::AdHoc
```
rakupp 4.0.1-84: differs — the missing arguments are filled in (`1, * + * ... *` gives 1, 1, 2, 3 and a three-parameter block gives endless zeros).

### SQ-18  `last` ends the sequence                                   D:no R:yes V:spec
`last` inside the generator ends the sequence after the values already
produced; `last` inside a code endpoint ends it before the value being
tested. A generator whose first call does `last` leaves the seeds alone.
```
say (1, { last if $_ > 3; $_ + 1 } ... *).raku, " ", (1, 2, { last if $_ >= 5; $_ + 1 } ... *)[lazy ^10].raku, " ", (5, 4, 3, { $_ - 1 || last } ... *)[lazy ^10].raku, " ", (1, { last } ... *).raku, " ", (1 ... { last if $_ > 2; False }).raku, " ", (1, { last if $_ > 3; $_ + 1 } ... 100).raku
# rakudo 2026.08: (1, 2, 3, 4).lazy.Seq (1, 2, 3, 4, 5) (5, 4, 3, 2, 1) (1).lazy.Seq (1, 2).Seq (1, 2, 3, 4).Seq
```
rakupp 4.0.1-84: differs — `last` in a generator dies ("last without loop construct"); the line died there.

### SQ-19  What a generator may return                                 D:no R:yes V:spec
A Slip returned by the generator is flattened into the sequence and its
elements feed the next call, which is how one generator interleaves
several sequences; `()`, `Nil` and a List are single elements, so a
List-returning generator nests.
```
say (1, { Slip.new(2, 3) } ... *).head(4).List.raku, " ", (1, 1, 1, { slip $^a + 1, $^b * 2, $^c - 1 } ... *)[^9].raku, " ", (-> { slip 'zero', 'one' } ... *).head(5).List.raku, " ", (1, { () } ... *).head(3).List.raku, " ", (1, { Nil } ... *).head(3).List.raku, " ", (1, { (2, 3) } ... *).head(3).List.raku, " ", ((1, 2), (3, 4), { $_ } ... *).head(3).List.raku, " ", (1, 2, { $^a, $^b } ... *).head(4).List.raku
# rakudo 2026.08: (1, 2, 3, 2) (1, 1, 1, 2, 2, 0, 3, 4, -1) ("zero", "one", "zero", "one", "zero") (1, (), ()) (1, Nil, Nil) (1, (2, 3), (2, 3)) ((1, 2), (3, 4), (3, 4)) (1, 2, (1, 2), (2, $(1, 2)))
```
rakupp 4.0.1-84: differs — Slips are kept as elements (`slip(2, 3)`), so nothing interleaves.

### SQ-20  Code endpoints                                             D:partial R:yes V:spec
A code endpoint is called with the last n values, n being its arity (or
its count when the arity is 0; a slurpy gets all values so far), once n
values exist, and the value that makes it True ends the sequence, included
unless `...^`. A parameterless block or pointy block counts as a plain
value and is smartmatched, which calls it and stops at the first element.
`* > 10` and `*.is-prime` work as endpoints; a type object endpoint stops
at the first value of that type.
```
say (1, { $_ * 2 } ... { $_ > 10 }).raku, " ", (1, { $_ * 2 } ...^ { $_ > 10 }).raku, " ", (1, { $_ * 2 } ... * > 10).raku, " ", (1, { $_ * 2 } ...^ * > 10).raku, " ", (1, { $_ + 1 } ... { $^a + $^b > 8 }).raku, " ", (1, { $_ + 1 } ...^ { $^a + $^b > 8 }).raku, " ", (1 ... { $^a + $^b == 5 }).raku, " ", (1 ... -> *@a { @a.sum > 10 }).raku, " ", (1 ... { True }).raku, " ", (1 ... -> { True }).raku, " ", (1 ... { False }).head(3).List.raku, " ", (1 ... *.is-prime).raku, " ", (1 ... { .is-prime }).raku, " ", (1, { $_ + 2 } ... { $_ > 3 && $_ %% 3 }).raku, " ", (1, 2 ... { $_ == 4 }).raku, " ", (1, 2 ...^ { $_ == 4 }).raku, " ", (1, 2 ... * == 4).raku, " ", (1 ... { $_ > 3 }).elems, " ", do { sub f($x) { $x > 3 ?? q{liftoff!} !! $x + 1 }; (1, &f ... Str).head(5).List.raku }
# rakudo 2026.08: (1, 2, 4, 8, 16).Seq (1, 2, 4, 8).Seq (1, 2, 4, 8, 16).Seq (1, 2, 4, 8).Seq (1, 2, 3, 4, 5).Seq (1, 2, 3, 4).Seq (1, 2, 3).Seq (1, 2, 3, 4, 5).Seq (1,).Seq (1,).Seq (1, 2, 3) (1, 2).Seq (1, 2).Seq (1, 3, 5, 7, 9).Seq (1, 2, 3, 4).Seq (1, 2, 3).Seq (1, 2, 3, 4).Seq 4 (1, 2, 3, 4, "liftoff!")
```
rakupp 4.0.1-84: differs — a two-parameter endpoint block and a slurpy one stop late (`1, { $_ + 1 } ... { $^a + $^b > 8 }` runs to 9 instead of 5, `-> *@a { @a.sum > 10 }` to 11 instead of 5).

### SQ-21  Exceptions raised while producing                           D:no R:no V:quirk
A deduction failure raised while a sequence is iterated is caught by a
`try` only when the sequence was **created** inside that `try`; a
sequence created outside and iterated inside a `try` block escapes it and
ends the program. A generator whose step fails (here `Str.pred` past
"a") emits the Failure as an element. Recorded as observed; step two
should catch at the point of iteration.
```
say do { my $s = (1, 2, 4, 7 ... *); my @got; try { for $s { @got.push($_); } }; @got.raku ~ " " ~ $!.^name }
# rakudo 2026.08: Unable to deduce arithmetic or geometric sequence from: 2,4,7|Did you really mean '..'?|  in block <unit> at -e line 1|
say do { my $s = (1, 2, 4, 7 ... *); my $r = try { $s.head(5).List.raku }; ($r // "caught") ~ " " ~ $!.^name ~ " " ~ do { my $t = (1, 2, 4, 7 ... *); my $u = (try $t.eager) // "caught2"; $u } ~ " " ~ do { my $v = try { (1, 2, 4, 7 ... *).eager }; $v // "caught3" } }, " ", (try ('c', 'b', 'a' ... *).head(4).List.raku) // 'caught4', " ", ('c', 'b', 'a' ... *).head(3).List.raku
# rakudo 2026.08: caught X::Sequence::Deduction caught2 caught3 ("c", "b", "a", Failure.new(exception => X::AdHoc.new(payload => "Decrement out of range"), backtrace => Backtrace.new)) ("c", "b", "a")
```
rakupp 4.0.1-84: differs — the `for` loop over a scalar-held lazy Seq iterated it as one item, and the underflow stops the sequence silently after "a".

## D. Strings and other types

### SQ-22  Single-character strings walk codepoints                     D:yes R:yes V:spec
One single-character seed and a single-character endpoint walk every
codepoint between them, in either direction, punctuation included (`'A' ...
'a'` has 33 elements); `'z' ... *` continues with `aa`. Digits are
strings too: `'9' ... '12'` compares as strings and counts down to "2".
```
say ('a' ... 'e').raku, " ", ('e' ... 'a').raku, " ", ('a' ...^ 'e').raku, " ", ('a' ^...^ 'e').raku, " ", ('a' ... 'a').raku, " ", ('A' ... 'a').elems, " ", ('A' ... 'a').head(4).List.raku, " ", ('A' ... 'a').tail(4).List.raku, " ", ('a' ... 'A').elems, " ", ('A' ... 'z').elems, " ", ('α' ... 'ω').elems, " ", ('b' ... 'a').raku, " ", ('a' ... *).head(3).List.raku, " ", ('z' ... *).head(3).List.raku, " ", ('1' ... '3').raku, " ", ('3' ... '1').raku, " ", ('9' ... '12').raku
# rakudo 2026.08: ("a", "b", "c", "d", "e").Seq ("e", "d", "c", "b", "a").Seq ("a", "b", "c", "d").Seq ("b", "c", "d").Seq ("a",).Seq 33 ("A", "B", "C", "D") ("^", "_", "`", "a") 33 58 25 ("b", "a").Seq ("a", "b", "c") ("z", "aa", "ab") ("1", "2", "3").Seq ("3", "2", "1").Seq ("9", "8", "7", "6", "5", "4", "3", "2").Seq
```
rakupp 4.0.1-84: differs — `'9' ... '12'` gives `("9",)`.

### SQ-23  Strings of the same length: a per-position product           D:yes R:yes V:spec
One seed and an endpoint of the same length (more than one character)
give the cross product of the per-position ranges, in either direction
per position (`'ac' ... 'ca'` is ac ab aa bc bb ba cc cb ca), and the
sequence ends there; extra right-hand elements still follow.
```
say ('aa' ... 'cc').raku, " ", ('ac' ... 'ca').raku, " ", ('cc' ... 'aa').raku, " ", ('aa' ...^ 'cc').raku, " ", ('aa' ^... 'cc').raku, " ", ('a1' ... 'c3').raku, " ", ('ab' ... 'aa').raku, " ", ('aa' ... 'aa').raku, " ", ('ba' ... 'ab').raku, " ", ('ZZ' ... 'AA')[*-1]
# rakudo 2026.08: ("aa", "ab", "ac", "ba", "bb", "bc", "ca", "cb", "cc").Seq ("ac", "ab", "aa", "bc", "bb", "ba", "cc", "cb", "ca").Seq ("cc", "cb", "ca", "bc", "bb", "ba", "ac", "ab", "aa").Seq ("aa", "ab", "ac", "ba", "bb", "bc", "ca", "cb", "cc").Seq ("ab", "ac", "ba", "bb", "bc", "ca", "cb", "cc").Seq ("a1", "a2", "a3", "b1", "b2", "b3", "c1", "c2", "c3").Seq ("ab", "aa").Seq ("aa",).Seq ("ba", "bb", "aa", "ab").Seq AA
```
rakupp 4.0.1-84: differs — no product: `'aa' ... 'cc'` walks every string from aa to cc by `succ`, `'ba' ... 'ab'` walks 27 strings down.

### SQ-24  Strings of different lengths                               D:no R:partial V:spec
With different lengths the walk is by `succ` while the value is not after
the endpoint and not longer than it (`'a' ... 'zz'` has 702 elements,
`'a' ... 'bb'` is just a, b because "c" sorts after "bb"), or by `pred`
while the value is not before the endpoint (`'x' ... 'ab'` runs from x
down to b). `pred` below "a" fails with an `X::AdHoc` ("Decrement out of
range"), also for a numeric endpoint after a string seed.
```
say ('x' ... 'ab').raku, " ", ('a' ... 'zz').elems, " ", ('a' ... 'zz').tail(3).List.raku, " ", ('a' ... 'bb').raku, " ", ('A' ...^ 'ZZ')[*-1], " ", (try ('ab' ... 'a').raku) // $!.^name ~ ":" ~ $!.message, " ", (try ('ba' ... 'a').raku) // $!.^name, " ", ('bb' ... 'b').raku, " ", (try ('a' ... 5).head(3).List.raku) // $!.^name ~ ":" ~ $!.message, " ", (1 ... 'c').head(3).List.raku
# rakudo 2026.08: ("x", "w", "v", "u", "t", "s", "r", "q", "p", "o", "n", "m", "l", "k", "j", "i", "h", "g", "f", "e", "d", "c", "b").Seq 702 ("zx", "zy", "zz") ("a", "b").Seq ZY X::AdHoc:Decrement out of range X::AdHoc ("bb", "ba").Seq X::AdHoc:Decrement out of range (1, 2, 3)
```
rakupp 4.0.1-84: differs — `'x' ... 'ab'` gives `("x",)`, `'a' ... 'bb'` walks 28 strings, the underflow is silent, `'a' ... 5` continues in Nums, `1 ... 'c'` gives `(1, 0)`.

### SQ-25  Two or more string seeds                                    D:no R:no V:quirk
Non-numeric seeds never deduce a step: the walk is by `succ` or `pred`
only, the direction taken from the last seed against the endpoint when
they are of the same type, otherwise from the last two seeds. So `'a',
'c' ... 'i'` continues c, d, e, …, and `'x', 'y' ... 'ab'` emits x, y and
then walks **backwards** from y because "y" is after "ab". A mixed `1,
'a' ... *` walks strings from the last seed.
```
say ('a', 'b' ... 'e').raku, " ", ('a', 'c' ... 'i').raku, " ", ('a', 'c' ... 'j').head(7).List.raku, " ", ('e', 'd' ... 'a').raku, " ", ('a', 'b' ... *).head(4).List.raku, " ", ('a', 'b', 'c' ... 'e').raku, " ", ('a', 'a' ... *).head(3).List.raku, " ", ('x', 'y' ... 'ab').head(6).List.raku, " ", ('a', 'b' ... 'zz').elems, " ", ('a', 'b' ... 'zz').tail(2).List.raku, " ", ('a', 'b' ... 'zzz').head(3).List.raku, " ", ('a', 'b' ... 5).head(3).List.raku, " ", ('c', 'b' ... 'e').head(4).List.raku, " ", (1, 'a' ... *).head(3).List.raku
# rakudo 2026.08: ("a", "b", "c", "d", "e").Seq ("a", "c", "d", "e", "f", "g", "h", "i").Seq ("a", "c", "d", "e", "f", "g", "h") ("e", "d", "c", "b", "a").Seq ("a", "b", "c", "d") ("a", "b", "c", "d", "e").Seq ("a", "a", "a") ("x", "y", "x", "w", "v", "u") 702 ("zy", "zz") ("a", "b", "c") ("a", "b", "c") ("c", "b", "c", "d") (1, "a", "b")
```
rakupp 4.0.1-84: differs — `'a', 'a' ... *` gives a, a, b; `'x', 'y' ... 'ab'` stops at y; `'a', 'b' ... 5` continues with `0e0`.

### SQ-26  Dates and other types with `succ`                           D:no R:no V:spec
Any type with `succ`/`pred` and `cmp` works: Dates walk by day toward a
Date endpoint in either direction, toward `*`, and toward a code endpoint;
two Date seeds do not deduce a step (`succ` from the last). A type without
`succ` throws `X::Method::NotFound`. A Date endpoint after an Int seed
never matches.
```
say (Date.new('2026-01-01') ... Date.new('2026-01-04')).raku, " ", (Date.new('2026-01-04') ... Date.new('2026-01-01')).map(*.Str).raku, " ", (Date.new('2026-01-01') ... *).head(3).map(*.Str).raku, " ", (Date.new('2026-01-01'), Date.new('2026-01-03') ... *).head(3).map(*.Str).raku, " ", (Date.new('2026-01-01') ... { .day == 3 }).map(*.Str).raku, " ", (Date.new('2026-01-01'), Date.new('2026-01-08') ... Date.new('2026-01-22')).map(*.day).raku, " ", (Date.new('2026-01-01') ...^ Date.new('2026-01-04')).map(*.day).raku, " ", (Date.new('2026-01-01') ... 3).head(3).map(*.Str).raku, " ", (try (v1.0 ... v1.3).raku) // $!.^name, " ", (1 ... Date.new('2026-01-01')).head(3).List.raku, " ", (Date.new('2026-01-30') ... Date.new('2026-02-02')).map(*.Str).raku
# rakudo 2026.08: (Date.new(2026,1,1), Date.new(2026,1,2), Date.new(2026,1,3), Date.new(2026,1,4)).Seq ("2026-01-04", "2026-01-03", "2026-01-02", "2026-01-01").Seq ("2026-01-01", "2026-01-02", "2026-01-03").Seq ("2026-01-01", "2026-01-03", "2026-01-04").Seq ("2026-01-01", "2026-01-02", "2026-01-03").Seq (1, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22).Seq (1, 2, 3).Seq ("2026-01-01", "2026-01-02", "2026-01-03").Seq X::Method::NotFound (1, 2, 3) ("2026-01-30", "2026-01-31", "2026-02-01", "2026-02-02").Seq
```
rakupp 4.0.1-84: differs — after the first Date the values are Nums (day counts), so `.day` died on a Num; the line died there.

## E. Chains and the result

### SQ-27  Chained sequences                                          D:no R:yes V:spec
`a ... b ... c` is evaluated left to right: a numeric or string middle
endpoint ends its segment **exclusively** and starts the next, so it
appears once (`1 ... 5 ... 1` is 1 2 3 4 5 4 3 2 1); a middle `*` or block
is consumed as the endpoint of its segment and the next segment starts from
what follows it; the last list's extra elements are appended. Each segment
deduces from its own seeds (`1, 2 ... 4, 8 ... 32` steps by 4 after 4).
```
say (1 ... 5 ... 1).raku, " ", (1 ... 3 ... 6 ... 4).raku, " ", (1 ... 5 ... 10).raku, " ", (1 ... 5 ... *).head(8).List.raku, " ", (1, 3 ... 9 ... 1).raku, " ", (1 ... 3 ... { $_ == 6 }).raku, " ", (1 ... 3 ... 'e').head(5).List.raku, " ", ('a' ... 'c' ... 'a').raku, " ", (1 ^... 5 ^... 1).raku, " ", (1 ^...^ 5 ^...^ 1).raku, " ", (1 ... 5 ... 1 ... 5).raku, " ", (1 ... 1 ... 1).raku, " ", (1, 2 ... 4, 8 ... 32).raku, " ", ((1 ... 3) ... 5).raku, " ", (1 ... (3 ... 5)).raku, " ", (1 ... * ... 5).head(3).List.raku, " ", (1 ... 3 ... *).head(3).map(*.^name).raku, " ", (0, 2 ... 8, 11 ... 17, 18 ... 21).raku
# rakudo 2026.08: (1, 2, 3, 4, 5, 4, 3, 2, 1).Seq (1, 2, 3, 4, 5, 6, 5, 4).Seq (1, 2, 3, 4, 5, 6, 7, 8, 9, 10).Seq (1, 2, 3, 4, 5, 6, 7, 8) (1, 3, 5, 7, 9, 8, 7, 6, 5, 4, 3, 2, 1).Seq (1, 2, 3, 4, 5, 6).Seq (1, 2, 3, 4, 5) ("a", "b", "c", "b", "a").Seq (2, 3, 4, 5, 4, 3, 2, 1).Seq (2, 3, 4, 5, 4, 3, 2, 1).Seq (1, 2, 3, 4, 5, 4, 3, 2, 1, 2, 3, 4, 5).Seq (1,).Seq (1, 2, 3, 4, 8, 12, 16, 20, 24, 28, 32).Seq (1, 2, 3, 4, 5).Seq (1, 2, 3, 4, 5).Seq (1, 2, 3) ("Int", "Int", "Int").Seq (0, 2, 4, 6, 8, 11, 14, 17, 18, 19, 20, 21).Seq
```
rakupp 4.0.1-84: differs — `1 ... 5 ... *` stops at 5, `1 ... 3 ... 'e'` turns back, `1, 2 ... 4, 8 ... 32` steps by 1 after 8, `1 ... (3 ... 5)` stops at 3.

### SQ-28  `...^` does not chain; `^...^` chains without excluding       D:no R:no V:bug
`1 ...^ 5 ...^ 1` is a compile-time `X::Syntax::NonListAssociative`; the
same with `…^`. `^...` chains and drops the first element; `^...^` chains
but only drops the first element, keeping the final 1 — the source marks
this unfinished, and the operator's own meaning says the end is excluded.
```
say (try EVAL q{(1 ...^ 5 ...^ 1).raku}) // $!.^name, " ", (try EVAL q{(1 …^ 5 …^ 1).raku}) // $!.^name, " ", (try EVAL q{(1 ^... 5 ^... 1).raku}) // $!.^name, " ", (try EVAL q{(1 ^...^ 5 ^...^ 1).raku}) // $!.^name, " ", (try EVAL q{(1 ...^ 5).raku}) // $!.^name, " ", (try EVAL q{(1 ... 5 ...^ 1).raku}) // $!.^name, " ", (try EVAL q{(1 ...^ 5 ... 1).raku}) // $!.^name
# rakudo 2026.08: X::TypeCheck::Argument+{X::Comp} X::TypeCheck::Argument+{X::Comp} (2, 3, 4, 5, 4, 3, 2, 1).Seq (2, 3, 4, 5, 4, 3, 2, 1).Seq (1, 2, 3, 4).Seq X::Syntax::NonListAssociative X::Syntax::NonListAssociative
```
rakupp 4.0.1-84: differs — `...^` chains (giving 1 2 3 4 3 2) and chained `^...^` excludes the end, which is the intended reading, not Rakudo's.

### SQ-29  The result object and precedence                            D:partial R:partial V:spec
A sequence is a Seq whose `.elems` caches (a second `.elems` works),
`eager` gives a List, `.Str` and `.gist` are the space-joined form, and
Z, `sum`, `reverse`, `cache`, subscripts, `.Array` and `.Slip` work as on
any Seq. On the left an Array, a List, a Slip, an itemised list, a Seq or a
Range all contribute their elements as seeds (`1..3 ... 6` is 1 to 6,
`..` binding tighter). `my $x = 1 ... 3` binds as `(my $x = 1) ... 3`:
item assignment is tighter than the list infix, so `$x` is 1 and the
sequence is sunk with a warning; list assignment `my @a = 1 ... 3` is not.
```
say do { my $s = (1 ... 3); $s.elems ~ " " ~ ((try $s.elems) // $!.^name) }, " ", (eager 1 ... 3).^name, " ", do { my @a = 1 ... 3; @a.raku }, " ", (1 ... 3).List.^name, " ", (1 ... 3).Str, " ", (1 ... 3).gist, " ", ((1 ... 3) Z (4 ... 6)).raku, " ", (1 ... 5).sum, " ", (1 ... 3).cache.raku, " ", (1 ... 5).reverse.raku, " ", (1 ... 5)[2], " ", (1 ... 5)[*-1], " ", (1 ... 5).elems, " ", (1 ... 5).Array.raku, " ", (1 ... 5).Slip.raku, " ", ((1, 2, 3) ... 6).raku, " ", ([1, 2, 3] ... 6).raku, " ", (|(1, 2, 3) ... 6).raku, " ", ($(1, 2, 3) ... 6).head(3).List.raku, " ", ((1, 2, 3).Seq ... 6).raku, " ", (1..3 ... 6).raku, " ", ((1..3) ... 6).raku, " ", do { my $x; quietly { $x = 1 ... 3; }; $x.raku }, " ", do { my @a = 1 ... 3; @a.elems }
# rakudo 2026.08: 3 3 List [1, 2, 3] List 1 2 3 (1 2 3) ((1, 4), (2, 5), (3, 6)).Seq 15 (1, 2, 3) (5, 4, 3, 2, 1).Seq 3 5 5 [1, 2, 3, 4, 5] slip(1, 2, 3, 4, 5) (1, 2, 3, 4, 5, 6).Seq (1, 2, 3, 4, 5, 6).Seq (1, 2, 3, 4, 5, 6).Seq (1, 2, 3) (1, 2, 3, 4, 5, 6).Seq (1, 2, 3, 4, 5, 6).Seq (1, 2, 3, 4, 5, 6).Seq 1 3
```
rakupp 4.0.1-84: differs — `eager 1 ... 3` is still a Seq.

## Counts

| | items |
|---|---|
| total | 29 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 13 |
| Rakudo bugs (do not imitate) | 2 — SQ-06 `none` endpoint taken for Inf, SQ-28 chained `^...^` keeps its end |
| quirks (recorded, step two decides) | 3 — SQ-13 seeds dropped when already beyond the endpoint, SQ-21 exception escapes the iterating `try`, SQ-25 two string seeds walk backwards |
| rakupp 4.0.1-84 differs | 28 |
| rakupp 4.0.1-84 matches | 1 |

Recurring rakupp gaps, for step two: the seeds are not checked against
the endpoint before deduction (SQ-07) and deduction uses every seed
instead of the last three (SQ-08, SQ-10); later seeds are emitted as
written instead of regenerated, and Rat steps decay to Num (SQ-12,
SQ-15); a non-numeric, non-string endpoint is treated as 0 (SQ-05); no
per-position string product and no length bound for strings (SQ-23,
SQ-24); `last` inside a generator dies and Slips are not flattened
(SQ-18, SQ-19); a code endpoint with two or more parameters is fed wrongly
(SQ-20); Dates decay to day counts (SQ-26); chains do not restart each
segment's deduction and `*` in the middle ends the chain (SQ-27); missing
generator arguments are silently filled (SQ-17). The one place rakupp is
closer to the language than Rakudo is the chained `^...^` (SQ-28).

## Method (how this sheet was produced)

As for [IO.md](IO.md): one probe line per item, both engines, `alarm 20`,
outputs assembled from one file per probe per engine. Traps met here: a
deduction failure is thrown at reification, so it must be provoked inside
the `try` (`.head(n).List.raku`), and a `try` around the iteration alone
does not catch it (SQ-21); `Str.pred` past "a" returns a Failure that ends
the line when used, so those probes carry `try`; `{ $_ > 3 ?? last !! … }`
is a parse error ("!! gobbled"), write `last if`; `$_` inside a `sub` is
not the argument; `my $s = 1 ... 3` assigns 1 (SQ-29); and a chained
`...^` is a compile-time error that kills the whole line, so it is probed
through `EVAL`. D flags from `doc/Language/operators.rakudoc` (the `infix
...` section) and `doc/Type/X/Sequence/Deduction.rakudoc`; R flags from
`S03-sequence/*.t`.
