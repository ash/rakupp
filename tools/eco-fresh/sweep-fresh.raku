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

sub classify($target, $log, $exit) {
    return ('timeout', '') if $exit == 142;      # SIGALRM through the shell
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
        my $p = run '/usr/bin/perl', '-e',
                    'alarm shift; open(STDIN, "<", "/dev/null"); exec "/bin/sh", "-c", $ARGV[0] or exit 127',
                    ~$timeout, $cmd, :err;
        $p.err.slurp(:close);
        my $secs = ((now - $t0) * 10).Int / 10;
        my $log = $logfile.e ?? $logfile.slurp !! '';
        my ($verdict, $detail) = classify($name, $log, $p.exitcode);
        my $err = $verdict eq 'pass' ?? '' !! first-error($log);
        my $h = $out.IO.open(:a);
        $h.say: "$name\t$ver\t$date\t$verdict\t{cell($detail)}\t$secs\t{$p.exitcode}\t{cell($err)}";
        $h.close;
        note sprintf("[%3d/%3d] %-14s %5.1fs  %s%s", $i + 1, $n, $verdict, $secs, $name,
                     $detail ?? " ($detail)" !! '');
    }
    note "done";
}
