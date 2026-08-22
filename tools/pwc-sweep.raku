#!/usr/bin/env rakupp
# pwc-sweep.raku — run Weekly Challenge solutions under Rakudo and rakupp and
# report where they differ.
#
#   rakupp tools/pwc-sweep.raku --from=371 --to=386 [--repo=PATH] [--out=PATH]
#   rakupp tools/pwc-sweep.raku --files=LIST --state=FILE [--sample=10] \
#          [--state-out=FILE] [--commit=SHA]
#
# Modes:
#   --from/--to     sweep whole challenge directories (the original mode).
#   --files=LIST    sweep exactly the paths listed in LIST (one per line,
#                   relative to --repo) — this is what the git delta drives,
#                   so a solution merged late into an old directory costs the
#                   same as a fresh one.
#   --state=FILE    previous per-file verdicts. Adds to the run: the open set
#                   (last week's mismatches) and a rotating --sample=N% slice
#                   of last week's passes. A pass that stops matching is a
#                   REGRESSION and gets its own output line. The merged
#                   verdicts are written to --state-out (default: over
#                   --state); its `# run=` counter rotates the pass slice so
#                   the whole pass set re-verifies every 100/N weeks.
#
# A file counts only when Rakudo itself can run it headlessly and REPEATABLY:
# solutions that want arguments, modules, input files or a terminal fail under
# Rakudo too, and ones that print a timestamp or a random draw differ from
# themselves. Both are skipped rather than counted as agreement.
#
# Comparison is stdout plus success/failure. stderr is deliberately out of the
# comparison — the two engines word their warnings differently — but the first
# line of rakupp's stderr rides along in the mismatch record, because it is
# the cheapest clustering signature there is.
constant TIMEOUT = 8;

