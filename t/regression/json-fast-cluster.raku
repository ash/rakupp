# Regression: ten general faults found by running JSON::Fast's own test suite
# (the dist ends 14/14 — one better than Rakudo, which fails a race file here).
# Every expectation checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- enum values do Enumeration; a pair-valued enum keeps its values --------
enum Bloop <Squee Moo Meep>;
ck (Squee ~~ Enumeration), True, 'an enum value does Enumeration';
ck (Squee ~~ Bloop), True, 'and its own type';
ck (5 ~~ Enumeration), False, 'a plain Int does not';
enum Blerp (One => "Eins", Two => "Zwei");
ck One.value, "Eins", 'a pair-valued enum keeps the value';
ck One.key, "One", 'beside its key';
ck Bloop.defined, False, 'the enum TYPE object is undefined';
my $fired = False;
with Bloop { $fired = True }
ck $fired, False, 'so `with` does not fire on it';

# --- Bool:: and enum stashes ------------------------------------------------
ck Bool::.values.sort.List.raku, (False, True).List.raku, 'Bool:: enumerates the enum';
ck Bloop::.values.elems, 3, 'a user enum stash holds its values';

# --- .join / nqp-level concat compose (NFG) ---------------------------------
ck ("b", "a", 0x30A.chr).join.encode.bytes, 3, '.join composes combining marks';
ck ("a" ~ 0x30A.chr).encode.bytes, 2, 'as ~ already did';

# --- an object doing Positional/Associative answers istype ------------------
class TC does Positional does Associative {
    method list { (1, 2).List }
    method of { Mu }   # both roles supply one; the class must resolve it
}
use nqp;
ck (?nqp::istype(TC.new, Associative)), True, 'a role arrives at nqp::istype';
ck (?nqp::istype("0e".Numeric, Failure)), True, 'and a Failure matches Failure';

# --- .Rat on a RatStr sheds the Str half ------------------------------------
ck RatStr.new(0.5, "x").Rat.^name, 'Rat', '.Rat on a RatStr is a plain Rat';
ck IntStr.new(5, "x").Int.^name, 'Int', '.Int on an IntStr is a plain Int';

# --- Rational[Int,Int].new ---------------------------------------------------
ck Rational[Int,Int].new(3,10) == 0.3, True, 'the parameterized role constructs';

# --- a hyper around a user infix --------------------------------------------
multi sub infix:<=~=>(Int \l, Int \r) { l == r }
my @a = 1, 2, 3;
my @b = 1, 2, 4;
ck (@a »=~=« @b).List, (True, True, False), 'a hyper around a user infix';
my @c = 1, 2;
@c <<+=>> 10;
ck @c.List, (11, 12), 'hyper compound assignment is untouched';

# --- ternary and inc/dec as rw write-back targets ---------------------------
my $x = 1; my $y = 2;
(True ?? $x !! $y) = 9;
ck $x, 9, 'a ternary is an lvalue over the picked branch';
sub bump(int $p is rw) { $p = $p + 5 }
sub relay(int $pos is rw) { bump(++$pos); $pos }
my int $z = 0;
ck relay($z), 6, 'rw write-back reaches through ++$var';
ck $z, 6, 'and all the way to the origin';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
