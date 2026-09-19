#!/usr/bin/env raku
# Generated coverage for the `-O` unboxed loop lanes (UNBOX-PLAN.md).
#
# This gate exists because the ordinary corpus does not exercise the pass. Of
# the 701 programs in `t/regression/` and `examples/`, ONE carries a lane: a
# test file's loop body prints on every iteration, and a lane refuses any loop
# that calls something. So the cases have to be generated, and two of the seven
# bugs in the pass were found only this way — a `Num` between 0 and 1 used as a
# truth value, and one C-style loop nested inside another. Neither shape exists
# anywhere in this repository's test corpus.
#
# Two matrices, both small and both exhaustive over what the lane admits:
#
#   operators   every binary, compound-assignment and unary operator, against
#               every combination of Int and Num operands
#   shapes      every loop kind (while / until / C-style / for over an
#               inclusive range / for over an exclusive one) against every body
#               shape (plain, next, last, both, if-else, elsif, postfix unless,
#               a nested block, a float temporary, an integer overflow), each
#               of them also one level nested inside itself
#
# Every generated program is run interpreted and compiled with `-O`, and the two
# are compared. The interpreter is the oracle. A program that fails to COMPILE
# is a failure too, not a skip: the lane emits C++, and emitting C++ that does
# not build takes the whole program's native build down with it.
#
#   rakupp t/exe/fuzz.raku              # both matrices
#   rakupp t/exe/fuzz.raku --ops        # operators only
#   rakupp t/exe/fuzz.raku --shapes     # shapes only
#   rakupp t/exe/fuzz.raku --keep       # leave the generated programs on disk
#
# Exit 1 on any mismatch or compile failure.

my $ROOT = $*PROGRAM.parent.parent.parent;
my $rakupp = $*EXECUTABLE;
my @args = @*ARGS;
my $onlyOps    = so @args.grep('--ops');
my $onlyShapes = so @args.grep('--shapes');
my $keep       = so @args.grep('--keep');

# Run a command with a wall-clock bound. Returns (Nil, …) if it had to be
# killed, which is how a non-terminating generated program is caught rather
# than waited on forever.
my $LIMIT = 20;
sub bounded(*@cmd) {
    my $p = Proc::Async.new(|@cmd);
    my ($o, $e) = '', '';
    $p.stdout.tap({ $o ~= $_ });
    $p.stderr.tap({ $e ~= $_ });
    my $done = $p.start;
    my $killed = False;
    await Promise.anyof($done, Promise.in($LIMIT));
    unless $done {
        $killed = True;
        $p.kill: SIGKILL;
        try await $done;
    }
    $killed ?? (Nil, $o, $e) !! ((try await $done).exitcode, $o, $e)
}

my $tmp = $*TMPDIR.add("rakupp-lane-fuzz-$*PID");
$tmp.mkdir;
END { if !$keep && $tmp.e { for $tmp.dir { .unlink }; $tmp.rmdir } }

my @progs;   # (name => source)

# ---- the operator matrix ---------------------------------------------------
unless $onlyShapes {
    my @bin = « + - * / % '<' '<=' '>' '>=' == != '&&' '||' »;
    my @cmp = '+=', '-=', '*=', '/=', '%=';
    my @un  = '-', '+', '!';
    my %v = I => ('3', '7'), F => ('3e0', '7e0');
    for @bin -> $op {
        for <I F> -> $lt { for <I F> -> $rt {
            @progs.push: "op-{$op.subst(/\W/, {'_' x 1}, :g)}-$lt$rt" =>
                qq:to/END/;
                my \$a = %v{$lt}[0]; my \$b = %v{$rt}[1]; my \$acc = 0; my \$i = 0;
                while \$i < 4 \{ my \$r = \$a $op \$b; \$acc = \$acc + (\$r ?? 1 !! 0); \$i = \$i + 1 }
                say "\$acc";
                END
        } }
    }
    for @cmp -> $op {
        for <I F> -> $lt { for <I F> -> $rt {
            @progs.push: "cmp-{$op.subst(/\W/, '_', :g)}-$lt$rt" =>
                qq:to/END/;
                my \$a = %v{$lt}[0]; my \$b = %v{$rt}[1]; my \$i = 0;
                while \$i < 4 \{ \$a $op \$b; \$i = \$i + 1 }
                say "\$a";
                END
        } }
    }
    for @un -> $op {
        for <I F> -> $lt {
            @progs.push: "un-{$op.subst(/\W/, '_', :g)}-$lt" =>
                qq:to/END/;
                my \$a = %v{$lt}[0]; my \$acc = 0; my \$i = 0;
                while \$i < 4 \{ my \$r = $op\$a; \$acc = \$acc + (\$r ?? 1 !! 0); \$i = \$i + 1 }
                say "\$acc";
                END
        }
    }
    # a Num in boolean context, which is bug 5: (long long)0.25 is 0
    for '25e-2', '0e0', '3', '0' -> $v {
        @progs.push: "truth-{$v.subst(/\W/, '_', :g)}" => qq:to/END/;
            my \$a = $v; my \$acc = 0; my \$i = 0;
            while \$i < 4 \{ if \$a \{ \$acc = \$acc + 1 }; \$acc = \$acc + (\$a ?? 10 !! 0); \$i = \$i + 1 }
            say "\$acc";
            END
    }
}

