# Regression: `--exe` could not compile ANY program that used `no strict`.
#
# Codegen emits a C++ local for every variable, so a name `no strict`
# auto-vivifies — which by definition is declared nowhere — was emitted as a
# reference to a local nobody declared, and the generated C++ failed to build:
#
#     nostrict.rakupp.gen.cpp:35:9: error: use of undeclared identifier 'v_szz'
#
# Not a CodegenError either, so it never reached the bundling fallback: `--exe`
# just failed. Now DeclCheck::findLaxVars answers which names the pragma
# auto-vivifies (the same walk that reports undeclared ones, and behind the same
# source-text backstop, so a name in that set is declared NOWHERE and can never
# collide with a local codegen does emit), and each one compiles to
# `RT.laxVarRef("$x")` — a slot in the live environment. A lax unit the check
# could not finish (EVAL and friends) throws CodegenError and bundles instead.
#
# Fixing the compiled side also settled where an auto-vivified name LIVES: the
# interpreter was defining it in whichever block wrote first, so
# `no strict; for 1..5 -> $k { $sum += $k }; say $sum` answered (Any) — a fresh
# $sum per iteration — where Rakudo answers 15. It now lands in the innermost
# enclosing package body, else the unit's own scope (Env::packageFrame).
#
# Every case asserts INTERPRETED == COMPILED == Rakudo's answer.
# Contract: exit 0 + last line PASS.

my $v = run($*EXECUTABLE, '--version', :out, :err);
my $banner = $v.out.slurp(:close); $v.err.slurp(:close);
unless $banner.contains('rakupp') {
    # only rakupp has --exe; under Rakudo there is nothing this file can compile
    note 'no-strict-exe: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("no-strict-exe-$*PID");
mkdir $work;
my @made;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

# Run CODE interpreted and again as a compiled binary; both must equal WANT.
# :native asserts the compile stayed native — the point of the fix is that these
# programs no longer have to fall back — and :bundled asserts it did fall back.
sub agree(Str $name, Str $code, Str $want, Bool :$native = False, Bool :$bundled = False) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $bin = $work.add($name ~ '-bin');
    @made.push($src.Str, $bin.Str);

    my $i = run($*EXECUTABLE, $src.Str, :out, :err);
    my $interp = $i.out.slurp(:close).lines.join('|');
    $i.err.slurp(:close);

    my $c = run($*EXECUTABLE, '--exe', '-o', $bin.Str, $src.Str, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    if $c.exitcode != 0 {
        $fails++;
        note "compile of $name failed:\n$cout";
        return;
    }
    if $native && !$cout.contains('(native)') {
        $fails++;
        note "$name was expected to compile natively, but did not:\n$cout";
    }
    if $bundled && $cout.contains('(native)') {
        $fails++;
        note "$name was expected to fall back to bundling, but compiled natively:\n$cout";
    }
    my $r = run($bin.Str, :out, :err);
    my $compiled = $r.out.slurp(:close).lines.join('|');
    $r.err.slurp(:close);

    check("$name: interpreted",     $interp,   $want);
    check("$name: compiled agrees", $compiled, $interp);
}

# 1. The report: the smallest lax program there is.
agree('scalar', q:to/END/, '5', :native);
    no strict;
    $zz = 5;
    say $zz;
    END

# 2. Each sigil brings its own empty default, so a mutator works on the slot
#    the first write created (an Any would have no .push).
agree('array-and-hash', q:to/END/, '[1 2 3 4]|3', :native);
    no strict;
    @arr = 1, 2, 3;
    @arr.push(4);
    say @arr;
    %h<a> = 1; %h<b> = 2;
    say %h<a> + %h<b>;
    END

# 3. The case that outed the interpreter: one slot for the whole loop, not one
#    per iteration. Rakudo answers 15.
agree('accumulate-in-a-loop', q:to/END/, '15', :native);
    no strict;
    for 1..5 -> $k { $sum += $k }
    say $sum;
    END

# 4. A lax name written in a sub is the same slot the mainline reads.
agree('written-in-a-sub', q:to/END/, '9', :native);
    no strict;
    sub f { $shared = 9 }
    f();
    say $shared;
    END

# 5. Lax and declared names side by side: the declared one must still compile
#    to a local (routing it through the runtime would be the mirror-image bug).
agree('lax-beside-declared', q:to/END/, '42', :native);
    no strict;
    my $declared = 10;
    $lax = 32;
    say $declared + $lax;
    END

# 6. The int fast lane works on unboxable locals; a runtime slot has to decline
#    it rather than emit `RT.laxVarRef("$i").i`.
agree('increment-in-the-int-lane', q:to/END/, '5', :native);
    no strict;
    $i = 0;
    $i++ while $i < 5;
    say $i;
    END

# 7. The pragma is lexical here too: the block's lax name is auto-vivified, the
#    declared one after it is an ordinary local.
agree('lexical-block', q:to/END/, '7|1', :native);
    { no strict; $zz = 7; say $zz; }
    my $x = 1;
    say $x;
    END

# 8. EVAL can conjure names the check never sees, so its lax-name set may be
#    short — the unit must go to the interpreter whole rather than have codegen
#    guess. (Bundled, so no `(native)`.)
agree('lax-with-eval', q:to/END/, 'eval ran|1', :bundled);
    use MONKEY-SEE-NO-EVAL;
    no strict;
    $x = 1;
    EVAL q[say "eval ran"];
    say $x;
    END

unlink $_ for @made;
try rmdir $work;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
