#!/usr/bin/env rakupp
# Run test files under the race detector, and keep what it says.
# (docs/dev/plans/RACE-DETECTOR-PLAN.md, P4.)
#
#   rakupp tools/race-sweep.raku [--roast=DIR] [-j=N] [--timeout=S] [--skip=WORD] [--out=FILE] [PATTERN ...]
#   rakupp tools/race-sweep.raku --dir=DIR [...]      # every .t / .rakutest / .raku under DIR
#   rakupp tools/race-sweep.raku --root=DIST [...]    # a distribution's t/, run from DIST with -I DIST/lib
#
# With no --dir, the files are Roast's (default $ROAST, else ~/roast) whose
# path contains a PATTERN (default S17), fudged for rakudo.moar as
# tools/run-roast.raku fudges them. Each file runs twice: plain, then under
# --detect-races. A file is
#   clean    the detector ran it and said nothing;
#   races    it reported, and the reports are kept verbatim;
#   changed  the two runs passed a different number of tests or exited
#            differently (66 aside), which the detector must never cause.
# --out=FILE writes the full reports; the summary goes to stdout. --skip=WORD
# leaves out the files whose name holds WORD (`--skip=stress`: the stress
# files spawn hundreds of threads or processes, which the detector slows).

sub MAIN(*@patterns, Str :$roast = %*ENV<ROAST> // "%*ENV<HOME>/roast", Str :$dir is copy, Str :$root, Str :$skip,
         Int :j($jobs) = 4, Int :$timeout = 300, Str :$out, Str :$rakupp = $*EXECUTABLE.absolute) {
    my @files;
    my %run-as;
    my @lib = $root ?? ('-I', $root.IO.add('lib').absolute) !! ();
    $dir = $root.IO.add('t').Str if $root;
    if $dir {
        my @todo = $dir.IO;
        while @todo {
            for @todo.shift.dir.sort -> $e {
                if $e.d { @todo.push: $e }
                elsif $e.extension eq 't' | 'rakutest' | 'raku' { @files.push: $e.absolute }
            }
        }
    }
    else {
        @patterns ||= 'S17';
        my @todo = $roast.IO;
        while @todo {
            for @todo.shift.dir.sort -> $e {
                if $e.d { @todo.push: $e unless $e.basename.starts-with('.') }
                elsif $e.extension eq 't' && @patterns.first({ $e.absolute.contains($_) }) && !($skip && $e.basename.contains($skip)) { @files.push: $e.absolute }
            }
        }
        my $fudge = $roast.IO.add('fudge');
        for @files -> $f {
            next unless $f.IO.lines.first(*.trim-leading.starts-with('#?')).defined;
            my $p = run 'perl', $fudge.Str, '--keep-exit-code', '--version=v6.d', 'rakudo.moar', $f, :out, :err;
            my $picked = $p.out.slurp(:close).trim;
            $p.err.slurp(:close);
            %run-as{$f} = $picked if $picked && $picked ne $f && $picked.IO.e;
        }
    }
    @files = @files.sort;
    say "{+@files} files, -j=$jobs, timeout {$timeout}s";

    my $scratch = $*TMPDIR.add("race-sweep-$*PID");
    $scratch.mkdir;
    # one run: (exit code, passed tests, stderr, timed out)
    sub run-one(Str $file, @opts) {
        my $proc = Proc::Async.new($rakupp, |@lib, |@opts, %run-as{$file} // $file);
        my ($o, $e) = '', '';
        $proc.stdout.tap(-> $c { $o ~= $c; Nil });
        $proc.stderr.tap(-> $c { $e ~= $c; Nil });
        my $done = $proc.start(:cwd($root ?? $root.IO.absolute !! $scratch.Str));
        my $timedout = False;
        await Promise.anyof($done, Promise.in($timeout));
        unless $done {
            $timedout = True;
            $proc.kill(9);
            try await $done;
        }
        my $code = $done.status ~~ Kept ?? $done.result.exitcode !! -1;
        my $passed = +$o.lines.grep(/^ 'ok ' \d/);
        ($code, $passed, $e, $timedout)
    }
    my @results;
    my $next = 0;
    my $lock = Lock.new;
    await (^$jobs).map: -> $w {
        start {
            loop {
                my $i = $lock.protect({ $next++ });
                last if $i >= @files;
                my $f = @files[$i];
                my ($pc, $pp, $, $pt) = run-one($f, ());
                my ($rc, $rp, $re, $rt) = run-one($f, ('--detect-races',));
                my @reports;
                my $cur;
                for $re.lines -> $l {
                    if $l.starts-with('rakupp: data race on ') { @reports.push: $cur = $l ~ "\n" }
                    elsif $cur.defined && $l.starts-with('  ') { $cur ~= $l ~ "\n"; @reports[*-1] = $cur }
                    else { $cur = Nil }
                }
                my $verdict = $pt || $rt ?? 'timeout'
                           !! ($pp != $rp || ($pc != $rc && !($rc == 66 && @reports))) ?? 'changed'
                           !! @reports ?? 'races' !! 'clean';
                $lock.protect({
                    @results.push: %( :file($f.subst($roast ~ '/', '').subst(($root // '/nonesuch').IO.absolute ~ '/', '')), :$verdict, :@reports,
                                      :plain("exit $pc, $pp ok"), :raced("exit $rc, $rp ok") );
                });
            }
        }
    };
    run 'rm', '-rf', $scratch.Str;

    my %count;
    for @results.sort(*<file>) -> %r {
        %count{%r<verdict>}++;
        next if %r<verdict> eq 'clean';
        say sprintf "%-8s %s  (plain: %s; detector: %s; %d report%s)", %r<verdict>, %r<file>, %r<plain>, %r<raced>,
                    +%r<reports>, %r<reports> == 1 ?? '' !! 's';
    }
    say %count.sort.map({ "{.value} {.key}" }).join(', '), "; {[+] @results.map({ +(.<raced> ~~ /(\d+) ' ok'/)[0] })} tests passed under the detector";
    if $out {
        my $fh = open $out, :w;
        for @results.sort(*<file>).grep(*<reports>) -> %r {
            $fh.say: "=== %r<file> (%r<verdict>)";
            $fh.print: $_ for %r<reports>.list;
            $fh.say: '';
        }
        $fh.close;
        say "reports: $out";
    }
}
