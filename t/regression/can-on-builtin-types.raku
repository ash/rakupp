# Regression: `.can` on a built-in TYPE object probes a sentinel of that type.
#
# A built-in type has no ClassInfo, so `.can`/`.^can` answers it by dispatching
# the name on a sentinel value and reading the result. Only four types had a
# sentinel — Str, Int, Num and Bool — so `Rat.can('Str')` answered [] for a
# method every Rat plainly has, and an `augment class Rat` was invisible to
# introspection even while its methods ran. Test's `can-ok` is `.^can`, which is
# how Rat::Precise's suite failed a single assertion with everything working.
#
# Contract: exit 0 + last line PASS.
use MONKEY-TYPING;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub can-it($type, $name) { ?$type.^can($name) }

# the four that always worked, as the control
check can-it(Str, 'Int'),   True, 'Str.^can(Int)';
check can-it(Int, 'Str'),   True, 'Int.^can(Str)';
check can-it(Num, 'Rat'),   True, 'Num.^can(Rat)';
check can-it(Bool, 'Int'),  True, 'Bool.^can(Int)';

# the ones that had no sentinel
check can-it(Rat, 'Str'),        True, 'Rat.^can(Str)';
check can-it(Rat, 'numerator'),  True, 'Rat.^can(numerator)';
check can-it(FatRat, 'Str'),     True, 'FatRat.^can(Str)';
check can-it(Complex, 're'),     True, 'Complex.^can(re)';
check can-it(Hash, 'keys'),      True, 'Hash.^can(keys)';
check can-it(Array, 'push'),     True, 'Array.^can(push)';
check can-it(List, 'elems'),     True, 'List.^can(elems)';
check can-it(Pair, 'key'),       True, 'Pair.^can(key)';

# a name nothing has is still absent — the probe has to be able to say no
check can-it(Rat, 'no-such-method-anywhere'),  False, 'an absent name on Rat';
check can-it(Hash, 'no-such-method-anywhere'), False, 'an absent name on Hash';

# and the case that found it: an augmented built-in reports the added method
my role Precise { method precise() { 'p' } }
augment class Rat does Precise { }
check can-it(Rat, 'precise'), True, 'an augmented method answers .^can';
check (0.5).precise, 'p', 'and it runs';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
