# Regression: a regex match made on the CALLER'S BEHALF must not publish `$/`.
#   * A junction of regexes collapsing to a Bool ran every eigenstate through
#     regexMatch, and each published into `$/` — so `'abc' ~~ all(/(a)/, /(b)/)`
#     answered True and left `$/` holding ｢b｣, the eigenstate that happened to run
#     last. Rakudo leaves `$/` untouched by the collapse. Once the collapse
#     short-circuited it became the eigenstate that ran FIRST instead, which is how
#     the leak was noticed: the stale value changed without the rule changing.
#   * The same went for the pattern `.grep`/`.first` are handed: `<ab ac>.grep(/(a)/)`
#     clobbered a `$/` that was set before it.
#   * And for the junction-TOPIC-against-a-regex form, which legitimately answers a
#     junction of Match objects — those belong in the RESULT, not in `$/`.
# Fixed by suppressing setMatchVar around exactly those matches
# (Interpreter::MatchVarGuard).
#
# The other half of the contract, and the easy thing to break: the guard must NOT
# reach into USER code. A block handed to .grep/.map is the caller's own, and a
# `$/` or `$0` read inside it is a read the program is entitled to.
#
# EVERY `$/` read here is at the MAINLINE on purpose. `$/` is scoped per routine,
# so a `sub slash { ~$/ }` helper would read its OWN `$/` — always undefined — and
# the whole file would pass without testing anything. That mistake is why this
# file is written the long way.
#
# Verified identical under Rakudo 2026.08 (run it with `rakudo`, not `raku`:
# /usr/local/bin/raku is a symlink to this project's own build).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got '$got' want '$want'") unless $got eq $want }
my $s;

# --- the collapse publishes nothing ----------------------------------------
$/ = Nil; my $a = 'abc' ~~ all(/(a)/, /(b)/); $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'all-of-regexes leaves $/ alone');
check($a.gist, 'True', 'all-of-regexes still answers True');

$/ = Nil; my $b = 'abc' ~~ any(/(a)/, /(b)/); $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'any-of-regexes leaves $/ alone');
check($b.gist, 'True', 'any-of-regexes still answers True');

$/ = Nil; my $c = 'abc' ~~ one(/(a)/, /(x)/); $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'one-of-regexes leaves $/ alone');
check($c.gist, 'True', 'one-of-regexes still answers True');

$/ = Nil; my $d = 'abc' ~~ none(/(x)/, /(y)/); $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'none-of-regexes leaves $/ alone');
check($d.gist, 'True', 'none-of-regexes still answers True');

# --- .grep/.first matchers publish nothing ---------------------------------
$/ = Nil; my @g = <ab ac>.grep(/(a)/); $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'grep with a plain regex leaves $/ alone');
check(@g.join(','), 'ab,ac', 'grep with a plain regex still selects');

$/ = Nil; my @h = <ab cd>.grep(any(/(a)/, /(c)/)); $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'grep with a junction of regexes leaves $/ alone');
check(@h.join(','), 'ab,cd', 'grep with a junction of regexes still selects');

$/ = Nil; my $f = <ab cd>.first(none /(a)/); $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'first with none-of-regex leaves $/ alone');
check($f.gist, 'cd', 'first with none-of-regex still finds');

# an EARLIER $/ survives a later grep — the guard RESTORES, it does not clear
$/ = Nil; 'abc' ~~ /(b)/; my @keep = <ab ac>.grep(/(a)/); $s = $/ ?? ~$/ !! 'undef';
check($s, 'b', 'a grep does not clobber the $/ set before it');

# --- the junction TOPIC form: Matches in the RESULT, not in $/ -------------
$/ = Nil; my $t = any('4', '44') ~~ /(4)/; $s = $/ ?? ~$/ !! 'undef';
check($s, 'undef', 'junction topic vs regex leaves $/ alone');
check($t.so.gist, 'True', 'junction topic vs regex still matches');
check($t.^name, 'Junction', 'junction topic vs regex still answers a Junction');

# --- USER code keeps its own $/ (the guard must not over-reach) ------------
my @blk = <ab cd>.grep({ $_ ~~ /(a)/ ?? ~$/ eq 'a' !! False });
check(@blk.join(','), 'ab', 'a block handed to grep still sees its own $/');

my @cap = <ab ac>.map({ $_ ~~ /a(.)/ ?? ~$0 !! 'x' });
check(@cap.join(','), 'b,c', 'a block handed to map still sees its own $0');

my $j = 'abc' ~~ any({ $_ ~~ /(a)/ && ~$/ eq 'a' }, { False });
check($j.gist, 'True', 'a Code eigenstate still sees its own $/');

# --- and an ordinary match still publishes ---------------------------------
$/ = Nil; 'abc' ~~ /(b)/; $s = $/ ?? ~$/ !! 'undef';
check($s, 'b', 'a plain match still publishes $/');
check(~$0, 'b', 'a plain match still publishes $0');

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
