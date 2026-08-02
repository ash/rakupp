#!/usr/bin/env raku
# Run every ```raku block in docs/ on BOTH engines and classify the result.
#
#   raku tools/doc-examples-diff.raku [rakupp=build/rakupp] [PATH-SUBSTRING]
#
# Why this exists: the guides say their examples are verified, and they were —
# against `rakupp` only. Running them on the permissive engine alone cannot
# catch code that does not travel, and a sweep on 2026-08-02 found five such
# examples, including one whose documented output no engine has ever produced.
#
# Buckets, in descending order of interest:
#   RAKUDO-FAILS  runs here, rejected there — the DANGEROUS direction. Our
#                 laxness teaches a habit that breaks on the other engine.
#   DIFFER        both run, outputs disagree. Usually deliberate ($*RAKU.compiler)
#                 or non-deterministic (time, PID, .roll); occasionally real.
#   RAKUPP-FAILS  runs there, rejected here — a plain gap in this engine.
#   BOTH-FAIL     usually a fragment that was never a whole program, or one
#                 needing a module/lib dir. Both agree, so not a portability bug.
#   TIMEOUT       servers and deliberately non-terminating examples.
#
# A DIFFER or a failure is not automatically a bug — read it before believing it.
# In particular, docs/dev/findings/ and docs/dev/experiments/ exist to record code
# that breaks somewhere, so failures there are the content. Pass a PATH-SUBSTRING
# (e.g. `guide`) to sweep only the pages that are meant to work everywhere.

my $RAKUPP = @*ARGS[0] // 'build/rakupp';
my $FILTER = @*ARGS[1] // '';
unless $RAKUPP.IO.x { note "no runnable binary at $RAKUPP"; exit 2 }

my $tmp = $*TMPDIR.add("doc-diff-{$*PID}.raku");
my %tally;
my @interesting;

sub run-with-timeout($bin, $file, $secs) {
    my $proc = Proc::Async.new($bin, $file);
    my ($out, $err) = '', '';
    $proc.stdout.tap(-> $c { $out ~= $c });
    $proc.stderr.tap(-> $c { $err ~= $c });
    my $done = $proc.start;
    await Promise.anyof($done, Promise.in($secs));
    # string compare, as tools/run-roast.raku does — smartmatching the enum is unreliable
    if $done.status ne 'Kept' { try $proc.kill(9); return ('TIMEOUT', '', '') }
    ($done.result.exitcode, $out, $err)
}

# every ```raku fence in the tree, with the line it starts on
sub blocks($path) {
    my @out;
    my ($in, $start, @cur) = False, 0;
    for $path.IO.lines.kv -> $i, $l {
        if !$in && $l.trim eq '```raku' { $in = True; $start = $i + 1; @cur = () }
        elsif $in && $l.trim eq '```'   { $in = False; @out.push($start => @cur.join("\n")) }
        elsif $in                        { @cur.push($l) }
    }
    @out
}

my @files = qx{find docs -name '*.md'}.lines.grep(*.chars).grep({ !$FILTER || .contains($FILTER) }).sort;

for @files -> $f {
    for blocks($f) -> (:key($line), :value($src)) {
        $tmp.spurt($src ~ "\n");
        my ($ac, $ao, $ae) = run-with-timeout($RAKUPP, ~$tmp, 12);
        my ($bc, $bo, $be) = run-with-timeout('raku',   ~$tmp, 30);
        my $kind = do {
            # parens matter: junctive `|` binds TIGHTER than `eq`, so the
            # unparenthesised form chains into `$ac eq ('TIMEOUT'|$bc) eq
            # 'TIMEOUT'` and is true for everything.
            when ($ac eq 'TIMEOUT') || ($bc eq 'TIMEOUT') { 'TIMEOUT' }
            default {
                my ($aok, $bok) = $ac == 0, $bc == 0;
                $aok && $bok && $ao eq $bo ?? 'MATCH'
                  !! $aok && $bok          ?? 'DIFFER'
                  !! $aok                  ?? 'RAKUDO-FAILS'
                  !! $bok                  ?? 'RAKUPP-FAILS'
                  !!                          'BOTH-FAIL';
            }
        }
        %tally{$kind}++;
        say sprintf('%-13s %s:%d', $kind, $f, $line);
        @interesting.push([$kind, $f, $line, $ao, $bo, $ae, $be]) if $kind eq 'DIFFER' | 'RAKUDO-FAILS' | 'RAKUPP-FAILS';
    }
}
$tmp.unlink;

say '';
say '== totals ==';
say sprintf('%4d  %s', %tally{$_}, $_) for %tally.keys.sort({ -%tally{$_} });

if @interesting {
    say "\n== worth reading ==";
    for @interesting -> [$kind, $f, $line, $ao, $bo, $ae, $be] {
        say "\n--- $kind  $f:$line";
        given $kind {
            when 'DIFFER' {
                say "  rakupp: {$ao.lines.join(' | ').substr(0, 100)}";
                say "  rakudo: {$bo.lines.join(' | ').substr(0, 100)}";
            }
            when 'RAKUDO-FAILS' { say "  rakudo: {$be.lines.head(2).join(' ').substr(0, 140)}" }
            when 'RAKUPP-FAILS' { say "  rakupp: {$ae.lines.head(2).join(' ').substr(0, 140)}" }
        }
    }
}
exit 0;
