# Regression: dividing by zero yields a FAILURE carrying X::Numeric::DivideByZero.
#
# Three verbatim copies of the check lived inside one `if` block, and each
# returned a bare `Failure` TYPE OBJECT — which has no exception to detonate and
# leaves `$!` unset, so there was no way to ask what went wrong. All three also
# hard-coded `infix:<%%>` in the one message they did build, whatever operator
# was actually used.
#
# `try` now also sets `$!` when its block RETURNS a Failure rather than throwing:
# nothing was thrown, but the failure is still what happened, and Rakudo reports
# it the same way.
#
# NOT folded in, deliberately: `(0/1) ** -1`. That is `<1/0>` in Rakudo — a
# legitimate zero-denominator Rat, not a divide-by-zero — and we answer Failure.
# A real divergence, but a different rule; folding it into this helper would
# create a new one.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my $m = (try { 1 % 0 });
check($m.defined.gist, 'False',                    '1 % 0 is undefined');
check($!.^name,        'X::Numeric::DivideByZero', 'and $! names the failure');

my $d = (try { 1 div 0 });
check($d.defined.gist, 'False',                    '1 div 0 likewise');
check($!.^name,        'X::Numeric::DivideByZero', 'with the same class');

my $o = (try { 1 mod 0 });
check($o.defined.gist, 'False',                    'and mod');

# %% throws eagerly rather than returning a Failure
check((try { 1 %% 0 }).defined.gist, 'False', '%% by zero fails');
check($!.^name, 'X::Numeric::DivideByZero',   'with the same class again');

# ordinary division is untouched
check(1 % 3,     '1', 'a normal modulo');
check(7 div 2,   '3', 'a normal div');
check(10 %% 5,   'True', 'a normal divisibility test');
check((1/3).raku, '<1/3>', 'and a normal Rat');

# a try whose block succeeds clears $!
try { 1 + 1 }
check($!.defined.gist, 'False', 'a successful try leaves $! undefined');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
