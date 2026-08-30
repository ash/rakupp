#!/usr/bin/env rakupp
# sweep-fresh.raku — run `rakupp test` over a list of distributions and record
# what happened to each one.
#
#   rakupp sweep-fresh.raku --store=DIR --logs=DIR --out=FILE --timeout=N LIST.tsv
#
# LIST.tsv is pick-fresh.raku output (date, name, version, dist). Each dist is
# tested against ONE shared store that this sweep owns: dependencies accumulate
# there (so the tenth dist needing JSON::Fast does not refetch it) and the
# user's ~/.raku is never touched.
#
# Resumable: a name already present in --out is skipped, so a wedged sweep can
# be restarted without losing what it measured.
#
# Both streams go to the dist's log FILE, not to a pipe: a chatty suite can
# outrun a pipe buffer and deadlock a reader that slurps one stream at a time.
# The timeout is perl's alarm — macOS ships no timeout(1), and a wedged child
# must not take the sweep with it.
#
# Two things a sweep of this size has to get right, both learned the hard way:
# the timeout kills a process GROUP, and a dist that never ran gets no verdict.
# See TIMEOUT-GROUP and the abort in MAIN.

# perl is the timeout, because macOS ships no timeout(1) — and it FORKS rather
# than execs, so something is still alive to enforce the budget. The child
# leads its own process group (setpgrp) and the alarm kills that whole GROUP:
# the shell, `rakupp test`, and every test process under them. An earlier
# version had perl exec the shell and let the alarm kill perl, which killed
# nothing — the tree below was merely ORPHANED, and a suite that spawns
# processes went on spawning them with no budget left to stop it.
# Test::Selector 0.4.2 is such a suite (t/all.rakutest re-invokes itself
# through a generated temp script, unbounded), and one dist took the machine
# to its 4,000-process ceiling. The group kill runs on the normal exit path
# too, which reaps the servers and workers a suite starts and never stops.
# Exit 142 is the timeout (128 + SIGALRM's 14), the code the shell used to
# report when the alarm killed perl itself.
my constant TIMEOUT-GROUP = q:to/PERL/.trim;
    my ($limit, $cmd) = @ARGV;
    open(STDIN, "<", "/dev/null");
    my $pid = fork();
    exit(127) unless defined $pid;
    unless ($pid) { setpgrp(0, 0); exec("/bin/sh", "-c", $cmd); exit(127) }
    $SIG{ALRM} = sub { kill(-9, $pid); waitpid($pid, 0); exit(142) };
    alarm($limit);
    waitpid($pid, 0);
    my $status = $?;
    alarm(0);
    kill(-9, $pid);
    exit($status & 127 ? 128 + ($status & 127) : $status >> 8);
    PERL

