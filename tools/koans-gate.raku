#!/usr/bin/env rakupp
# Run Raku Koans — an adopter's course — the way its own CI does, on a candidate.
#
#   rakupp tools/koans-gate.raku                               # native: the rakupp Gate picks
#   RAKUPP=/path/to/rakupp rakupp tools/koans-gate.raku        # native: a named binary
#   rakupp tools/koans-gate.raku --rakujs=rakujs/playground    # a Raku.js build, their harness
#
# Raku Koans (https://github.com/hankache/raku-koans, see live/ADOPTIONS.md) is
# a course of Raku koans graded in the browser by Raku.js. It pins a Raku++
# release by checksum and moves to a newer one only after its own check passes
# on it: every line holding a `___` must fail while unfilled, and every solution
# must pass. A release that breaks a koan blocks that upgrade. This runs the same
# two checks here first.
#
# Native is the default because it needs no WebAssembly build and takes about a
# minute. `--rakujs=DIR` takes a directory holding rakujs.js and rakujs.wasm,
# copies them into the checkout's public/runtime/ (git-ignored there; whatever
# was in place is restored afterwards) and runs the checkout's own
# `node scripts/check-koans.mjs` — byte for byte what the course runs when it
# moves to a new release. It needs Node 22 or newer.
#
# The checkout is RAKU_KOANS=/path, or ~/raku-koans. Pull it before a release:
# upstream adds koans, and every one of them passed on the release it pins.
#
# RED (exit 1) is a koan the candidate gets wrong. To tell a regression from a
# koan that is new upstream, run the same command against the previous release's
# binary: green there and red here is ours. Exit 2 is "could not judge" — no
# checkout, no binary, no node — never a pass.
#
# The native check is a copy of scripts/check-koans.mjs and scripts/lib/koans.mjs
# and has to follow them if they change: the prelude folded onto one line, the
# `# key: value` header lines dropped, `\n}\ndone-testing;\n` closing the block
# the prelude opens, and a learner's line read back from "at web line N" as N-1.
# It is stricter in one place: a solution must also print its plan (`1..N`), so
# an engine that runs nothing and exits 0 cannot pass.

use lib $?FILE.IO.parent.add('lib').Str;
use Gate;

my constant CAP      = 30;                      # seconds per program; the browser allows 8
my constant EPILOGUE = "\n}\ndone-testing;\n";

my $rakujs;
for @*ARGS -> $a {
    if $a ~~ / ^ '--rakujs=' (.+) $ / { $rakujs = (~$0).IO }
    else { note "koans-gate: unknown argument: $a"; exit 2 }
}

