# Raku++ — Language Reference

An exhaustive, example-driven reference for the Raku dialect that **`rakupp`
actually implements**. Unlike [FEATURES.md](FEATURES.md) (thematic inventory) and
[COOKBOOK.md](COOKBOOK.md) (task recipes), this document is organised as a *lookup
sheet*: every operator, built-in subroutine, method, and syntactic form, each with
a signature and a runnable example.

**Every example below was executed on `rakupp` and shows its real output** (`# → …`).
Reproduce any of them with:

```sh
./build/rakupp -e 'CODE'
```

Scope note: this reflects the current build, defaulting to **Raku 6.d**. Where
`rakupp` differs from Rakudo or omits something, it is called out inline and
collected in [§14 rakupp-specific notes](#14-rakupp-specific-notes--caveats). The
full machine-extracted inventories (183 subroutines, 562 methods) are in the
[appendices](#appendix-a--all-built-in-subroutines).

---

## Table of contents

1. [Variables, sigils & twigils](#1-variables-sigils--twigils)
2. [Literals & quoting](#2-literals--quoting)
3. [Operators](#3-operators)
4. [Metaoperators](#4-metaoperators)
5. [Built-in subroutines](#5-built-in-subroutines)
6. [Methods by receiver](#6-methods-by-receiver)
7. [Control flow & statement modifiers](#7-control-flow--statement-modifiers)
8. [Routines & signatures](#8-routines--signatures)
9. [Classes, roles, enums, subsets](#9-classes-roles-enums-subsets)
10. [Phasers](#10-phasers)
11. [Regexes & grammars](#11-regexes--grammars)
12. [Special & dynamic variables](#12-special--dynamic-variables)
13. [The type & number tower](#13-the-type--number-tower)
14. [rakupp-specific notes & caveats](#14-rakupp-specific-notes--caveats)
- [Appendix A — all built-in subroutines](#appendix-a--all-built-in-subroutines)
- [Appendix B — all methods](#appendix-b--all-methods)

---

## 1. Variables, sigils & twigils

A sigil marks the *container shape*; a twigil (second character) marks *scope/kind*.

| Sigil | Shape | Example | Result |
|---|---|---|---|
| `$` | scalar (single item) | `my $x = 5; say $x` | `5` |
| `@` | positional (array/list) | `my @a = 1,2,3; say @a[1]` | `2` |
| `%` | associative (hash) | `my %h = a=>1; say %h<a>` | `1` |
| `&` | callable | `my &f = {$_*2}; say f(4)` | `8` |

Twigils:

| Twigil | Meaning | Example |
|---|---|---|
| `$*x` | dynamic (context) variable | `$*PID`, `$*IN`, `$*OUT` |
| `$.x` | public attribute accessor | `has $.x` inside a class |
| `$!x` | private attribute | `has $!x` |
| `$^x` | positional placeholder param | `{ $^a + $^b }` |
| `$?x` | compile-time constant | `$?FILE`, `$?LINE` |
| `$=x` | Pod data | `$=pod` |

Declarators: `my` (lexical), `our` (package), `has` (attribute), `state`
(persistent per-closure), `constant` (compile-time), `temp`/`let` (dynamic
save-restore).

```raku
my  $a = 1;                 # lexical
our $b = 2;                 # package-scoped
state $c = 0; $c++;         # persists across calls
constant K = 42;  say K;    # → 42
```

Binding vs assignment: `=` copies a value into a container; `:=` binds a name
directly to a container (aliasing).

```raku
my $x = 1; my $y := $x; $x = 9; say $y;   # → 9   (aliased)
```

---

## 2. Literals & quoting

### Numbers

```raku
say 42;                 # → 42       Int (arbitrary precision)
say 2 ** 100;           # → 1267650600228229401496703205376
say 0xFF;               # → 255      hex   (also 0o.. octal, 0b.. binary)
say 1_000_000;          # → 1000000  underscore separators
say 3.14;               # → 3.14     Rat (exact rational, not float!)
say (0.1+0.2).raku;     # → <3/10>   decimals are Rat: no float error
say 1.5e3;              # → 1500     Num (e-notation opts into float)
say 3+4i;               # → 3+4i     Complex
say 1/3;                # → 0.333333 Rat; (1/3).nude → [1 3]
```

Constants: `pi` / `π`, `e`, `tau` / `τ`, `Inf` / `∞`, `-Inf`, `NaN`.

```raku
say pi;  say e;  say tau;  say Inf;  say NaN;   # → 3.14159… 2.71828… 6.28318… Inf NaN
```

### Strings & quoting forms

| Form | Interpolates? | Example | Result |
|---|---|---|---|
| `'…'` / `q{…}` | no | `q{no $x}` | `no $x` |
| `"…"` / `qq{…}` | yes | `qq{v={1+1}}` | `v=2` |
| `Q{…}` | nothing (fully raw) | `Q{raw \n}` | `raw \n` |
| `<…>` | word list → List | `<a b c>` | `(a b c)` |
| `«…»` / `qw{…}` | word list | `qw{x y z}` | `(x y z)` |
| `q:to/END/` | heredoc | see below | multi-line |

```raku
say "sum is {1+2}";      # → sum is 3        block interpolation
say "arr @a[]";          # interpolate an array with []
my $s = q:to/END/;
    indented heredoc
    second line
    END
say $s.lines.elems;      # → 2   (the /…/ marker's indent is stripped)
```

> **Note:** the `$(…)` string-contextualiser interpolation form is **not**
> supported inside strings — use `{…}` for any computed interpolation.

---

## 3. Operators

Operators are grouped by precedence tier, **tightest first**. Each entry gives the
operator, a one-line meaning, and a verified example. Word-form operators (`div`,
`eqv`, `Z`, …) are interleaved at their real precedence.

### 3.1 Method / postfix (tightest)

| Op | Meaning | Example → result |
|---|---|---|
| `.meth` | method call | `"hi".uc` → `HI` |
| `.+meth` / `.?meth` | all-candidates / maybe call | `$obj.?maybe` |
| `[ ]` | positional index | `(1,2,3)[1]` → `2` |
| `{ }` | associative index | `%h<k>` / `%h{'k'}` |
| `( )` | invoke | `&f(3)` |
| `++` / `--` | auto-increment / decrement | `my $w=4; say $w++; say $w` → `4`,`5` |

```raku
say (1,2,3)[*-1];         # → 3     *-1 = last index
my %h = a=>1, b=>2;
say %h<a b>;              # → (1 2) hash slice
say <a b c>[1,2];         # → (b c) list slice
```

### 3.2 Exponentiation (right-assoc)

| Op | Meaning | Example → result |
|---|---|---|
| `**` | power | `2 ** 10` → `1024` |

```raku
say 2 ** 0.5;            # → 1.4142135623730951
say 2 ** 3 ** 2;         # → 512   (right-assoc: 2**(3**2))
```

### 3.3 Symbolic unary (prefix)

| Op | Meaning | Example → result |
|---|---|---|
| `-` | numeric negate | `-5` → `-5` |
| `+` | numeric coerce | `+"42"` → `42` |
| `~` | string coerce | `~42` → `42` |
| `?` | Bool coerce | `?1` → `True` |
| `!` | Bool negate | `!1` → `False` |
| `\|` | flatten / slip into args | `f(\|@args)` |
| `+^` `~^` `?^` | bitwise / string / bool NOT | `~0x0F` → `15` |

```raku
say ?1, ' ', !1, ' ', +"42", ' ', ~42, ' ', -5;   # → True False 42 42 -5
```

### 3.4 Multiplicative

| Op | Meaning | Example → result |
|---|---|---|
| `*` | multiply | `6 * 7` → `42` |
| `/` | divide (→ Rat/Num) | `7 / 2` → `3.5` |
| `%` | modulo | `7 % 3` → `1` |
| `%%` | divisible-by (Bool) | `12 %% 3` → `True` |
| `!%%` | not divisible-by | `12 !%% 5` → `True` |
| `div` | integer division | `7 div 2` → `3` |
| `mod` | integer modulo | `7 mod 3` → `1` |
| `gcd` | greatest common divisor | `12 gcd 18` → `6` |
| `lcm` | least common multiple | `4 lcm 6` → `12` |
| `+&` `~&` `?&` | bitwise / buffer / bool AND | `5 +& 6` → `4` |
| `+<` `~<` | bit / string shift-left | `1 +< 4` → `16`… see note |
| `o` / `∘` | function composition | `(&a ∘ &b)(x)` |

```raku
say 6 +& 3;   # → 2      bitwise AND
say 1 +< 4;   # → 16     left shift
```

> **Note:** the *right*-shift forms `+>` / `~>` are **deliberately not lexed** in
> `rakupp` (the `>` collides with other syntax). Left-shift `+<` / `~<` work.

### 3.5 Additive

| Op | Meaning | Example → result |
|---|---|---|
| `+` | add | `7 + 3` → `10` |
| `-` | subtract | `7 - 3` → `4` |
| `+\|` `~\|` `?\|` | bitwise / string / bool OR | `3 +\| 4` → `7` |
| `+^` `~^` `?^` | bitwise / string / bool XOR | `6 +^ 3` → `5` |

### 3.6 Replication

| Op | Meaning | Example → result |
|---|---|---|
| `x` | string repeat | `'ab' x 3` → `ababab` |
| `xx` | list repeat | `(1,2) xx 2` → `([1 2] [1 2])` |

### 3.7 Concatenation

| Op | Meaning | Example → result |
|---|---|---|
| `~` | string concatenation | `'ab' ~ 'cd'` → `abcd` |

### 3.8 Junctive (autothreading)

| Op | Meaning | Example → result |
|---|---|---|
| `\|` | *any* junction | `5 == (1\|5\|9)` → `True` |
| `&` | *all* junction | `(1&2&3).so` → `True` |
| `^` | *one* junction | `(1^2)` |

```raku
say 3 == (1 | 3 | 5);    # → True   any-junction autothreads the comparison
```

### 3.9 Set / bag operators

ASCII and Unicode spellings are equivalent. Combining ops return a `Set`/`Bag`;
relational ops return `Bool`.

| ASCII | Unicode | Meaning | Example → result |
|---|---|---|---|
| `(\|)` | `∪` | union | `(<a b> (\|) <b c>).keys.sort` → `(a b c)` |
| `(&)` | `∩` | intersection | `<a b c> (&) <b c d>` → `{b, c}` |
| `(-)` | `∖` | difference | `<a b c> (-) <b>` → `{a, c}` |
| `(^)` | `⊖` | symmetric diff | `<a b> (^) <b c>` → `{a, c}` |
| `(+)` | `⊎` | baggy sum | `<a b> (+) <a c>` → bag `a=>2 b=>1 c=>1` |
| `(.)` | `⊍` | baggy multiply | |
| `(elem)` | `∈` | membership | `'a' (elem) <a b c>` → `True` |
| `(!elem)` | `∉` | non-membership | `'z' (!elem) <a b c>` → `True` |
| `(cont)` | `∋` | contains | `<a b> (cont) 'a'` → `True` |
| `(<=)` | `⊆` | subset-or-equal | `<a b> (<=) <a b c>` → `True` |
| `(<)` | `⊂` | strict subset | |
| `(>=)` `(>)` | `⊇` `⊃` | superset / strict | `<a b c> (>) <a b>` → `True` |

> `say` of a `Set`/`Bag` in `rakupp` prints one `element⏎True`/count line per
> member rather than Rakudo's `Set(a b c)`. Use `.keys.sort` / `.raku` for a
> stable rendering.

### 3.10 Ranges (non-chaining binary)

| Op | Meaning | Example → result |
|---|---|---|
| `..` | inclusive range | `1..5` → `1..5` |
| `..^` | exclude top | `1..^5` → `1 2 3 4` |
| `^..` | exclude bottom | `1^..5` → `2 3 4 5` |
| `^..^` | exclude both | `1^..^5` → `2 3 4` |
| `^N` | `0..^N` shorthand | `^3` → `0 1 2` |
| `but` / `does` | runtime mixin | `my $v = 5 but "t"; say $v+1` → `6` |

### 3.11 Chaining comparison

Comparisons **chain**: `1 < 2 < 3` means `1 < 2 and 2 < 3`.

| Numeric | String | Meaning | Example → result |
|---|---|---|---|
| `==` | `eq` | equal | `3 == 3.0` → `True` |
| `!=` | `ne` | not equal | `3 != 4` → `True` |
| `<` | `lt` | less | `1 < 2` → `True` |
| `<=` | `le` | ≤ | |
| `>` | `gt` | greater | |
| `>=` | `ge` | ≥ | |
| `<=>` | `leg` | three-way (`Less`/`Same`/`More`) | `3 <=> 5` → `Less` |
| `cmp` | | generic three-way | `'b' cmp 'a'` → `More` |
| `~~` | | smartmatch | `5 ~~ Int` → `True` |
| `===` | | value identity | `5 === 5` → `True` |
| `eqv` | | structural equivalence | `(1,2) eqv (1,2)` → `True` |
| `=:=` | | container identity | `$a =:= $b` |
| `=~=` / `≅` | | approximately-equal | `1 =~= 1.0000000001` |

```raku
say 1 < 2 < 3;             # → True    chained
say 5 ~~ 1..10;            # → True    range smartmatch
say 'foo' ~~ /o+/;         # → ｢oo｣    regex smartmatch (returns Match)
```

### 3.12 Tight-and / tight-or

| Op | Meaning | Example → result |
|---|---|---|
| `&&` | logical and (returns operand) | `5 && 6` → `6` |
| `\|\|` | logical or | `0 \|\| 'x'` → `x` |
| `//` | defined-or | `Nil // 42` → `42` |
| `^^` | logical xor | `True ^^ False` → `True` |
| `min` | numeric minimum | `5 min 2` → `2` |
| `max` | numeric maximum | `5 max 2` → `5` |

```raku
say 0 // 42;    # → 0     // tests definedness, not truth (0 is defined)
say Nil // 42;  # → 42
```

### 3.13 Ternary

| Op | Meaning | Example → result |
|---|---|---|
| `?? !!` | conditional | `5 ?? 'y' !! 'n'` → `y` |

### 3.14 Assignment

| Op | Meaning |
|---|---|
| `=` | assign (copy) |
| `:=` | bind (alias) |
| `+=` `-=` `*=` `/=` `**=` `%=` | compound arithmetic |
| `~=` | compound concat |
| `x=` `xx=` | compound repeat |
| `\|\|=` `&&=` `//=` | compound logical |

```raku
my $n = 10; $n += 5;  say $n;   # → 15
my $s = 'a'; $s ~= 'b'; say $s; # → ab
```

### 3.15 List infix & loose logic

| Op | Meaning | Example → result |
|---|---|---|
| `,` | list constructor | `1,2,3` |
| `=>` | pair constructor | `a => 1` |
| `Z` | zip | `(1,2,3) Z (4,5,6)` → `([1 4] [2 5] [3 6])` |
| `X` | cross | `(1,2) X (3,4)` → `([1 3] [1 4] [2 3] [2 4])` |
| `...` | sequence (lazy) | `1, 3 ... 11` → `(1 3 5 7 9 11)` |
| `...^` | sequence, exclude end | `1 ...^ 5` → `(1 2 3 4)` |
| `and` `or` `not` `xor` | loose logic | `5 and 6` → `6` |
| `andthen` `orelse` | defined-chaining | |

```raku
say 1, 2, 4 ... 32;       # → (1 2 4 8 16 32)   geometric, inferred *2
say 'a' ... 'e';          # → (a b c d e)        string succession
say (10 ... 1);           # → (10 9 … 1)         counts down
```

### 3.16 Feed operators

| Op | Meaning | Example |
|---|---|---|
| `==>` | forward feed (data flows left→right) | see below |
| `<==` | backward feed (right→left) | |

```raku
(1..10) ==> grep(* %% 2) ==> my @evens;
say @evens;               # → [2 4 6 8 10]

my @evens2 <== grep(* %% 2) <== (1..10);
say @evens2;              # → [2 4 6 8 10]
```

---

## 4. Metaoperators

Metaoperators transform an existing operator into a new one.

### Reduce `[OP]`

Fold a list with an infix operator.

```raku
say [+] 1..10;            # → 55
say [*] 1..5;             # → 120
say [~] <a b c>;          # → abc
say [max] 3, 7, 2;        # → 7
say [<=] 1, 2, 3;         # → True    (chained comparison reduced)
say [\+] 1..5;            # → (1 3 6 10 15)   triangular (\) keeps intermediates
```

### Hyper `»OP«` / `>>OP<<`

Apply an operator element-wise; Unicode `»«` or ASCII `>>`/`<<`. Pointy ends
indicate which side may be auto-extended.

```raku
say (2,3,4) »+» 1;        # → (3 4 5)     scalar broadcast
say (1,2,3) «*« 10;       # → (10 20 30)
say (1,2) »+« (10,20);    # → (11 22)     element-wise
say (1,2,3).map(-*);      # → (-1 -2 -3)  (prefer .map for prefix ops)
```

Hyper method calls: `(1,2,3)>>.abs` calls `.abs` on each element. (Avoid the
`<a b c>>>.meth` spelling — the `>>` collides with the `<…>` closer; use a
parenthesised list.)

### Zip `Z` / Cross `X` with an operator

```raku
say (1,2) Z+ (3,4);       # → (4 6)          zip-with-+
say (1,2) X* (3,4);       # → (3 4 6 8)       cross-with-*
```

### Reverse `R`, Sequential `S`, Cross `X`, Negate `!`

| Meta | Effect | Example → result |
|---|---|---|
| `R` | swap operands | `2 R- 10` → `8` (`10 - 2`) |
| `X` | cross metaop | `Xeqv`, `X~` … |
| `!` | negate a comparison | `!==` already covered by `!=` |

---

## 5. Built-in subroutines

`rakupp` registers **183** built-in subroutines. Below are the commonly used ones
with verified examples, grouped by purpose. The complete alphabetical list is in
[Appendix A](#appendix-a--all-built-in-subroutines).

> **Call-form caveat:** the *method* forms (`@list.map(…)`, `@list.grep(…)`,
> `@list.sort(…)`) are the reliable spelling. The bare-sub form with a **block as
> the first argument** — `map({…}, @list)` — is unreliable in the current build;
> the Whatever-star form `map(*+1, @list)` and all method forms work correctly.

### Output & I/O

| Sub | Meaning | Example → result |
|---|---|---|
| `say` | `.gist` + newline | `say 42` → `42` |
| `print` | `.Str`, no newline | `print "x"` → `x` |
| `put` | `.Str` + newline | `put "x"` |
| `note` | to `$*ERR` + newline | `note "warn"` |
| `printf` / `sprintf` | C-style format | `sprintf('%05.2f', 3.14159)` → `03.14` |
| `warn` | resumable warning | |
| `die` | throw exception | `die "boom"` |
| `prompt` | read a line with a prompt | `prompt("? ")` |
| `slurp` / `spurt` | read / write whole file | `spurt('f', 'data')` |
| `open` / `close` | file handle | `open('f', :r)` |
| `dir` | directory listing | `dir('.')` |
| `run` / `shell` | run external command | `run(<echo hi>)` |

### Math

| Sub | Example → result |
|---|---|
| `abs` | `abs(-5)` → `5` |
| `sign` | `sign(-3)` → `-1` |
| `sqrt` | `sqrt(2)` → `1.4142135623730951` |
| `floor` `ceiling` `round` | `round(3.14159, 0.01)` → `3.14` |
| `exp` `log` `log10` `log2` | `log10(1000)` → `3` |
| `sin` `cos` `atan2` … | `atan2(1,1)` → `0.7853981633974483` |
| `min` `max` `minmax` | `minmax(3,1,2)` → `1..3` |
| `sum` | `sum(1..5)` → `15` |
| `is-prime` | `is-prime(7)` → `True` |
| `srand` `rand` | seed / random |

### Strings

| Sub | Example → result |
|---|---|
| `chars` | `chars('café')` → `4` |
| `uc` `lc` `tc` | `tc('foo')` → `Foo` |
| `chr` `ord` `chrs` `ords` | `chr(65)` → `A` |
| `index` `rindex` | `index('abc','b')` → `1` |
| `split` | `split(',', 'a,b,c')` → `[a b c]` |
| `join` | `join('-', 1,2,3)` → `1-2-3` |
| `words` `lines` | `words('a  b c')` → `(a b c)` |
| `uniname` `uniprop` `unival` | Unicode property lookups |

### Lists & sequences

| Sub | Example → result |
|---|---|
| `elems` `end` | `elems(<a b c>)` → `3` |
| `reverse` | `reverse(1,2,3)` → `[3 2 1]` |
| `sort` | `sort(3,1,2)` → `[1 2 3]` |
| `first` | `first(* > 2, 1,2,3,4)` → `3` |
| `grep` | `grep(* %% 2, 1..6)` → `[2 4 6]` |
| `map` | `map(*+1, 1,2,3)` → `(2 3 4)` |
| `reduce` | `reduce(&infix:<+>, 1..4)` → `10` |
| `produce` | `produce(&infix:<+>, 1..4)` → `(1 3 6 10)` |
| `flat` | `flat((1,2),(3,4))` → `(1 2 3 4)` |
| `slip` | `slip((1,2),(3,4))` → `(1 2 3 4)` |
| `roundrobin` | `roundrobin((1,2,3),(4,5,6))` → `((1 4) (2 5) (3 6))` |
| `lazy` `eager` `cache` | force / defer / memoise |
| `push` `pop` `shift` `unshift` `append` `prepend` `splice` | mutators |
| `keys` `values` `kv` `pairs` | from hashes/lists |

### Type / coercion / introspection

| Sub | Meaning | Example → result |
|---|---|---|
| `so` | boolify | `so(5)` → `True` |
| `not` | negate | `not(0)` → `True` |
| `defined` | is defined? | `defined(Nil)` → `False` |
| `item` | scalarise | `item([1,2])` → `[1 2]` |
| `list` | listify | `list(1,2,3)` → `(1 2 3)` |
| `hash` `set` `bag` `mix` | build containers | `bag(<a a b>)` |
| `VAR` `WHAT` | container / type introspection | |
| `EVAL` | evaluate a string as code | `EVAL('1+2')` → `3` |

### Control, concurrency, testing

| Sub | Purpose |
|---|---|
| `exit` `sleep` `sleep-till` | process control |
| `start` `await` `Promise` | async (returns awaitable) |
| `react` `whenever` `supply` `emit` `take` | supplies / reactive |
| `callsame` `callwith` `nextsame` `nextwith` `samewith` `lastcall` | dispatch redirection |
| `gather` / `take` | lazy generator |
| `plan` `ok` `nok` `is` `isnt` `is-deeply` `like` `unlike` `cmp-ok` `dies-ok` `lives-ok` `throws-like` `subtest` `pass` `flunk` `done-testing` … | the **Test** suite (built in) |

```raku
say EVAL('1 + 2');        # → 3
say (gather { take $_ for 1..3 });   # → [1 2 3]
```

---

## 6. Methods by receiver

`rakupp` implements **562** methods (full list in [Appendix B](#appendix-b--all-methods)).
Below are the high-traffic ones grouped by the type they act on.

### On `Str`

```raku
say 'Hello'.chars;               # → 5
say 'Hello'.uc;                  # → HELLO
say 'Hello'.lc;                  # → hello
say 'Hello'.fc;                  # → hello   (fold-case)
say 'hello'.tc;                  # → Hello   (title-case first)
say 'hello world'.tclc;          # → Hello world
say 'hi there'.wordcase;         # → Hi There
say 'Hello'.flip;                # → olleH
say 'Hello'.substr(1,3);         # → ell
say 'a,b,c'.split(',');          # → [a b c]
say '  hi  '.trim;               # → hi
say 'Hello'.contains('ell');     # → True
say 'Hello'.starts-with('He');   # → True
say 'Hello'.ends-with('lo');     # → True
say 'Hello'.index('l');          # → 2
say 'foo'.comb;                  # → (f o o)
say 'a1b2'.comb(/\d/);           # → (1 2)
say 'abc'.trans('a-c' => 'A-C'); # → ABC
say 'Hello'.subst('l','L',:g);   # → HeLLo
say '2a'.parse-base(16) // 42.base(16);   # → 2A   (see note: parse-base absent)
say 42.base(2);                  # → 101010
```

### On `Int` / `Rat` / numeric

```raku
say (3.14159).round(0.01);       # → 3.14
say (7/2).Int;                   # → 3
say 10.sqrt;                     # → 3.1622776601683795
say (-5).abs;                    # → 5
say 5.is-prime;                  # → True
say 5.polymod(2, 2);             # → (1 0 1)
say (1/3).numerator;             # → 1
say (1/3).denominator;           # → 3
say (1/3).nude;                  # → [1 3]
say 3.14.floor;                  # → 3
say 3.14.ceiling;                # → 4
say (-3.7).truncate;             # → -3
say 255.chr;                     # → ÿ
say 'A'.ord;                     # → 65
say 42.fmt('%05d');              # → 00042
```

### On `List` / `Array` / `Seq`

```raku
say (1,2,3,4).head(2);           # → (1 2)
say (1,2,3,4).tail(2);           # → (3 4)
say (1,2,3,4,5).skip(2);         # → (3 4 5)
say (1,2,3).reverse;             # → (3 2 1)
say (3,1,2).sort;                # → (1 2 3)
say (3,1,2).sort({ $^b <=> $^a });  # → (3 2 1)  (custom comparator)
say <b a>.sort(*.uc);            # → (a b)        (by key)
say (1,2,3).rotate;              # → (2 3 1)
say (1,2,3).rotate(-1);          # → (3 1 2)
say (1,2,3).elems;               # → 3
say (1,2,3).sum;                 # → 6
say (1,2,3,4).grep(* %% 2);      # → (2 4)
say (1,2,3).map(* + 1);          # → (2 3 4)
say (1,2,3,4).first(* > 2);      # → 3
say (1..6).classify(* %% 2).sort;# → (False => [1 3 5] True => [2 4 6])
say (1..5).rotor(2);             # → ((1 2) (3 4))
say (1,2,2,3,3).unique;          # → (1 2 3)
say (1,1,2,2).squish;            # → (1 2)
say (1,2,3).roll(2);             # → e.g. (1 3)   (with replacement)
say (1,2,3).pick(2);             # → e.g. (2 3)   (without replacement)
say (1,2,3).permutations.elems;  # → 6
say (1,2,3).combinations.elems;  # → 8
say (1,2,3).kv;                  # → (0 1 1 2 2 3)
say (1,2,3).join(',');           # → 1,2,3
```

### On `Hash` / `Map`

```raku
my %h = a => 1, b => 2;
say %h.keys.sort;                # → (a b)
say %h.values.sort;              # → (1 2)
say %h.pairs.sort;               # → (a => 1 b => 2)
say %h.kv.sort;                  # → (1 2 a b)
say %h.elems;                    # → 2
say %h.invert.sort;              # → (1 => a 2 => b)
say %h<a>;                       # → 1        (<> literal-key subscript)
say %h{'a'};                     # → 1        ({} expression-key subscript)
say %h.map(*.value).sort;        # → (1 2)
```

### Introspection & coercion (universal)

```raku
say 5.WHAT;                      # → (Int)
say 5.^name;                     # → Int
say 5.WHICH;                     # → Int|5      (value identity)
say 5.HOW.^name;                 # → Metamodel::ClassHOW
say 5.Str; say 5.Int; say 5.Num; say 5.Rat;   # → 5 5 5 5
say 42.so;                       # → True
say Int.defined;                 # → False    (type object is undefined)
say 5.defined;                   # → True
say Int.^mro;                    # method-resolution order
```

---

## 7. Control flow & statement modifiers

### Conditionals

```raku
if 5 > 3      { say 'a' } elsif 5 > 4 { say 'b' } else { say 'c' }
unless 0      { say 'runs' }
with   $maybe { say "defined: $_" }          # runs if defined, topicalises
without $x    { say 'undef' } else { .say }  # inverse of with
```

### Loops

```raku
for 1..3 { .say }
loop (my $i = 0; $i < 3; $i++) { }           # C-style
my $j = 0; while $j < 3 { $j++ }
$j = 0; repeat { $j++ } while $j < 3;        # test-at-bottom
```

### `given`/`when` (topic dispatch)

```raku
given 3 {
    when 1  { say 'one' }
    when 3  { say 'three' }
    default { say '?' }
}                                            # → three
```

Loop controls: `last`, `next`, `redo`. `when`/`given` controls: `proceed`,
`succeed`.

### Everything is an expression: `do`

```raku
my $r = do given 3 { when 3 { 'three' } default { '?' } };  say $r;  # → three
my @sq = do for 1..3 { $_ * $_ };  say @sq;                          # → [1 4 9]
```

### Statement modifiers (postfix forms)

```raku
say $_ for 1..3;
say 'yes' if 5 > 3;
$_++ while $_ < 10;
.say for @list;
```

---

## 8. Routines & signatures

```raku
sub add($a, $b) { $a + $b }               # positional
sub greet($name = 'world') { "hi $name" } # default value
sub vary(*@rest) { @rest.sum }            # slurpy positional
sub named(:$x, :$y) { $x + $y }           # named params
named(x => 1, y => 2);                     # → 3
sub typed(Int $n --> Str) { "$n" }        # type + return constraint
```

Blocks & lambdas:

```raku
my &sq = -> $x { $x * $x };  say sq(6);   # → 36     pointy block
say { $^a + $^b }(2, 3);                   # → 5      placeholder params
say (-> $x { $x + 1 })(10);                # → 11     immediately invoked
my $c = { $_ * 2 };  say $c(21);           # → 42     $_ is the arg
```

Multi-dispatch:

```raku
multi describe(Int $x) { "int $x" }
multi describe(Str $x) { "str $x" }
say describe(3);      # → int 3
say describe('a');    # → str a
```

`multi` candidates are chosen by arity and type; add a `proto` to declare a shared
signature. `return` exits a routine; `return-rw` returns a writable container.

---

## 9. Classes, roles, enums, subsets

### Classes

```raku
class Point {
    has $.x;                         # public, read-only accessor $.x
    has $.y is rw;                   # writable accessor
    method sum { $.x + $.y }
    method dist { sqrt($.x² + $.y²) }
}
my $p = Point.new(x => 3, y => 4);
say $p.sum;                          # → 7
say $p.x;                            # → 3
```

`has $.name` auto-generates an accessor; `has $!name` is private. `.new` is the
default constructor; override `BUILD`/`TWEAK` to customise. Inheritance: `is
Parent`; introspect with `.^name`, `.^methods`, `.^attributes`, `.^mro`.

### Roles

```raku
role Greet { method hi { "hello" } }
class C does Greet { }
say C.new.hi;                        # → hello
```

### Enums

```raku
enum Color <Red Green Blue>;
say Green;                           # → Green
say Green.value;                     # → 1
```

### Subsets

```raku
subset Even of Int where * %% 2;
my Even $e = 4;  say $e;             # → 4    (assigning an odd value would fail)
```

---

## 10. Phasers

Blocks that run at defined points in a program's life.

| Phaser | Runs |
|---|---|
| `BEGIN` | at compile time |
| `END` | at program end |
| `ENTER` / `LEAVE` | on block entry / exit |
| `FIRST` / `NEXT` / `LAST` | loop: first / each / last iteration |
| `KEEP` / `UNDO` | block exit, on success / failure |
| `CATCH` | exception handling within a block |
| `CONTROL` | control exceptions (warnings, `next`, …) |

```raku
{
    CATCH { default { say "caught: {.message}" } }
    die "boom";
}                                    # → caught: boom
```

`try { … }` wraps a block, traps exceptions, and sets `$!`:

```raku
try { die "boom" };  say $!.message; # → boom
```

---

## 11. Regexes & grammars

### Match & substitution operators

| Form | Meaning |
|---|---|
| `/ … /` or `rx/ … /` | regex literal |
| `m/ … /` | match (returns a `Match`) |
| `m:i/ … /` | `:ignorecase` match |
| `m:s/ … /` | `:sigspace` match (whitespace in the pattern must match whitespace) |
| `m:g/ … /` | `:global` — all matches |
| `s/ … / … /` | substitute (in place, on an lvalue) |
| `tr/ … / … /` | transliterate |
| `~~` / `!~~` | apply against a string |

> **Not spec, and inert:** the two-letter forms `mm//` and `ms//` are **not**
> official Raku (the language spells these `m:m//`/`m:ignoremark//` and
> `m:s//`/`m:sigspace//`). `rakupp`'s lexer accepts `mm`/`ms` as match keywords but
> attaches no adverb, so they currently behave like a plain `m//`. Use the
> adverbial forms. (`:sigspace` works via `m:s//`; `:ignoremark` is not yet
> implemented.)

```raku
say 'foo123' ~~ / (\d+) /;    # → ｢123｣      $0 captures the group
say $0;                        # → ｢123｣
say 'hello' ~~ / l+ /;         # → ｢ll｣
say 'ABC' ~~ m:i/ abc /;       # → ｢ABC｣      case-insensitive
'2023-01-15' ~~ / (\d**4) '-' (\d\d) '-' (\d\d) /;
say "$0/$1/$2";                # → 2023/01/15
my $t = 'Hello World'; $t ~~ s/World/Raku/;  say $t;   # → Hello Raku
say 'a1b2c3' ~~ m:g/ \d /;     # → (｢1｣ ｢2｣ ｢3｣)   :g = global
```

Common regex atoms: `\d \w \s` (+ negated `\D \W \S`), `.` any, `<[abc]>` char
class, `<-[abc]>` negated, `+ * ? **N **N..M` quantifiers, `|` alternation, `( )`
capture, `[ ]` non-capture group, `<name>` subrule, `<?before>`/`<?after>`
look-around, anchors `^ $ « »`.

### Grammars

```raku
grammar G {
    token TOP  { <num>+ % ',' }
    token num  { \d+ }
}
say G.parse('12,34,56');       # → ｢12,34,56｣   (a Match, or Nil on failure)
```

`grammar` groups named `token`/`rule`/`regex` rules; `.parse` anchors the whole
string, `.subparse` allows a partial match. Add an actions class with
`:actions($obj)` to build a result tree via `make`/`.made`.

---

## 12. Special & dynamic variables

| Variable | Meaning | Example → result |
|---|---|---|
| `$_` | the topic | `.say for 1..3` |
| `$/` | last match | `$0`, `$<name>` index into it |
| `$!` | last exception | `try {…}; say $!.message` |
| `$0 $1 …` | positional captures | from `$/` |
| `@*ARGS` | command-line arguments | |
| `%*ENV` | environment variables | `%*ENV<HOME>` |
| `$*IN $*OUT $*ERR` | standard handles | |
| `$*PID` | process id | `say $*PID > 0` → `True` |
| `$*KERNEL` | kernel info | `$*KERNEL.name` → `darwin` |
| `$*DISTRO` | distribution info | `$*DISTRO.name` → `macos` |
| `$*RAKU` | language object | `$*RAKU.version` → `6.d` |
| `$*CWD` | current directory | |
| `$?FILE $?LINE` | compile-time file/line | |

> `$*OS` is **not** populated in `rakupp` (returns `(Any)`); use `$*KERNEL` /
> `$*DISTRO` instead.

---

## 13. The type & number tower

Numeric literals promote losslessly and only degrade to floating point when you
opt in (e-notation) or exceed the rational representation.

```
Int  ⊂  Rat  ⊂  FatRat        (exact)
                 ↘
Int → Rat → Num → Complex      (Num = 64-bit float, opt-in via 1e0 etc.)
```

| Type | Literal | Notes |
|---|---|---|
| `Int` | `42`, `0xFF`, `2**100` | arbitrary precision |
| `Rat` | `3.14`, `1/3` | **exact** rational; default for decimals |
| `FatRat` | `1.FatRat / 3` | unbounded-denominator rational |
| `Num` | `1.5e0`, `pi` | IEEE double |
| `Complex` | `3+4i` | |
| `Str` | `"…"` | grapheme-based, Unicode-correct |
| `Bool` | `True` / `False` | an enum; `.Int` → `1`/`0` |
| `Array` `List` `Seq` | `[…]` `(…)` | mutable / immutable / lazy |
| `Hash` `Map` | `{…}` | mutable / immutable assoc |
| `Pair` | `a => 1` | |
| `Range` | `1..5` | |
| `Set` `Bag` `Mix` | `set(…)` `bag(…)` | + mutable `SetHash`/`BagHash`/`MixHash` |
| `Junction` | `1\|2`, `1&2` | autothreading |
| `Nil` / `Any` / `Mu` | the undefined / root types | |

```raku
say 0.1 + 0.2 - 0.3;      # → 0        exact Rat arithmetic (no float error)
say (0.1 + 0.2).raku;     # → <3/10>   it's a Rat
say 0.1e0 + 0.2e0 - 0.3e0;# → 5.55e-17 opt into Num and the error returns
```

---

## 14. rakupp-specific notes & caveats

Places where the current build differs from Rakudo or has known gaps — verified
while writing this sheet:

- **Right-shift `+>` / `~>`** are deliberately not lexed (the `>` collides with
  other syntax). Left-shift `+<` / `~<` work.
- **`$(…)` string interpolation** is unsupported; use `"{ … }"` for computed
  interpolation.
- **`say` of a `Set`/`Bag`** prints one `element ⏎ True`/count line per member
  rather than `Set(a b c)`. Use `.keys.sort` or `.raku` for a stable rendering.
- **Bare-sub call with a leading block** — `map({…}, @list)`, `first({…}, @list)`
  — is unreliable; use the **method form** (`@list.map({…})`) or a Whatever star
  (`map(*+1, @list)`), both of which are correct.
- **`$*OS`** is unset; use `$*KERNEL` / `$*DISTRO`.
- **`mm//` / `ms//`** are non-spec lexer aliases (official Raku uses `m:m//` /
  `m:s//`). In the current build they attach no adverb and behave like plain
  `m//`. `:sigspace` (`m:s//`) works; `:ignoremark` is not implemented.
- A handful of Rakudo methods are absent, e.g. `Str.indices`, `Str.parse-base`,
  `Int.factorial` (use `[*] 1..$n`), `Pair.Hash`, `Bool.pick`.
- The `<a b c>>>.method` spelling trips the `<…>`/`>>` lexer boundary — use a
  parenthesised list, `(1,2,3)>>.method`, for hyper method calls.
- `now` returns a `Num` (POSIX seconds) rather than an `Instant` object.

---

## Appendix A — all built-in subroutines

The 183 subroutines registered by `Interpreter::registerBuiltins()`
(`src/Builtins.cpp`), alphabetically:

```
!!! ... ??? EVAL Slip VAR WHAT __format__ __radix abs acosec acosech acotan
acotanh append asec asech atan2 await bag bail-out bail_out cache callsame
callwith ceiling chars chdir chmod chr chrs cis classify close cmp-ok cosec
cosech cotan cotanh cross dd defined diag die dies-ok dir does-ok done
done-testing done_testing eager elems emit end eval-dies-ok eval-lives-ok
exit exp expmod fail fails-like first flat floor flunk get getc gist grep
hash index is is-approx is-deeply is-prime isa-ok isnt item join keys kv
lastcall lazy lc leave like lines list lives-ok log log10 log2 make
make-temp-dir make-temp-file map max min minmax mix mkdir nextsame nextwith
nok not note ok open ord ords pass plan pop prepend print printf proceed
produce prompt push put quietly react reduce reverse rindex rmdir round
roundrobin run samewith say sec sech set shell shift sign sink skip
skip-rest sleep sleep-till sleep-timer slip slurp snip so sort splice split
sprintf spurt sqrt srand start subtest succeed sum supply take tc
throws-like times todo truncate uc unimatch uniname uninames uniprop unival
univals unlike unlink unpolar unshift use-ok values warn whenever words zip
```

(Test-suite subroutines — `plan`, `ok`, `is`, `like`, `dies-ok`, `subtest`, … —
are built in so Roast `.t` files run without external modules. Aliases such as
`done_testing`/`bail_out` mirror their hyphenated forms; `__format__`/`__radix`
are internal helpers. `!!!` / `???` / `...` are the stub operators — die / warn /
"not-yet-implemented".)

## Appendix B — all methods

The 562 method names dispatched in `Interpreter::methodCall()`
(`src/Builtins.cpp`), alphabetically. Names in `TitleCase` are coercion/type
methods (`.Int`, `.Str`, `.Bag`, `.NFC`); `UPPER-CASE` are introspection/protocol
methods (`.WHAT`, `.AT-POS`, `.BIND-POS`); the rest are ordinary methods.

```
ACCEPTS ASSIGN-KEY ASSIGN-POS AT-KEY AT-POS Array BIND-KEY BIND-POS Bag
BagHash Baggy Bool Bridge Capture Channel Complex DEFINITE DELETE-KEY
DELETE-POS DISTROnames Date DateTime EVAL EXISTS-KEY EXISTS-POS Failure
FatRat HOW Hash IO Instant Int KERNELnames List Map Mix MixHash Mixy NFC
NFD NFKC NFKD Num Numeric Pair Range Rat Real SPEC STORE Seq Set SetHash
Setty Slip Str Stringy Supply THREAD UInt VAR VMnames WHAT WHICH WHO ^name
abs absolute accept accessed acos acosec acosech acosh acot acotan acotanh
acoth acquire acsc acsch act add add_method after all allocate allof
antipair antipairs any anyof api-matcher append archname are arg arity asec
asech asin asinh assuming ast at atan atan2 atanh attributes auth
auth-matcher authority backend base base-repeating basename batch before
bits bless bool-only bounds break broken bytes cache can cancel cancelled
candidates canonpath caps categorize categorize-list cause ceiling changed
chars child chmod chomp chop chr chrs cis classify classify-list cleanup
clear clone close close-stdin closed codename codes comb combinations
command compiler condition config conj contains contents cos cosec cosech
cosh cot cotan cotanh coth count count-only cpu-arch cpu-cores created csc
csch cue d day day-fraction day-of-month day-of-week day-of-year daycount
days-in-month dd-mm-yyyy decode deepmap default defined delayed denominator
desc dir dirname do does done dow duckmap e eager earlier elems emit empty
encode encoding end endian ends-with enums eof err exception excludes-max
excludes-min exists exitcode exp expmod extension f fail fc find_method
finish first first-index flat flatmap flip floor fmt formatter from
from-list from-posix get getc getline gist grab grabpairs grep grep-index
handled has_accessor hash head hh-mm-ss hour hyper id im in in-timezone
indent index indices infinite int-bounds interval invert is-absolute
is-deterministic is-initial-thread is-int is-lazy is-leap-year
is-monotonically-increasing is-prime is-relative is-win isNaN is_dispatcher
isa iso88591 item iterator join julian-date keep kept key keyof keys kill
kv kxxv l lang-version last-date-in-month later latin1 lazy lc level lines
list live loads local lock log log10 log2 lookup lsb made magnitude map
match max maxpairs merge message methods migrate min minmax minpairs minute
mkdir mm-dd-yyyy mode modified modified-julian-date month mro msb multi
name named narrow new new-from-pairs nl-in nl-out nodemap none norm not
note now nude numerator of offset on-close on-demand one open optional ord
ords orig out package pair pairs pairup parameterize parameters params
parent parents parse parse-base parsefile parts path path-sep perl
permutations pick pickpairs pid plus polar poll polymod pop posix postmatch
pred prematch prepend preserving print printf produce protect
protect-or-queue-on-recursion pull-one push push-all push-at-least
push-exactly push-until-lazy put quit r race raku rand re read read-bits
read-ubits readchars readonly reals receive recv reduce release repeated
resolve result resume rethrow returns reverse rindex rmdir
role_typecheck_list roles roll roots rotate rotor round run rw rwx rx s
samecase say schedule-on sec sech second self send shape shift short-name
sibling sign signal signature sin sinh sink sink-all skip skip-at-least
skip-at-least-pull-one skip-one slurp slurp-rest slurpy snip snitch so sort
splice split sprintf spurt sqrt squish stable start starts-with status
stderr stdout subbuf subbuf-rw subparse subst subst-mutate substr substr-eq
substr-rw succ sum t tail take tan tanh tap tc tclc then throw timezone to
today toggle total trans tree trim trim-leading trim-trailing truncate
truncate-to truncated-to try-acquire try_acquire type uc unimatch uniname
uninames uniprop uniprops unique unival univals unlink unlock unpolar
unshift unwrap utc value values version version-matcher volume vow w wait
week week-number week-year weekday-of-month what whatever whole-second
windows1252 wordcase words wrap write write-bits write-ubits wx x year
yyyy-mm-dd z zip
```

---

*Generated against the `rakupp` build in this repository; every `# →` output was
produced by running the snippet. If a feature regresses or a caveat is fixed,
update the relevant section — see [FEATURES.md](FEATURES.md) and
[COOKBOOK.md](COOKBOOK.md) for the companion docs.*
