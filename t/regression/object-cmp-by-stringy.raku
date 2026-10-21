# Regression: `cmp` over OBJECTS orders them as Rakudo does (2026-10-21).
# valueCmp compared an object by its default string, `Name<address>`, so
# `sort`, `min`, `max`, `[max]`, `Zcmp` and `before` over objects answered by
# allocation order and changed from run to run; `$a cmp $b` itself went through
# .Numeric instead. Rakudo's `cmp` is `a.Stringy cmp b.Stringy` unless both
# sides are Reals: an object is Same as itself, a `does Real` / `is Int` object
# compares as its number, and a `method Stringy` outranks `method Str`.
# `sort`, `min` and `max` also took `&infix:<cmp>` / `&[cmp]` for a key mapper
# (a multi dispatcher has no signature of its own), so a user `multi
# infix:<cmp>` passed to them was ignored.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class V { has $.x; method Numeric { $!x }; method Str { "s" ~ $!x } }
my ($a, $b) = V.new(x => 10), V.new(x => 9);

# "s10" lt "s9": the strings decide, not the numbers and not the addresses.
# Each check runs over freshly made objects several times, since an
# address order is right by chance about half the time.
for ^4 {
    my ($p, $q) = V.new(x => 10), V.new(x => 9);
    ck($p cmp $q, Less, "`cmp` is by .Str (round $_)");
    ck((($p,) Zcmp ($q,)).List, (Less,), "`Zcmp` is by .Str (round $_)");
    ck(sort($p, $q).map(*.x).List, (10, 9), "`sort` is by .Str (round $_)");
    ck(([max] $p, $q).x, 9, "`[max]` is by .Str (round $_)");
    ck((($p,) Zmin ($q,)).List, ($p,), "`Zmin` is by .Str (round $_)");
}
ck($a before $b, True, '`before` is by .Str');
ck($a after $b, False, '`after` is by .Str');
ck(($a max $b).x, 9, '`max` is by .Str');
ck(($a min $b).x, 10, '`min` is by .Str');
ck(V.new(x => 2) before V.new(x => 10) before V.new(x => 3), False, 'a `before` chain is by .Str');

my @v = V.new(x => 1), V.new(x => 3), V.new(x => 2), V.new(x => 10);
ck(@v.sort.map(*.x).List, (1, 10, 2, 3), '`.sort` is by .Str');
ck((@v.min.x, @v.max.x), (1, 3), '`.min` and `.max` are by .Str');
ck($a cmp $a, Same, 'an object is Same as itself');
ck(V.new(x => 1) cmp V.new(x => 1), Same, 'two objects with one string are Same');
ck(V.new(x => 3) cmp "s4", Less, 'an object against a Str');
ck(5 cmp V.new(x => 3), Less, 'an Int against a non-Real object is by string');
ck(V.new(x => 3) cmp Inf, Less, 'Inf is above any object');
ck(-Inf cmp V.new(x => 3), Less, '-Inf is below any object');

class Q { has $.x; method Stringy { "q" ~ $!x }; method Str { "z" } }
ck(Q.new(x => 2) cmp Q.new(x => 1), More, 'a `method Stringy` outranks `method Str`');

class R does Real { has $.v; method Bridge { $!v.Num } }
ck(R.new(v => 10) cmp R.new(v => 9), More, 'two Real objects compare as numbers');
ck((R.new(v => 10), R.new(v => 9), R.new(v => 100)).sort.map(*.v).List, (9, 10, 100), '…and sort as numbers');
ck(R.new(v => 10) cmp 9, More, 'a Real object against an Int');
ck(11 cmp R.new(v => 10), More, 'an Int against a Real object');
ck(R.new(v => 3) before R.new(v => 20), True, '`before` over Real objects');

class MyInt is Int { }
ck(MyInt.new(10) cmp MyInt.new(9), More, 'an `is Int` object compares as its Int');
ck((MyInt.new(10), MyInt.new(9)).sort.map(*.Str).List, ("9", "10"), '…and sorts so');

ck(((1 => V.new(x => 3)), (1 => V.new(x => 10))).sort.map(*.value.x).List, (10, 3), 'a Pair value is by .Str');
ck(((V.new(x => 3), 1), (V.new(x => 10), 1)).sort.map(*[0].x).List, (10, 3), 'a List element is by .Str');

# an operator handed to sort / min / max is a comparator, user candidates too
{
    multi sub infix:<cmp>(V $p, V $q) { $q.x <=> $p.x }
    my @w = V.new(x => 1), V.new(x => 3), V.new(x => 2);
    ck(@w.sort(&infix:<cmp>).map(*.x).List, (3, 2, 1), '`.sort(&infix:<cmp>)` takes a user multi');
    ck(@w.sort(&[cmp]).map(*.x).List, (3, 2, 1), '`.sort(&[cmp])` too');
    ck(sort(&infix:<cmp>, @w).map(*.x).List, (3, 2, 1), '`sort(&infix:<cmp>, …)` too');
    ck(@w.max(&[cmp]).x, 1, '`.max(&[cmp])` too');
    ck(@w.min(&[cmp]).x, 3, '`.min(&[cmp])` too');
    ck(@w.max(:by(&[cmp])).x, 1, '`.max(:by(&[cmp]))` too');
    ck(@w.sort.map(*.x).List, (1, 2, 3), 'a plain `.sort` is CORE `cmp`, not the user multi');
}
ck((3, 1, 2).sort(&infix:<cmp>).List, (1, 2, 3), '`.sort(&infix:<cmp>)` over Ints');
ck((3, 10, 2).sort(&[leg]).List, (10, 2, 3), '`.sort(&[leg])` over Ints');
ck((3, 1, 2).sort(&[<=>]).List, (1, 2, 3), '`.sort(&[<=>])` over Ints');
ck((3, 1, 2).max(&[cmp]), 3, '`.max(&[cmp])` over Ints');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
