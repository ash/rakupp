# Regression: a Failure is a CONCRETE value for a `:D` parameter. `.defined`
# answers False on a Failure — that is how one gets noticed — but the smiley
# asks whether the argument is an instance, and a Failure is one. The binder
# read `.defined`, so `"x".Int` handed to an `Any:D` parameter raised
# X::Parameter::InvalidConcreteness ("must be an object instance … not a type
# object"), and for `Int:D` the same, where the honest answer is a TYPE
# mismatch. Getopt::Long's `store-direct(Int:D $value)` met exactly that on
# `--count x`, and reported the wrong error for it.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub outcome(&code) {
    { code(); return 'bound'; CATCH { default { return .^name } } }
}

# each answers a plain string, so no Failure is ever RETURNED into sink
# context — that would throw it, which is not what is being measured here
sub any-d(Any:D $v) { 'ok' }
sub int-d(Int:D $v) { 'ok' }
sub int-u(Int:U $v) { 'ok' }
sub any-u(Any:U $v) { 'ok' }
sub plain-int(Int $v) { 'ok' }
sub takes-failure(Failure $v) { 'ok' }
sub hand-back(Any:D $v) { $v }

ck(outcome({ any-d("x".Int) }),         'bound',
   'a Failure binds to Any:D — it is an instance');
ck(outcome({ int-d("x".Int) }),         'X::TypeCheck::Binding::Parameter',
   'and is refused by Int:D as a type mismatch, not a concreteness one');
ck(outcome({ plain-int("x".Int) }),     'X::TypeCheck::Binding::Parameter',
   'a bare Int parameter refuses it the same way');
ck(outcome({ takes-failure("x".Int) }), 'bound',
   'a Failure parameter takes it');
ck(outcome({ any-u("x".Int) }),         'X::Parameter::InvalidConcreteness',
   'and :U refuses it, because it is not a type object');

# the checks the smiley is FOR still hold
ck(outcome({ any-d(Int) }),  'X::Parameter::InvalidConcreteness', 'a type object still fails :D');
ck(outcome({ int-d(42) }),   'bound',                             'an instance still binds :D');
ck(outcome({ int-u(Int) }),  'bound',                             'a type object still binds :U');
ck(outcome({ int-u(42) }),   'X::Parameter::InvalidConcreteness', 'an instance still fails :U');

# what the binding hands on: the Failure itself, unhandled, still reporting
my $f = hand-back("x".Int);
ck($f ~~ Failure, True, 'the parameter received the Failure');
ck($f.handled, False,   'still unhandled');
ck($f.exception.^name, 'X::Str::Numeric', 'and still carrying its exception');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
