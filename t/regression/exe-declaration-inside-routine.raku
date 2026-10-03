# Regression: an `enum` or a `class` declared inside a routine, in a program
# compiled with --exe. Only TOP-LEVEL ones were ever emitted; one in a sub was
# dropped: `enum Size (S => 1 + 1, M => 5)` there read `0 0` (Rakudo `2 5`), a
# plain `enum Col <R G>` as well, and a `my class` had no methods. The refusal
# of non-literal enum values only ever saw the top level. Both are refused now
# when they are not at the top level, and the program bundles the interpreter.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub computed() { enum Size (S => 1 + 1, M => 5); S.Int ~ ' ' ~ M.Int }
sub literal()  { enum Col <R G B>; G.Int ~ ' ' ~ B.key }
sub with-class($n) {
    my class K { has $.v; method twice { $!v * 2 } }
    K.new(v => $n).twice
}

ck computed(),     '2 5', 'an enum with computed values in a sub';
ck literal(),      '1 B', 'a literal enum in a sub';
ck with-class(21), 42,    'a class declared in a sub has its methods';
{
    enum Dir <N E S W>;
    ck W.Int, 3, 'an enum in a bare block';
}

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
