# The SLIM negative suite (docs/dev/plans/SLIM-PLAN.md, P3 + P4 gates).
# Proves the cut side of --slim: that every forced-out feature THROWS a
# catchable, named X::Feature::NotBuilt instead of crashing or quietly
# misbehaving, that the grammar's conflicts are loud errors naming the
# alternatives, that the embedded manifest round-trips through --exe-info,
# that the size budgets hold (-all ≤ 12.5 MB, bare --slim ≤ 12.75 MB on hello) —
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
# Sizes cross-checked between blocks: the ABSOLUTE budgets are the plan's
# darwin-arm64 measurements (SLIM-PLAN's header pins the machine), so they
# gate on darwin only — the first CI run on Linux showed the same cut on a
# bigger ELF baseline (5,937,336 where darwin is 4,856,936). What holds on
# EVERY platform is the mechanism: cutting all four features must remove
# megabytes, asserted below as full − slim ≥ 2 MB against this run's own
# full build.
#
# Re-pinned 2026-08: uncuttable core growth (the regex-alias/comb(:match)
# correctness fix, then the JSON::Fast native codec — Mach-O grows in
# 16,512-byte page steps) took -all hello from the plan's 4,856,936 to
# 5,794,456, ten pages past the original 5.5 MB line. The -all budget is
# now 5.75 MB (≈230 KB of headroom); bare --slim stays at 6.0 MB.
#
# Re-pinned 2026-08-27: five days of engine work ate that headroom — the
# TAP-install batch (seven fixes), nqp::stat, the subset-return/dispatch
# band — leaving CI's hello-all at 6,030,328, a thousand bytes past the
# line. All of it is reachable runtime code (the archive split keeps the
# REPL, MCP server and Jupyter kernel out of --exe binaries; nm shows no
# CLI-only weight left to carve), so the line moves: -all is now 6.25 MB,
# bare --slim 6.5 MB. The margin beyond CI's need is cross-arch slack:
# the same tree's hello-all from the dev box's default-arch build/ (which
# configures to x86_64 there) is 6,501,232 — 471 KB fatter than CI's arm64
# — and this darwin gate must hold on both slices.
#
# Re-pinned 2026-09-04: the issue #67 tracer (397bbe8, 0142f38) — every
# uncaught error, warn, fail and await now carries the frames it came
# from — added 67,504 bytes of reachable runtime code, putting CI's
# hello-all at 6,575,736, four pages past the line. The three
# --target=js commits in the same window cost nothing here: Js.cpp and
# JsRuntimeSrc.cpp are out of rakupp_rt, so no --exe binary carries the
# backend or its 200 KB runtime blob (nm on the emitted binary finds no
# REPL, JS, MCP or Jupyter symbol either). So the line moves again, and
# this time it is the x86_64 slice that sets it: the same tree's
# hello-all is 6,557,736 from the dev box's arm64 build/ and 7,069,936
# from build-x64/ — a 512,200-byte spread, and the wider slice was
# already past BOTH old budgets while CI (arm64 only) saw just the -all
# one fail. -all is now 7.0 MB (≈264 KB over the x86_64 measurement),
# bare --slim 7.25 MB.
#
# Re-pinned 2026-09-18, and this time deliberately loose. CI's arm64
# hello-all went 7,334,376 → 7,350,920 across 2e994990 (~19 one-assertion
# Roast losses): one 16,544-byte Mach-O page step, against a line that had
# 5,656 bytes of headroom left. That single step turned macos-universal red
# on three pushes running, two of them documentation-only — the engine
# commit rode in with a README. The growth is reachable: Interpreter, Lexer,
# Parser and MethodCall*, with nothing CLI-only to carve, since CMakeLists
# still holds main.cpp, Repl.cpp, Js.cpp and JsRuntimeSrc.cpp out of
# rakupp_rt. Four re-pins in six weeks is a gate tracking ordinary engine
# growth one page at a time, so the new line carries a megabyte of headroom
# rather than a page: -all 8.0 MB, bare --slim 8.25 MB. The x86_64 figure
# is DERIVED here, not measured — arm64's 7,350,920 plus the 512,200-byte
# spread recorded above is ≈7,863,000, which 8.0 MB clears by ≈525 KB; no
# dev-box build was current enough to measure against. What actually gates
# the cut is relative and cannot go stale: full − slim ≥ 2 MB, asserted
# below against this run's own full build. These absolutes only catch a
# runaway.
#
# Re-pinned 2026-09-22, and this one IS the runaway the absolutes are for.
# CI's arm64 hello-all went 7,350,920 (the 2026-09-18 re-pin, which left a
# megabyte of headroom) to 8,300,504 two days later: +949,584 bytes, 57 page
# steps, not one. The cause is structural rather than incremental. The tier-up
# JIT (09ed608c, 4cbe0208) calls emitJitKernel from the RUNTIME, so Codegen.cpp
# had to join rakupp_rt — and it is the biggest object in the tree: 663,096
# bytes of C++ backend, plus Jit.cpp's 141,784, against a jump of ~950 KB.
# Every --exe binary now carries the C++ code generator, `--slim=-all hello`
# included, because a hot loop in ANY compiled program may tier up.
#
# So this was not the four earlier re-pins' story of ordinary growth with
# nothing left to carve, and it was NOT absorbed. There was something to carve:
# the call was reachable by the LINKER and by nothing else. `--jit` is parsed in
# main.cpp, and the only JIT a generated binary can switch on is copy-and-patch,
# which lowers from the AST and emits no C++ at all — so emitJitKernel could
# never run in an --exe binary, it was merely named there. It goes through
# jit::g_emitKernel now, a pointer the CLI installs, and Codegen.cpp joins
# Repl.cpp and the JS backend outside rakupp_rt. Measured on this box:
# --slim=-all hello 8,552,232 -> 8,254,168, and a full --exe hello
# 12,384,712 -> 12,086,616, both 298 KB lighter. (Less than Codegen.cpp.o's
# 663,096, because the linker was already dead-stripping about half of what
# the object carried.) nm finds no emitJitKernel or transpileToCpp in a
# compiled binary now, and 77 of them in the CLI.
#
# So the line comes back down, to 8.75 MB and 9.0 MB. It does not go back to
# the 8.0 MB it was at before the JIT: 8.0 would clear this box's 8,254,168 by
# only 134 KB, and the arm64-x86_64 spread recorded above (512,200) puts the
# wider slice near 8,766,000, which 8.0 does not clear at all. 8.75 clears the
# wider slice by ~200 KB and this box by ~921 KB, which is a gate with teeth
# rather than the 1.25 MB of slack the emergency line left. The relative gate
# below is unchanged and is still the one that cannot go stale.
#
# Re-pinned 2026-09-27, to 11.0 MB and 11.25 MB. CI's arm64 hello-all was
# 8,655,992 on the last green run (e65bdc54, 94 KB under 8.75) and 10,223,176
# on the first run of the red streak that reached this suite (44dfbbf7); this
# box measures 10,342,488 at a0d499cc. That is the Roast campaign: ~22,000
# lines of engine across three days, 10,500 of them in Interpreter.cpp. It is
# growth, not a leak. The full --exe hello grew alongside it (CI's
# 12,854,520 then, this box's 14,822,936 now), the cut still removes 4.48 MB,
# and nm finds no Repl, Codegen, JS, MCP or Jupyter symbol in the slim
# binary: there was nothing to carve. The x86_64 figure is DERIVED again
# (build-x64/ is 23 days stale): the spread is 7.8% of the binary, so
# ≈11,150,000, which 11.0 MB clears by ~380 KB and this box by ~1.19 MB.
#
# Re-pinned 2026-10-01, to 12.5 MB and 12.75 MB. CI's arm64 hello-all crept
# from 11,198,520 (e7be1d17, 09-29) to 11,458,136 on the last green run
# (63ca2339), then 11,574,440 at c494f3ab — 40,104 bytes past the line. The
# last step, +116 KB, is the --cnp `for @array` tier-up (Jit.cpp, Cnp.cpp,
# the stencils), the blockAtArgs scan in InterpreterCalls/Core, and the
# realloc branch in ValueVec.h, which every RVec instantiation of
# growAndBuild and dealloc now carries. Growth, not a leak: nm finds no
# Repl, Codegen, JS, MCP or Jupyter symbol in the slim binary. The x86_64
# figure is DERIVED again (7.8% spread): ≈12,480,000, which 12.5 MB clears
# by only ~20 KB — this line is a stopgap. Five re-pins upward say the cut
# list itself needs re-asking (what does --slim actually remove now?), and
# that round is what should bring the number down, not another pin.
#
# Not re-pinned 2026-10-17: the cut list was re-asked instead. CI's arm64
# hello-all went 13,092,072 on the last green run (6724350f) to 13,177,576 at
# 5c323658, 70,376 bytes past the line, by the same steady growth (full and
# slim rising together since 10-01, full − slim flat near 4.7 MB). A linker
# map of the slim binary found one table file --slim had never cut:
# unicode_props2_gen.cpp, the enumerated properties (Age, Line_Break,
# East_Asian_Width, …), read only by uniprop naming them and <:Prop<Value>>.
# SLIM-PLAN had filed it with the string-operation tables by its name. It
# rides unicode-props now (its Bidi_Mirroring_Glyph map stays behind, in
# unicode_mirror_gen.cpp, for the Lexer), and this box's arm64 hello-all went
# 13,179,960 → 12,862,456, 244,744 bytes under the line. The rest of the map
# is the interpreter: InterpreterCore, MethodCall*, Builtins*, Regex, nothing
# CLI-only. The x86_64 slice, derived at the 7.8% spread, is ≈13,866,000 and
# over this line, as it already was before the move; CI measures arm64 only.
#
# Later the same day the question went one level down, to the largest single
# symbols in that map: ucd::BINPROPS, the binary properties (219,640 bytes),
# was filed with the string-operation tables but is read only by uniprop,
# unimatch and a <:Prop> naming one (the regex classes answer from the
# general category first). It rides unicode-props now, with ID_Continue and
# Unified_Ideograph copied out for the parser and collation, and this box's
# arm64 hello-all went 12,879,880 → 12,665,256; the builtin Raku a binary
# without `eval` now carries pre-parsed puts back 912 bytes (12,666,168).
my $full-size;
my $all-size;

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
    $all-size = $bin.IO.s;
    check $*KERNEL.name ne 'darwin' || $all-size <= 12.5 * 1024 * 1024,
          "--slim=-all hello is within the 12.5 MB darwin budget ($all-size bytes; darwin-only gate)";
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
    $full-size = $bin.IO.s;
    check $full-size - $all-size >= 2 * 1024 * 1024,
          "cutting all four features removes >= 2 MB on this platform "
          ~ "($full-size full vs $all-size slim, delta {$full-size - $all-size})";
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
    check $*KERNEL.name ne 'darwin' || $bin.IO.s <= 12.75 * 1024 * 1024,
          "--slim hello is within the 12.75 MB darwin budget ({$bin.IO.s} bytes; darwin-only gate)";
    check $full-size - $bin.IO.s >= 2 * 1024 * 1024,
          "bare --slim removes >= 2 MB from hello on this platform "
          ~ "(delta {$full-size - $bin.IO.s})";
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
{
    # General_Category's long names are spelled in Unicode.cpp, never cut
    my $gcl = probe('rx-gc-long.raku',
        q{print 'A' ~~ /<:gc<Uppercase_Letter>>/ && 'a' !~~ /<:Uppercase_Letter>/ ?? 'M' !! 'N';});
    my ($rc, $log, $bin) = compile($gcl, 'rx-gc-long-bin', '--exe', '--slim');
    check $rc == 0, 'a long-name category regex compiles under --slim', $log;
    my ($xc, $out, $) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq 'M' && $line.contains('"unicode-props"'),
          '<:gc<Uppercase_Letter>>/<:Uppercase_Letter> need no cut table, so props still cuts — and matches', "$out / $line";
}
# The enumerated properties (unicode_props2_gen.cpp) are cut with the rest of
# unicode-props, and two names reach them that the regex rules read as a
# category letter: the value in <:Hangul_Syllable_Type<L>>, and .uniprop('W'),
# which uniprop answers as East_Asian_Width. Each alone must keep the feature.
# A uniprop CALL is judged by its property argument (since 2026-10-17; before,
# every call kept props and names like a bare &uniprop). The binary properties
# (unicode_props_gen.cpp) ride unicode-props too, and uniprop resolves a name
# its own way: .uniprop('Lowercase') is the binary property, where the regex
# <:Lowercase> is the category Ll.
for ('up-w',    q{print 'a'.uniprop('W');},                                         'Na',
                '.uniprop(\'W\') (East_Asian_Width by a value name)'),
    ('up-math', q{print '∑'.uniprop('Math');},                                     'True',
                ".uniprop('Math') (a binary property)"),
    ('up-lower', q{print 'a'.uniprop("Lowercase");},                               'True',
                '.uniprop("Lowercase") (the binary property, not the category)'),
    ('rx-math', q{print '∑' ~~ /<:Math>/ ?? 'M' !! 'N';},                          'M',
                'a <:Math> regex (a binary property)'),
    ('rx-hst',  q{print "\x[1100]" ~~ /<:Hangul_Syllable_Type<L>>/ ?? 'M' !! 'N';}, 'M',
                'a <:Hangul_Syllable_Type<L>> regex'),
    # …and an alias the never-cut tables do not spell: <:ccc<A>> is 230,
    # found through the PropertyValueAliases table, which rides the cut
    ('rx-ccc',  q{print "\x[301]" ~~ /<:ccc<A>>/ ?? 'M' !! 'N';},                  'M',
                'a <:ccc<A>> regex (an alias from the cut alias table)')
