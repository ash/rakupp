# Regression: method-call shapes that did not parse or dispatch, and core
# method tables that were missing. Found running mutsu's own t/ suite
# (oo/method/, 2026-10-04).
#
# - `.²` is the power postfix in method form (`2.²`, `.³` on the topic,
#   `2².³` is (2²)³); `[+].^name` calls on the zero-operand reduction;
#   `$obj.*m;` is not an `m;…;` match; `@.m(…)` / `@.m: …` are self calls in
#   list context; `submethod (…) {…}` is a term, and a Submethod's type is
#   Submethod (a Routine, no Method); `returns ::?CLASS` resolves.
# - `.$var` holding a Str is a string being invoked, not a method name;
#   `.WHERE`/`.VAR` on a WhateverCode answer directly.
# - The default constructor refuses a POSITIONAL Pair; `%h.STORE` /
#   `@a.STORE` replace in place; Int/Num/Hash have `.^method_table`;
#   `True.enums`; `Nil.abs` is the Int 0; Cool methods on an undefined Any are
#   X::Method::NotFound, and numeric ones on a Block too.
# - A `my class` inside a method is named under its class (Q::X).
# - An interpolated call's quoted argument may hold a `)`.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub err(&code) { CATCH { default { return $_ } }; code(); 'lived' }

# --- shapes that parse -------------------------------------------------------
ck(2.², 4, '2.² is a power');
ck((3,).map({ .³ }).head, 27, '.³ powers the topic');
ck(2².³, 64, '2².³ is (2²)³');
ck([+].^name, 'Int', '[+].^name calls on the zero-operand reduction');
class MA { method m { 1 } }
class MB is MA { method m { 2 } }
ck(MB.new.*m.sort.join(','), '1,2', '$obj.*m; is a method call, not a match');
class Seven {
    method seven(*@a) { 7 }
    method colon-args { @.seven: 1 }
    method paren-args { @.seven(1, 2) }
    method colon-empty { @.seven: }
}
ck(Seven.new.colon-args.List, (7,), '@.m: args is a self call in list context');
ck(Seven.new.paren-args.List, (7,), '@.m(args) too');
ck(Seven.new.colon-empty.List, (7,), '@.m: with nothing after the colon');
ck((submethod ($x) { 1 }).^name, 'Submethod', 'a submethod term is a Submethod');
class WithSub { submethod s { } }
my $sm = WithSub.^find_method('s');
ck(($sm.^name, $sm ~~ Method, $sm ~~ Routine), ('Submethod', False, True), 'a Submethod is a Routine, no Method');
class C3 { method m(*%a) returns ::?CLASS { self } }
ck(C3.m.^name, 'C3', 'returns ::?CLASS resolves');

# --- what calls mean ---------------------------------------------------------
my $not-code = 'uc';
ck(err({ 'x'.$not-code }).message, "No such method 'CALL-ME' for string 'uc'", '.$str invokes, never looks up');
ck((* + 1).WHERE.^name, 'Int', '.WHERE on a WhateverCode answers directly');
ck((* + 1).VAR.^name, 'WhateverCode', '.VAR too');

# --- constructors, containers, core tables ---------------------------------
class Plain { has $.x }
my $pp = x => 1;
ck(err({ Plain.new($pp) }).^name, 'X::Constructor::Positional', 'a positional Pair is refused by the default new');
ck(Plain.new(:x(2)).x, 2, 'a named argument still binds');
my %hs = a => 1;
my $alias := %hs;
%hs.STORE((b => 2,));
ck($alias, {b => 2}, '%h.STORE replaces in place');
my @as = 1, 2;
@as.STORE((5, 6, 7));
ck(@as, [5, 6, 7], '@a.STORE replaces the elements');
ck(Int.^method_table<abs>(-3), 3, 'Int.^method_table holds callable methods');
ck(True.enums.sort.List, (False => 0, True => 1), 'True.enums');
ck((quietly Nil.abs).^name, 'Int', 'Nil.abs is the Int 0');
my $undef;
ck(err({ $undef.comb }).message, "No such method 'comb' for invocant of type 'Any'", 'Cool on Any is NotFound');
ck(err({ { 1 }.abs }).^name, 'X::Method::NotFound', 'a Block has no .abs');

# --- naming and interpolation ----------------------------------------------
class Q { method b { my class X { }; X.^name } }
ck(Q.b, 'Q::X', 'a my class in a method is named under its class');
my $s = 'a)b)c';
ck("y: $s.subst(")", "-")", 'y: a-b)c', 'a quoted ) in an interpolated call');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
