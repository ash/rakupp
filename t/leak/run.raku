#!/usr/bin/env rakupp
# The leak corpus (docs/dev/plans/CYCLE-GC-PLAN.md): one file per shape of
# cyclic garbage, and per shape of live cycle that must NOT be collected. Each
# counts DESTROY calls, a deterministic signal where RSS is not, and is its own
# TAP test written for both engines — `--engine=rakudo` checks the corpus
# itself against Rakudo's tracing collector.
#
# Under rakupp each file runs twice: as is, and with RAKUPP_CYCLE_POISON=1,
# where a collected object keeps its contents and has its class swapped for a
# sentinel that dies on any method call. The second run is the check that the
# collector never counted a reference that does not exist: DESTROY still runs
# there, so every count still holds, and a wrongly collected object fails
# loudly instead of reading a cleared attribute.
#
#   rakupp t/leak/run.raku
#   rakupp t/leak/run.raku --engine=/opt/homebrew/bin/rakudo
#   rakupp t/leak/run.raku --only=pair,resurrect

my $ENGINE = $*EXECUTABLE.Str;
my @ONLY;
for @*ARGS -> $a {
    if    $a.starts-with('--engine=') { $ENGINE = $a.substr(9) }
    elsif $a.starts-with('--only=')   { @ONLY = $a.substr(7).split(',').grep(*.chars) }
    else { note "unknown argument: $a"; exit 2 }
}
my $DIR = $?FILE.IO.parent;
my @cases = $DIR.dir(test => /'.raku' $/).grep(*.basename ne 'run.raku').sort(*.basename);
@cases .= grep({ .basename.subst(/'.raku' $/, '') eq any(@ONLY) }) if @ONLY;

my $rakupp = $ENGINE.IO.basename.starts-with('rakupp');
my @modes = $rakupp ?? ('', 'poison') !! ('',);
my $n = 0;
my $fails = 0;
for @cases -> $f {
    my $name = $f.basename.subst(/'.raku' $/, '');
    for @modes -> $mode {
        my %env = %*ENV;
        %env<RAKUPP_CYCLE_POISON> = '1' if $mode eq 'poison';
        my $p = run($ENGINE, $f.Str, :out, :err, :%env);
        my $out = $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);
        my @tap = $out.lines.grep(/^ 'not '? 'ok ' \d+/);
        my @bad = @tap.grep({ .starts-with('not ') && !.contains('# TODO') });
        my $poisoned = $err.contains('CYCLE-COLLECTED') || $out.contains('CYCLE-COLLECTED');
        my $okay = $p.exitcode == 0 && @tap && !@bad && !$poisoned;
        $n++;
        $fails++ unless $okay;
        say "{$okay ?? 'ok' !! 'not ok'} $n - {$name}{$mode ?? " ($mode)" !! ''}";
        unless $okay {
            note "# $_" for @bad;
            note "# exit {$p.exitcode}" if $p.exitcode;
            note "# a collected object was used afterwards" if $poisoned;
            note "# stderr: $_" for $err.lines.head(5);
        }
    }
}
say "1..$n";
exit $fails ?? 1 !! 0;
