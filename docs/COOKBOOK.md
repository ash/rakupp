# Raku++ — Cookbook

A cookbook companion to [FEATURES.md](FEATURES.md): short, runnable one-liner
snippets. For complete example programs, see [examples/](../examples/); for mid-size
showcase programs, [showcase/](../showcase/). **Every
snippet below has been run on `rakupp` and produces the output shown** (`# → …`).
Run any of them with:

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

Decimal literals are exact rationals (`Rat`), not floating-point — so the classic
`0.1 + 0.2` rounding error simply doesn't happen:

```raku
say 0.1 + 0.2 - 0.3;      # → 0        (exact Rat arithmetic, not 5.55e-17)
say (0.1 + 0.2).raku;     # → <3/10>   (a Rat, not a Num)
say 0.1e0 + 0.2e0 - 0.3e0;  # → 5.551115123125783e-17   (opt into Num with e-notation)
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

```raku
sub postfix:<!>(Int $n) { [*] 1..$n }
say 5!;                   # → 120      (user-defined postfix operator)

say <a b c d>[*-1];       # → d        (Whatever-star index)
say (1, 5, 10, 2).sort(~*);  # → (1 10 2 5)   (prefix `~*` sorts as strings)
```

Custom operators come in all six categories, and a meta-operator (`[ ]`, `Z`, …)
can drive a user-defined one:

```raku
sub infix:<avg>($a, $b) { ($a + $b) / 2 }
say 10 avg 20;            # → 15       (user-defined infix)
say [avg] 10, 20, 30;     # → 22.5     (reduce over a user operator)

sub circumfix:<˹ ˺>($x) { $x * 10 }
say ˹4˺;                  # → 40       (custom circumfix)
```

Precedence traits actually reshape binding — `foo` here binds tighter than `+`:

```raku
sub infix:<foo>($a, $b) is tighter(&infix:<+>) { $a * $b }
say 2 + 3 foo 4;          # → 14       (parses as 2 + (3 foo 4), not (2+3) foo 4)
```

Bitwise/boolean operators, function composition, and feed pipelines:

```raku
say 6 +& 3;               # → 2        (numeric AND; also +| +^, ~& ~| ~^, ?& ?| ?^)
my &f = (* + 1) ∘ (* * 2);
say f(5);                 # → 11       (compose: f(x) = (x*2)+1; ASCII alias `o`)
my @r <== sort() <== map(* + 1) <== (5, 3, 1);
say @r;                   # → [2 4 6]  (feed: source flows right-to-left)
```

## Control Flow

```raku
.say for 1 .. 3;                          # → 1 / 2 / 3
given 5 { when * > 3 { say "big" } }      # → big
.say if $_ %% 3 for 1 .. 10;              # → 3 / 6 / 9   (chained modifiers)
my @s = gather { take $_ for 1 .. 3 };
say @s;                                   # → [1 2 3]     (gather/take)
```

A `gather` with an infinite `loop` is lazy — only as much runs as is demanded:

```raku
my $fib = gather { my ($a, $b) = 0, 1; loop { take $a; ($a, $b) = $b, $a + $b } };
say $fib[^10];                            # → (0 1 1 2 3 5 8 13 21 34)
```

`CATCH` handles exceptions; `.resume` continues right after the throw:

```raku
class X::Retry is Exception { method message { "retry" } }
{
    X::Retry.new.throw;
    say "continued";                      # ← reached via .resume
    CATCH { when X::Retry { say "caught"; .resume } }
}
# → caught / continued
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

Wrap a routine — the wrapper runs in front of it and `callsame`s through to the
original:

```raku
sub greet($n) { "hi $n" }
&greet.wrap(-> $n { "[" ~ callsame ~ "]" });
say greet("bob");                         # → [hi bob]    (wrapped)
```

Coercion-type container and a named destructuring parameter:

```raku
my Int(Str) $n = '42';
say $n; say $n.WHAT;                      # → 42 / (Int)   (coerced on assignment)

sub head-tail(@a [$first, *@rest]) { "$first | @rest[]" }
say head-tail([1, 2, 3, 4]);              # → 1 | 2 3 4    (binds @a AND unpacks it)
```

## Containers & Binding

`$_` is rw-aliased to array elements in a `for` loop, so the loop mutates the
array in place:

```raku
my @a = 1, 2, 3;
$_ *= 10 for @a;
say @a;                                   # → [10 20 30]   (elements written back)
```

