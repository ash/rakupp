# Regression: three general fixes that let Zef::CLI load and run under rakupp —
#   1. `@%h` / `@$hash` (the `@` list-contextualizer on a Hash) yields the hash's
#      Pairs, not the hash as a single element (zef: `for @$node -> $sub-node`).
#   2. `$obj<key>` / `$obj[i]` on an object delegates to AT-KEY / AT-POS (incl.
#      `handles <AT-KEY …>`), with :exists/:delete → EXISTS-KEY/DELETE-KEY.
#   3. `class Foo {…}.new(…)` as a statement (a sub's last statement) applies the
#      postfix — was parsed as a bare declaration, dropping `.new`.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. @%h / @$hash -> Pairs
my %h = a => 1, b => 2;
@fail.push('at-hash')  unless @%h.map(*.^name).unique eqv ('Pair',);
@fail.push('at-hash-kv') unless @%h.sort(*.key).map(*.value) eqv (1, 2);
my $hr = { x => 10 };
@fail.push('at-scalar-hash') unless (@$hr).map(*.key) eqv ('x',);

# 2. AT-KEY / AT-POS delegation through `handles`
my $cfg = class :: {
    has %.hash handles <AT-KEY EXISTS-KEY DELETE-KEY keys>;
}.new(hash => { host => 'x', port => 80 });
@fail.push("at-key ({$cfg<host> // 'undef'})") unless $cfg<host> eq 'x';
@fail.push('exists-key') unless $cfg<port>:exists && !($cfg<nope>:exists);
@fail.push('slice')      unless ($cfg<host port>) eqv ('x', 80);

# 3. `class Foo {}.new` as a statement value (sub / do block)
sub make-anon { class :: { has $.n }.new(n => 42) }
@fail.push('anon-stmt') unless make-anon().n == 42;
sub make-named { class Widget { has $.w }.new(w => 7) }
@fail.push('named-stmt') unless make-named().w == 7;
my $d = do { class :: { has $.v }.new(v => 3) };
@fail.push('do-stmt') unless $d.v == 3;

# a plain class declaration still declares (no postfix)
class Plain { has $.p }
@fail.push('plain-decl') unless Plain.new(p => 5).p == 5;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
