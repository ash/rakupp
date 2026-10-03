#!/usr/bin/env rakupp
# The native-module-bodies gate (docs/guide/NATIVE.md, "Modules"; src/AotModules.h).
#
# `--exe` compiles the body of each routine of the modules a program embeds to
# native code, and the interpreter enters it after binding the call. Outputs
# alone cannot tell whether that happened: a routine that quietly stopped
# compiling natively, or a native body that is never entered, gives the right
# answer — interpreted. So each case here is checked twice over.
#
# THE ANSWER. Every case is a directory under t/aot/cases/ holding `main.raku`,
# its modules under `lib/`, and `main.out` — the program's output under Rakudo,
# recorded with `--record`. The program is compiled with `--exe` and run three
# ways: with its native bodies, with RAKUPP_NO_AOT=1 (the same binary, modules
# interpreted), and by the interpreter. Each must print `main.out` and exit 0.
#
# THE PATH. Each module says, in `#aot:` comments, which path each routine and
# statement takes:
#
#     sub f($x) {          #aot: native
#         $obj.Base::m;    #aot: delegated      (the interpreter runs this statement)
#     }
#     sub g($x is rw) { }  #aot: interpreted (is rw)
#
# The words: `native`, `interpreted` (an optional parenthesized text must occur
# in the compiler's reason), `delegated` (`delegated*2` for two statements on
# one line). An annotation sits on the line of the routine's declarator, or of
# the statement. The runner asserts them against the compiler's report
# (RAKUPP_AOT_REPORT) in both directions — an unannotated routine or delegated
# statement fails as surely as a wrong annotation — and against the binary's
# run log (RAKUPP_AOT_RUNLOG): each `native` routine's body must have been
# ENTERED in the native run, and nothing may be entered with RAKUPP_NO_AOT=1.
# So every routine annotated `native` must be called by its program, and not be
# a pure-integer routine the interpreter's integer kernel runs instead.
#
# Directives, as comments in main.raku:
#   #aot-compile-env: NAME=VALUE    set for the compile (a fault knob)
#   #aot-run-env: NAME=VALUE        set for the native run only
#   #aot-extra-run-env: NAME=VALUE  one more native run, with this set too
#   #aot-expect: bundled            the program itself falls back to bundling
#   #aot-expect: refused MODULE     the binary refuses MODULE's table: its
#                                   routines run interpreted, none is entered
#   #aot-expect: declined NAME      NAME's native body declines every call
#
#   rakupp t/aot/run.raku                   # every case
#   rakupp t/aot/run.raku attributes …      # the named cases
#   rakupp t/aot/run.raku --record CASE …   # (re)write main.out from Rakudo
#   rakupp t/aot/run.raku --oracle          # also re-run Rakudo, compare with main.out
#   RAKUDO=/path/to/rakudo                  # the oracle (default: `rakudo` on PATH)
#
# One core, ~5 s a case (the C++ compile of each binary). Exit 1 on any failure.

my $ROOT  = $*PROGRAM.IO.absolute.IO.parent.parent.parent;
my $CASES = $ROOT.add('t/aot/cases');
my $rakupp = $*EXECUTABLE.absolute;
my $rakudo = %*ENV<RAKUDO> // 'rakudo';

my @args = @*ARGS;
my $record = so @args.grep('--record');
my $oracle = so @args.grep('--oracle');
my $keep   = so @args.grep('--keep');
@args = @args.grep({ !.starts-with('--') });

my @cases = @args
    ?? @args.map({ $CASES.add($_) })
    !! $CASES.dir.grep(*.d).sort(*.basename);
for @cases -> $c { unless $c.add('main.raku').f { note "no such case: {$c.basename}"; exit 2 } }

my $tmp = $*TMPDIR.add("rakupp-aot-gate-$*PID");
$tmp.mkdir;
END { if $tmp.e && !$keep { run 'rm', '-rf', $tmp.Str } }

# Every run is capped, with stdin closed — Battery's run-capped, whose cap is
# a perl `alarm`: a Promise race cannot tell a hung process from one that ended.
use lib $?FILE.IO.absolute.IO.parent.parent.parent.add('tools/lib').Str;
use Battery;
my $LIMIT = 120;
sub bounded(@cmd, :%env, :$cwd) {
    my %r = run-capped(@cmd.map(*.Str), :cap($LIMIT), :work($cwd.IO), :%env);
    my $rc = %r<hung> ?? Nil !! %r<signal> ?? 128 + %r<signal> !! %r<rc>;
    ($rc, %r<out>, %r<err> ~ (%r<hung> ?? "\n(killed after {$LIMIT}s)" !! ''))
}

