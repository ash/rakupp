# Regression: a routine produced by `.assuming` reported an arity and a count of
# ZERO, whatever it had actually been primed down to.
#
# `.assuming` leaves the residual signature in the Callable's `primedParams` and
# leaves `params` null. `.signature` already knew that — makeSignature prefers
# primedParams ("a .assuming wrapper carries its residual params") and computes
# the signature's own arity/count from that list — but the Code's `.arity` and
# `.count` read `params`, found null, and fell through to the placeholder count,
# which for a primed routine is empty. So every primed routine answered 0.
#
# The sharp form of the bug is not "rakupp disagrees with Rakudo" but that rakupp
# disagreed with ITSELF: `&k.arity` said 0 while `&k.signature.arity` said 1, off
# the same object. That invariant is what this file mainly pins, because it holds
# no matter what either engine decides about a particular signature later.
#
# Found while running the v4.0.0 release gates, 2026-09-17.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
# The Code and its own Signature must never disagree.
sub agree(&c, $what) {
    check &c.arity, &c.signature.arity, "$what: .arity vs .signature.arity";
    check &c.count, &c.signature.count, "$what: .count vs .signature.count";
}

# ---- an ordinary routine: unchanged, and self-consistent -------------------
sub plain($a, $b) { }
check &plain.arity, 2, 'plain arity';
check &plain.count, 2, 'plain count';
agree &plain, 'plain';

# ---- primed down to one parameter ------------------------------------------
sub h(Int $a, Int $b) { "ok" }
my &k = &h.assuming(1);
check &k.signature.raku, ':(Int $b)', 'primed signature';
check &k.arity, 1, 'primed arity is the residual, not 0';
check &k.count, 1, 'primed count likewise';
agree &k, 'primed';

# ---- primed, with a type capture in the original signature -----------------
sub f(::T, T $a, T $b) { "ok" }
my &g = &f.assuming(Int);
check &g.signature.raku, ':(Int $a, Int $b)', 'capture resolved in the primed signature';
check &g.arity, 2, 'two residual parameters';
check &g.count, 2, 'and a count to match';
agree &g, 'primed over a capture';

# ---- priming everything leaves an empty signature, NOT the placeholder count
sub two($a, $b) { }
my &none = &two.assuming(1, 2);
check &none.arity, 0, 'fully primed arity is 0';
check &none.count, 0, 'fully primed count is 0';
agree &none, 'fully primed';

# ---- a slurpy still reports Inf for count, primed or not -------------------
sub slurp($a, *@rest) { }
check &slurp.arity, 1, 'slurpy arity counts only the required';
check &slurp.count, Inf, 'slurpy count is Inf';
agree &slurp, 'slurpy';
my &sp = &slurp.assuming(1);
check &sp.count, Inf, 'and stays Inf once primed';
agree &sp, 'slurpy primed';

# ---- an optional is counted but not required -------------------------------
sub opt($a, $b?) { }
check &opt.arity, 1, 'optional does not raise arity';
check &opt.count, 2, 'but does raise count';
agree &opt, 'optional';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
