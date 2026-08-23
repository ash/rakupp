# The bindings gate you run first: do the documented examples still print
# what the documentation says they print?
#
# Every example in bindings/examples/ is run in every language that has a
# toolchain here, and its output is compared against the recorded expectation
# in bindings/examples/expected/. Two rules, and the file names say which
# applies:
#
#   expected/<example>.txt          one file: every host must print this,
#                                   byte for byte. calc is like this — the
#                                   whole point is that six languages agree.
#   expected/<example>.<host>.txt   per host: the example deliberately shows
#                                   each language's own native data, so the
#                                   outputs differ and each is pinned.
#
# A per-host file wins when both exist. To re-record after an intended change:
#
#   build/rakupp tools/bindings-smoke.raku --record
#
# Run:  build/rakupp tools/bindings-smoke.raku [build-dir]   (default: build)
#
# Hosts whose toolchain is missing — or whose toolchain is an x86_64 build
# that cannot load an arm64 librakupp — skip loudly rather than failing, the
# same convention grammar-smoke.raku and embed-smoke.raku use. For the deep
# gate (the same grammar and a 2000-line corpus through every binding,
# byte-compared against plain rakupp) run tools/grammar-smoke.raku.

my $ROOT   = $?FILE.IO.parent.parent;
my @args   = @*ARGS.grep(* ne '--record');
my $record = so @*ARGS.grep(* eq '--record');
my $BUILD  = $ROOT.add(@args[0] // 'build');
my $EX     = $ROOT.add('bindings/examples');
my $EXPECT = $EX.add('expected');
my $errors = 0;

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

my $libname = do given $*KERNEL.name {
    when 'darwin' { 'librakupp.dylib' }
    when 'win32'  { 'rakupp.dll' }
    default       { 'librakupp.so' }
};
my $lib = $BUILD.add($libname);

unless $lib.e {
    say "skip - $lib is not built; configure with -DRAKUPP_BUILD_SHARED=ON";
    say "bindings-smoke: ok (nothing to check)";
    exit 0;
}

# A toolchain of the wrong architecture cannot use the library; that is the
# machine's problem, not the binding's, so say so and move on. The dlopen'ing
# hosts say it at load time; the LINKING ones (Go, Rust) say it at link time
# instead, and only obliquely — the linker skips the wrong-architecture file
# and then reports our own rk_* symbols as undefined. That pair is the
# signature: a genuinely missing library says "library not found" instead.
sub arch-skip($err) {
    $err.contains('incompatible architecture') || $err.contains('wrong architecture')
        || $err.contains('not a valid Win32 application')
        || ($err.contains('symbol(s) not found for architecture') && $err.contains('_rk_'))
}

sub have($tool) {   # go and wolframscript spell the flag their own way;
                    # a crashing toolchain = absent
    my $flag = $tool eq 'go'            ?? 'version'
            !! $tool eq 'wolframscript' ?? '-version'
            !! '--version';
    my $p = try run $tool, $flag, :out, :err;
    so $p && $p.exitcode == 0
}

# Each host: how to run one example, and whether its toolchain is here. The
# commands are the ones the guides print — if a guide's command rots, so does
# this gate, which is the point.
my @hosts =
    %(  name => 'python', label => 'Python',
        run  => -> $ex { ['sh', '-c',
            "cd {$ROOT} && RAKUPP_LIB={$lib} python3 bindings/python/examples/$ex.py"] },
        here => have('python3') ),

    %(  name => 'js', label => 'JS',
        run  => -> $ex { ['sh', '-c',
            "cd {$ROOT} && RAKUPP_LIB={$lib} bun bindings/js/examples/$ex.mjs"] },
        here => have('bun') ),

    %(  name => 'go', label => 'Go',
        run  => -> $ex { ['sh', '-c',
            "cd {$ROOT.add('bindings/go')} && " ~
            "CGO_LDFLAGS='-L{$BUILD} -Wl,-rpath,{$BUILD}' go run ./examples/$ex"] },
        here => $*KERNEL.name ne 'win32' && have('go') ),

    %(  name => 'rust', label => 'Rust',
        run  => -> $ex { ['sh', '-c',
            "cd {$ROOT} && RAKUPP_LIB_DIR={$BUILD} cargo run --quiet " ~
            "--manifest-path bindings/rust/Cargo.toml --example $ex"] },
        here => $*KERNEL.name ne 'win32' && have('cargo') ),

    %(  name => 'cpp', label => 'C++',
        run  => -> $ex { ['sh', '-c', cpp-command($ex)] },
        here => True ),

    # wolframscript -version answers without a kernel, so `here` is true on an
    # installed-but-unactivated Engine too — running an example then stops on
    # the activation prompt, which is the right loud failure for that state.
    %(  name => 'wolfram', label => 'Wolfram',
        run  => -> $ex { ['sh', '-c',
            "cd {$ROOT} && RAKUPP_LIB={$lib} wolframscript -file bindings/wolfram/examples/$ex.wls"] },
        here => have('wolframscript') ),
;

# C++ is the odd one: it compiles first, and links rather than dlopen'ing.
sub cpp-command($ex) {
    my $cxx = %*ENV<CXX> // 'c++';
    my $exe = $*TMPDIR.add("bindings-smoke-$ex-$*PID");
    my $link = $*KERNEL.name eq 'darwin'
        ?? "{$lib} -Wl,-rpath,{$BUILD}"
        !! "-L{$BUILD} -lrakupp -Wl,-rpath,{$BUILD} -lpthread";
    "cd {$ROOT} && $cxx -std=c++17 -Iinclude bindings/cpp/examples/$ex.cpp $link " ~
    "-o {$exe} && {$exe}; rc=\$?; rm -f {$exe}; exit \$rc"
}

# An example IS its Raku: bindings/examples/<name>.raku names it, and each
# language's own bindings/<lang>/examples/ holds the program that runs it. So
# a new example needs no edit here — add the .raku, the five host programs,
# and record the expectation.
my @examples = $EX.dir(test => *.ends-with('.raku')).map(*.basename.subst(/'.raku'$/, '')).sort;
say "examples: {@examples.join(', ')}";

$EXPECT.mkdir unless $EXPECT.e;

for @examples -> $ex {
    my %got;                        # host name -> its output, for --record
    for @hosts -> %h {
        unless %h<here> {
            say "skip - no toolchain for the {%h<label>} leg";
            next;
        }
        my $shared = $EXPECT.add("{$ex}.txt");
        my $mine   = $EXPECT.add("{$ex}.{%h<name>}.txt");
        my $file   = $mine.e ?? $mine !! $shared;

        my $p   = run |%h<run>($ex), :out, :err;
        my $out = $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);

        if $p.exitcode != 0 && arch-skip($err) {
            say "skip - $ex/{%h<label>}: toolchain architecture cannot load $lib";
            next;
        }
        unless $p.exitcode == 0 {
            check False, "$ex runs under {%h<label>}", $err;
            next;
        }

        if $record {
            %got{%h<name>} = $out;
            next;
        }

        unless $file.e {
            check False, "$ex/{%h<label>} has a recorded expectation",
                  "no {$file.basename}; run with --record";
            next;
        }
        my $want = $file.slurp;
        if $want eq $out {
            check True, "$ex under {%h<label>} matches {$file.basename}";
        }
        else {
            my @w = $want.lines;
            my @g = $out.lines;
            my $at = (^(@w.elems max @g.elems)).first({ (@w[$_] // '') ne (@g[$_] // '') });
            check False, "$ex under {%h<label>} matches {$file.basename}",
                  "first divergence at line {$at + 1}:\n" ~
                  "  expected: {@w[$at] // '(missing)'}\n" ~
                  "  got:      {@g[$at] // '(missing)'}";
        }
    }

    # One shared expectation when every host that ran agrees; per-host files
    # when they legitimately differ. Deciding AFTER the runs is what lets the
    # file name carry the claim.
    if $record && %got {
        .unlink for $EXPECT.dir(test => *.starts-with("{$ex}."));
        if %got.values.unique.elems == 1 {
            $EXPECT.add("{$ex}.txt").spurt(%got.values.head);
            say "recorded - $ex: all {%got.elems} hosts agree -> {$ex}.txt";
        }
        else {
            for %got.kv -> $host, $out {
                $EXPECT.add("{$ex}.{$host}.txt").spurt($out);
            }
            say "recorded - $ex: {%got.elems} hosts differ -> {$ex}.<host>.txt";
        }
    }
}

# ---- the loader contract: a named library is used AS GIVEN ------------------
# Both searching hosts (Python's ctypes, JS's bun:ffi) walk a candidate list
# when nothing is named. When something IS named and will not load, they must
# say so rather than quietly loading the next candidate — silently running a
# DIFFERENT library than the one asked for puts the symptom (some other
# build's behaviour) nowhere near the cause. Pointing RAKUPP_LIB at a file
# that is definitely not a library tests exactly that, on any architecture.
unless $record {
    my $notalib = $EX.add('calc.raku');
    my $probes  = $*TMPDIR.add("bindings-smoke-probe-$*PID");
    $probes.mkdir;
    LEAVE { .unlink for $probes.dir; $probes.rmdir }

    my $py = $probes.add('probe.py');
    $py.spurt(qq:to/PY/);
        import sys
        sys.path.insert(0, "{$ROOT.add('bindings/python')}")
        import rakulang
        rakulang.interpreter()
        PY

    my $js = $probes.add('probe.mjs');
    $js.spurt(qq:to/JS/);
        import \{ interpreter \} from "{$ROOT.add('bindings/js/rakulang.js')}";
        interpreter();
        JS

    my %probe = python => ['python3', $py.Str], js => ['bun', $js.Str];
    for @hosts.grep({ %probe{.<name>}:exists }) -> %h {
        next unless %h<here>;
        my $p = run 'sh', '-c',
                "cd {$ROOT} && RAKUPP_LIB={$notalib} {%probe{%h<name>}.join(' ')}",
                :out, :err;
        my $err = $p.err.slurp(:close) ~ $p.out.slurp(:close);
        # The exact phrasing matters: the fall-back path's own "librakupp not
        # found" message also mentions RAKUPP_LIB, so only naming the offending
        # file distinguishes "used as given and failed" from "searched and
        # found nothing".
        check $p.exitcode != 0 && $err.contains('which could not be loaded')
                               && $err.contains('calc.raku'),
              "{%h<label>}: a named library that cannot load is an error, not a fallback",
              $err;
    }
}

if $record {
    say "bindings-smoke: recorded";
    exit 0;
}
if $errors {
    say "bindings-smoke: $errors FAILED";
    exit 1;
}
say "bindings-smoke: ok";
