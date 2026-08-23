#!/usr/bin/env rakupp
# seed-store.raku — install the ecosystem's most-depended-on modules into a
# sweep store WITHOUT their test suites, so a later `rakupp test X` finds its
# dependencies present and spends its whole budget on X's own suite. A dist's
# own verdict is untouched: `rakupp test` always builds and tests the dists it
# was NAMED, installed or not.
#
#   rakupp seed-store.raku seed.tsv --store=DIR --logs=DIR [--timeout=240]
#
# seed.tsv is rank-deps.raku output (name TAB count). Resumable the cheap way:
# a name the store already holds is answered from dist/ in about a second, so
# restarting a wedged seeding re-pays only the plan, not the installs.

sub MAIN($list, :$store!, :$logs!, Int :$timeout = 240,
         :$rakupp = $*EXECUTABLE.absolute) {
    $logs.IO.mkdir unless $logs.IO.d;
    my @names = $list.IO.lines.map({ .split("\t")[0] }).grep(*.chars);
    my $n = +@names;
    my ($ok, $bad) = 0, 0;
    for @names.kv -> $i, $name {
        my $logfile = $logs.IO.add('seed-' ~ $name.subst('::', '-', :g) ~ '.log');
        my $cmd = "'$rakupp' install --no-test --to='$store' '$name' > '$logfile' 2>&1";
        my $t0 = now;
        # perl's alarm as the timeout — macOS ships no timeout(1), and one
        # wedged build hook must not take the whole seeding with it
        my $p = run '/usr/bin/perl', '-e',
                    'alarm shift; open(STDIN, "<", "/dev/null"); exec "/bin/sh", "-c", $ARGV[0] or exit 127',
                    ~$timeout, $cmd, :err;
        $p.err.slurp(:close);
        my $secs = ((now - $t0) * 10).Int / 10;
        if $p.exitcode == 0 {
            $ok++;
        }
        else {
            $bad++;
        }
        note sprintf("[%3d/%3d] %-8s %6.1fs  %s", $i + 1, $n,
                     $p.exitcode == 0 ?? 'ok' !! "exit {$p.exitcode}", $secs, $name);
    }
    note "seeded: $ok ok, $bad failed";
}
