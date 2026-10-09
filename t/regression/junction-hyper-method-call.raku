# Regression: a hyper method call on a Junction autothreads (#116 follow-up).
#
# `all(@p)».status` flattened the junction into an Array of statuses. As with
# any call on a junction, each eigenstate is called on its own, here a hyper
# call that gives a one-element list for a scalar, and the answers form a
# junction of the same kind: all((Kept), (Kept)) (2026-10-09).
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my @p = Promise.kept(1), Promise.kept(2);
my $st = all(@p)».status;
check $st.^name,            'Junction',              'the answer is a Junction';
check $st.gist,             'all((Kept), (Kept))',   'all(@p)».status';
check all(1, 2)».succ.gist, 'all((2), (3))',         'an all junction';
check any('a', 'b')».uc.gist, 'any((A), (B))',       'an any junction keeps its kind';
check 1».succ.gist,         '(2)',                   'a scalar invocant gives a one-element list';
check (^3)».succ.List,      (1, 2, 3),               'a list invocant as before';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
