# A store into a `$` variable in compiled code does what `=` does interpreted:
# a Nil resets the container to its default, and a TYPED container checks what
# it is given. Compiled code did neither. `my $r = try die "x"` held Nil, not
# Any (and `my @a = Nil` was [], not [Any]); `my Int $x; $x = "a"` stored the
# Str without a word, and `$x = Nil` left Nil in an Int container. Natively a
# store checks the type the parser resolved the name to (VarExpr::readType),
# by the interpreter's own rule (enforceTypedValue); in a bundle a module-mode
# routine's store into the interpreter's variables asks that variable's
# declaration (rtOuterScalarStore).
#
# Each program runs interpreted and compiled, with -O and without; the answers
# are Rakudo 2026.09's. The second program bundles (a `subset`), so its routines
# take the module-mode path.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-scalar-store-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $common = q:to/END/;
    my $a = Nil; say $a.^name;
    sub n() { Nil }
    my $b = n(); say $b.^name;
    my $c = try die "x"; say $c.^name;
    sub g() { my $r = try die "q"; $r.^name }
    say g();
    sub h() { my $r = Nil; my $s = n(); my $t; $t = Nil; my $u = 3; $u = n(); "{$r.^name} {$s.^name} {$t.^name} {$u.^name}" }
    say h();
    sub k() { my Int $r = try die "q"; my Str $s = Nil; "{$r.^name} {$s.^name}" }
    say k();
    my @l = try die "x"; say @l.raku;
    sub t() { my Int $x = 5; $x = Nil; my $r = $x.^name; try { $x = "a" }; $r ~ " " ~ ($! ?? $!.^name !! "no error") ~ " " ~ $x.^name }
    say t();
    class Animal {}; class Dog is Animal {};
    sub cl() { my Animal $a = Dog.new; try { $a = 5 }; "{$a.^name} {$!.^name}" }
    say cl();
    sub ip(Int $x is copy) { $x = Nil; my $r = $x.^name; try { $x = "s" }; "$r {$!.^name}" }
    say ip(4);
    sub lp() { my Int $s = 0; for ^1000 { $s = $s + $_ }; $s }
    say lp();
    my Int $g = 3; sub sg() { $g = Nil; $g.^name }; say sg();
    sub fa() { my Int $f; try { $f = "x".Int }; $!.^name }
    say fa();
    sub mu() { my Mu $m = Nil; $m.^name }
    say mu();
    END
my $want = "Any\nAny\nAny\nAny\nAny Any Any Any\nInt Str\n[Any]\nInt X::TypeCheck::Assignment Int\n" ~
           "Dog X::TypeCheck::Assignment\nInt X::TypeCheck::Assignment\n499500\nInt\nX::Str::Numeric\nMu";
my @progs =
    ('native',  $common, $want),
    ('bundled', $common ~ "subset Pos of Int where * > 0;\n" ~
                "sub sp() \{ my Pos \$p = 3; try \{ \$p = -1 }; \"\{\$p} \{\$!.^name}\" }\nsay sp();\n",
                $want ~ "\n3 X::TypeCheck::Assignment");

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
