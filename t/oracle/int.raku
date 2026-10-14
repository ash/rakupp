# ORACLE-PLAN P2: Int against Python's int, on generated operands, for as long
# as the budget allows.
#
# src/BigInt.cpp is base-1e9 limbs with fast paths at 64 and 128 bits, a
# schoolbook kernel that carries every 18 rows, Karatsuba from 96 limbs and
# Knuth's algorithm D for division; src/IntKernel.cpp runs closed integer subs
# in int64 and bails when a value outgrows it. t/oracle/int-oracle.py draws
# operands around every one of those thresholds (and the shapes long division
# gets wrong: all-nine limbs, a top limb either side of BASE/2, dividends one
# off a multiple) and Python answers; this file asks the engine the same.
#
#   rakupp t/oracle/int.raku [--seconds=60] [--seed=N] [--rounds=N]
#
# Each op is its own plain sub, so the integer kernels may take it; run it
# again under RAKUPP_NO_KERNELS=1 to check the generic path on its own.
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

my $ORACLE = $*PROGRAM.parent.add('int-oracle.py').absolute;
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

sub op-add($a, $b)  { $a + $b }
sub op-sub($a, $b)  { $a - $b }
sub op-mul($a, $b)  { $a * $b }
sub op-div($a, $b)  { $a div $b }
sub op-mod($a, $b)  { $a % $b }
sub op-dmi($a, $b)  { ($a div $b) * $b + $a % $b }
sub op-cmp($a, $b)  { ($a <=> $b).value }
sub op-gcd($a, $b)  { $a gcd $b }
sub op-lcm($a, $b)  { $a lcm $b }
sub op-pow($a, $b)  { $a ** $b }
sub op-shl($a, $b)  { $a +< $b }
sub op-shr($a, $b)  { $a +> $b }
sub op-and($a, $b)  { $a +& $b }
sub op-or($a, $b)   { $a +| $b }
sub op-xor($a, $b)  { $a +^ $b }
sub op-not($a)      { +^$a }

sub answer(Str $op, Str $a, Str $b, Str $c) {
    given $op {
        when 'add'             { op-add($a.Int, $b.Int) }
        when 'sub'             { op-sub($a.Int, $b.Int) }
        when 'mul'             { op-mul($a.Int, $b.Int) }
        when 'div'             { op-div($a.Int, $b.Int) }
        when 'mod'             { op-mod($a.Int, $b.Int) }
        when 'divmod-identity' { op-dmi($a.Int, $b.Int) }
        when 'cmp'             { op-cmp($a.Int, $b.Int) }
        when 'gcd'             { op-gcd($a.Int, $b.Int) }
        when 'lcm'             { op-lcm($a.Int, $b.Int) }
        when 'pow'             { op-pow($a.Int, $b.Int) }
        when 'expmod'          { expmod($a.Int, $b.Int, $c.Int) }
        when 'shl'             { op-shl($a.Int, $b.Int) }
        when 'shr'             { op-shr($a.Int, $b.Int) }
        when 'and'             { op-and($a.Int, $b.Int) }
        when 'or'              { op-or($a.Int, $b.Int) }
        when 'xor'             { op-xor($a.Int, $b.Int) }
        when 'not'             { op-not($a.Int) }
        when 'base'            { $a.Int.base($b.Int) }
        when 'parse-base'      { $a.parse-base($b.Int) }
        when 'msb'             { $a.Int.msb }
        when 'lsb'             { $a.Int.lsb }
        when 'str'             { $a.Int }
        default                { die "unknown op $op" }
    }
}

sub short(Str $s) { $s.chars > 60 ?? "{$s.substr(0, 25)}…{$s.substr(*-25)} ({$s.chars} chars)" !! $s }

my @fails;
my $cases = 0;
my $per-round = 400;

sub one-round(Int $round-seed) {
    my $file = $*TMPDIR.add("rakupp-oracle-int-$*PID-$round-seed.tsv");
    my $g = run 'python3', '-I', $ORACLE, 'gen', $file.Str, ~$round-seed, ~$per-round, :out, :err;
    my $gerr = $g.err.slurp(:close); $g.out.slurp(:close);
    if $g.exitcode != 0 { @fails.push: "seed $round-seed: the generator failed: $gerr"; return }
    my $n = 0;
    for $file.lines -> $line {
        $n++;
        my ($op, $a, $b, $c, $want) = $line.split("\t");
        $cases++;
        my $got = try answer($op, $a, $b, $c);
        my $gs = $got.defined ?? ~$got !! "died: {$!.message}";
        next if $gs eq $want;
        @fails.push: "seed $round-seed line $n: $op a={short $a} b={short $b}{$c ne '-' ?? " c={short $c}" !! ''}\n"
                   ~ "      want {short $want}\n       got {short $gs}";
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

say "# $n rounds, $cases cases{%*ENV<RAKUPP_NO_KERNELS> ?? ', RAKUPP_NO_KERNELS=1' !! ''}";
if @fails {
    say "not ok - $_" for @fails.head(20);
    say "# ... and {@fails - 20} more" if @fails > 20;
    say 'FAIL';
    exit 1;
}
say 'PASS';
