# Regression: `~` written out meets a user `multi infix:<~>` over an object or
# an enum operand (2026-11-05, found beside SPEED-BACKLOG 53). evalBinary's
# own arm for `~` (armCat) asked a user candidate only over core operands, and
# concatenated an object through its Str before anything looked: with an
# (Rt, Int) candidate declared, `Rt.new ~ 1` was `Rt<…>1`, `Red ~ "x"` with a
# (Color, Str) candidate was "Redx", while the same shapes over `**` or `+`,
# and `[~]`, `Z~`, `~=` and `&infix:<~>(…)`, took the candidate. As old as
# v5.6.2. The arm now asks objectOperandInfix first, as the general path does.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class Rt { method Str { "rt" } }
multi infix:<~>(Rt $a, Int $b) { "rt-cat" }
multi infix:<~>(Rt $a, Rt $b) { "rt-rt" }
my $r = Rt.new;
ck(Rt.new ~ 1, "rt-cat", 'an (object, Int) candidate takes `Rt.new ~ 1`');
ck($r ~ 1, "rt-cat", '…and a variable holding the object');
my $one = 1;
ck($r ~ $one, "rt-cat", '…and two variables');
ck($r ~ $r, "rt-rt", 'an (object, object) candidate');
ck(1 ~ $r, "1rt", 'no candidate for (Int, object): the object concatenates by its Str');
ck($r ~ "s", "rts", '…nor for (object, Str)');
ck(([~] $r, 1), "rt-cat", '`[~]` takes it (it always did)');
ck((($r,) Z~ (1,)).List, ("rt-cat",), '`Z~` takes it (it always did)');
{ my $s = $r; $s ~= 1; ck($s, "rt-cat", '`~=` takes it (it always did)'); }

enum Color <Red Green>;
multi infix:<~>(Color $c, Str $s) { "color-cat" }
ck(Red ~ "x", "color-cat", 'a (enum, Str) candidate takes an enum operand');
ck("x" ~ Red, "xRed", '…and the built-in the other way round');

# with no candidate that takes them, object operands concatenate as before
role R { method Str { "R's-Str" } }
class Plain { method Str { "plain" } }
ck(("bb" but R) ~ Plain.new, "bbplain", 'a Str with a role mixed in concatenates by its value');
ck("a" ~ "b", "ab", 'two Strs are the built-in');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
