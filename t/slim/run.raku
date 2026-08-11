# The SLIM negative suite (docs/dev/plans/SLIM-PLAN.md, P3 + P4 gates).
# Proves the cut side of --slim: that every forced-out feature THROWS a
# catchable, named X::Feature::NotBuilt instead of crashing or quietly
# misbehaving, that the grammar's conflicts are loud errors naming the
# alternatives, that the embedded manifest round-trips through --exe-info,
# that the size budgets hold (-all ≤ 5.0 MB, bare --slim ≤ 5.5 MB on hello) —
# and, since P4, that the SCAN decides right: cuts what a program provably
# does not use, keeps what it does (uniname calls, script assertions), keeps
# EVERYTHING when a force-full trigger fires (and says so), and that max
# trusts static evidence while auto does not.
#
# Run:  build/rakupp t/slim/run.raku     (any build of the rakupp under test)
#
# Compiles ~17 small binaries via --exe/--aot/--bundle, so it needs a C++
# compiler — which is why it lives here and not in t/regression/: that suite
# must stay runnable on a machine with none. Wired into CI next to the embed
# smoke; run it by hand after touching --slim, the stubs, the scan, or the
# archive set. The other half of the P4 gate is tools/slim-diff.raku — the
# behaviour differential over the whole corpus.

my $tmp = $*TMPDIR.add("rakupp-slim-{$*PID}");
$tmp.mkdir;
my $errors = 0;
my @made;                       # every file we create, for cleanup

sub check(Bool $ok, $desc, $detail = '') {
    if $ok {
        say "ok - $desc";
    }
    else {
        $errors++;
        say "NOT OK - $desc";
        say $detail.indent(4) if $detail;
    }
}

sub probe($name, $code) {
    my $f = $tmp.add($name);
    $f.spurt($code);
    @made.push($f);
    $f.Str
}

# Compile SOURCE with the given extra flags; return (exitcode, stderr) and
# remember the output for cleanup.
sub compile($src, $out, *@flags) {
    my $o = $tmp.add($out);
    @made.push($o);
    my $p = run $*EXECUTABLE, |@flags, $src, '-o', $o.Str, :out, :err;
    ($p.exitcode, $p.err.slurp(:close) ~ $p.out.slurp(:close), $o.Str)
}

sub run-bin($bin) {
    my $p = run $bin, :out, :err;
    ($p.exitcode, $p.out.slurp(:close), $p.err.slurp(:close))
}

my $hello = probe('hello.raku', q{say 'Hello';});

# ---- 1. the grammar: every wrong ask is a loud error naming what exists ----

for ('safe,none',     'at most one level'),
    ('-eval,+eval',   'both +eval and -eval'),
    ('-bogus',        'Unknown --slim feature'),
    ('none,-eval',    'conflict, not a refinement'),
    ('why:',          'takes a feature name')      # why: with no feature
-> ($spec, $expect) {
    my $p = run $*EXECUTABLE, '--exe', $hello, "--slim=$spec", :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    check $p.exitcode == 4 && $err.contains($expect),
          "--slim=$spec is refused, naming the problem",
          "exit {$p.exitcode}: $err";
}

# ---- 2. nothing cut: all four features work in one compiled binary ---------

my $all4 = probe('all4.raku',
    q{print 'A'.uniname, '|', ('a' unicmp 'b'), '|', 'A'.uniprop('Script'), '|', EVAL '40+2';});
{
    my ($rc, $log, $bin) = compile($all4, 'all4', '--exe');
    check $rc == 0, 'uncut probe compiles', $log;
    my ($xc, $out, $) = run-bin($bin);
    check $xc == 0 && $out eq 'LATIN CAPITAL LETTER A|Less|Latin|42',
          'uniname, unicmp, uniprop(Script) and EVAL all work uncut', $out;
}

# ---- 3. each cut feature throws X::Feature::NotBuilt, naming itself --------

my %probes =
    'unicode-names'     => probe('names.raku', q{print 'A'.uniname;}),
    'unicode-collation' => probe('coll.raku',  q{print 'a' unicmp 'b';}),
    'unicode-props'     => probe('props.raku', q{print 'A'.uniprop('Script');});

for %probes.sort -> (:key($feat), :value($src)) {
    my ($rc, $log, $bin) = compile($src, "cut-$feat", '--exe', "--slim=-$feat");
    check $rc == 0, "-$feat compiles against the stub archive", $log;
    my ($xc, $, $err) = run-bin($bin);
    check $xc == 1 && $err.contains("needs the '$feat' feature")
                   && $err.contains('--slim'),
          "the cut $feat throws, naming the feature and the rebuild flag",
          "exit $xc: $err";
}

