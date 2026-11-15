#!/usr/bin/env rakupp
# The --target=microbit gate (MICROBIT-PLAN.md).
#
# Every program in t/microbit/cases/ runs on the interpreter's simulated board
# (`use MicroBit`, rakulib/MicroBit.rakumod), and its transcript — the serial
# port on stdout; on stderr, one line per change of the LEDs, per text shown
# or scrolled and per pin written — must equal the golden in expected/. Then
# its C++, built for this computer with `--target=microbit --sim` against the
# host core (src/codegen/MicroBitSim.h), must write the same bytes and exit
# the same way: the interpreter is the oracle for the C++. Both run in
# virtual time, with the inputs a case's header lines give — `# stop: MS` is
# RAKUPP_MICROBIT_STOP, `# buttons:` RAKUPP_MICROBIT_BUTTONS, and so on for
# accel, gestures, heading, temp, light, pins and seed — so a run is exact and
# takes milliseconds. stderr is compared without the location lines under a
# `die` message: the interpreter prints a backtrace, a board the line. For
# the same reason the goldens are kept without them.
#
# Then what the board refuses: programs outside the subset must stop the build
# with exit 5 and a message naming the construct and its line.
#
#   rakupp t/microbit/run.raku                 # the gate, ~7 s
#   rakupp t/microbit/run.raku --record        # rewrite expected/ from the interpreter
#   rakupp t/microbit/run.raku --codal=DIR     # also build each program into a
#                                              # MICROBIT.hex with CODAL, in a
#                                              # checkout of microbit-v2-samples
#                                              # (its build.py, CMake and an
#                                              # arm-none-eabi-gcc on PATH)
#
# Without a C++ compiler the --sim half is skipped, and says so. Exit 1 when
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
my $tmp   = $*TMPDIR.add("rakupp-microbit-$*PID");
$tmp.mkdir;

# A case's inputs: its header lines, `# key: value` to the end of the line.
my constant @INPUTS = <stop buttons accel gestures heading temp light pins seed>;
sub inputs(IO::Path $f) {
    my %env = RAKUPP_MICROBIT_STOP => '1000';
    for $f.lines -> $l {
        last unless $l.starts-with('#');
        if $l ~~ /^ '#' \s* (\w+) ':' \s* (\S .*?) \s* $/ && ~$0 (elem) @INPUTS {
            %env{'RAKUPP_MICROBIT_' ~ $0.uc} = ~$1;
        }
    }
    %env
}
sub without-locations(Str $s) { $s.lines.grep({ !/^ \s+ ['in ' | 'at line ' | \d+ ' |'] / }).map(* ~ "\n").join }

sub run-in(@cmd, %env, :$cwd = $cases) {
    my %e = %*ENV;   # the case's inputs alone: none of this shell's own
    %e{%e.keys.grep(*.starts-with('RAKUPP_MICROBIT_'))}:delete;
    %e{.key} = .value for %env;
    my $p = run |@cmd, :out, :err, :cwd($cwd.Str), :env(%e);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    %(:$out, :$err, :exit($p.exitcode))
}