# ---- the shape matrix ------------------------------------------------------
unless $onlyOps {
    # The counter is advanced at the TOP for `while` and `until`, and the body
    # reads a copy. Advancing it at the bottom — the obvious way to write it —
    # makes any body containing a `next` an INFINITE LOOP, because `next` skips
    # the increment. That is correct Raku and a broken test: the generator hung
    # three processes for half an hour before this was noticed, and a generator
    # that can hang is worse than no generator, since a hang looks exactly like
    # a slow compile.
    sub mkloop($kind, $v, $body) {
        given $kind {
            when 'while' { "my \$c$v = -1;\nwhile \$c$v < 3 \{\n    \$c$v = \$c$v + 1;\n    my \$$v = \$c$v;\n$body\n}" }
            when 'until' { "my \$c$v = -1;\nuntil \$c$v >= 3 \{\n    \$c$v = \$c$v + 1;\n    my \$$v = \$c$v;\n$body\n}" }
            when 'cloop' { "loop (my \$$v = 0; \$$v < 4; \$$v++) \{\n$body\n}" }
            when 'forin' { "for 0 .. 3 -> \$$v \{\n$body\n}" }
            when 'forex' { "for 0 ..^ 4 -> \$$v \{\n$body\n}" }
        }
    }
    my %bodies =
        plain  => '$t = $t + $V;',
        nextif => 'next if $V == 2;' ~ "\n    " ~ '$t = $t + $V;',
        lastif => 'last if $V == 3;' ~ "\n    " ~ '$t = $t + $V;',
        both   => 'next if $V == 1;' ~ "\n    " ~ 'last if $V == 3;' ~ "\n    " ~ '$t = $t + $V;',
        ifelse => 'if $V % 2 { $t = $t + 1 } else { $t = $t + 100 }',
        elsif  => 'if $V == 0 { $t = $t + 1 } elsif $V == 1 { $t = $t + 10 } else { $t = $t + 1000 }',
        unless => '$t = $t + $V unless $V == 2;',
        block  => '{ my $qN = $V * 2; $t = $t + $qN; }',
        float  => 'my $dN = $V * 1e0 + 5e-1; $t = $t + ($dN > 2e0 ?? 1 !! 0);',
        ovf    => 'my $hN = 9223372036854775800; $hN = $hN + $V * 1000; $t = $t + ($hN > 0 ?? 1 !! 0);',
        ;
    for <while until cloop forin forex> -> $k {
        for %bodies.keys.sort -> $bn {
            my $b = %bodies{$bn};
            my $inner = $b.subst('$V', '$a', :g).subst('N', '1', :g).lines.map({ '    ' ~ $_ }).join("\n");
            @progs.push: "$k-{$bn}" => "my \$t = 0;\n{mkloop($k, 'a', $inner)}\nsay \"\$t\";\n";
            # ...and one level deeper, with DISTINCT names: a nested loop that
            # reuses a name is miscompiled by the backend itself, with -O and
            # without, so it belongs in its own finding and not in this gate.
            my $deep = $b.subst('$V', '$b', :g).subst('N', '2', :g).lines.map({ '    ' ~ $_ }).join("\n");
            my $mid  = mkloop($k, 'b', $deep).lines.map({ '    ' ~ $_ }).join("\n");
            @progs.push: "$k-{$bn}-nested" => "my \$t = 0;\n{mkloop($k, 'a', $mid)}\nsay \"\$t\";\n";
        }
    }
}

# ---- run them --------------------------------------------------------------
my ($agree, $mismatch, $cfail, $laned) = 0, 0, 0, 0;
my @bad;
my $bin = $tmp.add('a.out');

for @progs -> (:key($name), :value($src)) {
    my $f = $tmp.add("$name.raku");
    $f.spurt: $src;

    my $cpp = run $rakupp.Str, '--cpp', '-O', $f.Str, :out, :err;
    my $gen = $cpp.out.slurp(:close); $cpp.err.slurp(:close);
    $laned++ if $gen.contains('unboxed loop lane');

    my ($irc, $want, $ierr) = bounded($rakupp.Str, $f.Str);
    unless $irc.defined {
        # The GENERATOR produced a program that does not terminate. That is a
        # bug in this file, not in the compiler, and it must be loud: an earlier
        # version emitted `while` bodies whose `next` skipped the increment, and
        # three of them span for half an hour looking exactly like a slow build.
        $cfail++; @bad.push: $name;
        say "not ok - $name: the generated program does not terminate (a bug in this gate)";
        say "      source: {$src.lines.join(' / ')}";
        next;
    }

    $bin.unlink if $bin.e;
    my $c = run $rakupp.Str, '--exe', '-O', '-q', $f.Str, '-o', $bin.Str, :out, :err;
    my $cerr = $c.err.slurp(:close); $c.out.slurp(:close);
    unless $c.exitcode == 0 && $bin.e {
        $cfail++;
        @bad.push: $name;
        say "not ok - $name: the emitted C++ did not compile";
        say "      " ~ $cerr.lines.grep(*.contains('error')).head(1).join;
        next;
    }
    my ($grc, $got, $gerr) = bounded($bin.Str);
    unless $grc.defined {
        $mismatch++; @bad.push: $name;
        say "not ok - $name: the COMPILED program does not terminate where the interpreter does";
        say "      source: {$src.lines.join(' / ')}";
        next;
    }
    if $got eq $want { $agree++ }
    else {
        $mismatch++;
        @bad.push: $name;
        say "not ok - $name: -O disagrees with the interpreter";
        say "      interpreted: {$want.trim}";
        say "      -O         : {$got.trim}";
        say "      source     : {$src.lines.join(' / ')}";
    }
}

say "";
say "programs        {+@progs}";
say "agreeing        $agree";
say "mismatching     $mismatch";
say "not compiling   $cfail";
say "carrying a lane $laned  (the rest exercise the REFUSAL path, which is also a result)";
say "generated in    $tmp" if $keep;
exit @bad ?? 1 !! 0;
