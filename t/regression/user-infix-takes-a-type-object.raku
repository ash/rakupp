# Regression (RAKUDO-ISSUES-PLAN R4; rakudo#6860): the program's own
# `multi infix:<op>` meets the TYPE OBJECT of one of its classes, as it meets
# an instance.
#
# Before the fix `class A { method Str { self } }; multi infix:<~>(Str() $a,
# A $b) { "B" }; say "A: " ~ A` printed "A: ", the built-in concatenation,
# where Rakudo prints B. An instance (`"A: " ~ A.new`) already met the
# candidate; the type object went straight to the built-in, which
# stringified it as an undefined value.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

class A { method Str { self } }
multi infix:<~>(Str() $a, A $b) { "B" }
check '"A: " ~ A (rakudo#6860)', "A: " ~ A, 'B';
check '…and an instance, as before', "A: " ~ A.new, 'B';

class U { }
multi infix:<+>(U:U $a, Int $b) { 'plus' }
multi infix:<*>(U:U $a, Int $b) { 'times' }
multi infix:<==>(U:U $a, U:U $b) { 'eq' }
multi infix:<cmp>(U:U $a, U:U $b) { 'cmp' }
check 'a numeric operator', U + 1, 'plus';
check 'another', U * 2, 'times';
check 'a comparison', U == U, 'eq';
check 'cmp', U cmp U, 'cmp';
check 'the built-ins otherwise', (3 * 2, 1 + 1, 1 cmp 2), (6, 2, Less);

# a core type object is still the built-in's: undefined in numeric context
multi infix:<+>(Str $a, Int $b) { 's' }
my $name = 'nothing';
try { my $x = Int + 1; CATCH { default { $name = .^name } } }
check 'Int + 1 beside an unrelated candidate', $name, 'X::Numeric::Uninitialized';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
