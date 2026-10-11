# Regression: issue #157 (2026-10-11).
#
# `%*ENV<X>:delete` took X out of the hash but not out of the process
# environment, so every child `run` afterwards still inherited it: an
# assignment was pushed to the process before a spawn, a deletion never was.
# (A git index pointed at through GIT_INDEX_FILE for two calls stayed
# pointed at for the rest of the program.) The sync now also unsets the
# names the hash no longer has.
#
# Expected answers are Rakudo 2026.09's (2026-10-11).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

sub child($name) {
    my $p = run 'sh', '-c', "echo \"\$\{$name:-nothing}\"", :out;
    $p.out.slurp(:close).trim
}

check child('RAKUPP_ENV_DEL_DEMO'), 'nothing', 'unset to begin with';
%*ENV<RAKUPP_ENV_DEL_DEMO> = 'temporary';
check child('RAKUPP_ENV_DEL_DEMO'), 'temporary', 'an assignment reaches a child';
%*ENV<RAKUPP_ENV_DEL_DEMO>:delete;
check (%*ENV<RAKUPP_ENV_DEL_DEMO>:exists), False, ':delete takes it out of the hash';
check child('RAKUPP_ENV_DEL_DEMO'), 'nothing', '…and out of what a child inherits';
%*ENV<RAKUPP_ENV_DEL_DEMO> = 'again';
check child('RAKUPP_ENV_DEL_DEMO'), 'again', 'set again after a delete';

# a variable the program inherited, deleted
check child('HOME') ne 'nothing', True, 'an inherited variable is passed on';
my $home = %*ENV<HOME>:delete;
check child('HOME'), 'nothing', 'and deleting it stops that';
%*ENV<HOME> = $home;
check child('HOME'), $home, 'and restoring it brings it back';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
