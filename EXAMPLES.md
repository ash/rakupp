# Raku++ — Runnable Examples

A cookbook companion to [FEATURES.md](FEATURES.md). **Every snippet below has been
run on `rakupp` and produces the output shown** (`# → …`). Run any of them with:

```sh
./build/rakupp -e 'CODE'
```

---

## Lexical & Literals

```raku
say 2 ** 100;             # → 1267650600228229401496703205376  (bignum)
say 0xFF + 0b1010;        # → 265                                (radix literals)
say 3 + 4i;               # → 3+4i                               (Complex)
say (1/3).nude;           # → [1 3]                              (Rat numerator/denominator)
say "café".chars;         # → 4                                  (grapheme count)
say ∞;                    # → Inf
```

## Operators

```raku
say 12 %% 3;              # → True     (divisible by)
say 12 !%% 5;             # → True     (NOT divisible by)
say [+] 1 .. 10;          # → 55       (reduce)
say (1,2,3) Z (4,5,6);    # → ([1 4] [2 5] [3 6])   (zip)
say (1,2) X (3,4);        # → ([1 3] [1 4] [2 3] [2 4])  (cross)
my @a = 1, 2, 3;
say @a >>*>> 2;           # → (2 4 6)  (hyper)
say 5 <=> 3;              # → More     (spaceship)
```

## Control Flow

```raku
.say for 1 .. 3;                          # → 1 / 2 / 3
given 5 { when * > 3 { say "big" } }      # → big
.say if $_ %% 3 for 1 .. 10;              # → 3 / 6 / 9   (chained modifiers)
my @s = gather { take $_ for 1 .. 3 };
say @s;                                   # → [1 2 3]     (gather/take)
```

## Subs, Signatures & Dispatch

```raku
multi f(Int $x) { "int" }
multi f(Str $x) { "str" }
say f(3), f("a");                         # → intstr      (multi dispatch)

my &sq = -> $x { $x * $x };
say sq(5);                                # → 25          (lambda)

sub greet(:$name = "world") { "hi $name" }
say greet(name => "Raku");                # → hi Raku     (named param + default)

say (1 .. 5).map(* ** 2);                 # → (1 4 9 16 25)  (Whatever)
```

## Objects, Classes, Roles

```raku
class Point { has $.x; method sq { $!x ** 2 } }
say Point.new(x => 4).sq;                 # → 16

role Greet { method hi { "hi" } }
class C does Greet {}
say C.new.hi;                             # → hi

enum Color <red green blue>;
say green.value;                          # → 1

say 42.^name;                             # → Int         (metamodel)
```

## Regexes & Grammars

```raku
if "2024-01-15" ~~ /(\d+)\-(\d+)\-(\d+)/ {
    say "$0/$1/$2";                       # → 2024/01/15   (captures interpolate in strings)
}

say "hello world".subst(/o/, "0", :g);    # → hell0 w0rld  (string replacement)
say "hello world".subst(/\w+/, *.uc, :g); # → HELLO WORLD  (callable replacement)

say S/cat/dog/ given "cat food";          # → dog food     (non-mutating S///)

say "abcДЕФ".comb(/<:Latin>/).join;       # → abc          (script property)
say "αβγabc".comb(/<:Greek>/).join;       # → αβγ

grammar G { token TOP { \d+ } }
say G.parse("123").defined;               # → True
```

## Unicode

```raku
say "🇦🇺".chars;                           # → 1           (flag = 1 grapheme, UAX #29)
say "café".NFD.codes;                     # → 5           (decomposed codepoints)
say "A".uniname;                          # → LATIN CAPITAL LETTER A
say "½".unival;                           # → 0.5
say "a".uniprop("Script");                # → Latin
say "Д".uniprop("Script");                # → Cyrillic
say "\c[LATIN SMALL LETTER A]";           # → a           (named char)
```

## Data Types & Built-ins

```raku
say (3, 1, 2).sort;                       # → (1 2 3)
say <a b a c b>.Bag<a>;                   # → 2           (multiset count)
my %h = a => 1, b => 2;
say %h.pairs.sort;                        # → (a => 1 b => 2)
say (1 .. 10).grep(* %% 2).sum;           # → 30
say <the quick brown>.map(*.tc);          # → (The Quick Brown)
```

## I/O, System, Concurrency

```raku
my $p = run "echo", "hi", :out;
say $p.out.slurp(:close).chomp;           # → hi          (capture subprocess output)

say $*RAKU.compiler.name;                 # → Raku++
say $*RAKU.compiler.backend;              # → cpp
say "/tmp/x.txt".IO.extension;            # → txt
```

## Phasers, State & Testing

```raku
END { say "bye" }
say "hi";                                 # → hi / bye    (END runs last)

my $x = do { state $n = 0; ++$n };
say $x;                                   # → 1           (state variable)
```

```raku
use Test;
plan 2;
ok 1 == 1, "eq";                          # → ok 1 - eq
is 2 + 2, 4, "math";                      # → ok 2 - math
```
