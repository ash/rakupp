# Regression: signature rendering + `.assuming` priming (S06-currying).
#   1. Signature.raku is `:(…)`; .gist/.Str are the bare parens.
#   2. named params render with their `:` (`:$a`, `:key($var)`, nested
#      `:b(:c($a))`), required nameds keep `!`, slurpies keep `*`/`**`/`+`,
#      captures render `|c`, and defaults render (literals fold, thunks are
#      `Code.new`).
#   3. priming a NAMED param leaves it in the residual signature, optional,
#      defaulting to the primed value; priming a POSITIONAL removes it.
#   4. `*` in a priming is a hole the call's next positional fills.
#   5. a capture param does not CONSUME its args — two captures see the same list.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1-2. rendering
check(sub (:$a) {}.signature.raku,   ':(:$a)',            'named');
check(sub (:$a) {}.signature.gist,   '(:$a)',             'named-gist');
check(sub (:$a!) {}.signature.raku,  ':(:$a!)',           'named-required');
check(sub (:a($x)) {}.signature.raku, ':(:a($x))',        'named-alias');
check(sub (:b(:c($a))!) {}.signature.raku, ':(:b(:c($a))!)', 'nested-alias');
check(sub (*@r) {}.signature.raku,   ':(*@r)',            'slurpy-flat');
check(sub (**@r) {}.signature.raku,  ':(**@r)',           'slurpy-noflat');
check(sub (+@r) {}.signature.raku,   ':(+@r)',            'slurpy-one');
check(sub (*%h) {}.signature.raku,   ':(*%h)',            'slurpy-named');
check(sub (|c) {}.signature.raku,    ':(|c)',             'capture');
check(sub ($x = 42) {}.signature.raku, ':($x = 42)',      'default-int');
check(sub ($x = 'q') {}.signature.raku, ':($x = "q")',    'default-str');
check(sub ($x = -1) {}.signature.raku, ':($x = Code.new)', 'default-thunk');
check(sub (Int:D $x) {}.signature.raku, ':(Int:D $x)',    'smiley');
check(sub ($a, $b?, *@c, :$d, :$e!) {}.signature.raku, ':($a, $b?, *@c, :$d, :$e!)', 'mixed');
check(sub ($, @, %) {}.signature.raku, ':($, @, %)',      'anon');

# 3. residual signature after priming
check(sub (:$a) {}.assuming(:a).signature.raku,       ':(:$a = Bool::True)', 'prime-named');
check(sub (:$a!) {}.assuming(:a).signature.raku,      ':(:$a = Bool::True)', 'prime-required-named');
check(sub (:$a, :$b) {}.assuming(:b).signature.raku,  ':(:$a, :$b = Bool::True)', 'prime-one-named');
check(sub ($a, $b, :$c) {}.assuming(1).signature.raku, ':($b, :$c)',        'prime-positional');
check(sub (:b($a)!) {}.assuming(:b).signature.raku,   ':(:b($a) = Bool::True)', 'prime-alias');
check(sub (:b(:c($a))!) {}.assuming(:c).signature.raku, ':(:b(:c($a)) = Bool::True)', 'prime-inner-alias');

# 4. `*` is a hole, not a value
sub two($a, $b) { "$a|$b" }
check(&two.assuming(*, 2).signature.raku, ':($a)', 'hole-sig');
check(&two.assuming(*, 2)(1), '1|2',              'hole-call');
check(&two.assuming(1)(2),    '1|2',              'plain-prime-call');
sub three($a, $b, $c) { "$a|$b|$c" }
check(&three.assuming(1, *, 3).signature.raku, ':($b)', 'hole-mid-sig');
check(&three.assuming(1, *, 3)(2), '1|2|3',              'hole-mid-call');

# the caller's named wins over the primed one
sub named3(:$a = '', :$b = '', :$c = '') { "a$a b$b c$c" }
check(&named3.assuming(b => 'x')(a => 'w', c => 'y'), 'aw bx cy', 'prime-named-call');
check(&named3.assuming(b => 'x')(b => 'z'), 'a bz c', 'named-override');

# 5. captures don't consume
sub capcap (|c ($a, $b?), |d ($p, $q?)) { "a$a b$b c$p d$q" }
check(capcap('a', 'b'), 'aa bb ca db', 'two-captures');
check(&capcap.assuming('a')('b'), 'aa bb ca db', 'two-captures-primed');

# 6. a priming whose args can't bind throws AT `.assuming`, not at call time
sub thrown(&code) { my $e; try { code(); CATCH { default { $_.defined; $e = $_ } } }; $e }
my $e1 = thrown({ sub (:$a) { }.assuming(:nosuch) });
@fail.push('unexpected-named-not-thrown') unless $e1 ~~ X::AdHoc && $e1.payload ~~ /Unexpected/;
my $e2 = thrown({ sub (Str $a) { }.assuming(1) });
@fail.push('typecheck-not-thrown') unless $e2 ~~ X::TypeCheck::Binding
    && $e2.expected =:= Str && $e2.symbol eq '$a';
my $e3 = thrown({ sub ($a) { }.assuming(1, 2) });
@fail.push('too-many-not-thrown') unless $e3 ~~ X::AdHoc && $e3.payload ~~ /'Too many positionals'/;
@fail.push('good-prime-threw') if thrown({ sub ($a, $b) { }.assuming(1) }).defined;

# the roast helper turns that throw back into a mixed-in Failure — `.can` has to
# see a mixin's methods for it to work
my $mixed = Any but Failure.new(X::AdHoc.new(payload => 'nope'));
@fail.push('can-misses-mixin') unless $mixed.can('Failure');
check($mixed.Failure.exception.payload, 'nope', 'mixin-failure-payload');

# 7. `die "msg"` — X::AdHoc.payload IS the message
try { die 'boom' }
check($!.payload, 'boom', 'adhoc-payload');

# 8. parameter binding is not assignment: an itemized Array binds to an `@`
#    param as the Positional it is
sub takes-array(@x) { @x.raku }
my @m = [[1, 2], [3, 4]];
check(takes-array(@m[0]), '[1, 2]', 'itemized-elem-binds-flat');
check(takes-array($[1, 2]), '[1, 2]', 'itemized-literal-binds-flat');
check(takes-array([1, 2]), '[1, 2]', 'plain-array-binds-flat');
my @assigned = $[1, 2];
check(@assigned.raku, '[[1, 2],]', 'assignment-still-nests');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
