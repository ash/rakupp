#!/usr/bin/env rakupp
# eco-sweep.raku — run `rakupp test` over a list of distributions and record
# one verdict per line, so a batch of an author's ecosystem can be measured the
# same way twice.
#
#   rakupp tools/eco-sweep.raku --out=RESULT.tsv --logs=DIR [--timeout=480] LIST.tsv
#
# NOTE THE ORDER: every --option must come BEFORE LIST.tsv. Raku's MAIN parser
# stops treating `--x=y` as an option once a positional has been seen, so the
# order this line used to show (LIST.tsv first) prints the usage message and
# sweeps nothing — on both rakupp and Rakudo.
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

use lib $?FILE.IO.parent.add('lib').Str;
use Gate;

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
    # --reclassify REWRITES rather than appends. It used to append, so a
    # reclassified sweep left two rows per distribution in --out and every
    # consumer had to know to take the last one — a silent trap for anything
    # that counted verdicts. The rows are collected and written at the end;
    # a measuring run still appends line by line, so a wedged sweep keeps
    # everything it has already paid for.
    my $fh = $reclassify ?? Nil !! open($out, :a);
    my @reclassified;
    sub emit(Str $row) {
        if $reclassify { @reclassified.push($row) }
        else           { $fh.say($row); $fh.flush }
    }
    my $n = +@names;
    my $measured = 0;
    my $skipped  = 0;
    # Say WHAT is being swept with. A sweep's verdicts are a headline figure
    # (README: "N of the ecosystem's 2,524 distributions") and carried no record
    # of the engine that produced them.
    my $ver = binary-version($rakupp);
    note "eco-sweep: rakupp $ver ($rakupp) | $n names from $list | -> $out";
    note "eco-sweep: {%done.elems} already in $out and will be skipped" if %done.elems && !$reclassify;
    for @names.kv -> $i, $name {
        if %done{$name} && !$reclassify { $skipped++; note "[{$i+1}/$n] $name — already recorded, skipping"; next }
        $measured++;
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
            emit(join "\t", $i + 1, $name, $v, '', $nt);
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
        emit(join "\t", $i + 1, $name, $verdict, $secs, $note);
        note "[{$i+1}/$n] $name — $verdict ({$secs}s)";
    }
    if $reclassify {
        $out.IO.spurt(@reclassified.join("\n") ~ "\n");
        note "reclassified {@reclassified.elems} rows -> $out (rewritten, not appended)";
    }
    else { $fh.close }
    # Report what was MEASURED, not the size of the input list. This said
    # "sweep done: 2524 modules" whether it ran 2,524 or zero — the resume skip
    # is keyed on a name already appearing in --out, so pointing a re-sweep at
    # the previous result file does nothing and says the same sentence. v3.23.0
    # asks for the ecosystem re-swept "from scratch rather than from the last
    # verdict file", which is exactly the distinction this line used to hide.
    my $verb = $reclassify ?? 'reclassified' !! 'measured';
    note "sweep done: $measured $verb, $skipped skipped (already in \$out), $n in the list -> $out";
    note "NOTHING WAS MEASURED — every name was already in $out. Use a fresh --out for a full re-sweep."
        if $measured == 0 && $n > 0;
}
