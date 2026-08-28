#!/usr/bin/env rakupp
# eco-sweep.raku — run `rakupp test` over a list of distributions and record
# one verdict per line, so a batch of an author's ecosystem can be measured the
# same way twice.
#
#   rakupp tools/eco-sweep.raku LIST.tsv --out=RESULT.tsv --logs=DIR [--timeout=480]
#
# LIST.tsv is `date TAB name TAB version` (a `date` header line is skipped);
# only the NAME column is used, and the file's order is the sweep's order.
#
# Verdicts: pass (its own suite is green end to end), self-fail (the target's
# suite fails), dep-fail (a dependency's suite fails first — the target never
# ran), build-fail, unresolved, timeout, error.
#
# Resumable: a name already present in --out is skipped, so a wedged sweep
# restarts without re-paying the modules it already measured.

sub classify($log) {
    my $txt = $log.IO.e ?? $log.IO.slurp !! '';
    return ('error', 'no log') unless $txt;
    return ('pass', '')  if $txt.contains('suite green');
    # "NAME: its own test suite fails under rakupp" — whose? The name carries
    # `::`, so the class that stops at the colon stops inside it.
    if $txt ~~ /^^ (\S+?) ': its own test suite fails' / {
        return ('self-fail', ~$0);   # caller decides self vs dep by comparing names
    }
    return ('build-fail', ~$0)  if $txt ~~ /'BUILD FAILED: ' (\N+)/;
    return ('unresolved', ~$0)  if $txt ~~ /'cannot resolve: ' (\N+)/;
    return ('error', ~$0)       if $txt ~~ /^^ 'FAILED: ' (\N+)/;
    return ('error', 'no verdict');
}

# The first thing that actually went wrong, for the triage column.
sub diagnosis($log) {
    my $txt = $log.IO.e ?? $log.IO.slurp !! '';
    for $txt.lines -> $l {
        next unless $l.starts-with('#') || $l.starts-with('===SORRY');
        next if $l.starts-with('# Subtest') || $l ~~ /^ '#' \s* $/;
        next if $l.contains('Looks like you');
        return $l.trim.substr(0, 200);
    }
    for $txt.lines -> $l {
        return $l.trim.substr(0, 200) if $l.contains('FAILED') || $l.contains('Error');
    }
    return '';
}

sub MAIN($list, :$out!, :$logs!, Int :$timeout = 480, :$store = '',
         Bool :$reclassify = False,   #= re-read the logs of a finished sweep, run nothing
         :$rakupp = $*EXECUTABLE.absolute) {
    $logs.IO.mkdir unless $logs.IO.d;
    my @names = $list.IO.lines.grep({ !.starts-with('date') && .chars })
                             .map({ .split("\t")[1] // .split("\t")[0] })
                             .grep(*.chars);
    my %done;
    if $out.IO.e {
        for $out.IO.lines -> $l { %done{$l.split("\t")[1] // ''} = True }
    }
    my $fh = open $out, :a;
    my $n = +@names;
    for @names.kv -> $i, $name {
        if %done{$name} && !$reclassify { note "[{$i+1}/$n] $name — already recorded, skipping"; next }
        my $logfile = $logs.IO.add($name.subst('::', '-', :g) ~ '.log');
        my $toArg = $store ?? "--to='$store'" !! '';
        my $cmd = "'$rakupp' test $toArg '$name' > '$logfile' 2>&1";
        my $t0 = now;
        if $reclassify {
            my ($v, $w) = classify($logfile);
            my $nt = '';
            if $v eq 'self-fail' && $w && $w ne $name { $v = 'dep-fail'; $nt = $w }
            elsif $v eq 'self-fail' { $nt = diagnosis($logfile) }
            else { $nt = $w || diagnosis($logfile) }
            $fh.say(join "\t", $i + 1, $name, $v, '', $nt);
            $fh.flush;
            note "[{$i+1}/$n] $name — $v";
            next;
        }
        # perl's alarm as the timeout: macOS ships no timeout(1), and one wedged
        # build hook must not take the whole sweep with it.
        my $p = run '/usr/bin/perl', '-e',
                    'alarm shift; open(STDIN, "<", "/dev/null"); exec "/bin/sh", "-c", $ARGV[0] or exit 127',
                    ~$timeout, $cmd, :err;
        $p.err.slurp(:close);
        my $secs = ((now - $t0) * 10).Int / 10;
        my ($verdict, $who) = $p.exitcode == 142 || $secs >= $timeout
                              ?? ('timeout', '') !! classify($logfile);
        # `self-fail` names the dist whose suite failed — if it is not the
        # target, the target never got to run.
        my $note = '';
        if $verdict eq 'self-fail' && $who && $who ne $name {
            $verdict = 'dep-fail'; $note = $who;
        }
        elsif $verdict eq 'self-fail' { $note = diagnosis($logfile) }
        else { $note = $who || diagnosis($logfile) }
        $fh.say(join "\t", $i + 1, $name, $verdict, $secs, $note);
        $fh.flush;
        note "[{$i+1}/$n] $name — $verdict ({$secs}s)";
    }
    $fh.close;
    note "sweep done: $n modules -> $out";
}
