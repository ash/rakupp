# From drilling JSON::Class in the v2 battery — six independent fixes, each a
# construct real modules use at load time:
#
#   * $*RAKU.compiler.version now answers the ORACLE ERA (v2026.07), not
#     rakupp's own release: modules gate with `< v2023.12` to ask "modern
#     semantics?", and v1.5.x read as pre-2000 Rakudo. .release keeps ours.
#   * `-> \p (:key($k) is raw, …)` — sub-signature on a SIGILLESS param.
#   * `class A is export(:TAG) {…}` left the tag list in the token stream and
#     silently swallowed the NEXT STATEMENT.
#   * `subset S is export of Mu where …` — traits and `of` in either order.
#   * `is x-wrapper` on a class dispatches to a user `trait_mod:<is>` — AFTER
#     the class registers (the trait body wants `type.^add_role`), with a trait
#     body that DIES propagating rather than masquerading as UnknownParent.
#   * `.^add_role` at runtime, and the X::Wrapper core role exists to add.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check($*RAKU.compiler.version >= v2023.12, True,  'compiler.version answers the oracle era');
check($*RAKU.compiler.name,      'Raku++',        'while .name stays truthful');

for (a => 1,) -> \p (:key($k) is raw, :value($v) is raw) {
    check($k, 'a', 'sigilless param with a sub-signature: key');
    check(p.value, 1, 'and the whole stays bound to the bare name');
}

class Tagged is export(:SOME-TAG) {
    method probe { 'tagged' }
}
my $after-class = 'reached';   # this exact statement used to be EATEN
check($after-class, 'reached', 'the statement after is export(:TAG) survives');
check(Tagged.probe, 'tagged',  'and the class itself works');

subset Rev is export of Int where * > 0;
check(5 ~~ Rev, True,  'subset with is-before-of: accepts');
check(-5 ~~ Rev, False, 'and rejects');

my @trait-log;
my multi sub trait_mod:<is>(Mu \type, Bool :marked($)!) { @trait-log.push(type.^name) }
role Extra { method extra-method { 'from-role' } }
my multi sub trait_mod:<is>(Mu \type, Bool :role-adder($)!) { type.^add_role(Extra) }

class Plain is marked { }
check(@trait-log.List, ('Plain',), 'a user is-trait fires on a class');

role Marker { }
class Both does Marker is marked { }
check(@trait-log.List, ('Plain', 'Both'), 'also when it follows a does (the extra-parents path)');

class Enriched is role-adder { }
check(Enriched.new.extra-method, 'from-role', 'a trait body can .^add_role into the fresh class');
check(Enriched ~~ Extra, True, 'and the composed role answers to smartmatch');

# Rakudo's core X::Wrapper role is available to add
class Wrapping is role-adder { }
Wrapping.^add_role(::('X::Wrapper'));
check(Wrapping ~~ ::('X::Wrapper'), True, 'the X::Wrapper core role exists and composes');

# a trait body that DIES must surface its own error, not UnknownParent
my multi sub trait_mod:<is>(Mu \type, Bool :exploder($)!) { die "boom from the trait" }
my $err = '';
try { EVAL 'class Kaboom is exploder { }'; CATCH { default { $err = .message } } }
check($err.contains('boom from the trait'), True, 'a dying trait body propagates its own error');
try { EVAL 'class NoSuch is not-a-trait-or-type { }'; CATCH { default { $err = .message } } }
check($err.contains('unknown'), True, 'while a name that is neither still says unknown parent');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
