# Regression: one parameterization of a parametric role per `does`
# (ROAST-TRACKS-PLAN track C, 2026-09-27). A class doing R[Int] gets R's
# methods re-closed over T = Int, instead of every method learning its
# parameters at call time from the class, which cannot tell R[Int] from R[Str].
# Before: `multi method foo(T $t)` matched NOTHING, even for one `does R[Int]`
# (the dispatcher looked T up in the caller's scope), and `does R[Str] does
# R[Int]` gave both parameterizations T = Str.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    my role R[::T] { multi method foo(T $t) { T.^name } }
    my class A does R[Int] { }
    ck(A.new.foo(5), 'Int', 'a multi typed by the role parameter dispatches');
    ck((try A.new.foo('x')) // $!.^name, 'X::Multi::NoMatch', '…and refuses what the parameter is not');
    my class B does R[Str] does R[Int] { }
    ck(B.new.foo(5), 'Int', 'two parameterizations: each keeps its own multi');
    ck(B.new.foo('x'), 'Str', '…the other one');
}
{
    my role R2[::T] { method bar(T $t) { T.^name } }
    my class C does R2[Int] { }
    ck(C.new.bar(5), 'Int', 'a plain method typed by the parameter');
    ck((try C.new.bar('x')) // $!.^name, 'X::TypeCheck::Binding::Parameter', '…binds only that type');
}
{
    my role R3[::T] { method t { T.^name } }
    my class D does R3[Str] { method own { self.t } }
    ck(D.new.own, 'Str', 'the role method sees its parameter when the class calls it');
    ck(R3[Int].t, 'Int', 'a pun of the role sees it too');
    my $conflict = (try EVAL 'my class E does R3[Str] does R3[Int] { }; 1') // $!.^name;
    ck($conflict ~~ /'X::Role::Unresolved::Method'/ ?? 'conflict' !! $conflict, 'conflict',
       'the same plain method from R3[Str] and R3[Int] is a conflict');
    ck((try EVAL 'my class F does R3[Int] does R3[Int] { }; F.new.t') // $!.^name, 'Int',
       'the same parameterization twice is one role');
}
{
    my role At[::T] { has T $.v; method vt { $!v.^name } }
    my class G does At[Int] { }
    ck(G.new(v => 5).vt, 'Int', 'an attribute typed by the parameter');
    ck((try G.new(v => 'x')) // $!.^name, 'X::TypeCheck::Assignment', '…is checked');
}
{
    my role P[$x] { method px { $x } }
    my class H does P[1] { }
    my class I does P[2] { }
    ck(H.new.px ~ I.new.px, '12', 'a value parameter per class');
}
{
    my role Q1[::T] { method of-type { T.^name } }
    my class J does Q1[Str] { method m { self.Q1::of-type } }
    ck(J.new.m, 'Str', 'a qualified call reaches the parameterization the class does');
    my class K does Q1[Int] does Q1[Str] { method of-type { self.Q1::of-type } }
    ck((try K.new.of-type) // $!.message, 'Ambiguous concretization lookup for Q1',
       '…and two of them at one level are ambiguous');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
