# A coercion type spelled by the short name an import gave it — GitHub issue #90.
#
# `class N-Error is export` inside `unit class Gnome::Glib::T-error` is
# registered as Gnome::Glib::T-error::N-Error, and its importer writes
# `my N-Error() $ne = $error`. The coercion looked the target up by that exact
# spelling, found no class, and died "Impossible coercion from 'N-Object' into
# 'N-Error'" without calling the class's COERCE. Writing the full name worked.
# Every form a coercion type takes is checked, for a unit class and a unit
# module.
#
# Passes under both rakupp and Rakudo.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppNestedCoerce;
use RakuppNestedCoerceM;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my NCInner() $a = 1;
ck $a.^name, 'RakuppNestedCoerce::NCInner', 'variable: the nested class answers';
ck $a.v, 1, 'variable: through its COERCE';

sub p(NCInner() $x) { $x.v }
ck p(2), 2, 'parameter';

class H { has NCInner() $.h }
ck H.new(h => 3).h.v, 3, 'attribute';

ck NCInner(4).v, 4, 'call form';

sub r(--> NCInner()) { 5 }
ck r().v, 5, 'return type';

my NCInner(Int) $z = 6;
ck $z.v, 6, 'with a constraint type';

my NCInnerM() $m = 7;
ck $m.^name, 'RakuppNestedCoerceM::NCInnerM', 'unit module: the nested class answers';
ck $m.v, 70, 'unit module: through its COERCE';

die "$fails failure(s)" if $fails;
say 'PASS';
