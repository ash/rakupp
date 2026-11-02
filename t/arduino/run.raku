#!/usr/bin/env rakupp
# The --target=arduino gate (ARDUINO-PLAN.md).
#
# Every program in t/arduino/cases/ runs on the interpreter's simulated board
# (`use Arduino`, rakulib/Arduino.rakumod), and its transcript — Serial on
# stdout, one line per pin change on stderr — must equal the golden in
# expected/. Then its sketch, built for this computer with
# `--target=arduino --sim` against the host core (src/codegen/ArduinoSim.h),
# must write the same bytes and exit the same way: the interpreter is the
# oracle for the C++. Both run in virtual time — a case's `# stop: MS` line is
# RAKUPP_ARDUINO_STOP and its `# pins:` line RAKUPP_ARDUINO_PINS — so a run is
# exact and takes milliseconds. stderr is compared without the location lines
# under a `die` message: the interpreter prints a backtrace, a board the line.
#
# Then what the board refuses: programs outside the subset must stop the build
# with exit 5 and a message naming the construct and its line.
#
#   rakupp t/arduino/run.raku                   # the gate, ~20 s
#   rakupp t/arduino/run.raku --record          # rewrite expected/ from the interpreter
#   rakupp t/arduino/run.raku --arduino15=DIR   # also compile each sketch for an
#                                               # Arduino Due with the SAM core under
#                                               # DIR (~/Library/Arduino15 on a Mac)
#
# Without a C++ compiler the sketch half is skipped, and says so. Exit 1 when
# anything fails.

my $count = 0;
my $fails = 0;
sub ok(Mu $cond, Str $desc) {
    $count++;
    if $cond { say "ok $count - $desc" } else { $fails++; say "not ok $count - $desc" }
}
sub skip(Str $desc) { $count++; say "ok $count - $desc # SKIP" }
sub diag(Str $m) { note "# $_" for $m.lines }

my $here  = $?FILE.IO.parent;
my $cases = $here.add('cases');
my $tmp   = $*TMPDIR.add("rakupp-arduino-$*PID");
$tmp.mkdir;

sub header(IO::Path $f, Str $key) {
    for $f.lines -> $l {
        last unless $l.starts-with('#');
        return ~$0 if $l ~~ /^ '#' \s* $key ':' \s* (\S+) /;
    }
    Nil
}
sub without-locations(Str $s) { $s.lines.grep({ !/^ \s+ ['in ' | 'at line ' | \d+ ' |'] / }).join("\n") }

sub run-in(@cmd, %env) {
    my $p = run |@cmd, :out, :err, :cwd($cases.Str), :env(%(%*ENV, %env));
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    %(:$out, :$err, :exit($p.exitcode))
}