-> ($name, $code, $want, $what) {
    my $src = probe("{$name}.raku", $code);
    my ($rc, $log, $bin) = compile($src, "{$name}-bin", '--exe', '--slim');
    check $rc == 0, "$what compiles under --slim", $log;
    my ($xc, $out, $err) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq $want && !$line.contains('"unicode-props"'),
          "the scan keeps unicode-props for $what — and it answers $want",
          "exit $xc: $out / $err / $line";
}
# …and with the feature cut by hand, an enumerated property throws, while the
# uniprop names that resolve from never-cut tables (the default
# General_Category, a case mapping, a category value) pass the
# enumerated-property lookup without touching it.
{
    my $src = probe('cut-enum.raku', q{print 'a'.uniprop('Line_Break');});
    my ($rc, $log, $bin) = compile($src, 'cut-enum-bin', '--exe', '--slim=-unicode-props');
    check $rc == 0, '-unicode-props compiles a Line_Break lookup against the stub archive', $log;
    my ($xc, $, $err) = run-bin($bin);
    check $xc == 1 && $err.contains("needs the 'unicode-props' feature")
                   && $err.contains('Line_Break') && $err.contains('--slim'),
          'the cut unicode-props covers the enumerated properties, and says so',
          "exit $xc: $err";
}
{
    my $src = probe('cut-enum-safe.raku',
        q{print 'a'.uniprop('Lu'), '|', 'a'.uniprop, '|', 'a'.uniprop('Uppercase_Mapping');});
    my ($rc, $log, $bin) = compile($src, 'cut-enum-safe-bin', '--exe', '--slim=-unicode-props');
    check $rc == 0, '-unicode-props compiles never-cut uniprop lookups', $log;
    my ($xc, $out, $err) = run-bin($bin);
    check $xc == 0 && $out eq 'Ll|Ll|A',
          '…and they answer without touching the cut tables', "exit $xc: $out / $err";
}
{
    my $src = probe('cut-binprop.raku', q{print '∑'.uniprop('Math');});
    my ($rc, $log, $bin) = compile($src, 'cut-binprop-bin', '--exe', '--slim=-unicode-props');
    check $rc == 0, '-unicode-props compiles a binary-property lookup against the stub archive', $log;
    my ($xc, $, $err) = run-bin($bin);
    check $xc == 1 && $err.contains("needs the 'unicode-props' feature") && $err.contains('binary'),
          'the cut unicode-props covers the binary properties, and says so', "exit $xc: $err";
}
# Two binary properties are asked by NAME from never-cut code, and read rt's
# own copy (unicode_props_core_gen.cpp): ID_Continue, which the parser asks of
# a non-ASCII character after an interpolated variable, and Unified_Ideograph,
# which collation asks of a Han character. Neither EVAL nor unicmp may need
# the props feature for them; the ordinary regex classes answer from the
# general category before any binary property.
{
    my $src = probe('cut-props-core.raku',
        q{print EVAL('my $v = 1; my $vкнига = 2; "$vкнига|$v…"'), '|', ('b' unicmp '中'), '|',
               <中 b 丁>.collate.join, '|', ('Ab1' ~~ /^\w+$/ && 'a' ~~ /<alpha>/ ?? 'M' !! 'N');});
    my ($rc, $log, $bin) = compile($src, 'cut-props-core-bin', '--exe', '--slim=-unicode-props');
    check $rc == 0, '-unicode-props compiles EVAL interpolation, Han collation and \\w', $log;
    my ($xc, $out, $err) = run-bin($bin);
    check $xc == 0 && $out eq '2|1…|Less|b丁中|M',
          '…and none of them needs the cut binary-property table', "exit $xc: $out / $err";
}
# A double-quoted property name is an InterpStr of one literal, still known at
# compile time: the scan keeps props for Script and cuts names.
{
    my $src = probe('up-script-dq.raku', q{print 'A'.uniprop("Script");});
    my ($rc, $log, $bin) = compile($src, 'up-script-dq-bin', '--exe', '--slim');
    check $rc == 0, '.uniprop("Script") compiles under --slim', $log;
    my ($xc, $out, $err) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq 'Latin' && !$line.contains('"unicode-props"')
                   && $line.contains('"unicode-names"'),
          'a literal uniprop property keeps only the group it names', "exit $xc: $out / $err / $line";
}
# Numifying a lone fraction character reads a never-cut copy of those rows
# (unicode_gen.cpp's FRAC), not NUMV. While it read NUMV through the quiet
# probe, a binary with unicode-names cut (by the scan: nothing here names it)
# called "½" not a number at all: a silently different answer, not a throw.
{
    my $src = probe('frac.raku',
        q{print "½".Numeric + 0, '|', "⅓" + 1, '|', "↉".Numeric + 0, '|', "Ⅻ".Numeric.defined;});
    my ($rc, $log, $bin) = compile($src, 'frac-bin', '--exe', '--slim');
    check $rc == 0, 'fraction numification compiles under --slim', $log;
    my ($xc, $out, $err) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq '0.5|1.333333|0|False' && $line.contains('"unicode-names"'),
          'a lone fraction numifies with unicode-names cut', "exit $xc: $out / $err / $line";
}
# nqp::getuniprop_* names its property by a NUMBER and reads it through
# .uniprop, so the scan cannot see which: both groups behind uniprop stay.
# Asked of --slim=list, because a native binary has no nqp property ops.
{
    my $src = probe('nqp-uniprop.raku',
        q{use nqp; print nqp::getuniprop_str(0x4E2D, nqp::unipropcode('East_Asian_Width'));});
    my $p = run $*EXECUTABLE, '--exe', $src, '--slim=list', :out, :err;
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    check so($out ~~ /'unicode-props' \s+ 'keep'/ && $out ~~ /'unicode-names' \s+ 'keep'/),
          'the scan keeps unicode-props and unicode-names for an nqp property read', $out;
}