A `Proxy` is a container whose reads and writes run your code — `FETCH` on read,
`STORE` on write:

```raku
my $c = 0;
my $p := Proxy.new(
    FETCH => method ()   { $c },
    STORE => method ($v) { $c = $v * 2 },
);
$p = 21;
say $p;                                    # → 42          (STORE doubled, FETCH read back)
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

augment class Int { method double { self * 2 } }
say 21.double;                            # → 42          (augment a built-in type)

say Int.^mro;                             # → ((Int) (Cool) (Any) (Mu))   (method resolution order)
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

## File & Path I/O

```raku
say "/tmp/x.txt".IO.extension;            # → txt

my $p = "/tmp/rkpp-demo.txt".IO;
$p.spurt("hello");                        # write
say $p ~~ :e;                             # → True        (filetest adverb: exists)
say $p.s;                                 # → 5           (size in bytes)
$p.chmod(0o600);  say $p.mode;            # → 0600        (permission bits)
$p.unlink;
say $p ~~ :e;                             # → False       (gone)
```

`say`/`print` honour a dynamic `$*OUT` — bind it to any object with a `.print`
method to capture or redirect output:

```raku
my $buf = class { has $.s = ""; method print(\a) { $!s ~= a } }.new;
{ my $*OUT = $buf; say "hi"; say "there"; }   # captured, not printed to screen
say $buf.s.lines.join("|");               # → hi|there
```

## System & Native

```raku
my $p = run "echo", "hi", :out;
say $p.out.slurp(:close).chomp;           # → hi          (capture subprocess output)

say $*RAKU.compiler.name;                 # → Raku++
say $*RAKU.compiler.backend;              # → cpp
```

Call a C function directly with `NativeCall` — integer and floating-point,
including libc and `<math.h>`:

```raku
use NativeCall;
sub strlen(Str is encoded('utf8') --> size_t) is native {*}
sub pow(num64, num64 --> num64) is native {*}
say strlen("hello");                      # → 5      (libc's strlen, via dlsym)
say pow(2e0, 10e0);                       # → 1024   (math.h; floats)
```

## Concurrency & Async

```raku
# Supplies, Supplier, react/whenever
my @seen;
react {
    whenever Supply.from-list(1, 2, 3) { @seen.push($_ * 10) }
}
say @seen;                                # → [10 20 30]

my $s = Supplier.new;
$s.Supply.tap({ say "got $_" });
$s.emit(42);                              # → got 42     (live push to the tap)
```

Combinators build a transforming tap-chain — on a list-backed supply or a live
one — and `zip` pairs streams element-wise:

```raku
my @z;
Supply.zip(Supply.from-list(1, 2, 3), Supply.from-list(<a b c>)).tap({ @z.push(.join) });
say @z;                                   # → [1a 2b 3c]

my @seen;
my $sup = Supplier.new;
$sup.Supply.grep(* %% 2).map(* + 100).tap({ @seen.push($_) });
$sup.emit($_) for 1..6;
say @seen;                                # → [102 104 106]   (grep→map runs per emit)
```

```raku
# Promises: manual keep/break, await, and a broken promise from a dying block
my $p = Promise.new;
$p.keep(42);
say $p.result, " ", $p.status;            # → 42 Kept

my $done = start { die "boom" };
say (try await $done) // "caught: {$!.message}";  # → caught: boom  (die → Broken)

my $a = Promise.allof(Promise.kept(1), Promise.kept(2));
say $a.status;                            # → Kept       (anyof/allof combinators)
```

```raku
# Channels — a thread-safe queue
my $c = Channel.new;
$c.send(1); $c.send(2);
$c.close;
say $c.receive;                           # → 1
say $c.poll;                              # → 2
say $c.closed.status;                     # → Kept       (closed + drained)

# Threads (is-initial-thread is False inside a spawned block)
say Thread.is-initial-thread;             # → True
Thread.start({ say Thread.is-initial-thread }).join;   # → False
```

`atomicint` and the ⚛ operators — a race-free shared counter across workers:

```raku
my atomicint $counter = 0;
await (1..10).map: { start { atomic-fetch-inc($counter) for 1..1000 } };
say $counter;                             # → 10000

my atomicint $x = 0;
$x⚛++; $x⚛++;
say $x;                                   # → 2
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
