# Regression: a `::T` type capture named a type and constrained nothing.
#
# `sub f(::T, T $a)` captures whatever arrives at the first parameter and uses
# that type as the constraint on `$a`. The NAME bound correctly — `T.^name`
# answered Int inside the body, which is why this went unnoticed — but the
# CONSTRAINT never fired: `T` is not a declared type, and typeCheckBind returns
# early for a type name it cannot resolve (deliberately, so an unimported module
# type binds freely rather than exploding). So `f(Int, "x")` bound happily where
# Rakudo throws X::TypeCheck::Binding::Parameter.
#
# Roast reaches this through `.assuming`, and the priming is a red herring worth
# writing down, because it sent the first diagnosis the wrong way. `.assuming`
# computes `primedParams` for INTROSPECTION only — `.signature` on the primed
# routine prints `:(Int $a)`, fully resolved — while the CALL binds the ORIGINAL
# parameters with the primed values prepended. The proof is that the body of a
# primed `sub f(::T $t, T $a)` still sees `$t` bound. So the parameter arrives
# spelled "T" on both paths, and one fix covers both.
#
# The discriminating pair: `sub (::T, Int $a)` always threw, because `Int` is a
# real type; `sub (::T $t, T $a)` did not.
#
# Found by the v4.0.0 release gate — S06-currying/misc.t against v3.28.0.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
# Did the call throw a binding type check, and nothing else?
sub threw(&c, $what) {
    my $r = try { c(); Nil };
    my $e = $!;
    return @fail.push("$what: returned {$r.raku} instead of throwing") unless $e;
    @fail.push("$what: threw {$e.^name}") unless $e ~~ X::TypeCheck::Binding::Parameter;
}
sub bound(&c, $want, $what) {
    my $r = try { c() };
    return @fail.push("$what: threw {$!.^name}") if $!;
    check $r, $want, $what;
}

# ---- the direct call --------------------------------------------------------
sub f(::T, T $a, T $b) { "ok" }
threw { f(Int, 42, "42") }, 'a captured Int refuses a Str at the third parameter';
threw { f(Int, "42", 42) }, 'and at the second';
threw { f(Str, 42, 42)   }, 'a captured Str refuses Ints';
bound { f(Int, 42, 7)    }, "ok", 'matching arguments still bind';
bound { f(Str, "a", "b") }, "ok", 'whatever the captured type is';

# The capture still NAMES the type — that half always worked and must stay.
sub named(::T $t, T $a) { T.^name ~ "/" ~ $t.^name ~ "/" ~ $a.^name }
bound { named(Int, 42) }, "Int/Int/Int", 'the captured name is still bound in the body';
threw { named(Int, "x") }, 'and a named capture constrains too';

# ---- through .assuming ------------------------------------------------------
my &g = &f.assuming(Int);
check &g.signature.raku, ':(Int $a, Int $b)', 'the primed signature resolves the capture';
threw { g(42, "42") }, 'a primed call refuses a Str';
bound { g(42, 7)    }, "ok", 'and accepts two Ints';

my &h = &named.assuming(Int);
threw { h("x") }, 'a primed NAMED capture refuses a Str';
bound { h(42)  }, "Int/Int/Int", 'and binds the capture in the body as before';

# ---- what must not have changed ---------------------------------------------
# A real type next to a capture was always checked; keep it that way.
# The bad argument comes through a variable on purpose: given a LITERAL of a
# statically-wrong type Rakudo refuses the call at compile time ("will never
# work with declared signature"), so a literal here would stop this file
# loading there and cost the cross-engine check the whole file exists for.
# rakupp decides at bind time either way; routing it through `$s` asks both
# engines the same question.
my $s = "x";
sub real(::T, Int $a) { "ok" }
threw { real(Int, $s) }, 'a real type beside a capture still refuses';
bound { real(Str, 42) }, "ok", 'and still accepts';

# A capture parameter itself takes anything — that is what it is for.
sub anyt(::T $t) { $t.^name }
bound { anyt(Int) }, "Int", 'a capture takes a type object';
bound { anyt(42)  }, "Int", 'and an instance, naming its type';

# An ordinary signature is untouched.
sub plain(Int $a, Str $b) { "ok" }
bound { plain(1, "x") }, "ok", 'an ordinary signature still binds';
threw { plain($s, $s) }, 'and still refuses';

# (rakupp also lets a parameter typed with an UNRESOLVABLE name bind freely,
# which is what the early return in typeCheckBind is for. That is a deliberate
# divergence — Rakudo refuses such a signature at compile time — so it is not
# asserted here: this file runs unchanged on both engines, which is what makes
# it evidence about Raku rather than about rakupp.)

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
