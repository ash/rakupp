# Regression: `import M` ran M's body a second time. #158 (1dbfcdb7) made an
# `import` build a module still pending — for a BEGIN/CHECK that runs before
# the mainline reaches `module M {…}` — but a package never retired its
# pending record when normal flow built it, so every later `import M` built it
# again: statements, side effects and all
# (S06-operator-overloading/imported-subs.t: "2 1 2 1" came out "2 1 2 1 2 1").

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

my @results;
module m {
    sub infix:<o1> ($a, $b) is equiv(&infix:<+>)  is export { push @results, 1 }
    sub infix:<o2> ($a, $b) is tighter(&infix:<+>) is export { push @results, 2 }
    1 o1 1 o2 1;
}
import m;
1 o1 1 o2 1;
ok @results eq '2 1 2 1', "the body ran once (got {@results})";

my $ran = 0;
module Counted { $ran++; sub ff is export { 7 } }
import Counted;
import Counted;
ok ff() == 7, 'the import still imports';
ok $ran == 1, "a twice-imported module's body ran once (ran $ran)";

# …and #158 itself: a CHECK importing a module declared above it
module FF { sub twice($x) is export { 2 * $x } }
my $from-check;
CHECK { import FF; $from-check = twice(21) }
ok $from-check == 42, 'import in a CHECK sees the module above it';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
