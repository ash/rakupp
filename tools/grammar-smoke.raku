# The standing grammar-service gate (GRAMMAR-PLAN G0-G2). The legs:
#
#   1. The shim's own contract, engine-side: grammar_shim.raku + its
#      self-test run under plain rakupp. No Python, no shared library —
#      this leg must pass on any machine the test suite passes on.
#      Plus the staleness check: src/GrammarShim.cpp (the copy baked into
#      librakupp for rk_grammar_shim) must match the canonical shim.
#   2. Byte-identical outputs, one leg per HOST BINDING: the same grammar
#      and the same 2000-line corpus driven by the Raku reference driver
#      and by each host driver — Python (ctypes), C++ (grammar.hpp),
#      JS (bun:ffi), Go (cgo), Rust (crate) — compared byte for byte.
#      This is the gate the plan names: a host must get exactly what rakupp
#      gets, or the service is decoration.
#
# Run:  build/rakupp tools/grammar-smoke.raku [build-dir]    (default: build)
# Host legs self-skip (loudly) when their toolchain or librakupp is missing,
# or when the toolchain's architecture cannot load the built library (an
# x86_64 bun/go on an arm64 machine) — the same convention embed-smoke.raku
# uses for its shared-library check. CI runs every leg its runner can.

my $ROOT  = $?FILE.IO.parent.parent;
my $BUILD = $ROOT.add(@*ARGS[0] // 'build');
my $errors  = 0;

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

my $rakupp = $BUILD.add('rakupp');
my $shim   = $ROOT.add('bindings/python/rakulang/grammar_shim.raku');
my $tmp    = $*TMPDIR.add("grammar-smoke-$*PID");
$tmp.mkdir;
LEAVE { .unlink for $tmp.dir; $tmp.rmdir }

sub with-shim($body-file, $to) {
    $to.spurt($shim.slurp ~ "\n" ~ $body-file.slurp);
    $to
}

# ---- 1. the shim under plain rakupp ----------------------------------------

my $selftest = with-shim($ROOT.add('tools/grammar/shim-selftest.raku'),
                         $tmp.add('selftest.raku'));
my $st = run $rakupp.Str, $selftest.Str, :out, :err;
my $st-out = $st.out.slurp(:close);
print $st-out.lines.grep(*.starts-with('NOT OK')).map(* ~ "\n").join;
check $st.exitcode == 0 && $st-out.contains(', 0 failed'),
      "shim self-test under plain rakupp ({$st-out.lines.tail // ''})",
      $st.err.slurp(:close);

# the baked copy (rk_grammar_shim) must match the canonical shim source
my $genchk = run $rakupp.Str, $ROOT.add('tools/grammar/gen-shim-src.raku').Str, '--check', :out, :err;
check $genchk.exitcode == 0, "src/GrammarShim.cpp is current", $genchk.err.slurp(:close);

# ---- the corpus both drivers read ------------------------------------------

my $corpus = $tmp.add('log.txt');
my $gen = run $rakupp.Str, $ROOT.add('tools/grammar/gen-log.raku').Str, :out;
$corpus.spurt($gen.out.slurp(:close));
check $corpus.s > 50_000, "the generated corpus is corpus-sized ({$corpus.s} bytes)";

# ---- 2 + 3 need python3 and librakupp --------------------------------------

my $lib = $BUILD.add($*KERNEL.name eq 'darwin' ?? 'librakupp.dylib' !! 'librakupp.so');
my $python = do {
    my $p = try run 'python3', '--version', :out, :err;
    $p && $p.exitcode == 0 ?? 'python3' !! Nil;
};

if !$lib.e {
    say "skip - no shared librakupp in $BUILD (build with -DRAKUPP_BUILD_SHARED=ON for the host legs)";
}
else {
    # the Raku driver: the reference output every host must reproduce
    my $driver = with-shim($ROOT.add('tools/grammar/driver-body.raku'),
                           $tmp.add('driver.raku'));
    my $ref = run $rakupp.Str, $driver.Str,
                  $ROOT.add('tools/grammar/log.raku').Str, $corpus.Str, :out, :err;
    my $ref-out = $ref.out.slurp(:close);
    check $ref.exitcode == 0 && $ref-out.starts-with('lines 2000'),
          "the Raku driver parses the corpus", $ref.err.slurp(:close);

    my $grammar = $ROOT.add('tools/grammar/log.raku').Str;

    # An x86_64 toolchain (Rosetta debris) cannot load an arm64 librakupp —
    # that is that machine's toolchain problem, not the binding's: skip.
    sub arch-skip($err) { $err.contains('incompatible architecture')
                       || $err.contains('found architecture') }

    # Run one host driver and byte-compare against the reference.
    sub host-leg($name, @cmd) {
        my $p = run |@cmd, :out, :err;
        my $out = $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);
        if $p.exitcode != 0 && arch-skip($err) {
            say "skip - $name driver: toolchain architecture cannot load $lib";
            return;
        }
        check $p.exitcode == 0, "the $name driver runs", $err;
        return if $p.exitcode != 0;
        if $ref-out eq $out {
            check True, "$name output is byte-identical to rakupp's ({$out.encode.bytes} bytes)";
        }
        else {
            my @r = $ref-out.lines;
            my @h = $out.lines;
            my $where = (^(@r.elems max @h.elems)).first({ (@r[$_] // '') ne (@h[$_] // '') });
            check False, "$name output is byte-identical to rakupp's",
                  "first divergence at line {$where + 1}:\n  rakupp: {@r[$where] // '(missing)'}\n  $name: {@h[$where] // '(missing)'}";
        }
    }

    sub have($tool) { # go spells it `go version`; a crashing toolchain = absent
        my $p = try run $tool, ($tool eq 'go' ?? 'version' !! '--version'), :out, :err;
        so $p && $p.exitcode == 0
    }

    # ---- Python: ctypes over the shared library ----------------------------
    if $python {
        host-leg('Python', [$python, $ROOT.add('tools/grammar/driver.py').Str,
                            $lib.Str, $grammar, $corpus.Str]);
    }
    else { say "skip - no python3 on PATH" }

    # ---- C++: grammar.hpp, LINKED against the library ---------------
    {
        my $cxx = %*ENV<CXX> // 'c++';
        my $exe = $tmp.add('driver-cpp');
        my @link = $*KERNEL.name eq 'darwin'
            ?? ($lib.Str, "-Wl,-rpath,{$BUILD}")
            !! ("-L{$BUILD}", '-lrakupp', "-Wl,-rpath,{$BUILD}", '-lpthread');
        my $cc = run $cxx, '-std=c++17', "-I{$ROOT.add('src')}",
                     $ROOT.add('tools/grammar/driver.cpp').Str, |@link,
                     '-o', $exe.Str, :err;
        check $cc.exitcode == 0, "driver.cpp compiles against grammar.hpp",
              $cc.err.slurp(:close);
        host-leg('C++', [$exe.Str, $grammar, $corpus.Str]) if $cc.exitcode == 0;
    }

    # ---- JS: bun:ffi --------------------------------------------------------
    if have('bun') {
        host-leg('JS', ['bun', $ROOT.add('tools/grammar/driver.mjs').Str,
                        $lib.Str, $grammar, $corpus.Str]);
    }
    else { say "skip - no bun on PATH for the JS leg" }

    # ---- Go: cgo (POSIX: env + cwd travel via sh) ---------------------------
    if $*KERNEL.name ne 'win32' && have('go') {
        host-leg('Go', ['sh', '-c',
            "cd {$ROOT.add('bindings/go')} && " ~
            "CGO_LDFLAGS='-L{$BUILD} -Wl,-rpath,{$BUILD}' " ~
            "go run ./driver {$grammar} {$corpus}"]);
    }
    else { say "skip - no go on PATH for the Go leg" }

    # ---- Rust: the crate ----------------------------------------------------
    if $*KERNEL.name ne 'win32' && have('cargo') {
        host-leg('Rust', ['sh', '-c',
            "RAKUPP_LIB_DIR={$BUILD} cargo run --quiet --release " ~
            "--manifest-path {$ROOT.add('bindings/rust/Cargo.toml')} " ~
            "--example driver -- {$grammar} {$corpus}"]);
    }
    else { say "skip - no working cargo on PATH for the Rust leg" }
}

if $errors {
    say "grammar-smoke: $errors FAILED";
    exit 1;
}
say "grammar-smoke: ok";
