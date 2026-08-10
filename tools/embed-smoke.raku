# The standing embed/ABI gate (ABI-PLAN A0, EMBED-PLAN E0). Two checks:
#
#   1. tools/embed/host.cpp compiles against the static runtime and runs Raku
#      from C++ — the smallest real embedder, kept working on every batch.
#   2. If a shared librakupp exists in the build directory, its export table
#      is exactly the extension ABI: every rk_* from src/rakupp_ext.h present,
#      and not one interpreter internal leaked. (A leaked internal symbol is a
#      permanent ABI liability the moment a binding ships.)
#
# Run:  build/rakupp tools/embed-smoke.raku [build-dir]     (default: build)
# The shared-library check self-skips when the directory has no librakupp —
# so the gate is safe to run against a plain static build too.

my $ROOT  = $?FILE.IO.parent.parent;
my $BUILD = $ROOT.add(@*ARGS[0] // 'build');
my $errors  = 0;
my $checked = 0;

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

# ---- 1. the C++ host, against the static runtime ---------------------------

my $rt = $BUILD.add('librakupp_rt.a');
if $rt.e {
    $checked++;
    my $cxx  = %*ENV<CXX> // 'c++';
    my $host = $BUILD.add('embed-smoke-host');
    my $cc = run $cxx, '-std=c++17', "-I{$ROOT.add('src')}",
                 $ROOT.add('tools/embed/host.cpp').Str, $rt.Str,
                 '-lpthread', '-o', $host.Str, :err;
    check $cc.exitcode == 0, "host.cpp compiles and links against librakupp_rt.a",
          $cc.err.slurp(:close);
    if $cc.exitcode == 0 {
        my $p = run $host.Str, :out, :err;
        check $p.exitcode == 0 && $p.out.slurp(:close).contains('embed host: ok'),
              "the C++ host runs Raku and gets the right answer",
              $p.err.slurp(:close);
    }
}
else {
    say "ok - static-host check # SKIP no librakupp_rt.a in $BUILD";
}

# ---- 2. the shared library's exported surface -------------------------------

my $shared = '';
for 'librakupp.dylib', 'librakupp.so' -> $name {
    my $cand = $BUILD.add($name);
    if $cand.e {
        $shared = $cand.Str;
        last;
    }
}

if $shared {
    $checked++;
    # Ground truth: every RK_API-marked entry point in the published header.
    my @want;
    for $ROOT.add('src/rakupp_ext.h').IO.lines -> $line {
        next unless $line.starts-with('RK_API');
        @want.push(~$0) if $line ~~ /('rk_' <[a..z_]>+) \s* '('/;
    }
    check @want.elems >= 26, "the header still declares the rk_* surface ({@want.elems} found)";

    my $nm = run 'nm', '-g', '--defined-only', $shared, :out, :err;
    my @exported;
    for $nm.out.slurp(:close).lines -> $line {
        my @w = $line.words;
        next unless @w.elems >= 3;
        my $sym = @w[2];
        $sym = $sym.substr(1) if $sym.starts-with('_rk_');
        @exported.push($sym);
    }

    my %seen;
    %seen{$_} = True for @exported;
    my @missing = @want.grep({ !%seen{$_} });
    check @missing.elems == 0, "librakupp exports every rk_* entry point",
          "missing: @missing[]";

    # No interpreter internals: nothing from `namespace rakupp` (Itanium
    # mangling spells the namespace as "6rakupp") may appear in the export
    # table. Vague-linkage std:: symbols the standard library forces out are
    # tolerated — they are the toolchain's ABI, not ours.
    my @leaked = @exported.grep({ .contains('6rakupp') });
    check @leaked.elems == 0, "librakupp leaks no interpreter internals",
          "leaked (first): {@leaked[0] // ''} of {@leaked.elems}";
}
else {
    say "ok - shared-library surface check # SKIP no librakupp in $BUILD";
}

if $checked == 0 {
    say "NOT OK - $BUILD has neither librakupp_rt.a nor librakupp — nothing to check";
    exit 1;
}
if $errors == 0 {
    say "ALL EMBED SMOKE CHECKS PASSED";
    exit 0;
}
else {
    say "$errors EMBED SMOKE CHECK(S) FAILED";
    exit 1;
}
