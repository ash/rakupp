# The race detector's corpus (docs/dev/plans/RACE-DETECTOR-PLAN.md, P0).
#
# Two halves, each program labeled by reading it against the memory model in
# docs/guide/ASYNC.md (Rakudo has no detector, so it cannot be the oracle):
#
#   must-stay-silent/  correct programs; `# edges:` names the sync edges each
#                      one exists to test, `# expect:` its last line of output
#   must-report/       programs with a data race; `# location:` names it
#
# plus t/stress programs labeled by reference in references.txt. (`t/race/`
# is a different subject: run-until-it-shows reproducers of the engine's own
# races.)
#
# Every must-stay-silent program runs under RAKUPP_RACE_TRACE=edges, by
# default and under RAKUPP_GIL=1, and has to print its expected output, show
# every edge it names (P1: an acquire that learned another thread's clock,
# or for a spawn kind, start, then or hyper, the fork) and report no race. A
# `# gil: skip (why)` label leaves the GIL leg out, and `# par: skip (why)`
# the default, parallel one (in references.txt: `gil:skip`, `par:skip`). A
# must-report program runs under --detect-races in both modes and has to
# report a race on the location its label names and exit 66. A label is a
# kind and a name:
# `scalar $n`, `array element @a[0]`, `array shape @out` (reported as "the
# length of @out"), `hash entry %h<k>`, `hash shape %seen` ("the keys of
# %seen"), `attribute $!count`.
#
#   rakupp t/race-detect/run.raku [--rakupp=build/rakupp] [--runs=N] [--only=SUBSTRING]
#
# --runs repeats every check (the plan's gate is 20). Exits 1 if any check
# fails.

my $here = $?FILE.IO.parent;
my $root = $here.parent.parent;

