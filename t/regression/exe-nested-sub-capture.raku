# Regression: `--exe` could not compile a nested `my sub` that closes over an
# enclosing `my` (Grand Review phase 2; docs/dev/findings/REVIEW-GRAND-DOCS.md).
# The generated C++ named a variable it had not declared yet —
# `use of undeclared identifier 'v_sscale'` — and three showcase programs
# (kvstore, fourier, orbits) could not be compiled at all.
#
# A lexical sub is installed at BLOCK ENTRY in Raku, so it can be called above
# its own declaration line; the compiler is right to hoist it. What was wrong is
# that the closure captured by value where it was created, while the variables
# it captured were declared further down. Now a name a nested closure reads is
# declared — as a shared cell — above the closure, and the later `my $x = …`
# assigns into that same slot.
#
# The cases below are the shapes that broke, each checked interpreted AND
# compiled. On Rakudo there is no `--exe`, so it checks the interpreted answers
# only: those are the answers the compiled ones must equal.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

# 1. interpreted — every shape, on both engines
sub plain() {
    my $scale = 3;
    my sub mul($x) { $x * $scale }
    mul(4);
}
check(plain(), 12, 'a nested sub reads an enclosing `my`');

sub two-subs() {
    my $base = 10;
    my sub a($x) { $x + $base }
    my sub b($x) { a($x) * 2 }
    b(1);
}
check(two-subs(), 22, 'a nested sub calls another that captures');

sub in-loop() {
    my @out;
    for 1..3 -> $i {
        my $t = $i * 10;
        my sub show() { "$i:$t" }          # the capture is inside an interpolation
        @out.push(show());
    }
    @out.join(',');
}
check(in-loop(), '1:10,2:20,3:30', 'a nested sub in a loop body, captured through interpolation');

sub comma-decls() {
    my $lo = 2, my $hi = 5;                 # one statement, two declarations
    my sub span() { $hi - $lo }
    span();
}
check(comma-decls(), 3, 'a nested sub captures comma-separated declarations');

sub list-decl() {
    my ($a, $b) = 3, 4;
    my sub sum() { $a + $b }
    sum();
}
check(list-decl(), 7, 'a nested sub captures `my ($a, $b) = …`');

sub mutated() {
    my $n = 0;
    my sub bump() { $n++ }
    bump() for ^3;
    $n;
}
check(mutated(), 3, 'a nested sub still mutates through the shared slot');

{
    my $k = 5;
    my sub inner($x) { $x + $k }
    check(inner(1), 6, 'a nested sub inside a bare block');
}

# 2. compiled — the same answers, from a binary (Raku++ only)
if $*RAKU.compiler.name eq 'Raku++' {
    my $dir = $*TMPDIR.add("rakupp-nestsub-{$*PID}");
    $dir.mkdir;
    LEAVE { for $dir.dir { .unlink }; try $dir.rmdir }

    my $src = $dir.add('prog.raku');
    $src.spurt(q:to/RAKU/);
        sub outer() {
            my $scale = 3;
            my sub mul($x) { $x * $scale }
            my @out;
            for 1..2 -> $i {
                my $t = $i * 10;
                my sub show() { "$i:$t" }
                @out.push(show());
            }
            my $lo = 2, my $hi = 5;
            my sub span() { $hi - $lo }
            my ($a, $b) = 3, 4;
            my sub sum() { $a + $b }
            say mul(4), " ", @out.join(','), " ", span(), " ", sum();
        }
        outer();
        RAKU

    my $want = "12 1:10,2:20 3 7\n";
    check(run($*EXECUTABLE, $src.Str, :out).out.slurp(:close), $want, 'interpreted: the combined program');

    my $bin = $dir.add('prog-bin');
    my $comp = run $*EXECUTABLE, '--exe', $src.Str, '-o', $bin.Str, :out, :err;
    check($comp.exitcode, 0, 'it compiles (the C++ named an undeclared variable)');
    if $comp.exitcode == 0 {
        check(run($bin.Str, :out).out.slurp(:close), $want, '…and the binary prints what the interpreter prints');
    }

    # a cell declared in one branch must not reach a sibling branch's closure
    my $br = $dir.add('branches.raku');
    $br.spurt(q:to/RAKU/);
        sub pick($which) {
            if $which eq 'a' {
                my $n = 0;
                my $add = { $n++ };
                $add(); $add();
                return $n;
            }
            else {
                my $m = 10;
                my $mul = { $m * 2 };
                return $mul();
            }
        }
        say pick('a'), " ", pick('b');
        RAKU
    my $bbin = $dir.add('branches-bin');
    my $bcomp = run $*EXECUTABLE, '--exe', $br.Str, '-o', $bbin.Str, :out, :err;
    check($bcomp.exitcode, 0, 'sibling branches with their own captured locals compile');
    if $bcomp.exitcode == 0 {
        check(run($bbin.Str, :out).out.slurp(:close), "2 20\n", '…and answer correctly');
    }
}

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
