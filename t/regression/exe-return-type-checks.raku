# A compiled routine applies its `--> T` on every way out, as the interpreter
# and Rakudo do. Compiled, nothing checked it: `sub r(--> Int) { "x" }`
# answered "x", `--> Str()` did not coerce, `--> Int:D` passed Int, and a
# `return` from a block (`.map({ return "blk" … })`) went past the check.
# Codegen::retWrap wraps the body, `return`s and its ReturnEx catch included,
# and checks what comes out by the interpreter's checkRetType (a value of a
# core type passes inline). Top-level, lexical and anonymous subs, multi
# candidates, methods and pointy blocks. And `-O` compiles a `--> Int` sub of
# no parameters that is an int kernel: its kernel guard named `__a`, which the
# direct overload does not have, and the C++ did not compile.
#
# Each program runs interpreted and compiled, -O and plain; the answers are
# Rakudo 2026.09's.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-ret-types-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    sub r1(--> Int) { "x" }
    sub r2(--> Num) { 3 }
    sub r3() returns Int { "y" }
    sub r4(--> Str()) { 42 }
    sub r5(--> Int:D) { Int }
    sub r6(--> Nil) { 5 }
    sub r7(--> 42) { 5 }
    sub r8($x --> Int) { return $x if $x > 0; "neg" }
    sub r9(--> Int) { fail "bad" }
    sub r10(--> Int) { Nil }
    sub r11(--> Int) { (1, 2).map({ return "blk" if $_ == 2; $_ }).eager; 0 }
    my sub r12(--> Int) { "lex" }
    my &r13 = sub (--> Int) { "anon" };
    class C { method m(--> Int) { "meth" }; method ok(--> Str) { "fine" } }
    sub r14(--> Int) { 7 }
    sub r15(--> Int) { my $r = 9; $r }
    for &r1, &r2, &r3, &r4, &r5, &r6, &r7, { r8(1) }, { r8(-1) }, &r9, &r10, &r11, &r12, &r13, { C.m }, { C.ok }, &r14, &r15 -> &f {
        my $v = try f();
        say $v.defined ?? "{$v.raku} {$v.^name}" !! ($! ?? $!.^name !! "undef {$v.^name}");
    }
    class A {}; class B is A {}
    multi sub m(Int $x --> Str) { $x }
    multi sub m(Str $x --> Str) { $x }
    sub cA(--> A) { B.new }
    sub cAD(--> A:D) { A }
    sub cB(--> B) { A.new }
    sub cs(--> Str(Int)) { 5 }
    sub cs2(--> Str(Int)) { "already" }
    sub cs3(--> Str(Int)) { 1.5 }
    sub bo(--> Bool) { 1 }
    sub po(--> Positional) { (1, 2) }
    sub po2(--> Positional) { 5 }
    sub rn(--> Rat) { 0.5 }
    sub ok3(--> Int) { 3 }
    my &pb = -> $x --> Int { $x };
    sub viaclosure(--> Int) { my @a = 1, 2; @a.map(-> $v { return "c" if $v == 2; $v }).eager; 1 }
    for { m(1) }, { m("s") }, &cA, &cAD, &cB, &cs, &cs2, &cs3, &bo, &po, &po2, &rn, &ok3, { pb(3) }, { pb("x") }, &viaclosure -> &f {
        my $v = try f();
        say $v.defined ?? "{$v.raku} {$v.^name}" !! ($! ?? $!.^name !! "undef {$v.^name}");
    }
    END
my $want = q:to/END/.trim;
    X::TypeCheck::Return
    X::TypeCheck::Return
    X::TypeCheck::Return
    "42" Str
    X::TypeCheck::Return
    undef Any
    42 Int
    1 Int
    X::TypeCheck::Return
    X::AdHoc
    undef Any
    X::TypeCheck::Return
    X::TypeCheck::Return
    X::TypeCheck::Return
    X::TypeCheck::Return
    "fine" Str
    7 Int
    9 Int
    X::TypeCheck::Return
    "s" Str
    B.new B
    X::TypeCheck::Return
    X::TypeCheck::Return
    "5" Str
    "already" Str
    X::TypeCheck::Return
    X::TypeCheck::Return
    $(1, 2) List
    X::TypeCheck::Return
    0.5 Rat
    3 Int
    3 Int
    X::TypeCheck::Return
    X::TypeCheck::Return
    END
my @progs = ('returns', $src, $want),;

for @progs -> ($name, $src, $expect) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my $i = run($bin, $f.absolute, :out, :err);
    my $iout = $i.out.slurp(:close).trim; $i.err.slurp(:close);
    @fail.push("$name interpreted: {$iout.raku}") unless $iout eq $expect;
    for ('-O', 'plain') -> $mode {
        my $exe = $dir.add("$name-$mode.bin");
        my @cmd = $bin, '--exe', |($mode eq '-O' ?? '-O' !! ()), $f.absolute, '-o', $exe.absolute;
        my $c = run(|@cmd, :out, :err);
        $c.out.slurp(:close); $c.err.slurp(:close);
        if !$exe.e { @fail.push("$name $mode: did not compile"); next }
        my $r = run($exe.absolute, :out, :err);
        my $cout = $r.out.slurp(:close).trim; $r.err.slurp(:close);
        @fail.push("$name $mode: {$cout.raku}") unless $cout eq $expect;
    }
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