sub MAIN(Bool :$record = False, Str :$arduino15) {
    my $cxx = %*ENV<CXX> // 'c++';
    my $have-cxx = !$*DISTRO.is-win && run('sh', '-c', "command -v $cxx", :!out, :!err).exitcode == 0;
    for $cases.dir.grep(*.extension eq 'raku').sort -> $case {
        my $name = $case.basename.subst(/'.raku' $/, '');
        my %env = RAKUPP_ARDUINO_STOP => header($case, 'stop') // '1000';
        with header($case, 'pins') { %env<RAKUPP_ARDUINO_PINS> = $_ }
        my %int = run-in([$*EXECUTABLE.Str, $case.basename], %env);
        my $want-out = $here.add("expected/$name.out");
        my $want-err = $here.add("expected/$name.err");
        if $record {
            $want-out.spurt(%int<out>);
            $want-err.spurt(%int<err>);
            say "# recorded $name (exit %int<exit>)";
            next;
        }
        ok($want-out.e && %int<out> eq $want-out.slurp && %int<err> eq $want-err.slurp,
           "$name: the simulated board's transcript is the golden");
        unless $have-cxx { skip("$name: the sketch on the host core (no $cxx on PATH)"); next }
        my $exe = $tmp.add($name).Str;
        my %b = run-in([$*EXECUTABLE.Str, '-q', '--target=arduino', '--sim', $case.basename, '-o', $exe], {});
        if %b<exit> != 0 { ok(False, "$name: --target=arduino --sim builds it"); diag(%b<err>); next }
        my %sim = run-in([$exe], %env);
        my $same = %sim<out> eq %int<out> && %sim<exit> == %int<exit>
                && without-locations(%sim<err>) eq without-locations(%int<err>);
        ok($same, "$name: its sketch writes the same bytes and exits the same way");
        unless $same {
            diag("exit: interpreter %int<exit>, sketch %sim<exit>");
            diag("stdout differs") if %sim<out> ne %int<out>;
            diag("stderr differs:\n" ~ %sim<err>) if without-locations(%sim<err>) ne without-locations(%int<err>);
        }
    }
    return if $record;

    # The board gets values: `.map` and `sin` ran while building, and the
    # table is in flash (PROGMEM on AVR) as bytes.
    my %f = run-in([$*EXECUTABLE.Str, '--target=arduino', 'fade.raku'], {});
    ok(%f<exit> == 0 && %f<out>.contains('static const uint8_t t_wave[32] RK_FLASH')
       && %f<out>.contains("void setup()") && %f<out>.contains("void loop()") && !%f<out>.contains('map'),
       'fade: the table was computed while building and is a const byte table in the sketch');

    # What the board refuses, and how it says so.
    for (
        'my $x = 5; say $x'                                => 'the untyped variable $x',
        'my int $x = 5; say $x / 2'                        => 'which is a Rat in Raku',
        'my int @a = 1, 2, 3'                              => 'has no shape',
        'use Test; ok 1'                                   => "a module's routines run while building",
        'pinMode(13, 1)'                                   => 'did you mean to `use Arduino`',
        'use Arduino; sub f(int $x) { $x * 2 }; say f(1)'  => 'which declares none',
        'my num $f = 1.5e0; say $f'                        => 'printing a num',
        'use Arduino; loop { delay(1) }; say "never"'      => 'after the top-level `loop`',
        'for 1..3 -> $k { $k = 5 }'                        => 'is read-only',
        'my %h; say 1'                                     => 'the board has no hashes',
        'class C { }'                                      => 'a class',
    ) -> (:key($code), :value($why)) {
        my %r = run-in([$*EXECUTABLE.Str, '--target=arduino', '-e', $code], {});
        ok(%r<exit> == 5 && %r<err>.starts-with('rakupp --target=arduino: -e, line 1: ') && %r<err>.contains($why),
           "refused: $code");
        diag(%r<err>) unless %r<exit> == 5 && %r<err>.contains($why);
    }
    my %s = run-in([$*EXECUTABLE.Str, '--sim', '-e', 'say 1'], {});
    ok((%s<out> ~ %s<err>).contains('Illegal option --sim') && !%s<out>.contains("1\n"),
       '--sim without --target=arduino is an illegal option, and runs nothing');

    # The Due's own compiler and core headers, when they are installed.
    with $arduino15 {
        my $root = .IO;
        my $core = ($root.add('packages/arduino/hardware/sam').dir.sort.tail if $root.add('packages/arduino/hardware/sam').d);
        my $gcc  = ($root.add('packages/arduino/tools/arm-none-eabi-gcc').dir.sort.tail.add('bin/arm-none-eabi-g++')
                   if $root.add('packages/arduino/tools/arm-none-eabi-gcc').d);
        if !$core || !$gcc || !$gcc.e { skip("the Due: no SAM core and arm-none-eabi-gcc under $root"); }
        else {
            my $sys = $core.add('system');
            for $cases.dir.grep(*.extension eq 'raku').sort -> $case {
                my $sketch = $tmp.add($case.basename ~ '.cpp');
                my %e = run-in([$*EXECUTABLE.Str, '-q', '--target=arduino', $case.basename, '-o', $sketch.Str], {});
                my $p = run $gcc.Str, '-c', '-Os', '-Wall', '-Werror', '-std=gnu++11', '-ffunction-sections', '-fdata-sections',
                    '-nostdlib', '-fno-threadsafe-statics', '-fno-rtti', '-fno-exceptions', '-mcpu=cortex-m3', '-mthumb',
                    '-DF_CPU=84000000L', '-DARDUINO=10819', '-DARDUINO_SAM_DUE', '-DARDUINO_ARCH_SAM', '-D__SAM3X8E__',
                    '-DUSB_VID=0x2341', '-DUSB_PID=0x003e', '-DUSBCON',
                    "-I{$sys.add('libsam')}", "-I{$sys.add('CMSIS/CMSIS/Include')}", "-I{$sys.add('CMSIS/Device/ATMEL')}",
                    "-I{$core.add('cores/arduino')}", "-I{$core.add('variants/arduino_due_x')}",
                    '-x', 'c++', $sketch.Str, '-o', $tmp.add($case.basename ~ '.o').Str, :out, :err;
                my $err = $p.err.slurp(:close);
                ok(%e<exit> == 0 && $p.exitcode == 0, "{$case.basename}: the sketch compiles for the Due, warning-free");
                diag($err) if $p.exitcode != 0;
            }
        }
    }
    LEAVE { try { .unlink for $tmp.dir; $tmp.rmdir } }
    say "1..$count";
    note $fails ?? "# $fails of $count checks FAILED" !! "# all $count checks passed";
    exit($fails ?? 1 !! 0);
}
