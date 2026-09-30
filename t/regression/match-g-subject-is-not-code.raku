# Regression: a multi-match `.match` ran the SUBJECT STRING as Raku code.
#
#   my $s = q[{ say "INJECTED" }]; my @m = $s.match(/<[{]>/, :g);
#   -> printed INJECTED
#
# `.match` with any adverb (`:g`, `:x`, `:nth`, …), `.match` with a Str needle
# and an adverb, and `.comb(…, :match)` all reuse the s/// occurrence selector
# and throw its output string away. They handed it the subject itself as the
# replacement TEMPLATE ("replace each match with itself"), and a template of
# the form `{ … }` is evaluated per match — so a subject that began with `{`
# and ended with `}` ran once per match, in the caller's scope. `{ x.y }` died
# with "Undefined routine 'x'". Data read from a file or a socket was enough.
# Fixed by passing no template: only the selected matches are wanted.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $ran = 0;
my $s = q[{ $ran++ }];

check +$s.match(/<[{]>/, :g),           1,   '.match(/<[{]>/, :g) count';
check +$s.match(/\{/, :g),              1,   '.match(/\{/, :g) count';
check +$s.match(/\$/, :x(1)),           1,   '.match(…, :x(1)) count';
check $s.match(/<[{}]>/, :2nd).Str,     '}', '.match(…, :2nd) picks the second';
check +$s.match('{', :g),               1,   '.match(Str, :g) count';
check $s.comb(/<[{};]>/, :match)».Str,  ('{', '}'), '.comb(…, :match) elements';
for $s.comb(/<[{]>/, :match) { }
check $ran, 0, 'the subject never ran as code';

# a subject that is not valid code must not die either
my $bad = q[{ x.y }];
check (try +$bad.match(/<[{]>/, :g)) // 'died', 1, 'an unparsable subject is just data';
my $obs = q[{ x->y }];
check (try +$obs.match(/<[{]>/, :g)) // 'died', 1, 'an obsolete-syntax subject is just data';

# $/ is still the List of matches afterwards
$s.match(/<[{}]>/, :g);
check $/».Str, ('{', '}'), '$/ after .match(:g)';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
