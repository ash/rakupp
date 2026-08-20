#!/usr/bin/env rakupp
# pwc-sweep.raku — run Weekly Challenge solutions under Rakudo and rakupp and
# report where they differ.
#
#   rakupp tools/pwc-sweep.raku --from=371 --to=386 [--repo=PATH] [--out=PATH]
#
# A file counts only when Rakudo itself can run it headlessly and REPEATABLY:
# solutions that want arguments, modules, input files or a terminal fail under
# Rakudo too, and ones that print a timestamp or a random draw differ from
# themselves. Both are skipped rather than counted as agreement.
#
# Comparison is stdout plus success/failure. stderr is deliberately out of it:
# the two engines word their warnings differently and that is not what this
# corpus is for.
constant TIMEOUT = 8;

sub run-capture(Str() $exe, IO::Path $file, Int $secs = TIMEOUT) {
    # perl's alarm is the portable timeout here: macOS ships no timeout(1), and
    # a wedged child must not take the sweep with it.
    my $p = run('/usr/bin/perl', '-e',
                'alarm shift; open(STDIN, "<", "/dev/null"); exec @ARGV or exit 127',
                ~$secs, $exe, ~$file,
                :out, :err, :cwd($file.parent));
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    ($out, $p.exitcode)
}

sub MAIN(Int :$from = 371, Int :$to = 386,
         Str :$repo = %*ENV<HOME> ~ '/perlweeklychallenge-club',
         Str() :$rakupp = $*EXECUTABLE.absolute,
         Str :$rakudo = 'raku',
         Str :$out = 'pwc-results.jsonl') {
    my @files;
    for $from .. $to -> $n {
        my $dir = $repo.IO.add(sprintf('challenge-%03d', $n));
        next unless $dir.d;
        @files.append: $dir.dir.grep(*.d).map({ .dir.grep(*.d) }).flat
                           .map({ .dir.grep({ .extension eq 'raku' | 'p6' }) }).flat;
    }
    @files = @files.sort(*.Str);
    note "sweeping {+@files} files from challenges $from..$to";

    my $fh = $out.IO.open(:w);
    my %tally;
    for @files.kv -> $i, $f {
        my ($r1, $e1) = run-capture($rakudo, $f);
        my $verdict;
        if $e1 != 0 && $r1.chars == 0 { $verdict = 'skip-rakudo-fails' }
        else {
            my ($r2, $e2) = run-capture($rakudo, $f);
            if $r1 ne $r2 || $e1 != $e2 { $verdict = 'skip-nondeterministic' }
            else {
                my ($rp, $ep) = run-capture($rakupp, $f);
                $verdict = ($rp eq $r1 && ($ep == 0) == ($e1 == 0)) ?? 'match' !! 'mismatch';
                if $verdict eq 'mismatch' {
                    $fh.say: qq[\{"file":"{$f.relative($repo)}","rakudo_exit":$e1,"rakupp_exit":$ep,] ~
                             qq["rakudo":"{$r1.substr(0, 400).subst('\\', '\\\\', :g).subst('"', '\\"', :g).subst("\n", '\\n', :g)}",] ~
                             qq["rakupp":"{$rp.substr(0, 400).subst('\\', '\\\\', :g).subst('"', '\\"', :g).subst("\n", '\\n', :g)}"\}];
                    $fh.flush;
                }
            }
        }
        %tally{$verdict}++;
        note "  [{$i + 1}/{+@files}] $verdict  {$f.relative($repo)}" if $verdict eq 'mismatch';
    }
    $fh.close;
    say "";
    say "%tally{$_} $_" for %tally.keys.sort;
    my $counted = (%tally<match> // 0) + (%tally<mismatch> // 0);
    say "byte-identical: {%tally<match> // 0} of $counted counted files"
        ~ ($counted ?? sprintf(" (%.1f%%)", 100 * (%tally<match> // 0) / $counted) !! "");
}
