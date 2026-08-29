unit module Gate;

# One place for the question every measuring tool in this repo has to answer
# before it measures anything: WHICH rakupp, and is it the right one for this
# machine?
#
# Six tools carried their own copy of this — perf-guard, run-optbench,
# doc-examples-diff, run-bench, run-roast and eco-sweep — in four slightly
# different forms, so a fix reached one copy and the same defect kept coming
# back. findings/GATES-3.22.md A5 named the shape once ("anything that picks a
# binary by taking the first path that exists has it"), fixed two tools, and
# left the rest; findings/TOOLS-3.23.md then found it in three more, one of them
# on a headline figure.
#
# The reason on record for NOT sharing was a note in t/run.raku — "rakupp's
# module `is export` is still flaky for many-sub modules". That was measured
# before this file was written and is no longer true: a 30-sub exporting module,
# and this one, behave identically on rakupp and Rakudo.
#
# Use it as:
#     use lib $?FILE.IO.parent.add('lib').Str;   # from anything in tools/
#     use Gate;

#| The architecture of the HOST, not of the asking process.
#|
#| `hw.optional.arm64` is 1 on Apple Silicon even when the process asking is
#| translated. `uname -m` reports the CALLER's architecture, so a tool run under
#| Rosetta would decide it was on an x86_64 machine and happily approve an
#| x86_64 binary — repeating the very bug this exists to catch. It is also why
#| CMakeLists.txt asks sysctl rather than cmake's own idea of the host.
sub host-arch(--> Str) is export {
    return '' if $*DISTRO.is-win;
    my $s = run('sysctl', '-n', 'hw.optional.arm64', :out, :err);
    my $v = $s.out.slurp(:close).trim; $s.err.slurp(:close);
    return 'arm64' if $v eq '1';
    my $u = run('uname', '-m', :out, :err);
    my $m = $u.out.slurp(:close).trim; $u.err.slurp(:close);
    $m
}

#| What a binary was built for: 'arm64', 'x86_64', or '' when it cannot be told.
#| An empty answer means "do not judge" — every caller treats it as no evidence
#| rather than as a mismatch.
sub binary-arch(Str $path --> Str) is export {
    my $f = run('file', '-b', $path, :out, :err);
    my $desc = $f.out.slurp(:close); $f.err.slurp(:close);
    $desc.contains('arm64') ?? 'arm64' !! $desc.contains('x86_64') ?? 'x86_64' !! ''
}

#| What a rakupp binary calls itself, or 'unknown'. Every figure this project
#| publishes should be able to name the build that produced it; before v3.23.0
#| none could.
sub binary-version(Str $path --> Str) is export {
    my $p = run($path, '--version', :out, :err);
    my $t = $p.out.slurp(:close); $p.err.slurp(:close);
    $t ~~ / '(rakupp)' \s+ $<v>=[\d+ ['.' \d+]+] / ?? ~$<v> !! 'unknown'
}

#| Choose the rakupp to measure, and say how the choice was made.
#|
#| An explicitly set environment variable is honoured EXACTLY as given — the
#| A/B usage every one of these tools documents depends on that, and
#| second-guessing it would make the comparison a lie. With none set, the search
#| filters by architecture rather than taking the first path that exists: on the
#| machine of record `build/` sat beside a native `build-arm64/` and held an
#| x86_64 build for two releases, so "first path that exists" measured a
#| translated binary and said nothing about it.
#|
#| Returns a Hash: path, picked-by-arch (a later candidate was preferred because
#| it matches the host), from-env, host, arch, version, and candidates.
sub pick-rakupp(IO::Path $repo, Str :$env-var = 'RAKUPP',
                :@candidates = <build/rakupp build-arm64/rakupp rakupp> --> Hash) is export {
    my $host  = host-arch();
    my @paths = @candidates.map({ $repo.add($_).Str });
    my $from-env = %*ENV{$env-var};
    my ($path, $by-arch) = $from-env, False;
    without $path {
        my @runnable = @paths.grep(*.IO.x);
        # prefer one built for THIS host; fall back to the first runnable so the
        # refusal below still explains itself instead of silently finding nothing
        my $native = $host ?? @runnable.first({ binary-arch($_) eq $host }) !! Nil;
        $by-arch = ?$native && ?@runnable && $native ne @runnable[0];
        $path = $native // @runnable[0] // @paths[0];
    }
    %(
        path            => $path,
        :picked-by-arch($by-arch),
        from-env        => $from-env.defined,
        host            => $host,
        candidates      => @paths,
        arch            => $path.IO.x ?? binary-arch($path) !! '',
        version         => $path.IO.x ?? binary-version($path) !! 'unknown',
    )
}

#| Stop unless the chosen binary exists and matches the host architecture.
#|
#| A MISSING binary must stop the run, not pass it: every kernel then "runs" in
#| under a millisecond and the tool reports an enormous speed-up and exits OK,
#| which is exactly what happened when a stale build directory was removed.
#|
#| A CROSS-ARCHITECTURE binary must stop it too. It runs perfectly well under
#| Rosetta — that is the trap — at a uniform 1.7-2x, so nothing crashes and every
#| number is simply wrong in the same direction.
#|
#| `$verdict` is the word the tool's own contract uses: the release gates say
#| INCONCLUSIVE (exit 2 is defined as "re-run", never as a pass), the reporting
#| tools say REFUSED.
sub require-native(%pick, Str :$tool!, Str :$verdict = 'REFUSED',
                   Str :$env-var = 'RAKUPP', Str :$consequence) is export {
    my $path = %pick<path>;
    unless $path.IO.x {
        note "$tool: no runnable binary at $path";
        note "Searched: {%pick<candidates>.join(', ')}";
        note "Set $env-var=/path/to/rakupp, or build one of those.";
        exit 2;
    }
    my ($bin, $host) = %pick<arch>, %pick<host>;
    if $bin && $host && $bin ne $host {
        note "$tool $verdict — $path is $bin on a $host host.";
        note $consequence // "It would run under translation, which costs a uniform 1.7-2x.";
        note "Build for this machine, or point $env-var at the $host binary.";
        exit 2;
    }
}

#| The line a tool prints to say what it is about to measure. No tool in this
#| repo printed one before v3.23.0, so a headline figure carried no record of
#| the build behind it — and `rakupp` on PATH resolves to three different
#| binaries on the machine of record.
sub provenance-line(Str $tool, %pick --> Str) is export {
    "$tool: {%pick<path>} (rakupp {%pick<version>})"
      ~ (%pick<from-env>       ?? "  ({%pick<host>} host; set explicitly)" !! '')
      ~ (%pick<picked-by-arch> ?? "  (chosen over an earlier candidate: it is the {%pick<host>} build)" !! '')
}