# ---- 6a1. a module the engine answers is not a disk load (issue #166) -------
# `use Data::Native` and its siblings are answered from the binary's own
# builtins: there is no code for the scan to see, and in a compiled binary the
# engine's answer is final, so --slim cuts all four. The json codecs' fallback
# (JSON::Native::Raku, for what the codec does not cover) travels embedded, so
# that call answers the same slim and full, with no module on any search path.
{
    my $src = probe('dn-zlib.raku', q{use Data::Native :zlib; print crc32(Blob.new(1, 2, 3));});
    my ($rc, $log, $bin) = compile($src, 'dn-zlib-bin', '--exe', '--slim');
    check $rc == 0 && !$log.contains('keeping every feature'),
          'use Data::Native :zlib is no dynamic construct under --slim', $log;
    my ($xc, $out, $err) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq '1438416925'
          && <unicode-names unicode-collation unicode-props eval>.map({ $line.contains("\"$_\"") }).all.so,
          '…all four features are cut, and crc32 answers', "exit $xc: $out / $err / $line";
}
{
    my $src = probe('jn-fallback.raku', q:to/END/);
        use JSON::Native;
        class P { has $.x = 1 }
        print to-json([1, {a => 2}], :!pretty), '|';
        print (try to-json(P.new)) // $!.message;
        END
    my ($rc, $log, $bin) = compile($src, 'jn-fallback-bin', '--exe', '--slim');
    check $rc == 0 && $log.contains('JSON::Native::Raku'),
          'use JSON::Native embeds the codec fallback', $log;
    my $p = run $bin, :out, :err, :cwd($tmp.Str), :env(%(PATH => '/usr/bin:/bin', HOME => $tmp.Str));
    my ($out, $err) = $p.out.slurp(:close), $p.err.slurp(:close);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $p.exitcode == 0 && $out eq '[1,{"a":2}]|Don\'t know how to jsonify P' && $line.contains('"eval"'),
          '…and an uncovered to-json answers from it, parser cut, nothing on disk',
          "exit {$p.exitcode}: $out / $err / $line";
}