my $KOANS = (%*ENV<RAKU_KOANS> // ((%*ENV<HOME> // '.') ~ '/raku-koans')).IO;
unless $KOANS.add('koans').add('_prelude.raku').f {
    note "koans-gate: no Raku Koans checkout at $KOANS — clone it:";
    note "  git clone https://github.com/hankache/raku-koans.git $KOANS";
    note "or set RAKU_KOANS=/path/to/raku-koans.";
    exit 2;
}
my $git = run('git', '-C', $KOANS.Str, 'log', '-1', '--format=%h %cs', :out, :err);
my $REV = $git.out.slurp(:close).trim || 'unknown revision';
$git.err.slurp(:close);

exit rakujs-check($rakujs) if $rakujs;

# ---------------------------------------------------------------------------
# native
# ---------------------------------------------------------------------------

my %PICK = pick-rakupp($?FILE.IO.parent.parent);
require-native(%PICK, :tool<koans-gate>, :verdict<INCONCLUSIVE>);
# absolute: each koan runs with its own working directory, where a relative
# `RAKUPP=build/rakupp` names nothing and every koan reads as failing
my $RAKUPP = %PICK<path>.IO.absolute;

my $PRELUDE = $KOANS.add('koans').add('_prelude.raku').lines
    .map(*.trim).grep({ $_ && !.starts-with('#') }).join(' ') ~ "\n";

my @koans;
my @sections = $KOANS.add('koans').dir.grep({ .d && .basename ~~ / ^ \d+ '-' / }).sort(*.basename);
for @sections -> $sec {
    my $sid = $sec.basename.subst(/ ^ \d+ '-' /, '');
    my @files = $sec.dir.grep({
        .basename ~~ / ^ \d+ '-' .* '.raku' $ / && !.basename.ends-with('.solution.raku')
    }).sort(*.basename);
    for @files -> $f {
        my @lines = $f.slurp.split("\n");
        @lines.shift while @lines && @lines[0] ~~ / ^ '#' \s* \w+ ':' /;
        my $sol = $f.parent.add($f.basename.subst(/ '.raku' $ /, '.solution.raku'));
        @koans.push: %(
            id       => $sid ~ '/' ~ $f.basename.subst(/ ^ \d+ '-' /, '').subst(/ '.raku' $ /, ''),
            code     => @lines.join("\n").trim ~ "\n",
            solution => $sol.f ?? $sol.slurp !! Str,
        );
    }
}

say provenance-line('koans-gate', %PICK);
say "koans-gate: Raku Koans at $KOANS ($REV), {@koans.elems} koans";

# Two koans write under /tmp, which the browser keeps in memory. Natively they
# are real files: note which of the paths the koans name exist now, and remove
# the ones this run created.
my @tmp-names = @koans.map({ |(.<code>, .<solution> // '') })
    .map({ |.match(/ '/tmp/' (<-[/'"\s;,)]>+) /, :g).map({ ~.[0] }) }).unique;
my %tmp-before = @tmp-names.map({ $_ => "/tmp/$_".IO.e });

my $WORK = $*TMPDIR.add("koans-gate-{$*PID}");
$WORK.mkdir;
my $PROG = $WORK.add('koan.raku');

sub run-koan(Str $src --> Hash) {
    $PROG.spurt($src);
    my $proc = Proc::Async.new($RAKUPP, $PROG.Str);
    my ($out, $err) = '', '';
    $proc.stdout.tap(-> $c { $out ~= $c });
    $proc.stderr.tap(-> $c { $err ~= $c });
    my $done = $proc.start(:cwd($WORK.Str));
    await Promise.anyof($done, Promise.in(CAP));
    # string compare, as tools/run-roast.raku does — smartmatching the enum is unreliable
    if $done.status ne 'Kept' {
        try $proc.kill(9);
        return %( rc => -1, lines => ["(killed after {CAP} s)"] );
    }
    %( rc => $done.result.exitcode, lines => ($out ~ $err).lines.List )
}

# As in TAP: a `not ok … # TODO` is not a failure.
sub passed(%r --> Bool) {
    return False unless %r<rc> == 0;
    return False if %r<lines>.first({ .starts-with('not ok') && !($_ ~~ /:i '# TODO' >> /) });
    so %r<lines>.first({ $_ ~~ / ^ '1..' <[1..9]> / });
}

my @problems;
sub problem(%k, Str $why, @lines = ()) {
    @problems.push: "{%k<id>}: $why";
    say "✗ {%k<id>}: $why";
    say '    ' ~ .subst(/ \e '[' <[0..9;]>* 'm' /, '', :g) for @lines.head(12);
}

for @koans -> %k {
    unless %k<code>.contains('___') { problem(%k, 'koan has no ___ blank to fill in'); next }

    # every line with a blank must be reported failing while unfilled
    my %u = run-koan($PRELUDE ~ %k<code> ~ EPILOGUE);
    my %failing;
    for %u<lines>.list -> $l {
        for $l.match(/ 'at web line ' (\d+) /, :g) -> $m { %failing{+$m[0] - 1} = True }
    }
    my @code = %k<code>.split("\n");
    my @silent = (^@code).grep({ @code[$_].contains('___') && !%failing{$_ + 1} }).map(* + 1);
    problem(%k, "blank on line {@silent.join(', ')} does not fail when left unfilled", %u<lines>)
        if @silent;

    without %k<solution> { problem(%k, 'missing .solution.raku'); next }
    my %s = run-koan($PRELUDE ~ %k<solution> ~ EPILOGUE);
    unless passed(%s) { problem(%k, 'solution does not pass', %s<lines>); next }
    say "✓ {%k<id>}" unless @silent;
}

for %tmp-before.kv -> $name, $existed {
    my $p = "/tmp/$name".IO;
    rm-tree($p) if !$existed && $p.e;
}
rm-tree($WORK);

sub rm-tree(IO::Path $p) {
    if $p.d && !$p.l { rm-tree($_) for $p.dir; $p.rmdir }
    else { $p.unlink }
}

say '';
if @problems {
    say "koans-gate: RED — {@problems.elems} problem{@problems.elems == 1 ?? '' !! 's'} "
      ~ "in {@koans.elems} koans on rakupp {%PICK<version>}:";
    say "  $_" for @problems;
    exit 1;
}
say "koans-gate: GREEN — {@koans.elems} koans sound on rakupp {%PICK<version>}.";
exit 0;

# ---------------------------------------------------------------------------
# --rakujs=DIR: the course's own harness on a Raku.js build
# ---------------------------------------------------------------------------

sub rakujs-check(IO::Path $dir --> Int) {
    my @need = <rakujs.js rakujs.wasm>;
    for @need -> $n {
        next if $dir.add($n).f;
        note "koans-gate: no $n in $dir — build Raku.js first (rakujs/build.sh).";
        return 2;
    }
    my $nv = try run('node', '--version', :out, :err);
    my $node = $nv ?? $nv.out.slurp(:close).trim !! '';
    $nv.err.slurp(:close) if $nv;
    unless $node ~~ / ^ 'v' (\d+) / && +$0 >= 22 {
        note "koans-gate: --rakujs needs Node 22 or newer on PATH (found: {$node || 'none'}).";
        return 2;
    }

    my $rt = $KOANS.add('public').add('runtime');
    my %saved = @need.map({ $_ => ($rt.add($_).f ?? $rt.add($_).slurp(:bin) !! Buf) });
    LEAVE {
        for @need -> $n {
            with %saved{$n} { $rt.add($n).spurt($_) }
            else            { $rt.add($n).unlink if $rt.add($n).e }
        }
    }
    $rt.add($_).spurt($dir.add($_).slurp(:bin)) for @need;

    say "koans-gate: Raku.js from $dir, node $node";
    say "koans-gate: Raku Koans at $KOANS ($REV), through scripts/check-koans.mjs";
    my $p = run('node', 'scripts/check-koans.mjs', :cwd($KOANS.Str), :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    my $rc  = $p.exitcode;
    .say for $out.lines.grep({ !.starts-with('✓ ') });
    note $err if $err.trim;
    say '';
    if $rc != 0 {
        say "koans-gate: RED — the course's own check exits $rc on this Raku.js.";
        return 1;
    }
    say "koans-gate: GREEN — the course's own check passes on this Raku.js.";
    0
}