# What every run adds to the environment it inherits: nothing — a knob set by
# whoever runs the gate goes through, which is how the gate is proved able to
# fail. Two would make it meaningless rather than failing, so they stop it.
if %*ENV<RAKUPP_NO_AOT> || %*ENV<RAKUPP_AOT_RANGE> {
    note "t/aot: RAKUPP_NO_AOT / RAKUPP_AOT_RANGE are set; every case would fail for that reason. Unset them.";
    exit 2;
}
sub base-env() { %() }

# ---- what a case says about itself ------------------------------------------

sub directives($main) {
    my %d = compile-env => {}, run-env => {}, extra => [], expect => [];
    for $main.lines {
        if / '#aot-compile-env:' \s* (\S+?) '=' (\S*) / { %d<compile-env>{~$0} = ~$1 }
        elsif / '#aot-run-env:' \s* (\S+?) '=' (\S*) / { %d<run-env>{~$0} = ~$1 }
        elsif / '#aot-extra-run-env:' \s* (\S+?) '=' (\S*) / { %d<extra>.push: ~$0 => ~$1 }
        elsif / '#aot-expect:' \s* (\S+) [\s+ (\S+)]? / { %d<expect>.push: ($0.Str, ($1 // '').Str) }
    }
    %d
}

# lib/A/B.rakumod is module A::B
sub module-files($case) {
    my @out;
    my @todo = $case.add('lib');
    while @todo {
        my $d = @todo.shift;
        next unless $d.d;
        for $d.dir.sort(*.Str) -> $f {
            if $f.d { @todo.push: $f }
            elsif $f.extension eq 'rakumod' {
                my $rel = $f.relative($case.add('lib')).subst(/ '.rakumod' $ /, '');
                @out.push: $rel.split('/').join('::') => $f;
            }
        }
    }
    @out
}

# MODULE => { LINE => { routine => 'native'|'interpreted'|Nil, why => Str, delegated => Int } }
sub annotations(@modules) {
    my %a;
    for @modules -> $p {
        my ($mod, $file) = $p.key, $p.value;
        for $file.lines.kv -> $i, $line {
            next unless $line ~~ / '#aot:' \s* (.*) $ /;
            my $text = ~$0;
            my %row = routine => Nil, why => '', delegated => 0;
            # the reason runs to the LAST parenthesis: reasons quote code
            if $text ~~ / '(' (.*) ')' / { %row<why> = ~$0; $text = $text.subst(/ '(' .* ')' /, '') }
            for $text.words -> $w {
                if $w eq 'native' | 'interpreted' { %row<routine> = $w }
                elsif $w ~~ / ^ 'delegated' [ '*' (\d+) ]? $ / { %row<delegated> += $0 ?? +$0 !! 1 }
                else { %row<bad> = "unknown annotation word '$w'" }
            }
            %a{$mod}{$i + 1} = %row;
        }
    }
    %a
}

# The compiler's report, in the same shape.
sub read-report($file) {
    my %r;
    my @problems;
    return ({}, ['no report was written']) unless $file.f;
    for $file.lines {
        next if .starts-with('#');
        my @f = .split("\t");
        given @f[0] {
            when 'routine' {
                my ($mod, $line, $who, $path, $why) = @f[1..5];
                %r{$mod}{+$line} //= {};
                my $row = %r{$mod}{+$line};
                @problems.push: "$mod line $line holds two routines; put each on its own line" if $row<routine>;
                $row<routine> = $path;
                $row<why> = $why // '';
                $row<who> = $who;
                $row<delegated> //= 0;
            }
            when 'delegated' {
                %r{@f[1]}{+@f[2]} //= {};
                my $row = %r{@f[1]}{+@f[2]};
                $row<delegated>++;
                $row<who> //= @f[3];
            }
            when 'disabled' { @problems.push: 'the report says the native bodies were disabled at compile time' }
            when 'failed'   { @problems.push: "the report says the compile failed: @f[1]" }
        }
    }
    (%r, @problems).List
}

# MODULE => event ('attached' / 'refused' / 'disabled'); "MODULE\tNAME" =>
# [entered, declined] summed over a routine's candidates
sub read-runlog($file) {
    my (%events, %bodies);
    return (%events, %bodies) unless $file.f;
    for $file.lines {
        next if .starts-with('#');
        my @f = .split("\t");
        if @f[0] eq 'body' {
            my $k = "@f[1]\t@f[2]";
            %bodies{$k} //= [0, 0, @f[3]];
            %bodies{$k}[0] += @f[4];
            %bodies{$k}[1] += @f[5];
        }
        else { %events{@f[1]} = @f[0] }
    }
    (%events, %bodies).List
}

# ---- one case ---------------------------------------------------------------

sub check-case($case) {
    my @why;
    my $name = $case.basename;
    my $main = $case.add('main.raku');
    my $lib  = $case.add('lib');
    my $work = $tmp.add($name);
    $work.mkdir;
    my %d = directives($main);
    my @modules = module-files($case);
    my $want-file = $case.add('main.out');

    if $record || $oracle {
        my ($rc, $o, $e) = bounded([$rakudo, '-I', $lib.Str, $main.Str], :env(base-env()), :cwd($case));
        if ($rc // -1) != 0 { return ["rakudo exits {$rc // 'timeout'}: {$e.lines.head(3).join(' | ')}"] }
        if $record { $want-file.spurt: $o; say "# recorded $name/main.out from $rakudo" }
        elsif $want-file.f && $o ne $want-file.slurp { @why.push: "Rakudo no longer prints main.out" }
    }
    return ["no main.out — record it from Rakudo with --record $name"] unless $want-file.f;
    my $want = $want-file.slurp;

    # compile
    my $bin = $work.add('bin');
    my $report = $work.add('report.tsv');
    my %cenv = base-env();
    %cenv{.key} = .value for %d<compile-env>.pairs;
    %cenv<RAKUPP_AOT_REPORT> = $report.Str;
    my ($crc, $co, $ce) = bounded([$rakupp, '--exe', '-I', $lib.Str, $main.Str, '-o', $bin.Str], :env(%cenv), :cwd($case));
    unless ($crc // -1) == 0 && $bin.e {
        return [|@why, "--exe failed (exit {$crc // 'timeout'}): {$ce.lines.grep(/error|rror:/).head(3).join(' | ')}"];
    }
    my $bundled = so $ce ~~ / 'bundling the whole program' /;
    my $wants-bundled = so %d<expect>.grep(*[0] eq 'bundled');
    @why.push: $wants-bundled ?? 'the program was expected to fall back to bundling, and did not'
                              !! 'the program fell back to bundling' if $bundled != $wants-bundled;

    # run three ways
    my $on-log  = $work.add('on.log');
    my $off-log = $work.add('off.log');
    my %on = base-env();
    %on{.key} = .value for %d<run-env>.pairs;
    %on<RAKUPP_AOT_RUNLOG> = $on-log.Str;
    my %off = base-env();
    %off<RAKUPP_NO_AOT> = '1';
    %off<RAKUPP_AOT_RUNLOG> = $off-log.Str;
    my %runs =
        'native'        => bounded([$bin.Str], :env(%on), :cwd($case)),
        'RAKUPP_NO_AOT' => bounded([$bin.Str], :env(%off), :cwd($case)),
        'interpreted'   => bounded([$rakupp, '-I', $lib.Str, $main.Str], :env(base-env()), :cwd($case));
    for %d<extra>.list -> $kv {
        my %x = %on;
        %x{$kv.key} = $kv.value;
        %x<RAKUPP_AOT_RUNLOG>:delete;
        %runs{"native, {$kv.key}={$kv.value}"} = bounded([$bin.Str], :env(%x), :cwd($case));
    }
    for %runs.keys.sort -> $how {
        my ($rc, $o, $e) = %runs{$how};
        if ($rc // -1) != 0 {
            @why.push: "$how: exit {$rc // 'timeout'}" ~ ($e ?? " — {$e.lines.head(2).join(' | ')}" !! '');
        }
        if $o ne $want {
            my @g = $o.lines; my @w = $want.lines;
            my $at = (^max(+@g, +@w)).first({ (@g[$_] // '<none>') ne (@w[$_] // '<none>') });
            @why.push: "$how: output differs from main.out at line {$at + 1}: got {(@g[$at] // '<none>').raku}, want {(@w[$at] // '<none>').raku}";
        }
    }

    # the annotations against the report
    my %ann = annotations(@modules);
    my ($rep-h, $rp) = read-report($report);
    my %rep = $rep-h;
    @why.append: @$rp;
    for @modules.map(*.key) -> $mod {
        my %a = %ann{$mod} // {};
        my %r = %rep{$mod} // {};
        for (%a.keys, %r.keys).flat.unique.sort(+*) -> $ln {
            my $a = %a{$ln};
            my $r = %r{$ln};
            my $at = "$mod line $ln";
            if $a && $a<bad> { @why.push: "$at: $a<bad>"; next }
            my $ar = $a ?? $a<routine> !! Nil;
            my $rr = $r ?? $r<routine> !! Nil;
            if $rr && !$ar {
                @why.push: "$at: {$r<who>} is $rr" ~ ($r<why> ?? " ($r<why>)" !! '') ~ ', and not annotated';
            }
            elsif $ar && !$rr { @why.push: "$at: annotated $ar, but the report has no routine there" }
            elsif $ar && $rr ne $ar {
                @why.push: "$at: {$r<who>} is annotated $ar, but is $rr" ~ ($r<why> ?? " ($r<why>)" !! '');
            }
            elsif $ar && $a<why> && !$r<why>.contains($a<why>) {
                @why.push: "$at: {$r<who>} is interpreted for '{$r<why>}', not '{$a<why>}'";
            }
            my $ad = $a ?? $a<delegated> !! 0;
            my $rd = $r ?? ($r<delegated> // 0) !! 0;
            @why.push: "$at: $rd statement(s) delegated, annotated $ad" if $ad != $rd;
        }
    }

    # the run logs
    my ($e1, $b1) = read-runlog($on-log);
    my ($e2, $b2) = read-runlog($off-log);
    my %ev-on = $e1; my %bodies-on = $b1; my %ev-off = $e2; my %bodies-off = $b2;
    my %refused  = %d<expect>.grep(*[0] eq 'refused').map({ .[1] => True });
    my %declined = %d<expect>.grep(*[0] eq 'declined').map({ .[1] => True });
    for @modules.map(*.key) -> $mod {
        my $natives = (%rep{$mod} // {}).values.grep({ ($_<routine> // '') eq 'native' }).elems;
        next unless $natives;
        my $ev = %ev-on{$mod} // 'nothing';
        if %refused{$mod} {
            @why.push: "$mod: expected the binary to refuse its table; it said '$ev'" unless $ev eq 'refused';
        }
        else {
            @why.push: "$mod: the binary did not attach its native bodies (it said '$ev')" unless $ev eq 'attached';
        }
        @why.push: "$mod: RAKUPP_NO_AOT=1 did not disable its native bodies" unless (%ev-off{$mod} // '') eq 'disabled';
        for (%rep{$mod} // {}).kv -> $ln, $r {
            next unless ($r<routine> // '') eq 'native';
            my $routine = $r<who>.words[1];
            my ($in, $dec) = (%bodies-on{"$mod\t$ln"} // [0, 0])[0, 1];
            my $at = "$mod line $ln ({$r<who>})";
            if %refused{$mod} {
                @why.push: "$at: entered $in times although the table was refused" if $in;
            }
            elsif %declined{$routine} {
                @why.push: "$at: expected to decline; entered $in, declined $dec" unless $in == 0 && $dec > 0;
            }
            else {
                @why.push: "$at: annotated native, but its body was never entered" ~ ($dec ?? " (declined $dec times)" !! '')
                    unless $in > 0 && $dec == 0;
            }
        }
    }
    my $leaked = %bodies-off.values.grep({ .[0] > 0 }).elems;
    @why.push: "RAKUPP_NO_AOT=1: $leaked native bodies were entered anyway" if $leaked;
    @why
}

my ($pass, $fail) = 0, 0;
my $n = 0;
for @cases -> $case {
    $n++;
    my @why = check-case($case);
    if @why { $fail++; say "not ok $n - {$case.basename}"; say "    # $_" for @why }
    else    { $pass++; say "ok $n - {$case.basename}" }
}
say "1..$n";
say "# t/aot: $pass of $n cases pass" ~ ($keep ?? " (work files kept in $tmp)" !! '');
exit $fail ?? 1 !! 0;