# ---- 6a2. the builtin Raku travels pre-parsed when eval is cut ---------------
# The runtime keeps a few classes and combinators as Raku source, compiled on
# first use (BuiltinRakuSrc.h). A binary without a parser carries their ASTs
# instead, so none of these may need the eval feature the scan proved unused.
{
    my $src = probe('builtin-raku.raku', q:to/END/);
        my $s = supply { emit 1; emit 2 };
        $s.map(* * 2).tap({ print $_, ' ' });
        $s.grep(* > 1).tap({ print $_, ' ' });
        my $c = Channel.new; $c.send(5); $c.close;
        react { whenever $c.Supply { print $_, ' ' } }
        react { whenever supply { emit 3 }.stable(0.01) { print $_, ' ' } }
        print IO::Notification::Change.new(path => 'p', event => FileChanged).path;
        END
    my ($rc, $log, $bin) = compile($src, 'builtin-raku-bin', '--exe', '--slim');
    check $rc == 0, 'Supply combinators, Channel.Supply and IO::Notification compile under --slim', $log;
    my ($xc, $out, $err) = run-bin($bin);
    my $info = run $*EXECUTABLE, '--exe-info', $bin, :out, :err;
    my $line = $info.out.slurp(:close);
    $info.err.slurp(:close);
    check $xc == 0 && $out eq '2 4 2 5 3 p' && $line.contains('"eval"'),
          '…and run with the parser cut, from their pre-parsed ASTs', "exit $xc: $out / $err / $line";
}
{
    my $src = probe('cathandle.raku', q{print IO::CatHandle.new($*IN).lines.join('+');});
    my ($rc, $log, $bin) = compile($src, 'cathandle-bin', '--exe', '--slim');
    check $rc == 0, 'IO::CatHandle compiles under --slim', $log;
    my $p = run $bin, :in, :out, :err;
    $p.in.print("a\nb\n"); $p.in.close;
    my ($out, $err) = $p.out.slurp(:close), $p.err.slurp(:close);
    check $p.exitcode == 0 && $out eq 'a+b', '…and reads with the parser cut', "exit {$p.exitcode}: $out / $err";
}
# Constructs the scan reads since the 2026-10-17 audit, each a cut that used
# to be wrong: a bare decimal \c65 resolves without the name table now; a
# composed class's later members are properties too; a grammar's method call
# with arguments evaluates them; 'Na' is East_Asian_Width, not Name.
for ('c-decimal', q{print 'A~' ~~ /\c65 <[\c32..\c126]>/ ?? 'M' !! 'N';},           'M'),
    ('rx-member', q{print '中' ~~ /<[a..z] + :Han>/ && 'Ж' ~~ /<+:Lu +:Cyrillic>/ ?? 'M' !! 'N';}, 'M'),
    ('rx-member-gc', q{print 'a1' ~~ /^<:L + :N>+$/ ?? 'M' !! 'N';},                     'M'),
    ('gr-method', q{grammar G { token TOP { 'a' <.f('x')> }; method f($x) { self } }; print so G.parse('a');}, 'True'),
    ('up-na',     q{print 'a'.uniprop('Na');},                                            'Na')
-> ($name, $code, $want) {
    my $src = probe("{$name}.raku", $code);
    my ($rc, $log, $bin) = compile($src, "{$name}-bin", '--exe', '--slim');
    my ($xc, $out, $err) = run-bin($bin);
    check $rc == 0 && $xc == 0 && $out eq $want,
          "--slim keeps what $name needs, and answers $want", "rc $rc exit $xc: $out / $err / $log";
}

# ---- 6a3. builtin wrappers around dynamic loading are triggers --------------
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
