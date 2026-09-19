# Regression: a `my` in a loop's HEADER is scoped to the ENCLOSING BLOCK, so two
# nested loops that declare the same name declare TWO variables, and the inner
# one shadows the outer only inside the outer loop's body.
#
# The native backend got this wrong and said nothing about it:
#
#     my $t = 0;
#     loop (my $i = 0; $i < 3; $i++) { loop (my $i = 0; $i < 3; $i++) { $t = $t + 1 } }
#     say $t;          # 9 interpreted and under Rakudo, 3 compiled
#
# `Codegen::hoistExprDecls` pre-declares an expression-position `my` as a C++
# variable and records the name so a later mention assigns into it rather than
# declaring a second. That record was kept per FUNCTION BODY rather than per C++
# scope, so the inner `my $i` found `$i` already hoisted, emitted no declaration,
# and reused the outer loop's C++ variable. The inner init then reset the outer
# counter, the outer loop ran exactly once, and the answer came out a third of
# the right one — in the DEFAULT compile, with `-O` and without.
#
# Worth keeping as a regression rather than a note because of how it hid: the
# optimizer differential (t/exe/run.raku) compares the two compiled lanes with
# each other and BOTH were wrong identically, so it could not see this. It was
# found by generating nested loop shapes, which is what t/exe/fuzz.raku now does.
#
# Every value below is what Rakudo prints.

# Rakudo prints "Redeclaration of symbol" warnings on the blocks below, to
# stderr, and then honours the shadowing anyway. That is the point of the file,
# so the warnings are expected and not something to silence by renaming: a test
# that avoids the shadow does not test the shadow.

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eqv $want }

# The reported shape. `$i` after the loop is the OUTER one, which ran to 3.
my $a = 0;
loop (my $i = 0; $i < 3; $i++) { loop (my $i = 0; $i < 3; $i++) { $a = $a + 1 } }
check $a, 9, 'a nested loop redeclaring the header name runs the full product';
check $i, 3, 'and the outer header `my` is the one visible afterwards';

# Three deep, to be sure it is not a one-level special case.
my $c = 0;
loop (my $k = 0; $k < 2; $k++) {
    loop (my $k = 0; $k < 2; $k++) {
        loop (my $k = 0; $k < 2; $k++) { $c = $c + 1 } } }
check $c, 8, 'three levels of the same name';
check $k, 2, 'and the outermost still reads its own counter';

# The outer counter must stay readable inside the outer body, after an inner
# loop that shadowed nothing — this is the case that broke first.
my @seen;
loop (my $m = 0; $m < 3; $m++) { loop (my $n = 0; $n < 2; $n++) { }; @seen.push($m) }
check @seen.join(','), '0,1,2', 'the outer counter survives an inner loop';
check $m, 3, 'and ends where it should';

# A `my` in an `if` branch shadows a header `my` for that branch only.
my $e = 0;
loop (my $p = 0; $p < 3; $p++) { if $p > 0 { my $p = 100; $e = $e + $p } }
check $e, 200, 'an inner block `my` shadows the header one';
check $p, 3, 'without disturbing it';

# A `while` body redeclaring the name its condition tests.
my $g = 0; my $h = 0;
while $g < 4 { my $g = 100; $h = $h + $g; $g = 0; last if $h > 500 }
check $h, 600, 'a while body may redeclare the name its condition tests';

# The ordinary, non-shadowing case must not have regressed.
my $f = 0;
loop (my $u = 0; $u < 3; $u++) { loop (my $v = 0; $v < 3; $v++) { $f = $f + 1 } }
check $f, 9, 'distinct header names still work';
check $u, 3, 'and the outer one ends where it should';

# The same defect in the OTHER set, found while checking this one. A top-level
# `my` becomes a C++ global, and the "am I at the top level" flag stayed true
# through every nested block of the mainline — so a `my` inside a block sharing
# a name with a top-level one assigned the GLOBAL instead of declaring a local,
# and the outer variable came back changed.
my $g = 7;
{ my $g = 100; check $g, 100, 'a block-local `my` shadows a top-level one' }
check $g, 7, 'and leaves the top-level one alone';

my $h = 1;
if True {
    my $h = 2;
    if True { my $h = 3; check $h, 3, 'three blocks deep' }
    check $h, 2, 'two deep';
}
check $h, 1, 'and the outermost is untouched';

# A loop body shadowing a top-level name, which is where it would bite in real
# code: the loop would then be writing the program's variable.
my $i2 = 50; my $sum2 = 0;
for 1 .. 3 { my $i2 = $_ * 10; $sum2 = $sum2 + $i2 }
check $sum2, 60, 'a loop body shadowing a top-level name computes with its own';
check $i2, 50, 'and the top-level one is unchanged';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