# …and the throw is TYPED: `when X::Feature::NotBuilt` catches it.
my $catch = probe('catch.raku', q:to/END/);
    {
        EVAL '1+1';
        print 'no-throw';
        CATCH {
            when X::Feature::NotBuilt {
                print 'caught|', .message.contains('eval') ?? 'names-eval' !! 'NO-NAME';
            }
        }
    }
    END
{
    my ($rc, $log, $bin) = compile($catch, 'catch-eval', '--exe', '--slim=-eval');
    check $rc == 0, '-eval compiles against the stub archive', $log;
    my ($xc, $out, $) = run-bin($bin);
    check $xc == 0 && $out eq 'caught|names-eval',
          'when X::Feature::NotBuilt catches a cut feature, message names it', $out;
}

# ---- 4. -all: the size budget, and the manifest round-trip -----------------

{
    my ($rc, $log, $bin) = compile($hello, 'hello-all', '--exe', '--slim=-all');
    check $rc == 0, '--slim=-all hello compiles', $log;
    my ($xc, $out, $) = run-bin($bin);
    check $xc == 0 && $out.trim eq 'Hello', '--slim=-all hello runs', $out;
    my $size = $bin.IO.s;
    check $size <= 5 * 1024 * 1024,
          "--slim=-all hello is within the 5.0 MB budget ($size bytes)";
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $info.exitcode == 0
          && $line.contains('"mode":"native"')
          && $line.contains('"unicode-names"') && $line.contains('"unicode-collation"')
          && $line.contains('"unicode-props"') && $line.contains('"eval"'),
          '--exe-info reads back the manifest with all four cuts', $line;
}

# …a binary with nothing cut still carries a (empty-cut) manifest…
{
    my ($rc, $log, $bin) = compile($hello, 'hello-def', '--exe');
    check $rc == 0, 'default hello compiles', $log;
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $info.exitcode == 0 && $line.contains('"cut":[]'),
          'an uncut binary carries a manifest with an empty cut list', $line;
}

# …and a binary that never had one says so instead of inventing one.
{
    my $info = run $*EXECUTABLE, '--exe-info', $*EXECUTABLE.Str, :out, :err;
    $info.out.slurp(:close);
    my $err = $info.err.slurp(:close);
    check $info.exitcode == 1 && $err.contains('no build manifest'),
          'rakupp itself has no manifest, and --exe-info says so', $err;
}

# ---- 5. precedence: a named feature beats the `all` group ------------------

{
    my ($rc, $log, $bin) = compile(%probes<unicode-names>, 'prec-names',
                                   '--exe', '--slim=-all,+unicode-names');
    check $rc == 0, '-all,+unicode-names compiles', $log;
    my ($xc, $out, $) = run-bin($bin);
    check $xc == 0 && $out eq 'LATIN CAPITAL LETTER A',
          '+unicode-names survives -all (named feature beats the group)', $out;
}
{
    my $evalsrc = probe('eval.raku', q{print EVAL '40+2';});
    my ($rc, $log, $bin) = compile($evalsrc, 'prec-eval',
                                   '--exe', '--slim=-all,+unicode-names');
    check $rc == 0, 'the sibling probe compiles', $log;
    my ($xc, $, $err) = run-bin($bin);
    check $xc == 1 && $err.contains("needs the 'eval' feature"),
          "…while eval stays cut in the same spec", $err;
}

# ---- 6. the scan (P4): auto cuts what is provably unused ------------------

