# Which lexical slots another thread may reach — PARALLEL-SCALING-PLAN P1.
#
# While workers are live, a `my` that no other thread can reach is read and
# written without the stripe lock; one another thread CAN reach keeps it, or a
# worker storing into it could tear the owner's copy. The verdict is made per
# slot when a body is first resolved, and RAKUPP_SLOT_TRACE=1 prints it:
#
#     slot: work $s private
#     slot: <mainline> $M shared — mentioned by sub work (line 3)
#
# One snippet per route by which another thread can reach a slot (the table in
# docs/dev/plans/PARALLEL-SCALING-PLAN.md), each checked for the slots it must
# mark shared and the ones it must leave private. A route that stopped marking
# would make t/stress's private-* programs crash; this catches it without a
# race. Each snippet runs in a child process: two of them switch the feature
# off for the whole program.

unless $*RAKU.compiler.name eq 'Raku++' {
    note 'slot-sharing: not rakupp, nothing to trace';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("slot-sharing-$*PID-{(^1e9).pick}");
mkdir $work;
LEAVE { .unlink for $work.dir; rmdir $work }

my $fails = 0;
sub trace(Str $code) {
    state $n = 0;
    my $f = $work.add("s{$n++}.raku");
    $f.spurt($code);
    my %env = %*ENV, RAKUPP_SLOT_TRACE => '1';
    %env<RAKUPP_PRIVATE_SLOTS>:delete;
    %env<RAKUPP_GIL>:delete;
    my $p = run $*EXECUTABLE, $f, :out, :err, :%env;
    $p.out.slurp(:close);
    $p.err.slurp(:close).lines.grep(*.starts-with('slot: ')).List
}
# `$want` is 'shared' or 'private'; `$why` (optional) must appear in the reason
sub verdict(@lines, Str $owner, Str $var, Str $want, Str $why = '', Str :$desc!) {
    my $line = @lines.first({ .starts-with("slot: $owner $var ") });
    my $ok = $line.defined && $line.substr("slot: $owner $var ".chars).starts-with($want)
             && (!$why || $line.contains($why));
    unless $ok {
        $fails++;
        say "FAIL: $desc — $owner $var: want {$want}{$why ?? " ($why)" !! ''}, got {$line // 'no line'}";
        note "  $_" for @lines;
    }
}

# a routine's own accumulators: nothing else can reach them (a Str one, so
# the routine runs interpreted: a routine compiled whole has no pads to trace)
my @t = trace(q:to/END/);
    sub work($m) { my $s = ''; my $x = 1; for ^$m { $x = $x * 3 % 7; $s ~= $x }; $s }
    say work(10);
    END
verdict(@t, 'work', '$s', 'private', :desc('a loop over locals'));
verdict(@t, 'work', '$x', 'private', :desc('a loop over locals'));
verdict(@t, 'work', '$m', 'private', :desc('a parameter'));

# 1. a closure: `start`, a block handed to map
@t = trace(q:to/END/);
    sub f() { my $v = 0; my $u = 0; my $p = start { $v++ }; await $p; $u + (1, 2).map({ $_ + $u }).sum }
    say f();
    END
verdict(@t, 'f', '$v', 'shared', 'mentioned by a block', :desc('a start block'));
verdict(@t, 'f', '$u', 'shared', 'mentioned by a block', :desc('a map block'));

# 2. a named sub declared inside, which a worker may call
@t = trace(q:to/END/);
    sub f() { my $v = 0; my $w = 0; sub g() { $v++ }; g(); $w++; $v }
    say f();
    END
verdict(@t, 'f', '$v', 'shared', 'sub g', :desc('a nested sub'));
verdict(@t, 'f', '$w', 'private', :desc('beside a nested sub'));

# a curried `*` keeps the expression and evaluates it later, in this scope
@t = trace(q:to/END/);
    sub f() { my $k = 2; my $j = 3; my &c = * + $k; c($j) }
    say f();
    END
verdict(@t, 'f', '$k', 'shared', 'curried', :desc('a WhateverCode'));

# `xx` thunks its left side (lazily for `*` and Inf)
@t = trace(q:to/END/);
    sub f() { my $k = 2; my $n = 3; my @l = $k xx $n; @l.elems }
    say f();
    END
verdict(@t, 'f', '$k', 'shared', 'xx', :desc('the left side of xx'));
verdict(@t, 'f', '$n', 'private', :desc('the count of xx'));

# a regex's interpolation is compiled when it runs, from its text
@t = trace(q:to/END/);
    sub f() { my $k = 'b'; my $r = 0; $r++ if 'abc' ~~ / $k /; $r }
    say f();
    END
verdict(@t, 'f', '$k', 'shared', 'regex', :desc('a regex interpolation'));
verdict(@t, 'f', '$r', 'private', :desc('beside a regex'));

# a phaser may run elsewhere (END, the supply phasers)
@t = trace(q:to/END/);
    sub f() { my $a = 1; my $b = 2; LEAVE { $a }; $a + $b }
    say f();
    END
verdict(@t, 'f', '$a', 'shared', 'phaser', :desc('a phaser'));
verdict(@t, 'f', '$b', 'private', :desc('beside a phaser'));

# 10. a pseudo-stash hands out the whole scope
@t = trace(q:to/END/);
    sub f() { my $a = 1; MY::.keys.elems }
    say f();
    END
verdict(@t, 'f', '$a', 'shared', 'pseudo-stash', :desc('MY::'));

# 11. EVAL reaches every name in scope
@t = trace(q:to/END/);
    sub f() { my $a = 1; my $b = 2; EVAL '$a + $b' }
    say f();
    END
verdict(@t, 'f', '$a', 'shared', 'EVAL', :desc('EVAL'));
verdict(@t, 'f', '$b', 'shared', 'EVAL', :desc('EVAL'));

# 12. a symbolic name: a literal names one slot, a computed one any
@t = trace(q:to/END/);
    sub f() { my $a = 1; my $b = 2; ::('$a') + $b }
    sub g($n) { my $c = 1; ::($n) }
    say f(), g('$c');
    END
verdict(@t, 'f', '$a', 'shared', 'by name', :desc('a literal symbolic name'));
verdict(@t, 'f', '$b', 'private', :desc('beside a literal symbolic name'));
verdict(@t, 'g', '$c', 'shared', 'run time', :desc('a computed symbolic name'));

# the statement form of `hyper for` is its own block, reaching outer names by name
@t = trace(q:to/END/);
    sub f() { my $v = 0; hyper for ^4 { $v = $_ }; $v }
    say f();
    END
verdict(@t, 'f', '$v', 'shared', :desc('a hyper for body'));

# 13. the dynamic-scope pseudo-packages reach frames that are not lexically
# around them: private slots are off for the whole program
for q[sub f() { my $a = 1; g() }; sub g() { CALLER::<$a> }; say f();],
    q[sub f() { my $a = 1; my &e = &EVAL; e('$a') }; say f();] -> $code {
    @t = trace($code);
    unless @t.first(*.contains('private slots off for the whole program')) {
        $fails++;
        say "FAIL: the program-wide switch for: $code";
        note "  $_" for @t;
    }
}

# 16. a module's unit scope chains to the mainline's frame: a mainline slot
# the module names escapes when the module loads
my $lib = $work.add('lib');
mkdir $lib;
$lib.add('SlotPeek.rakumod').spurt(q:to/END/);
    unit module SlotPeek;
    sub peek() is export { ::('$secret') }
    END
@t = trace(qq:to/END/);
    use lib '{$lib.Str}';
    my \$secret = 42;
    my \$other = 1;
    use SlotPeek;
    say peek() + \$other;
    END
unless @t.first(*.starts-with('slot: <mainline> $secret escaped')) {
    $fails++;
    say 'FAIL: a mainline slot a module names escapes';
    note "  $_" for @t;
}
.unlink for $lib.dir;
rmdir $lib;

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit($fails ?? 1 !! 0);
