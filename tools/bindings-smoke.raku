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
#       build/Release/rakupp.exe tools/bindings-smoke.raku build/Release
#                                                   (Windows, MSVC layout)
#
# Hosts whose toolchain is missing — or whose toolchain is an x86_64 build
# that cannot load an arm64 librakupp — skip loudly rather than failing, the
# same convention grammar-smoke.raku and embed-smoke.raku use. On Windows that
# is Go, Rust, JS and Wolfram; Python and C++ (MSVC) are the legs that run,
# and they are the ones a Windows user actually has. For the deep
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

my $WIN = $*KERNEL.name eq 'win32';

my $libname = do given $*KERNEL.name {
    when 'darwin' { 'librakupp.dylib' }
    when 'win32'  { 'librakupp.dll' }   # rakupp.lib is the EXE's import library
    default       { 'librakupp.so' }
};
my $lib = $BUILD.add($libname);

# `python3` on Windows is as often the Microsoft Store's stub as an
# interpreter, and the stub answers --version by opening the Store.
my $PYTHON = $WIN ?? 'python' !! 'python3';

# One child process. Every command used to be a string handed to `sh -c`
# ("cd DIR && VAR=VAL prog args"), which assumed a POSIX shell is installed
# and pasted paths into a double-quoted word — so a Windows C:\Users\... lost
# its backslashes before the program ever saw it. The argument list, the
# working directory and the environment are DATA now, and nothing on any
# platform re-parses them.
sub spawn(@args, :$cwd = $ROOT.absolute, :%env) {
    my %e = %*ENV;
    %e{.key} = .value for %env;
    my $p = run |@args, :$cwd, :env(%e), :out, :err;
    ($p.exitcode, $p.out.slurp(:close), $p.err.slurp(:close))
}

# A line ending is a platform convention, not a binding difference: the same
# print reaches us as \r\n through a Windows C runtime and \n everywhere
# else. Comparing and recording in \n keeps ONE recorded expectation good for
# every host on every platform, which is the whole claim calc.txt makes.
sub lf(Str() $s) { $s.subst("\r\n", "\n", :g) }

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
            !! $tool eq 'cl'            ?? '/?'        # MSVC: no --version
            !! '--version';
    my $p = try run $tool, $flag, :out, :err;
    so $p && $p.exitcode == 0
}

# The C++ leg is the one with a real prerequisite on Windows: MSVC on PATH
# (which means a developer environment) and librakupp.lib, the DLL's import
# library, beside the DLL. Name the missing one rather than skipping mutely —
# a silent skip on this platform is exactly what hid the wrong DLL name.
my $cpp-why = do {
    my $implib = $BUILD.add('librakupp.lib');
    !$WIN          ?? ''
    !! !have('cl') ?? 'no MSVC cl on PATH (run inside a developer environment)'
    !! !$implib.e  ?? "no {$implib.basename} beside the DLL to link against"
    !!                ''
};

# Each host: how to run one example, and whether its toolchain is here. The
# commands are the ones the guides print — if a guide's command rots, so does
# this gate, which is the point. Each `run` returns (exitcode, stdout, stderr).
my @hosts =
    %(  name => 'python', label => 'Python',
        run  => -> $ex { spawn [$PYTHON, "bindings/python/examples/$ex.py"],
                               env => %( RAKUPP_LIB => $lib.absolute ) },
        here => have($PYTHON) ),

    %(  name => 'js', label => 'JS',
        run  => -> $ex { spawn ['bun', "bindings/js/examples/$ex.mjs"],
                               env => %( RAKUPP_LIB => $lib.absolute ) },
        here => have('bun') ),

    %(  name => 'go', label => 'Go',
        run  => -> $ex { spawn ['go', 'run', "./examples/$ex"],
                               cwd => $ROOT.add('bindings/go').absolute,
                               env => %( CGO_LDFLAGS =>
                                         "-L{$BUILD.absolute} -Wl,-rpath,{$BUILD.absolute}" ) },
        here => !$WIN && have('go') ),

    %(  name => 'rust', label => 'Rust',
        run  => -> $ex { spawn ['cargo', 'run', '--quiet',
                                '--manifest-path', 'bindings/rust/Cargo.toml',
                                '--example', $ex],
                               env => %( RAKUPP_LIB_DIR => $BUILD.absolute ) },
        here => !$WIN && have('cargo') ),

    %(  name => 'cpp', label => 'C++',
        run  => -> $ex { cpp-run($ex) },
        here => $cpp-why eq '', why => $cpp-why ),

    # wolframscript -version answers without a kernel, so `here` is true on an
    # installed-but-unactivated Engine too — running an example then stops on
    # the activation prompt, which is the right loud failure for that state.
    %(  name => 'wolfram', label => 'Wolfram',
        run  => -> $ex { spawn ['wolframscript', '-file',
                                "bindings/wolfram/examples/$ex.wls"],
                               env => %( RAKUPP_LIB => $lib.absolute ) },
        here => have('wolframscript') ),
