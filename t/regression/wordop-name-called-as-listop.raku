# Regression: `say min(3, 9)` printed a blank line (2026-08-30). A word-infix
# operator name was vetoed from starting a list operator's argument — the rule
# that keeps `Seq eqv Seq` and `$x div $y` infix — so `say` became a
# no-argument call and `min` an infix between it and the parenthesised list.
# An identifier TIGHT against `(` is a call in Raku, never an infix.
# Found by the raku-corpus differential: books/perl6-at-a-glance/sub3.pl had
# been recorded MATCH and was silently printing nothing.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- the builtins, as a listop's first (bare) argument ----------------------
ok(min(3, 9) == 3, 'min(3, 9) is 3');
my @out;
@out.push: min(3, 9);
@out.push: max(3, 9);
ok(@out eqv [3, 9], 'min/max called tight-paren give their values');

# `say`'s argument is the CALL, not an infix between say and a parenthesised list
ok(("" ~ min(3, 9)) eq '3', 'min(3, 9) in an expression');
ok(minmax(3, 9).gist eq '3..9', 'minmax(3, 9) is a Range');

# --- a user sub sharing a word-operator's name ------------------------------
# In its own block: a `sub min` declaration is lexical and would shadow the
# builtin for the whole file, which the parenless cases below still need.
{
    sub min($x, $y) { $x < $y ?? $x !! $y }
    ok(min(-2, 2) == -2, 'a user sub named min is callable');
    ok(min(42, 24) == 24, '…and takes its second argument');
}

# --- the PARENLESS listop forms, decided by what is on the left -------------
# After a lowercase bareword — a routine name — min/max/minmax start its
# argument; after a bare TYPE NAME the infix is meant.
my @a = 5, 2, 8;
ok((min @a) == 2, 'min @a is a call, not an infix');
ok((min 3, 9) == 3, 'min 3, 9 takes both arguments');
sub lower($x) { $x + 100 }
ok((lower min 3, 9) == 103, 'lower min 3, 9 is lower(min(3, 9))');
ok((Inf min 5) == 5, 'after a bare TYPE NAME the infix still wins');

# --- and the infix reading still holds where it belongs ---------------------
ok((3 min 9) == 3, 'infix min still works');
ok((3 max 9) == 9, 'infix max still works');
ok((Int eqv Int), 'a word infix after a bareword type is still an infix');
ok((7 div 2) == 3, 'infix div still works');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
