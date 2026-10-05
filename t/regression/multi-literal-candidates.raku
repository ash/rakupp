# Regression: a literal parameter (`multi h(1)`) takes what conforms to the
# literal's TYPE and equals it — True and an IntStr <1> for `1`, not 1.0 and
# not "1". The native backend (`--exe`) compared with `eqv`, so `h(True)` went
# to the other candidate; it now asks the interpreter's rule
# (Interpreter::literalAccepts) for anything but a plain Int or Str, which it
# compares inline.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

multi h(1) { "one" }
multi h($) { "other" }
multi g("a") { "lit a" }
multi g(Str $s) { "str $s" }
multi g(Int $i) { "int $i" }
multi g($x) { "any {$x.^name}" }
multi k(2.5) { "rat" }
multi k(-1) { "neg" }
multi k($) { "other" }
multi fib(0) { 0 }
multi fib(1) { 1 }
multi fib(Int $n) { fib($n - 1) + fib($n - 2) }

ck((h(1), h(True), h(1.0), h("1")), ("one", "one", "other", "other"), 'an Int literal takes an Int and a Bool');
ck((g("a"), g("b"), g(3), g(True), g(1.5)), ("lit a", "str b", "int 3", "int True", "any Rat"), 'literal, typed and untyped candidates');
ck((k(2.5), k(5/2), k(2.5e0), k(-1), k(-1.0)), ("rat", "rat", "other", "neg", "other"), 'Rat and negative literals');
ck(fib(20), 6765, 'a recursive literal multi');

say $fails ?? "FAILED $fails" !! "PASS";
