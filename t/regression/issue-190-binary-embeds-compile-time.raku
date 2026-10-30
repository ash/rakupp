# Regression: a binary built with --exe, --aot or --bundle carries what the
# program's compile-time code came to — its constants, its BEGIN values, what
# its BEGIN blocks left in its variables — computed while it was BUILT, as a
# precompiled Rakudo module keeps them; it does not run that code again when
# it starts (issue #190).
#
# All three ran a constant's initializer and the BEGIN blocks at binary start,
# so `constant DATA = $?FILE.IO.parent.add('data.txt').slurp` read the file on
# the machine running the binary, and died where it was missing. The build
# runs the program's compile-time part now (src/CompileTime.cpp) and puts each
# value in the tree it compiles, written as Raku. What cannot be written so —
# an object — and what reads the program's own variables are computed at
# start, as before, and the build says which; a BEGIN block that reaches past
# the program's variables (a package's stash, EVAL) keeps running at start, and
# so does everything when the compile-time code fails while building.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

unless $*RAKU.compiler.name eq 'Raku++' {
    note 'issue-190: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("rakupp-issue-190-$*PID");
mkdir $work;
LEAVE { if $work.e { .unlink for $work.dir; $work.rmdir } }

my $data = $work.add('data.txt');
my $prog = $work.add('prog.raku');
$prog.spurt: q:to/END/;
    say 'main';
    constant DATA = $?FILE.IO.parent.add('data.txt').slurp.trim;
    my $first = BEGIN $?FILE.IO.parent.add('data.txt').lines[0];
    my %seen;
    BEGIN { %seen<begin> = 'set'; note 'BEGIN ran' }
    my $n = 2;
    constant SAME = $n;
    class P { has $.v }
    constant OBJ = P.new(v => DATA.chars);
    say DATA;
    say $first;
    say %seen<begin>;
    say SAME;
    say OBJ.v;
    END
my $want = "main\nline one\nline two\nline one\nset\n2\n17\n";

for <exe aot bundle> -> $mode {
    $data.spurt: "line one\nline two\n";
    my $bin = $work.add("p-$mode");
    my $b = run $*EXECUTABLE, "--$mode", $prog.Str, '-o', $bin.Str, :out, :err;
    my $built = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    ck $b.exitcode, 0, "--$mode builds";
    ck $built.contains('BEGIN ran'), True, "--$mode: the BEGIN block runs while building";
    ck $built.contains('constant OBJ (line 9) is a P'), True, "--$mode: the build names the object constant";
    ck $built.contains('(native)'), True, '--exe: still native' if $mode eq 'exe';
    $data.unlink;
    my $r = run $bin.Str, :out, :err;
    ck $r.out.slurp(:close), $want, "--$mode: the binary carries the values, with the file gone";
    my $err = $r.err.slurp(:close);
    ck $err.contains('BEGIN ran'), False, "--$mode: …and runs no BEGIN block";
    ck $r.exitcode, 0, "--$mode: …and exits 0";
}

# a BEGIN that dies while building leaves the binary as it was: it runs the
# BEGIN when it starts, where it may succeed
{
    my $p2 = $work.add('dies.raku');
    $p2.spurt: q:to/END/;
        BEGIN { die 'not here' unless %*ENV<RAKUPP_ISSUE_190> }
        constant C = 6 * 7;
        say 'ran ', C;
        END
    my $bin = $work.add('dies');
    my %env = %*ENV;
    %env<RAKUPP_ISSUE_190>:delete;
    my $b = run $*EXECUTABLE, '--bundle', $p2.Str, '-o', $bin.Str, :out, :err, :%env;
    my $built = $b.err.slurp(:close);
    $b.out.slurp(:close);
    ck $b.exitcode, 0, 'a failing BEGIN still builds';
    ck $built.contains('failed while building'), True, '…and the build says so';
    %env<RAKUPP_ISSUE_190> = 1;
    my $r = run $bin.Str, :out, :err, :%env;
    ck $r.out.slurp(:close), "ran 42\n", '…and the binary runs it when it starts';
    $r.err.slurp(:close);
}

# a BEGIN that does more than set the program's variables — here it writes a
# package's stash, as Sparrow6::DSL re-exports — keeps running at start
{
    my $p3 = $work.add('stash.raku');
    $p3.spurt: q:to/END/;
        sub helper { 'B' }
        BEGIN for <&helper> { EXPORT::DEFAULT::{$_} = ::($_) }
        say EXPORT::DEFAULT::{'&helper'}();
        END
    my $bin = $work.add('stash');
    my $b = run $*EXECUTABLE, '--bundle', $p3.Str, '-o', $bin.Str, :out, :err;
    my $built = $b.err.slurp(:close);
    $b.out.slurp(:close);
    ck $built.contains('reaches a stash'), True, 'a BEGIN writing a stash: the build says it stays';
    my $r = run $bin.Str, :out, :err;
    ck $r.out.slurp(:close), "B\n", '…and the binary runs it when it starts';
    $r.err.slurp(:close);
}

say 'PASS';
