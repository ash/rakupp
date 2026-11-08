# A binding failure says what Rakudo's X::TypeCheck says: a Failure refused
# by a typed parameter opens with the failure, "Earlier failure:\n boom\n"
# and its backtrace, then "\nFinal error:\n " and the type check, where the
# type check alone was said; and an `@` parameter given a Hash, a Map or a
# Pair goes on ". You have to pass an explicitly typed array, …", wrapped as
# Rakudo wraps it, named or positional. An Int gets no hint.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }
sub bare($m) { $m.lines.grep(none /^ '  in '/).join("\n") }   # (the frames differ in detail)

sub f(Int $x) { }
ck bare(msg({ f(Failure.new("boom")) })),
   "Earlier failure:\n boom\n\nFinal error:\n Type check failed in binding to parameter '\$x'; expected Int but got Failure (Failure.new(exceptio...)",
   'a Failure refused';
ck so(msg({ f(Failure.new("boom")) }) ~~ / "boom\n  in " .* " line " \d+ "\n\nFinal error:" /), True, 'with its backtrace';
sub fi { fail "inner" }
ck bare(msg({ f(fi()) })), "Earlier failure:\n inner\n\nFinal error:\n Type check failed in binding to parameter '\$x'; expected Int but got Failure (Failure.new(exceptio...)",
   "a sub's fail";
ck (try f(Failure.new("x"))) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'its type';
my $hint = ". You have to pass an\nexplicitly typed array, not one that just might happen to contain\nelements of the correct type.";
sub g(:@l) { }
ck msg({ g(l => {a => 1}) }), "Type check failed in binding to parameter '\@l'; expected Positional but got Hash (\{:a(1)})$hint",
   'a Hash for a named @';
sub g2(@l) { }
ck msg({ g2({a => 1}) }), "Type check failed in binding to parameter '\@l'; expected Positional but got Hash (\{:a(1)})$hint",
   'a Hash for a positional @';
ck msg({ g2(Map.new((a => 1))) }),
   "Type check failed in binding to parameter '\@l'; expected Positional but got Map (Map.new((:a(1))))$hint", 'a Map';
ck msg({ g2((a => 1)) }), "Type check failed in binding to parameter '\@l'; expected Positional but got Pair (:a(1))$hint",
   'a Pair';
my $five = 5;
ck msg({ g2($five) }), "Type check failed in binding to parameter '\@l'; expected Positional but got Int (5)", 'an Int: no hint';
ck msg({ g(l => 5) }), "Type check failed in binding to parameter '\@l'; expected Positional but got Int (5)", 'named, no hint';

say 'PASS';