sub MAIN(Bool :$record = False, Str :$codal) {
    my $cxx = %*ENV<CXX> // 'c++';
    my $have-cxx = !$*DISTRO.is-win && run('sh', '-c', "command -v $cxx", :!out, :!err).exitcode == 0;
    for $cases.dir.grep(*.extension eq 'raku').sort -> $case {
        my $name = $case.basename.subst(/'.raku' $/, '');
        my %env = inputs($case);
        my %int = run-in([$*EXECUTABLE.Str, $case.basename], %env);
        my $want-out = $here.add("expected/$name.out");
        my $want-err = $here.add("expected/$name.err");
        if $record {
            $want-out.spurt(%int<out>);
            $want-err.spurt(without-locations(%int<err>));
            say "# recorded $name (exit %int<exit>)";
            next;
        }
        # a die inside the module has its own lines in the backtrace, so the
        # golden keeps the message and not where the module says it
        ok($want-out.e && %int<out> eq $want-out.slurp && without-locations(%int<err>) eq $want-err.slurp,
           "$name: the simulated board's transcript is the golden");
        unless $have-cxx { skip("$name: the program on the host core (no $cxx on PATH)"); next }
        my $exe = $tmp.add($name).Str;
        my %b = run-in([$*EXECUTABLE.Str, '-q', '--target=microbit', '--sim', $case.basename, '-o', $exe], {});
        if %b<exit> != 0 { ok(False, "$name: --target=microbit --sim builds it"); diag(%b<err>); next }
        my %sim = run-in([$exe], %env);
        my $same = %sim<out> eq %int<out> && %sim<exit> == %int<exit>
                && without-locations(%sim<err>) eq without-locations(%int<err>);
        ok($same, "$name: its C++ writes the same bytes and exits the same way");
        unless $same {
            diag("exit: interpreter %int<exit>, C++ %sim<exit>");
            diag("stdout differs") if %sim<out> ne %int<out>;
            diag("stderr differs:\n" ~ %sim<err>) if without-locations(%sim<err>) ne without-locations(%int<err>);
        }
    }
    return if $record;

    # The pictures were made while building and are tables in flash; the
    # top-level loop is a C loop in main(), after uBit.init().
    my %h = run-in([$*EXECUTABLE.Str, '--target=microbit', 'images.raku'], {});
    ok(%h<exit> == 0 && %h<out>.contains('static const uint8_t t_SMILE[25] RK_FLASH')
       && %h<out>.contains('static const uint8_t t_Image_HEART[25] RK_FLASH') && %h<out>.contains('#include "MicroBit.h"')
       && %h<out>.contains("int main() \{\n    uBit.init();") && !%h<out>.contains('image('),
       'images: the pictures were made while building and are byte tables in the program');
    my %t = run-in([$*EXECUTABLE.Str, '--target=microbit', 'tilt.raku'], {});
    ok(%t<exit> == 0 && %t<out>.contains('for (;;) {   // the top-level loop')
       && %t<out>.contains('uBit.accelerometer.getX()') && %t<out>.contains('rk_pause('),
       'tilt: the top-level loop is a C loop in main(), and the board is CODAL\'s uBit');
    # was-gesture listens for the accelerometer's events as they are raised
    my %d = run-in([$*EXECUTABLE.Str, '--target=microbit', 'dice.raku'], {});
    ok(%d<exit> == 0 && %d<out>.contains('uBit.messageBus.listen(DEVICE_ID_GESTURE, DEVICE_EVT_ANY, rk_on_gesture, MESSAGE_BUS_LISTENER_IMMEDIATE)')
       && %d<out>.contains('uBit.random('),
       'dice: was-gesture registers a listener that runs as the event is raised, and randint is uBit.random');

    # What the board refuses, and how it says so.
    for (
        'my $x = 5; say $x'                                => 'the untyped variable $x',
        'display.show(1)'                                  => 'did you mean to `use MicroBit`',
        'use MicroBit; display.blink'                      => 'the display has show, scroll, clear',
        'use MicroBit; my int $d = display'                => 'display used as a value',
        'use MicroBit; display.show(Image.NOPE)'           => 'Image has no picture NOPE',
        'use MicroBit; my int $h = Image.HEART'            => 'Image.HEART used as a value',
        'use MicroBit; constant P = 1, 2, 3; display.show(P)' => 'a picture is 25 brightness levels',
        'use MicroBit; my uint8 @p[24]; display.show(@p)' => 'display.show of @p — a picture is 25',
        'use MicroBit; constant P = (^25).List; display.show(P)' => 'has a brightness of 10 at LED 10',
        'use MicroBit; display.scroll(Image.HEART)'        => 'it scrolls text',
        'use MicroBit; my num $t = 1.5e0; display.scroll($t)' => 'a num on the display',
        "use MicroBit; say accelerometer.was-gesture('wave')" => "'wave' is no gesture",
        'use MicroBit; my Str $g = "shake"; say accelerometer.is-gesture($g)' => 'the variable $g of type Str',
        'use MicroBit; say pin5.is-touched'                => 'pin5 is not a touch pin',
        'use MicroBit; pin17.write-digital(1)'             => 'pin17 is not on the edge connector',
        'use MicroBit; my int $x = pause(5)'               => 'the result of pause, which answers nothing',
        'use MicroBit; my int $x = display.show(5)'        => 'the result of display.show, which answers nothing',
        'use MicroBit; button-a.press'                     => 'a button has is-pressed, was-pressed and get-presses',
        'use MicroBit; display.show(image "#####")'        => 'image runs while building',
        'use MicroBit; loop { pause(1) }; say "never"'     => 'after the top-level `loop`',
        'use Arduino; digitalWrite(13, 1)'                 => 'a routine of `use MicroBit`',
    ) -> (:key($code), :value($why)) {
        my %r = run-in([$*EXECUTABLE.Str, '--target=microbit', '-e', $code], {});
        ok(%r<exit> == 5 && %r<err>.starts-with('rakupp --target=microbit: -e') && %r<err>.contains($why),
           "refused: $code");
        diag(%r<err>) unless %r<exit> == 5 && %r<err>.contains($why);
    }
    # A picture with a mistake dies while building, and the build stops there.
    my %bad = run-in([$*EXECUTABLE.Str, '--target=microbit', '-e',
                      'use MicroBit; constant P = image "##"; display.show(P)'], {});
    ok(%bad<exit> == 5 && %bad<err>.contains('a picture is 5 rows of 5') && %bad<err>.contains('compile-time code died'),
       'a picture that dies while building stops the build, saying why');

    # CODAL's own build, when a checkout of microbit-v2-samples is at hand:
    # each program as its source/main.cpp, built into a MICROBIT.hex, with no
    # warning from the program.
    with $codal {
        my $root = .IO;
        if !$root.add('build.py').e || !$root.add('source').d {
            skip("CODAL: $root is no microbit-v2-samples checkout (no build.py or source/)");
        }
        else {
            my $main = $root.add('source/main.cpp');
            my $saved = $main.e ?? $main.slurp !! Str;
            LEAVE { $main.spurt($saved) with $saved }
            for $cases.dir.grep(*.extension eq 'raku').sort -> $case {
                my %e = run-in([$*EXECUTABLE.Str, '-q', '--target=microbit', $case.basename, '-o', $main.Str], {});
                $root.add('MICROBIT.hex').unlink;
                my $p = run 'python3', 'build.py', :cwd($root.Str), :out, :err;
                my $log = $p.out.slurp(:close) ~ $p.err.slurp(:close);
                my $warned = $log.lines.grep(*.contains('main.cpp:')).grep(*.contains('warning')).elems > 0;
                ok(%e<exit> == 0 && $p.exitcode == 0 && $root.add('MICROBIT.hex').e && !$warned,
                   "{$case.basename}: CODAL builds it into a MICROBIT.hex, no warning from the program");
                diag($log.lines.tail(30).join("\n")) if $p.exitcode != 0 || $warned;
            }
        }
    }
    LEAVE { try { .unlink for $tmp.dir; $tmp.rmdir } }
    say "1..$count";
    note $fails ?? "# $fails of $count checks FAILED" !! "# all $count checks passed";
    exit($fails ?? 1 !! 0);
}
