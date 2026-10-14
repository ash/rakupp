# ORACLE-PLAN P2: FatRat against Python's fractions.Fraction, and .Num of an
# Int or a FatRat against Python's float(), which rounds correctly.
#
# Exact ops are compared as `.nude`, reduced with a positive denominator. The
# operands are drawn to up to 60 digits a side with shared factors, powers of
# two and ten off by one, and for .Num values exactly halfway between two
# doubles, where a conversion that divides in floating point rounds twice.
# t/oracle/rat-oracle.py says how each case is built.
#
#   rakupp t/oracle/rat.raku [--seconds=60] [--seed=N] [--rounds=N]
#
# Every round prints its seed; `--seed=N --rounds=1` replays it. Needs python3
# 3.8+; fails without it unless --allow-missing.
#
# Contract: exit 0 + last line PASS.

my $seconds = 60;
my $seed    = Int;
my $rounds  = Int;
my $allow-missing = False;
for @*ARGS -> $a {
    if    $a.starts-with('--seconds=') { $seconds = +$a.substr(10) }
    elsif $a.starts-with('--seed=')    { $seed    = +$a.substr(7) }
    elsif $a.starts-with('--rounds=')  { $rounds  = +$a.substr(9) }
    elsif $a eq '--allow-missing'      { $allow-missing = True }
    else { note "unknown option $a"; exit 2 }
}

my $ORACLE = $*PROGRAM.parent.add('rat-oracle.py').absolute;
{
    my $p = run 'python3', '-c', 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)', :out, :err;
    $p.out.slurp(:close); $p.err.slurp(:close);
    if $p.exitcode != 0 {
        if $allow-missing { say "# SKIP: no python3 3.8+ on PATH"; say 'PASS'; exit 0 }
        note "python3 3.8+ is needed as the oracle (pass --allow-missing to skip)";
        say 'FAIL';
        exit 1;
    }
}

sub fr(Str $n, Str $d) { FatRat.new($n.Int, $d.Int) }
sub nd($x) { my ($n, $d) = $x.FatRat.nude; "$n/$d" }

# A Num answer matches when it is the same double: equal, and for a zero the
# same sign. Python writes them with repr(); `inf` is its spelling of Inf.
sub same-num(Num $got, Str $want) {
    my $w = $want eq 'inf' ?? Inf !! $want eq '-inf' ?? -Inf !! $want.Num;
    return False unless $got == $w;
    return True unless $w == 0;
    $got.raku.starts-with('-') == $want.starts-with('-')     # -0e0 is not 0e0
}

sub check(Str $op, Str $an, Str $ad, Str $bn, Str $bd, Str $want) {
    given $op {
        when 'add'     { nd(fr($an, $ad) + fr($bn, $bd)) eq $want }
        when 'sub'     { nd(fr($an, $ad) - fr($bn, $bd)) eq $want }
        when 'mul'     { nd(fr($an, $ad) * fr($bn, $bd)) eq $want }
        when 'div'     { nd(fr($an, $ad) / fr($bn, $bd)) eq $want }
        when 'nude'    { nd(fr($an, $ad)) eq $want }
        when 'cmp'     { ~(fr($an, $ad) <=> fr($bn, $bd)).value eq $want }
        when 'floor'   { ~fr($an, $ad).floor eq $want }
        when 'ceiling' { ~fr($an, $ad).ceiling eq $want }
        when 'round'   { ~fr($an, $ad).round eq $want }
        when 'num'     { same-num(fr($an, $ad).Num, $want) }
        when 'int-num' { same-num($an.Int.Num, $want) }
        when 'base-repeating' {
            my ($nonrep, $rep) = fr($an, $ad).base-repeating($bn.Int);
            "$nonrep|$rep" eq $want
        }
        default { die "unknown op $op" }
    }
}

sub got(Str $op, Str $an, Str $ad, Str $bn, Str $bd) {
    given $op {
        when 'add' { nd(fr($an, $ad) + fr($bn, $bd)) }
        when 'sub' { nd(fr($an, $ad) - fr($bn, $bd)) }
        when 'mul' { nd(fr($an, $ad) * fr($bn, $bd)) }
        when 'div' { nd(fr($an, $ad) / fr($bn, $bd)) }
        when 'nude' { nd(fr($an, $ad)) }
        when 'cmp' { ~(fr($an, $ad) <=> fr($bn, $bd)).value }
        when 'num' { fr($an, $ad).Num.raku }
        when 'int-num' { $an.Int.Num.raku }
        when 'base-repeating' { my ($a, $b) = fr($an, $ad).base-repeating($bn.Int); "$a|$b" }
        default { ~fr($an, $ad)."$op"() }
    }
}

sub short(Str $s) { $s.chars > 70 ?? "{$s.substr(0, 30)}…{$s.substr(*-30)} ({$s.chars} chars)" !! $s }

my @fails;
my $cases = 0;
my $per-round = 400;

sub one-round(Int $round-seed) {
    my $file = $*TMPDIR.add("rakupp-oracle-rat-$*PID-$round-seed.tsv");
    my $g = run 'python3', '-I', $ORACLE, 'gen', $file.Str, ~$round-seed, ~$per-round, :out, :err;
    my $gerr = $g.err.slurp(:close); $g.out.slurp(:close);
    if $g.exitcode != 0 { @fails.push: "seed $round-seed: the generator failed: $gerr"; return }
    my $n = 0;
    for $file.lines -> $line {
        $n++;
        my ($op, $an, $ad, $bn, $bd, $want) = $line.split("\t");
        $cases++;
        my $ok = try check($op, $an, $ad, $bn, $bd, $want);
        next if $ok;
        my $g = $! ?? "died: {$!.message}" !! (try got($op, $an, $ad, $bn, $bd)) // "died: {$!.message}";
        @fails.push: "seed $round-seed line $n: $op a={short $an}/{short $ad}"
                   ~ ($bn ne '-' ?? " b={short $bn}/{short $bd}" !! '')
                   ~ "\n      want {short $want}\n       got {short $g}";
    }
    $file.unlink;
}

my $start = now;
my $n = 0;
my $base-seed = $seed // (now.Rat * 1000).Int % 2**31;
loop {
    my $s = $base-seed + $n;
    say "# round {$n + 1}, seed $s";
    one-round($s);
    $n++;
    last if $rounds.defined && $n >= $rounds;
    last if !$rounds.defined && now - $start >= $seconds;
    last if @fails >= 20;
}

say "# $n rounds, $cases cases";
if @fails {
    say "not ok - $_" for @fails.head(20);
    say "# ... and {@fails - 20} more" if @fails > 20;
    say 'FAIL';
    exit 1;
}
say 'PASS';