sub classify($target, $log, $exit) {
    return ('timeout', '') if $exit == 142;      # the alarm in TIMEOUT-GROUP
    my @lines = $log.lines;
    return ('pass', '') if $exit == 0 && @lines.grep({ /^ 'tested ' / && / '— suite green' / });

    for @lines.reverse -> $l {
        if $l ~~ /^ (\S+) ': its own test suite fails' / {
            my $who = ~$0;
            return $who eq $target ?? ('self-fail', '') !! ('dep-fail', $who);
        }
        if $l ~~ /^ (\S+) ': its build hook fails' / {
            my $who = ~$0;
            return $who eq $target ?? ('build-fail', '') !! ('dep-build-fail', $who);
        }
        if $l ~~ /^ 'cannot resolve: ' (.*) / { return ('unresolved', ~$0) }
        if $l ~~ /^ 'fetch failed: ' (.*) /   { return ('fetch-fail', ~$0) }
        if $l ~~ /^ 'checksum mismatch' /     { return ('checksum-fail', $l) }
    }
    ('other', @lines.grep({ .trim.chars }).tail // '')
}

# The first thing that went wrong, for the ledger: the first FAILED file of the
# target's own suite and the first diagnostic line under it, or the first
# ===SORRY!=== if it never compiled.
sub first-error($log) {
    my @lines = $log.lines;
    for @lines.kv -> $i, $l {
        if $l.starts-with('===SORRY!===') { return $l.trim }
        if $l.starts-with('FAILED: ') {
            my $detail = (@lines[$i + 1] // '').trim;
            return "$l | $detail";
        }
    }
    ''
}

# one TSV cell: no tabs, no newlines, and short enough to read in a terminal
# (a dlopen failure lists every path it tried and runs past 900 characters)
sub cell($s) {
    my $t = $s.subst("\t", ' ', :g).subst("\n", ' ', :g).trim;
    $t.chars > 300 ?? $t.substr(0, 300) ~ ' …' !! $t
}

sub MAIN($list, :$store!, :$logs!, :$out!, :$timeout = 180, :$rakupp = $*EXECUTABLE.absolute) {
    $logs.IO.mkdir unless $logs.IO.d;
    my %done;
    if $out.IO.e {
        for $out.IO.lines.skip(1) -> $l { %done{$l.split("\t")[0]} = True }
        note "resuming: {%done.elems} dists already recorded";
    }
    else {
        my $h = $out.IO.open(:w);
        $h.say: "name\tversion\treleased\tverdict\tdetail\tseconds\texit\tfirst-error";
        $h.close;
    }

    my @rows = $list.IO.lines.skip(1).map({ .split("\t") }).grep({ .[1] });
    my $n = +@rows;
    for @rows.kv -> $i, @r {
        my ($date, $name, $ver) = @r[0, 1, 2];
        next if %done{$name};
        my $logfile = $logs.IO.add($name.subst('::', '-', :g) ~ '.log');
        my $cmd = "'$rakupp' test --to='$store' '$name' > '$logfile' 2>&1";
        my $t0 = now;
        my $p = run '/usr/bin/perl', '-e', TIMEOUT-GROUP, ~$timeout, $cmd, :err;
        $p.err.slurp(:close);
        my $secs = ((now - $t0) * 10).Int / 10;
        my $log = $logfile.e ?? $logfile.slurp !! '';

        my ($verdict, $detail) = classify($name, $log, $p.exitcode);

        # `other` is the bucket classify() falls into when it recognizes
        # nothing in the log, and it is the one verdict that can also mean the
        # HARNESS failed rather than the dist. Recording that as a verdict is
        # how a saturated machine writes 2,400 rows that read exactly like
        # measurements and are not; it happened here, and the whole sweep had
        # to be thrown away.
        #
        # The two are told apart by asking whether `rakupp test` ever spoke.
        # It announces itself before it does anything — the resolved plan, or
        # a note ahead of it, or the one-line refusal it exits on — so a log
        # with any of that in it belongs to a dist that RAN, whatever the
        # clock says and however odd the exit. FindBin-libs is the case that
        # taught this: no META6.json in its archive, a legitimate verdict in
        # 0.8 seconds, which an earlier version of this guard called a machine
        # failure and stopped a 2,525-dist sweep over. What the harness
        # failing looks like instead is silence plus an exit code only the
        # wrapper can produce: 127 (fork or exec failed), 128 (the shell could
        # not exec), or -1 (rakupp could not spawn perl at all).
        my $spoke = $log.contains('plan (') || $log.contains('note: ')
                 || $log.contains('cannot resolve') || $log.contains('fetch failed')
                 || $log.contains('===SORRY');
        if $verdict eq 'other' && !$spoke && $secs < 5
           && $p.exitcode == -1 | 127 | 128 {
            note '';
            note "ABORTING at $name: `rakupp test` came back in {$secs}s with "
               ~ "nothing a verdict can be read from (exit {$p.exitcode}).";
            note "  " ~ ($log.lines.grep(*.trim.chars).head // '(empty log)').trim;
            note "That is the harness failing, not the dist — most likely the "
               ~ "machine is out of process slots (compare `ps ax | wc -l` "
               ~ "with `ulimit -u`) or --rakupp does not exist.";
            note "Nothing is recorded for $name. Fix it and re-run: $out is "
               ~ "resumable and keeps every dist already in it.";
            exit 3;
        }
        my $err = $verdict eq 'pass' ?? '' !! first-error($log);
        my $h = $out.IO.open(:a);
        $h.say: "$name\t$ver\t$date\t$verdict\t{cell($detail)}\t$secs\t{$p.exitcode}\t{cell($err)}";
        $h.close;
        note sprintf("[%3d/%3d] %-14s %5.1fs  %s%s", $i + 1, $n, $verdict, $secs, $name,
                     $detail ?? " ($detail)" !! '');
    }
    note "done";
}