{
    my ($rc, $log, $bin) = compile($hello, 'hello-auto', '--exe', '--slim');
    check $rc == 0, 'bare --slim (= auto) compiles', $log;
    my ($xc, $out, $) = run-bin($bin);
    check $xc == 0 && $out.trim eq 'Hello', '--slim hello runs', $out;
    check $bin.IO.s <= 5.5 * 1024 * 1024,
          "--slim hello is within the 5.5 MB budget ({$bin.IO.s} bytes)";
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $line.contains('"slim":"auto"')
          && <unicode-names unicode-collation unicode-props eval>.map({ $line.contains("\"$_\"") }).all.so,
          'the scan cut all four features from hello', $line;
}
{
    my ($rc, $log, $bin) = compile(%probes<unicode-names>, 'names-auto', '--exe', '--slim');
    check $rc == 0, '--slim on a uniname program compiles', $log;
    my ($xc, $out, $) = run-bin($bin);
    check $xc == 0 && $out eq 'LATIN CAPITAL LETTER A',
          'the scan KEEPS unicode-names where uniname is used', $out;
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check !$line.contains('"unicode-names"') && $line.contains('"eval"'),
          '…while still cutting what the program does not use', $line;
}
{
    my $evalprog = probe('eval-auto.raku', q{print EVAL '40+2';});
    my $p = run $*EXECUTABLE, '--exe', $evalprog, '--slim',
                '-o', $tmp.add('eval-auto').Str, :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    @made.push($tmp.add('eval-auto'));
    check $p.exitcode == 0 && $err.contains('keeping every feature') && $err.contains('EVAL'),
          'auto + EVAL: the trigger keeps everything and says so', $err;
    my $info = run $*EXECUTABLE, '--exe-info', $tmp.add('eval-auto').Str, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $line.contains('"cut":[]'), '…and the manifest shows nothing cut', $line;
    my ($xc, $out, $) = run-bin($tmp.add('eval-auto').Str);
    check $xc == 0 && $out eq '42', '…and the binary works', $out;
}
{
    my $evalprog = probe('eval-max.raku', q{print EVAL '40+2';});
    my ($rc, $log, $bin) = compile($evalprog, 'eval-max-bin', '--exe', '--slim=max');
    check $rc == 0, '--slim=max compiles the same program', $log;
    my ($xc, $out, $) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq '42'
          && !$line.contains('"eval"') && $line.contains('"unicode-names"'),
          'max keeps eval (static use) and still cuts the unused Unicode tables', "$out / $line";
}
{
    my $scr = probe('rx-script.raku', q{print 'Я' ~~ /<:Cyrillic>/ ?? 'M' !! 'N';});
    my ($rc, $log, $bin) = compile($scr, 'rx-script-bin', '--exe', '--slim');
    check $rc == 0, 'a <:Cyrillic> regex compiles under --slim', $log;
    my ($xc, $out, $) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq 'M' && !$line.contains('"unicode-props"'),
          'the scan keeps unicode-props for a script assertion — and the match works', "$out / $line";
}
{
    my $cat = probe('rx-cat.raku', q{print 'A5' ~~ /<:Lu><:Nd>/ ?? 'M' !! 'N';});
    my ($rc, $log, $bin) = compile($cat, 'rx-cat-bin', '--exe', '--slim');
    check $rc == 0, 'a category-only regex compiles under --slim', $log;
    my ($xc, $out, $) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq 'M' && $line.contains('"unicode-props"'),
          '<:Lu>/<:Nd> resolve from never-cut tables, so props still cuts — and matches', "$out / $line";
}

# ---- 6a2. builtin wrappers around dynamic loading are triggers --------------
# use-ok is a require in sub's clothing (the builtin Test module) — found by
# the battery leg of the differential when a slim'd 01-load.t threw where the
# full build passed. The scan must keep everything for it, like for require.

{
    my $useok = probe('useok.raku', q{use Test; plan 1; use-ok 'NoSuchModuleHere0'; done-testing;});
    my $p = run $*EXECUTABLE, '--exe', $useok, '--slim',
                '-o', $tmp.add('useok-bin').Str, :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    @made.push($tmp.add('useok-bin'));
    check $p.exitcode == 0 && $err.contains('use-ok'),
          'use-ok is a force-full trigger, named on stderr', $err;
    my $info = run $*EXECUTABLE, '--exe-info', $tmp.add('useok-bin').Str, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $line.contains('"cut":[]'), '…and nothing is cut', $line;
}

# ---- 6b. verify (P5): the proof-or-nothing directive -----------------------

{
    my $p = run $*EXECUTABLE, '--exe', %probes<unicode-names>,
                '--slim=safe,-unicode-names,verify', '-o', $tmp.add('vf').Str, :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    check $p.exitcode == 6 && $err.contains('DISAGREE'),
          'verify refuses to emit a binary whose cut changes behaviour', $err;
    check !$tmp.add('vf').e, '…and nothing is left behind';
}

# ---- 7. the other pipelines: AOT takes cuts, bundling refuses -eval --------

{
    my ($rc, $log, $bin) = compile($hello, 'hello-aot', '--aot', '--slim=-all');
    check $rc == 0, '--aot --slim=-all compiles (the stub surface holds for AOT)', $log;
    my ($xc, $out, $) = run-bin($bin);
    check $xc == 0 && $out.trim eq 'Hello', '…and the AOT binary runs', $out;
}
{
    my $p = run $*EXECUTABLE, '--bundle', $hello, '--slim=-eval',
                '-o', $tmp.add('hb').Str, :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    check $p.exitcode == 5 && $err.contains('incompatible with bundling'),
          '--bundle --slim=-eval is refused (a bundled binary IS the parser)', $err;
    check !$tmp.add('hb').e, '…and no half-made binary is left behind';
}

# ---- cleanup ---------------------------------------------------------------

.IO.unlink for @made;
$tmp.rmdir;

if $errors == 0 {
    say 'ALL SLIM CHECKS PASSED';
    exit 0;
}
else {
    say "$errors SLIM CHECK(S) FAILED";
    exit 1;
}