sub MAIN(Str :$rakupp = $root.add('build/rakupp').Str, Int :$runs = 1, Str :$only = '') {
    my constant @fork-kinds = <start then hyper timer worker>;
    # the location kinds the detector tracks; a report program on another
    # kind is labeled and waits
    my constant @located-kinds = <scalar array hash attribute>;
    my constant @edge-kinds = |@fork-kinds, |<promise lock lock-async semaphore channel
                                             channel-close supply react atomic>;

    # ---- the programs and their labels ----
    sub labels(IO::Path $p) {
        my %l;
        for $p.lines {
            last unless .starts-with('#');
            %l{~$0} = ~$1 if / ^ '#' \s* ('race-detect' | edges | expect | location | gil | par) ':' \s* (.*?) \s* $ /;
        }
        %l
    }
    my @progs;
    for <must-stay-silent must-report> -> $half {
        for $here.add($half).dir(test => *.ends-with('.raku')).sort -> $p {
            my %l = labels($p);
            @progs.push: %( path => $p, name => "$half/{$p.basename}",
                            verdict => %l<race-detect> // '', edges => (%l<edges> // '').words.List,
                            expect => %l<expect>, location => %l<location>, gil => %l<gil>, par => %l<par> );
        }
    }
    for $here.add('references.txt').lines -> $line {
        next if $line.starts-with('#') || !$line.trim;
        my ($path, $verdict, *@rest) = $line.words;
        my $gilSkip = @rest && @rest[*-1] eq 'gil:skip';   # (the reason is the comment above it)
        @rest.pop if $gilSkip;
        my $parSkip = @rest && @rest[*-1] eq 'par:skip';   # …and for the default, parallel leg
        @rest.pop if $parSkip;
        @progs.push: %( path => $root.add($path), name => $path, verdict => $verdict,
                        edges => ($verdict eq 'silent' ?? @rest.List !! ().List),
                        expect => ($verdict eq 'silent' ?? 'PASS' !! Str),
                        location => ($verdict eq 'report' ?? @rest.join(' ') !! Str),
                        gil => ($gilSkip ?? 'skip (references.txt says why)' !! Str),
                        par => ($parSkip ?? 'skip (references.txt says why)' !! Str) );
    }
    @progs = @progs.grep(*<name>.contains($only)) if $only;

    # ---- one run, under the trace (a silent program) or the detector alone ----
    # (a report program runs under the flag, `--detect-races`, as a user runs
    # it; the edge trace has no flag, only RAKUPP_RACE_TRACE)
    sub run-one(IO::Path $p, Bool :$gil, Bool :$trace = True) {
        my %env = %*ENV;
        %env<RAKUPP_RACE>:delete;
        if $trace { %env<RAKUPP_RACE_TRACE> = 'edges' }
        else      { %env<RAKUPP_RACE_TRACE>:delete }
        if $gil { %env<RAKUPP_GIL> = '1' }
        else    { %env<RAKUPP_GIL>:delete }
        my $proc = run $rakupp, |('--detect-races' unless $trace), $p.Str, :out, :err, :%env;
        my $out = $proc.out.slurp(:close);
        my $err = $proc.err.slurp(:close);
        ($proc.exitcode, $out, $err)
    }
    # Does a report name this location? A header gives a name, and the names
    # the other thread reached it by: `rakupp: data race on $x (also reached
    # as $v)`. An attribute's name goes on with its class (`$!count of Counter`).
    sub reported(Str $err, Str $name) {
        so $err.lines.first: -> $l {
            $l ~~ / ^ 'rakupp: data race on ' (.+?) [ ' (also reached as ' (.+) ')' ]? $ / &&
                so (~$0, |($1 ?? $1.Str.split(', ') !! ())).first({ $_ eq $name || .starts-with("$name of ") })
        }
    }
    sub shows(Str $err, Str $kind) {
        my $head = $kind (elem) @fork-kinds ?? "race-trace: fork $kind " !! "race-trace: acquire $kind ";
        so $err.lines.first(*.starts-with($head))
    }

    my $n = 0;
    my $failed = 0;
    my $pending = 0;
    for @progs -> %p {
        if %p<verdict> eq 'report' {
            unless %p<location> {
                $n++; $failed++;
                say "not ok $n - %p<name>: no `# location:` label";
                next;
            }
            my ($kind, $name) = %p<location>.words[0, *-1];
            unless $kind (elem) @located-kinds { $pending++; next }
            # a shape is reported by what it is: an array's length, a hash's keys
            $name = ($kind eq 'array' ?? 'the length of ' !! 'the keys of ') ~ $name
                if %p<location>.words[1] eq 'shape';
            for (False, True) -> $gil {
                my $mode = $gil ?? 'gil' !! 'default';
                if $gil && %p<gil> { $n++; say "ok $n - %p<name> ($mode) # SKIP %p<gil>"; next }
                if !$gil && %p<par> { $n++; say "ok $n - %p<name> ($mode) # SKIP %p<par>"; next }
                my @why;
                for ^$runs -> $r {
                    my ($code, $out, $err) = run-one(%p<path>, :$gil, :!trace);
                    @why.push("run {$r + 1}: exit $code, not 66") if $code != 66;
                    @why.push("run {$r + 1}: no race on $name reported") unless reported($err, $name);
                    last if @why;
                }
                $n++;
                if @why { $failed++; say "not ok $n - %p<name> ($mode): reports $name"; say "#   $_" for @why }
                else    { say "ok $n - %p<name> ($mode): reports $name" }
            }
            next;
        }
        unless %p<verdict> eq 'silent' && %p<edges> {
            $n++; $failed++;
            say "not ok $n - %p<name>: labels incomplete (race-detect: silent, edges: …)";
            next;
        }
        my @bad = %p<edges>.grep({ $_ !(elem) @edge-kinds });
        if @bad {
            $n++; $failed++;
            say "not ok $n - %p<name>: unknown edge kind(s) @bad[]";
            next;
        }
        for (False, True) -> $gil {
            my $mode = $gil ?? 'gil' !! 'default';
            if $gil && %p<gil> {
                $n++;
                say "ok $n - %p<name> ($mode) # SKIP %p<gil>";
                next;
            }
            my @why;
            for ^$runs -> $r {
                my ($code, $out, $err) = run-one(%p<path>, :$gil);
                my $last = $out.lines.tail // '';
                @why.push("run {$r + 1}: exit $code") if $code;
                @why.push("run {$r + 1}: said '$last', want '%p<expect>'") if %p<expect>.defined && $last ne %p<expect>;
                my @missing = %p<edges>.grep({ !shows($err, $_) });
                @why.push("run {$r + 1}: no @missing[] edge in the trace") if @missing;
                @why.push("run {$r + 1}: reported {.substr(8)}") for $err.lines.grep(*.starts-with('rakupp: data race on'));
                last if @why;
            }
            $n++;
            if @why { $failed++; say "not ok $n - %p<name> ($mode): %p<edges>[]"; say "#   $_" for @why }
            else    { say "ok $n - %p<name> ($mode): %p<edges>[]" }
        }
    }
    say "1..$n";
    say "# $pending must-report programs labeled, pending (their location kind has no hooks yet)" if $pending;
    say $failed ?? "# $failed of $n checks FAILED" !! "# all $n checks passed";
    exit $failed ?? 1 !! 0;
}
