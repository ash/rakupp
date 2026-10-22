# Answers the interpreter gave differently from Rakudo, and compiled code
# with it (FLOAT-HEAVY-ISSUES-2026-10-22.md, "Found while fixing"):
#  - Instant and Duration arithmetic: `-$d` and `abs $d` are Durations (were
#    bare Rats), Real − Duration / Instant and Real % one is the Num of the
#    Bridges (`0 - $d` was a Duration), `-$i` is a Num; compiled, every
#    operator on one dropped the tag (`$d - 0` was a Rat);
#  - `.push` / `.unshift` slip a Slip held in a Scalar (`@a.push($s)` stored
#    the Slip), and the `unshift` sub is the method: it answered the new
#    length, did not slip and did not type-check;
#  - `my ($k, $v) = %h` takes the Hash's pairs (it put the Hash in $k);
#  - `fail` in a block that another routine runs returns from the routine it
#    was written in, as a `return` does: `sub q { ap({ fail "z" }); … }`
#    returned the Failure from ap, and q sank it and died. Compiled, a sub
#    without a frame of its own caught a return aimed past it, and a module
#    routine's block (the bundled program) asked the builtin, which saw ap.
# Each program runs interpreted and compiled, -O and plain; the answers are
# Rakudo 2026.09's. The `-bundled` ones bundle (a typed @, a subset), so their
# routines take the module-mode path.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-rakudo-agree-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $temporal = q:to/END/;
    my $d = Duration.new(1.5); my $i = Instant.from-posix(10);
    say (-$d).^name, " ", (-$d).raku; say abs(-$d).raku; say (- -$d).^name; say $d.abs.^name;
    say (0 - $d).^name, " ", 0 - $d; say ($d - 0).^name; say (0.5 - $d).^name; say ($d - $i).^name;
    say ($d % 1).^name, " ", ($d % Duration.new(1)).^name, " ", (7 % $d).^name, " ", ($i % 3).^name;
    say ($d * 2).^name, " ", ($d + $d).^name;
    say (-$i).^name, " ", abs($i).^name, " ", (5 - $i).^name, " ", (5 + $i).^name, " ", ($i - 5).^name;
    my $z = $d; $z = -$z; say $z.^name;
    my $s = slip(1, 2);
    my @a; @a.push($s); say @a.raku;
    my @b; @b.push($s, 3); say @b.raku;
    my @c; @c.unshift($s); say @c.raku;
    my @e; @e.append($s); say @e.raku;
    my @f; unshift @f, $s; say @f.raku;
    my @x = 3; say unshift(@x, 1, 2); say unshift(@x, slip(7, 8));
    my @l; for ^2 { @l.push($s) }; say @l.raku;
    my %hh = a => 1; my ($o, $p) = %hh; say "{$o.raku}|{$p.raku}";
    my ($q, $r); ($q, $r) = %hh; say "{$q.raku}|{$r.raku}";
    sub hf { my %h = b => 2; my ($x, $y) = %h; "{$x.raku}|{$y.raku}" }; say hf();
    my ($u, @w) = %hh; say "{$u.raku}|{@w.raku}";
    my $hr = %hh; my ($v, $v2) = $hr; say "{$v.raku}|{$v2.raku}";
    my %e; my ($w1, $w2) = %e; say "{$w1.raku}|{$w2.raku}";
    END
my $temporal-want = q:to/END/.trim;
    Duration Duration.new(-1.5)
    Duration.new(1.5)
    Duration
    Duration
    Num -1.5
    Duration
    Num
    Duration
    Duration Duration Num Num
    Num Duration
    Num Instant Num Instant Instant
    Duration
    [1, 2]
    [1, 2, 3]
    [1, 2]
    [slip(1, 2),]
    [1, 2]
    [1 2 3]
    [7 8 1 2 3]
    [1, 2, 1, 2]
    :a(1)|Any
    :a(1)|Any
    :b(2)|Any
    :a(1)|[]
    :a(1)|Any
    Any|Any
    END
my $failing = q:to/END/;
    sub ap(&b) { b() }
    sub q($x) { ap({ fail "neg" if $x < 0 }); "ok" }
    say q(-1).^name; say q(1);
    sub q3($x) { my @r = (1, 2).map({ fail "m" if $x < 0; $_ }); "ok" }
    say q3(-1).^name;
    sub q4 { ap({ fail "z" }); "after" }
    my $v = q4(); say $v.^name, " ", $v.exception.message;
    sub q5 { ap(sub { fail "in-sub" }); "after5" }
    say (try q5()).^name;
    sub q6 { my $f = ap({ fail "w" }); "after6" }
    say q6().^name;
    sub q7 { for 1..2 { fail "loop" }; "after7" }
    say q7().^name;
    sub mk { -> { fail "y" } }
    sub useit { my &b = mk(); my $r = try ap(&b); "{$r.^name} {$!.^name}" }
    say useit();
    sub q10 { ap({ return 5 }); 6 }
    say q10();
    my $t = try ap({ fail "x" }); say $t.^name, " ", $!.^name;
    END
my $failing-want = q:to/END/.trim;
    Failure
    ok
    Failure
    Failure z
    Nil
    Failure
    Failure
    Any X::AdHoc
    5
    Any X::AdHoc
    END
my @progs =
    ('temporal',         $temporal, $temporal-want),
    ('temporal-bundled', $temporal ~ 'my Int @t; unshift @t, 4; say @t; say (try { unshift @t, "a" }) // $!.^name;' ~ "\n", $temporal-want ~ "\n[4]\nX::TypeCheck::Assignment"),
    ('fail',             $failing, $failing-want),
    ('fail-bundled',     "subset Pos of Int where * > 0;\n" ~ $failing, $failing-want);

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
