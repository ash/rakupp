# The standing embed/ABI gate (ABI-PLAN A0/A1, EMBED-PLAN E0). Three checks:
#
#   1. tools/embed/host.cpp compiles against the static runtime and runs Raku
#      from C++ — the smallest real embedder, kept working on every batch.
#   2. tools/embed/callback-ext.c compiles against the extension ABI and calls
#      back INTO Raku — rk_call, rk_call_value, rk_can, the error contract and
#      rooted handles, driven from tools/embed/ext-callback.raku.
#   3. If a shared librakupp exists in the build directory, its export table
#      is exactly the extension ABI: every rk_* from src/rakupp_ext.h present,
#      and not one interpreter internal leaked. (A leaked internal symbol is a
#      permanent ABI liability the moment a binding ships.)
#
# Run:  build/rakupp tools/embed-smoke.raku [build-dir]     (default: build)
# The shared-library check self-skips when the directory has no librakupp —
# so the gate is safe to run against a plain static build too.
#
# Checks 1 and 2 need a C/C++ compiler, which is why this lives here and not in
# t/regression/: that suite must stay runnable on a machine with none.

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
# The runtime is a SET of archives since the SLIM split (P2): the core plus the
# four feature groups. rt and parse reference each other, so single-pass GNU ld
# needs the group; ld64 iterates archives on its own and has no --start-group.
my @rtset = $rt.Str,
            |<ucd_names ucd_coll ucd_props parse>.map({ $BUILD.add("librakupp_$_.a").Str });
my @rtlink = $*KERNEL.name eq 'darwin'
    ?? @rtset
    !! ('-Wl,--start-group', |@rtset, '-Wl,--end-group');
if $rt.e {
    $checked++;
    my $cxx  = %*ENV<CXX> // 'c++';
    my $host = $BUILD.add('embed-smoke-host');
    my $cc = run $cxx, '-std=c++17', "-I{$ROOT.add('src')}",
                 $ROOT.add('tools/embed/host.cpp').Str, |@rtlink,
                 '-lpthread', '-o', $host.Str, :err;
    check $cc.exitcode == 0, "host.cpp compiles and links against the runtime archives",
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

# ---- 1b. a C host embedding the interpreter (ABI-PLAN A2) -------------------

if $rt.e {
    $checked++;
    my $cc   = %*ENV<CC> // 'cc';
    my $cxx  = %*ENV<CXX> // 'c++';
    # rakupp.h says `#include <rakupp/rakupp.h>`, so the include path has to be
    # the directory CONTAINING a `rakupp/` — the same symlink the extension
    # build below uses.
    my $inc0 = $BUILD.add('ext-include');
    $inc0.mkdir;
    my $ln = $inc0.add('rakupp');
    run 'ln', '-sfn', $ROOT.add('src').Str, $ln.Str, :err unless $ln.e;

    my $obj  = $BUILD.add('embed-host.o');
    my $host = $BUILD.add('embed-host');
    # Compiled as C — every FFI binding reaches this ABI through a C
    # declaration, so if the header needs C++ to be usable it is wrong.
    my $cp = run $cc, '-std=c99', '-c', "-I$inc0",
                 $ROOT.add('tools/embed/embed-host.c').Str, '-o', $obj.Str, :err;
    check $cp.exitcode == 0, "embed-host.c compiles as plain C against rakupp.h",
          $cp.err.slurp(:close);
    if $cp.exitcode == 0 {
        # …but linked with the C++ driver, since the runtime it calls is C++.
        my $lp = run $cxx, $obj.Str, |@rtlink, '-lpthread', '-o', $host.Str, :err;
        check $lp.exitcode == 0, "…and links against the runtime", $lp.err.slurp(:close);
        if $lp.exitcode == 0 {
            my $p = run $host.Str, :out, :err;
            my $out = $p.out.slurp(:close);
            check $p.exitcode == 0 && $out.contains('embed host: ok'),
                  "the C host drives the interpreter (eval, state, values, errors, output)",
                  $out ~ $p.err.slurp(:close);
        }
    }
}

# ---- 2. an extension that calls back into Raku ------------------------------

# Headers live in src/ in a checkout, under include/rakupp after an install;
# the extension says `#include <rakupp/rakupp_ext.h>`, so the include path has
# to be the directory CONTAINING a `rakupp/` — hence the symlink.
my $inc = $BUILD.add('ext-include');
$inc.mkdir;
my $link = $inc.add('rakupp');
unless $link.e {
    run 'ln', '-sfn', $ROOT.add('src').Str, $link.Str, :err;
}

if $link.e {
    $checked++;
    my $cc  = %*ENV<CC> // 'cc';
    my $ext = $BUILD.add($*KERNEL.name eq 'darwin' ?? 'libcallback-ext.dylib'
                                                   !! 'libcallback-ext.so');
    # An extension resolves rk_* from the host executable, so undefined symbols
    # at link time are expected — see docs/guide/EXTENSIONS.md.
    my @flags = $*KERNEL.name eq 'darwin' ?? ('-Wl,-undefined,dynamic_lookup') !! ();
    my $p = run $cc, '-shared', '-fPIC', "-I$inc", |@flags,
                $ROOT.add('tools/embed/callback-ext.c').Str, '-o', $ext.Str, :err;
    check $p.exitcode == 0, "callback-ext.c compiles against the extension ABI",
          $p.err.slurp(:close);

    if $p.exitcode == 0 {
        my $r = run $*EXECUTABLE.Str, $ROOT.add('tools/embed/ext-callback.raku').Str,
                    $ext.Str, :out, :err;
        my $out = $r.out.slurp(:close);
        check $r.exitcode == 0 && $out.lines.tail eq 'PASS',
              "the extension calls back into Raku (rk_call, roots, errors)",
              $out ~ $r.err.slurp(:close);
    }
}

# ---- 3. the shared library's exported surface -------------------------------

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
    # Ground truth: every RK_API-marked entry point in the published headers —
    # both of them, since rakupp.h (embedding) is exported the same way
    # rakupp_ext.h (extensions) is.
    my @want;
    for 'src/rakupp_ext.h', 'src/rakupp.h' -> $hdr {
        for $ROOT.add($hdr).IO.lines -> $line {
            next unless $line.starts-with('RK_API');
            @want.push(~$0) if $line ~~ /('rk_' <[a..z_]>+) \s* '('/;
        }
    }
    check @want.elems >= 33, "the headers still declare the rk_* surface ({@want.elems} found)";

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
