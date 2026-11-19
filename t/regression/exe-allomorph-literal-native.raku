# Regression guard: an allomorph literal compiles under --exe, and a Num
# literal keeps its sign and its Inf and NaN.
#
# `<42>`, `<1.5>`, `<2e3>` and the numeric words of `< … >` are AllomorphLit
# nodes, which Codegen refused: one in a program bundled the whole of it with
# the interpreter (42 Roast files had it as their first blocker), and the note
# named it "an unsupported construct (NK 24)". Codegen::ex now makes the value
# as evalExpr does: the number, typed IntStr, RatStr, NumStr or ComplexStr, its
# spelling the source's. On the way, a Num literal was written into the C++
# with `%.17g` alone: -0e0 became the Int `-0` (its sign lost, so `val(<-0e0>)`
# was +0) and Inf and NaN would be `inf` and `nan`; and the imaginary half of
# `<-0-0i>` added a real part of +0, so its real part lost its sign too. This
# file must compile natively (t/run.raku's list) and print the interpreter's
# answers, which are Rakudo 2026.09's.

my @got;
sub o(**@a) { @got.push: @a.map(*.Str).join(' ') }
sub neg-zero($n) { $n == 0 && $n.Str eq '-0' }

for ^3 {
    for <42>, <1.5>, <2e3>, <-7>, <0x1F> { o .^name, .Str, .Numeric }
    o <42> + 1, <42> eq '42', <42> ~~ Int, <42> ~~ Str, <42>.raku;
    o <1 two 3.5>.map(*.^name);
    my %h = :x<12>;
    o %h<x>.^name, <00012>.Str, <00012> + 0;
    o neg-zero(-0e0), neg-zero(<-0e0>.Num), neg-zero(val('-0e0').Num), neg-zero(0e0);
    o <-0-0i>, <-0+0i>, <0-0i>, <-0-0i> === <0-0i>, <-0-0i> === <-0-0i>;
    o Inf, -Inf, NaN, <Inf>.^name, <-Inf>, <NaN>, 1e308 * 10;
}

my @want = flat (q:to/WANT-END/.lines) xx 3;
IntStr 42 42
RatStr 1.5 1.5
NumStr 2e3 2000
IntStr -7 -7
IntStr 0x1F 31
43 True True True IntStr.new(42, "42")
IntStr Str RatStr
IntStr 00012 12
True True True False
-0-0i -0+0i 0-0i False True
Inf -Inf NaN NumStr -Inf NaN Inf
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
