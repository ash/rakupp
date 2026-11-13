# Regression (RAKUDO-ISSUES-PLAN R3; rakudo#2068): a typed hash's Pairs
# carry its element type, so `.value = …` through one is checked as `%h<k> =
# …` is.
#
# Before the fix `my Int %h = :42foo; %h.map: { .value = 'meows' }` stored
# the Str, and so did `.value = …` in `for %h.pairs`, `for %h` and through
# `%h.pairs[0]`, where Rakudo refuses X::TypeCheck::Assignment.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub refused(Str $what, &code) {
    my $name = 'nothing';
    try { code(); CATCH { default { $name = .^name } } }
    check $what, $name, 'X::TypeCheck::Assignment';
}

my Int %h = :42foo;
refused '%h.map: { .value = "meows" } (rakudo#2068)', { %h.map: { .value = 'meows' } };
refused 'for %h.pairs', { for %h.pairs { .value = 'x' } };
refused 'for %h', { for %h { .value = 'x' } };
refused '%h.pairs[0].value', { my $p = %h.pairs[0]; $p.value = 'x' };
check 'the hash keeps its Int', %h<foo>, 42;

for %h.pairs { .value = 7 }
check 'an Int is taken', %h<foo>, 7;
for %h -> $p { $p.value = 9 }
check '…through a pointy block too', %h<foo>, 9;

my Str %s = :a<x>;
refused 'a Str hash refuses an Int', { for %s.pairs { .value = 5 } };

my %u = :1a;
for %u.pairs { .value = 's' }
check 'an untyped hash takes anything', %u<a>, 's';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
