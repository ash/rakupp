# Regression: code that runs while the unit compiles — a constant's initializer,
# a BEGIN block, and a routine either of them calls — may not call a routine
# declared only further down; that is "Undeclared routine", X::Undeclared::Symbols,
# as in Rakudo (issue #196).
#
# rakupp declares every routine of a scope before the scope runs, and evaluates
# a constant where it stands, so `constant A = f(); sub f { … }` compiled and
# ran, and a routine that read a constant declared later still read Any. The
# call check (src/CallCheck.cpp) finds such a call before anything runs now,
# with Rakudo's reach: every name the initializer or a BEGIN body holds outside
# a nested block, run or not, and the names in the body of a routine declared
# above that one of them surely calls.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refused($code, $desc) {
    my $ok = try { EVAL $code; True };
    ck so($ok), False, "refused: $desc";
    ck $!.^name, 'X::Undeclared::Symbols', "…as an undeclared routine: $desc";
}

refused q:to/END/, 'the issue';
    constant A = f('x');
    sub f($s) { $s ~ B }
    constant B = 'y';
    END
refused 'constant A = f(); sub f { 42 }', 'a call';
refused 'constant A = 1 || f(); sub f { 42 }', 'a call that a thunk never runs';
refused 'constant A = 0 ?? f() !! 1; sub f { 42 }', 'a call in a branch never taken';
refused 'constant A = try f(); sub f { 42 }', 'a call inside try';
refused 'sub g { f() }; constant A = g(); sub f { 42 }', 'a routine declared above calling one below';
refused 'sub g { h() }; sub h { f() }; constant A = g(); sub f { 42 }', '…two deep';
refused 'sub g { return 5; f() }; constant A = g(); sub f { 42 }', '…past a return';
refused 'BEGIN { 1 || g() }; sub g { 7 }', 'a BEGIN block';
refused 'BEGIN my $x = g(); sub g { 7 }', 'a BEGIN statement';
refused 'class C { constant X = helper(); sub helper { 3 } }', 'a constant in a class';
refused 'sub s { constant X = f(); X }; sub f { 1 }', 'a constant in a routine';
refused 'constant A = f(); our sub f { 1 }', 'an our sub';
ck so(try { EVAL 'constant A = &f; sub f { 1 }'; True }), False, 'refused: a reference to one';

# the issue's program as a file: nothing runs
{
    my $file = $*TMPDIR.add("rakupp-issue-196-$*PID.raku");
    $file.spurt: "say 'ran';\nconstant A = f('x');\nsub f(\$s) \{ \$s ~ B \}\nconstant B = 'y';\nsay A;\n";
    my $p = run $*EXECUTABLE, $file, :out, :err;
    ck $p.out.slurp(:close), '', 'the issue\'s file prints nothing';
    ck $p.err.slurp(:close).contains('Undeclared routine'), True, '…it is an undeclared routine';
    ck $p.exitcode, 1, '…and exits 1';
    $file.unlink;
}

# …and what runs later, or was declared in time, is not
ck EVAL('sub f { 42 }; constant A = f(); A'), 42, 'a routine declared above';
ck EVAL('constant A = [1, 2].map({ f($_) }); sub f($x) { $x * 2 }; A').List, (2, 4), 'a block run later';
ck EVAL('constant A = do { if 0 { f() }; 1 }; sub f { 2 }; A'), 1, 'a nested block never run';
ck EVAL('multi f(Int) { 1 }; constant A = f(1); multi f(Str) { 2 }; A'), 1, 'a multi with a candidate above';
ck EVAL('my $x = f(); sub f { 42 }; $x'), 42, 'a call at run time';
ck EVAL('sub g { f() }; sub f { 42 }; constant A = g(); A'), 42, 'a routine calling one declared before the constant';

say 'PASS';
