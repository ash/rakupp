# Restoring coverage lost when a failed `use` became fatal: roast's
# packages/S12-meta/lib/TestHOW could not load, and S12-meta/classhow.t fell from
# 7/7 to 1/1. Two real gaps, both worth having on their own:
#
#   1. `Foo::<bar>` — a slot in a package's symbol table. TestHOW does
#      `EXPORTHOW::<class> = TestHOW`, which parsed as a call to a routine named
#      `EXPORTHOW::` and died "Cannot modify an immutable value".
#   2. Metamodel::PackageHOW/ModuleHOW/GrammarHOW `.new_type` — only ClassHOW and
#      the role HOWs had it.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# 1. a package symbol-table slot round-trips
my package P {}
P::<answer> = 42;
check(P::<answer>, 42, 'a package symbol slot assigns and reads back');
P::<answer> = 43;
check(P::<answer>, 43, 'and re-assigns');
check(P::<never-set>.defined, False, 'an unset slot is undefined, not an error');

# a type object in a slot, which is what EXPORTHOW actually holds. The type is
# named via .WHO-free comparison rather than .^name because a SEPARATE bug leaks a
# `my package P {}` block's prefix into whatever is declared after it (`class Q {}`
# then reports P::Q where Rakudo says Q) — not exercised by this fix, and pinning
# .^name here would tie this test to that bug.
my package EXPORTHOW {}
EXPORTHOW::<class> = Int;
check(EXPORTHOW::<class> === Int, True, 'a slot holds a type object');

# the pseudo-packages must still work — they share this syntax
my $lex = 7;
check(MY::<$lex>, 7, 'MY::<$x> still reads a lexical');

# 2. new_type across the HOW family
for <ClassHOW PackageHOW ModuleHOW GrammarHOW> -> $how {
    my $t = ::("Metamodel::$how").new_type(name => "Made$how");
    check($t.^name, "Made$how", "Metamodel::$how.new_type names the type");
}
# a class made this way still composes and instantiates
my $c = Metamodel::ClassHOW.new_type(name => 'Runtime1');
$c.^compose;
check(($c.new.WHAT ~~ $c), True, 'a runtime ClassHOW type instantiates');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
