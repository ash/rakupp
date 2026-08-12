# Regression: two type-registry divergences found building the G0 grammar
# service (GRAMMAR-PLAN.md, G0 outcome note).
#
# 1. `::('NoSuchClass')` MANUFACTURED a stub type object. A stub and a real
#    class are both undefined type objects, so existence checks were
#    impossible. Now: a broken Failure, exactly Rakudo's behaviour — the
#    lookup itself is soft (`try ::($n)` / `.defined` probe existence), and
#    the X::NoSuchSymbol fires on use. The strictness lives only on the ::()
#    resolution path (NameTerm.symbolicStrict) — ordinary bare-name
#    references keep the lenient forward-reference fallback, and
#    pseudo-packages (GLOBAL, EXPORT::DEFAULT, …) pass through.
#
# 2. A same-name type redeclaration from a LATER EVAL silently clobbered the
#    registry entry — every held type object of the first declaration adopted
#    the second's body (the registry is name-keyed). Rakudo refuses the
#    redeclaration at compile time. Now rakupp refuses at registration
#    (X::Redeclaration), leaving the original intact. Exempt, as at parse
#    level: the same declaration node re-evaluated (a type declared in a sub
#    body), `my`-scoped types (lexical in Rakudo, so legally redeclarable),
#    stubs, augment, parameterized roles, weak packages.
#
# Every check verified against Rakudo (this file runs on both engines).
#
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# ---- ::() fails softly on a missing name, resolves everything real ---------
my $missing = ::('NoSuchClass12345');
check $missing.defined, False, '::() on a missing name is undefined';
check ?($missing ~~ Failure), True, 'and it is a Failure, not a stub type';
check ::('Str').^name, 'Str', '::() still resolves a builtin';
class UserClass { }
check ::('UserClass').^name, 'UserClass', '::() still resolves a user class';
subset Small of Int where * < 5;
check ::('Small').^name, 'Small', '::() still resolves a subset';
module UserMod { }
check ::('UserMod').^name, 'UserMod', '::() still resolves a module';
enum Color <red green>;
check ?(try { ::('Color'); True }), True, '::() still resolves an enum';

# ---- redeclaration from a later EVAL is refused, not destructive ------------
my $g1 = EVAL(q[grammar RegrR { token TOP { 'one' } }] ~ ";\nRegrR");
try EVAL(q[grammar RegrR { token TOP { 'two' } }] ~ ";\nRegrR");
check ($! ?? $!.^name !! 'NO THROW'), 'X::Redeclaration', 'cross-EVAL redeclaration throws';
check ?$g1.parse('one'), True, 'the original body survives the refused redeclare';

# my-scoped types stay redeclarable (lexical)
my $m2 = EVAL(q[my grammar RegrM { token TOP { 'x' } }; RegrM]);
$m2 = EVAL(q[my grammar RegrM { token TOP { 'y' } }; RegrM]);
check ?$m2.parse('y'), True, 'my-scoped types may be redeclared across EVALs';

# the same declaration NODE re-evaluated is not a redeclaration
sub f() { grammar RegrInner { token TOP { 'q' } }; RegrInner }
f();
check ?f().parse('q'), True, 'a type declared in a sub body survives a second call';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
