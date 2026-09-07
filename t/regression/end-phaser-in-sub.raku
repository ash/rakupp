# Regression: issue #70 — an END phaser written inside a sub ran INLINE, at the
# point the statement was reached during the call, instead of at program end.
# `sub temp-dir { $dir.mkdir; END rm-rf($dir); $dir }` deleted the directory the
# instant it was created. Only a TOP-LEVEL END was deferred.
#
# An END is a COMPILE-time registration in Rakudo: it runs once, at exit, in
# reverse source order, wherever it is written — so one in a sub that is never
# called still runs, and one in a sub called three times still runs once, in the
# scope of the LAST entry into the block that holds it. Every expectation below
# was read off Rakudo 2026.07.
# Contract: exit 0 + last line PASS.
my @fail;

# Phasers that run at program END can only be observed from OUTSIDE the process,
# so each case is a child run of this same binary.
sub out(Str $prog --> Str) {
    my $r = run($*EXECUTABLE.absolute, '-e', $prog, :out, :err);
    my $o = $r.out.slurp(:close);
    $r.err.slurp(:close);
    $o
}
sub check(Str $desc, Str $prog, Str $want) {
    my $got = out($prog);
    @fail.push("$desc: got '$got', want '$want'") unless $got eq $want;
}

# 1. the issue's own repro: the END defers past the rest of the mainline
check 'END in a sub defers',
      'sub f { print "f "; END print "cleanup " }; f; print "after "',
      'f after cleanup ';

# 2. registration is not reached-at-run-time: a never-called sub's END runs
check 'END in an uncalled sub still runs',
      'sub f { print "f " }; END print "cleanup "; print "main "',
      'main cleanup ';
check 'END inside an uncalled sub still runs',
      'sub f { print "f "; END print "cleanup " }; print "main "',
      'main cleanup ';

# 3. ONE registration, however many calls — and it runs in the last call's scope
check 'END in a sub called three times runs once, with the last scope',
      'sub f($n) { END print "end$n " }; f(1); f(2); f(3); print "main "',
      'main end3 ';

# 4. reverse SOURCE order across depths: a nested END takes its place among the
#    mainline's own, rather than forming a group of its own
check 'nested and top-level ENDs share one reverse-source order',
      'END print "a "; sub f { END print "b " }; f; END print "c "; print "main "',
      'main c b a ';

# 5. a loop body's END: once, in the last iteration's scope
check 'END in a loop body runs once',
      'for 1..3 -> $i { END print "end$i " }; print "main "',
      'main end3 ';

# 6. the scope is captured on block ENTRY, not where the phaser is written —
#    the second call returns before reaching it and still wins
check 'the captured scope is the last ENTRY',
      'sub f($n) { return if $n == 2; END print "n=$n " }; f(1); f(2); print "main "',
      'main n=2 ';

# 7. the BLOCK form defers exactly as the statement form does (issue #70 named
#    both, and the two take different paths through the parser)
check 'the block form of END in a sub defers',
      'sub f { print "f "; END { print "cleanup " } }; f; print "after "',
      'f after cleanup ';

# 8. methods defer too (their body has its own statement runner)
check 'END in a method defers',
      'class C { method m { print "m "; END print "end " } }; C.new.m; print "main "',
      'm main end ';

# 9. a trailing END is the block's last statement and yields Nil, so the routine
#    returns Nil — it must not return the phaser's inline value
check 'a routine ending in END returns Nil',
      'sub f { 42; END 1 }; print f().raku',
      'Nil';

# 10. `END my $x = …` DECLARES in the enclosing scope, and the slot exists from
#    block entry — naming it must not throw X::Undeclared
check 'a statement-form END declares in the enclosing scope',
      'sub f { END my $z = 7; print "[$z] " }; f; print "main "',
      '[] main ';

# 11. an END in EVAL'd code inside a sub belongs to the program's end
check 'an EVAL END inside a sub defers',
      'sub f { EVAL q[END print "eval-end "]; print "f " }; f; print "main "',
      'f main eval-end ';

# 12. The scope is captured on entry of EVERY block that holds the phaser, not
#     just its own: Rakudo flattens an `if` body into its routine's frame, so
#     the call that never entered the branch still supplies the scope.
check 'a skipped inner block does not pin the scope',
      'sub f($n) { if $n == 1 { END print "n=$n " } }; f(1); f(2); print "main "',
      'main n=2 ';

# 13. A block that never ran at all has none of the containers Rakudo's
#     compile-time pad would carry — the phaser reads them as undefined rather
#     than dying with X::Undeclared and being swallowed
check 'an END in an unentered block reads its lexicals as undefined',
      'sub f($n) { my $x = $n * 2; END print "[$x] " }; print "main "',
      'main [] ';

# 14. EVAL is not compile time: its ENDs register when it RUNS, after every
#     compiled one, so they run before them
check 'an EVAL END outranks a mainline END written after it',
      'sub f { EVAL q[END print "e "] }; f; print "main "; END print "m "',
      'main e m ';

# 15. A module's ENDs sort at the `use` that loaded it — so with `use` at the
#     top (the usual layout) the mainline's own END runs FIRST, and the
#     module's after it. The opposite shape, `use` below the mainline END, is
#     pinned by t/regression/open-modes-bind-end-shift.raku; both are Rakudo's.
{
    my $dir = $*TMPDIR.add("rakupp-end70-{$*PID}");
    $dir.add('lib').mkdir;
    $dir.add('lib/EndAtUse.rakumod').spurt(q:to/M/);
        unit module EndAtUse;
        our sub helper() is export { END print "mod-sub " }
        END print "mod-top ";
        M
    my $prog = 'use EndAtUse; helper(); print "main "; END print "mine "';
    my $r = run($*EXECUTABLE.absolute, "-I{$dir.add('lib')}", '-e', $prog, :out, :err);
    my $got = $r.out.slurp(:close);
    $r.err.slurp(:close);
    @fail.push("module END order: got '$got', want 'main mine mod-top mod-sub '")
        unless $got eq 'main mine mod-top mod-sub ';
    # the module file goes: a per-PID path that never exists again still
    # strands a precomp entry keyed on it. The directory stays — Rakudo leaves
    # its own precomp store inside, and rmdir on a non-empty one throws.
    $dir.add('lib/EndAtUse.rakumod').unlink;
}

if @fail {
    note "FAIL: $_" for @fail;
    die "end-phaser-in-sub: {+@fail} failure(s)";
}
say "PASS";