sub run-capture(Str() $exe, IO::Path $file, Int $secs = TIMEOUT) {
    # perl's alarm is the portable timeout here: macOS ships no timeout(1), and
    # a wedged child must not take the sweep with it.
    my $p = run('/usr/bin/perl', '-e',
                'alarm shift; open(STDIN, "<", "/dev/null"); exec @ARGV or exit 127',
                ~$secs, $exe, ~$file,
                :out, :err, :cwd($file.parent));
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($out, $p.exitcode, ($err.lines[0] // ''))
}

sub esc(Str $s) {
    $s.subst('\\', '\\\\', :g).subst('"', '\\"', :g)
      .subst("\n", '\\n', :g).subst("\t", '\\t', :g).subst("\r", '', :g)
}

sub MAIN(Int :$from = 371, Int :$to = 386,
         Str :$repo = %*ENV<HOME> ~ '/perlweeklychallenge-club',
         Str() :$rakupp = $*EXECUTABLE.absolute,
         Str :$rakudo = 'raku',
         Str :$out = 'pwc-results.jsonl',
         Str :$tsv = '',        # every tested file's verdict this run
         Str :$files = '',      # path list that replaces the --from/--to walk
         Str :$state = '',      # previous verdicts: open set + pass slice + regressions
         Int :$sample = 10,     # % of previous passes to re-verify this run
         Str :$state-out = '',  # merged verdicts (default: back over --state)
         Str :$commit = '') {   # corpus commit id recorded in the state header
    # --- previous state, if any -------------------------------------------
    my %prev;          # file => verdict
    my $run-no = 0;
    if $state && $state.IO.e {
        for $state.IO.lines -> $line {
            if $line.starts-with('#') {
                $run-no = $0.Int if $line ~~ / 'run=' (\d+) /;
            }
            elsif $line && !$line.starts-with('verdict') {
                my ($v, $f) = $line.split("\t");
                %prev{$f} = $v if $f;
            }
        }
        note "state: {+%prev} files, run $run-no";
    }

    # --- the run set -------------------------------------------------------
    my @files;
    my $have-list = ?$files;
    if $have-list {
        for $files.IO.lines -> $line {
            my $l = $line.trim;
            next unless $l;
            next unless $l.ends-with('.raku') || $l.ends-with('.p6');
            @files.push: $l.starts-with('/') ?? $l.IO !! $repo.IO.add($l);
        }
        note "files list: {+@files} candidates";
    }
    if %prev {
        my @open = %prev.keys.grep({ %prev{$_} eq 'mismatch' }).sort;
        @files.append: @open.map({ $repo.IO.add($_) });
        note "open set: {+@open} standing mismatches";
        if $sample > 0 {
            my $buckets = 100 div $sample max 1;
            my @passes = %prev.keys.grep({ %prev{$_} eq 'match' }).sort;
            my @slice = @passes.keys.grep({ $_ % $buckets == $run-no % $buckets })
                               .map({ @passes[$_] });
            @files.append: @slice.map({ $repo.IO.add($_) });
            note "pass slice: {+@slice} of {+@passes} ({$sample}%, bucket {$run-no % $buckets}/$buckets)";
        }
    }
    elsif !$have-list {
        for $from .. $to -> $n {
            my $dir = $repo.IO.add(sprintf('challenge-%03d', $n));
            next unless $dir.d;
            @files.append: $dir.dir.grep(*.d).map({ .dir.grep(*.d) }).flat
                               .map({ .dir.grep({ .extension eq 'raku' | 'p6' }) }).flat;
        }
    }
    my %seen;
    @files = @files.grep({ !%seen{.Str}++ }).sort(*.Str);
    note "sweeping {+@files} files";

    # --- the sweep ---------------------------------------------------------
    my $fh = $out.IO.open(:w);
    my $tfh = $tsv ?? $tsv.IO.open(:w) !! Nil;
    $tfh andthen .say("verdict\tfile");
    my %tally;
    my %new;           # file (repo-relative) => verdict from this run
    my @regressions;
    for @files.kv -> $i, $f {
        my $rel = $f.relative($repo);
        my $verdict;
        if !$f.e {
            $verdict = 'gone';
        }
        else {
            my ($r1, $e1) = run-capture($rakudo, $f);
            if $e1 != 0 && $r1.chars == 0 { $verdict = 'skip-rakudo-fails' }
            else {
                my ($r2, $e2) = run-capture($rakudo, $f);
                if $r1 ne $r2 || $e1 != $e2 { $verdict = 'skip-nondeterministic' }
                else {
                    my ($rp, $ep, $errline) = run-capture($rakupp, $f);
                    $verdict = ($rp eq $r1 && ($ep == 0) == ($e1 == 0)) ?? 'match' !! 'mismatch';
                    if $verdict eq 'mismatch' {
                        $fh.say: qq[\{"file":"{esc($rel)}","rakudo_exit":$e1,"rakupp_exit":$ep,] ~
                                 qq["rakudo":"{esc($r1.substr(0, 400))}",] ~
                                 qq["rakupp":"{esc($rp.substr(0, 400))}",] ~
                                 qq["err":"{esc($errline.substr(0, 200))}"\}];
                        $fh.flush;
                    }
                }
            }
        }
        %tally{$verdict}++;
        %new{$rel} = $verdict;
        if %prev && (%prev{$rel} // '') eq 'match' && $verdict eq 'mismatch' {
            @regressions.push: $rel;
            note "  REGRESSION $rel";
        }
        $tfh andthen .say("$verdict\t$rel");
        note "  [{$i + 1}/{+@files}] $verdict  $rel" if $verdict eq 'mismatch';
    }
    $fh.close;
    $tfh andthen .close;

    # --- merged state ------------------------------------------------------
    my %merged = %prev;
    for %new.kv -> $f, $v {
        if $v eq 'gone' { %merged{$f}:delete }
        else { %merged{$f} = $v }
    }
    my $sout = $state-out || $state;
    if $sout {
        my $sfh = $sout.IO.open(:w);
        $sfh.say: "# run={$run-no + 1} commit=$commit date={Date.today}";
        for %merged.keys.sort -> $f {
            $sfh.say: "{%merged{$f}}\t$f";
        }
        $sfh.close;
        note "state written: $sout ({+%merged} files)";
    }

    # --- report ------------------------------------------------------------
    say "";
    say "REGRESSION $_" for @regressions;
    say "%tally{$_} $_" for %tally.keys.sort;
    my $counted = (%tally<match> // 0) + (%tally<mismatch> // 0);
    say "byte-identical: {%tally<match> // 0} of $counted counted files"
        ~ ($counted ?? sprintf(" (%.1f%%)", 100 * (%tally<match> // 0) / $counted) !! "");
    my $open-total = +%merged.values.grep(* eq 'mismatch');
    my $pass-total = +%merged.values.grep(* eq 'match');
    say "SUMMARY run={$run-no + 1} tested={+@files} match={%tally<match> // 0} "
      ~ "mismatch={%tally<mismatch> // 0} skip-rakudo-fails={%tally<skip-rakudo-fails> // 0} "
      ~ "skip-nondeterministic={%tally<skip-nondeterministic> // 0} gone={%tally<gone> // 0} "
      ~ "regressions={+@regressions} open-total=$open-total pass-total=$pass-total";
}