;

# C++ is the odd one: it compiles first, and LINKS rather than dlopen'ing.
# Every flag here has two spellings, and the run step differs too — a Windows
# executable has no rpath, so it finds librakupp.dll beside itself or on PATH.
sub cpp-run($ex) {
    my $src = "bindings/cpp/examples/$ex.cpp";
    my $exe = $*TMPDIR.add("bindings-smoke-$ex-$*PID" ~ ($WIN ?? '.exe' !! ''));
    my ($rc, $out, $err);

    if $WIN {
        my $cxx = %*ENV<CXX> // 'cl';
        # cl drops its .obj in the CURRENT directory, so the current directory
        # is a scratch one and everything else is absolute. Steering the object
        # with /Fo instead would need a trailing backslash, and a trailing
        # backslash inside a quoted argument escapes the quote.
        my $obj = $*TMPDIR.add("bindings-smoke-obj-$ex-$*PID");
        $obj.mkdir;
        ($rc, $out, $err) = spawn [$cxx, '/nologo', '/std:c++17', '/EHsc',
                                   '/I', $ROOT.add('include').absolute,
                                   $ROOT.add($src).absolute,
                                   '/Fe' ~ $exe.absolute,
                                   '/link', '/LIBPATH:' ~ $BUILD.absolute,
                                   'librakupp.lib'],
                                  cwd => $obj.absolute;
        .unlink for $obj.dir;
        $obj.rmdir;
    }
    else {
        my $cxx  = %*ENV<CXX> // 'c++';
        my @link = $*KERNEL.name eq 'darwin'
            ?? ($lib.absolute, '-Wl,-rpath,' ~ $BUILD.absolute)
            !! ('-L' ~ $BUILD.absolute, '-lrakupp',
                '-Wl,-rpath,' ~ $BUILD.absolute, '-lpthread');
        ($rc, $out, $err) = spawn [$cxx, '-std=c++17', '-Iinclude', $src,
                                   |@link, '-o', $exe.absolute];
    }
    return ($rc, $out, $err) unless $rc == 0;

    my %env = $WIN ?? %( PATH => $BUILD.absolute ~ ';' ~ (%*ENV<PATH> // '') ) !! %();
    my @ran = spawn [$exe.absolute], :%env;
    $exe.unlink;
    |@ran
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
            my $why = %h<why> // '';
            say "skip - no toolchain for the {%h<label>} leg"
                ~ ($why ?? ": $why" !! '');
            next;
        }
        my $shared = $EXPECT.add("{$ex}.txt");
        my $mine   = $EXPECT.add("{$ex}.{%h<name>}.txt");
        my $file   = $mine.e ?? $mine !! $shared;

        my ($rc, $raw, $err) = %h<run>($ex);
        my $out = lf($raw);

        if $rc != 0 && arch-skip($err) {
            say "skip - $ex/{%h<label>}: toolchain architecture cannot load $lib";
            next;
        }
        unless $rc == 0 {
            # A CRASH writes nothing to stderr, and the detail was $err alone:
            # the Windows C++ leg failed with an empty one, so the log said only
            # that it had failed. The exit code is the part that is always there,
            # and on Windows an abnormal exit IS its status code — hence the hex,
            # where 0xC0000005 is an access violation and 0xC00000FD a stack
            # overflow. Whatever the run did manage to print comes with it.
            my $detail = "exit code $rc"
                       ~ ($WIN ?? " (0x{ ($rc +& 0xFFFFFFFF).base(16) })" !! '');
            $detail ~= "\nstderr:\n" ~ $err.trim-trailing.indent(2) if $err.trim;
            $detail ~= "\nstdout:\n" ~ $out.trim-trailing.indent(2) if $out.trim;
            check False, "$ex runs under {%h<label>}", $detail;
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
        my $want = lf($file.slurp);
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

    my %probe = python => [$PYTHON, $py.Str], js => ['bun', $js.Str];
    for @hosts.grep({ %probe{.<name>}:exists }) -> %h {
        next unless %h<here>;
        my ($rc, $o, $e) = spawn %probe{%h<name>},
                                 env => %( RAKUPP_LIB => $notalib.absolute );
        my $err = $e ~ $o;
        # The exact phrasing matters: the fall-back path's own "librakupp not
        # found" message also mentions RAKUPP_LIB, so only naming the offending
        # file distinguishes "used as given and failed" from "searched and
        # found nothing".
        check $rc != 0 && $err.contains('which could not be loaded')
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
