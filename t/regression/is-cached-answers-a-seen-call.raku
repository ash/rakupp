# Regression (RAKUDO-ISSUES-PLAN R4; rakudo#4665, rakudo#4666): `is cached`
# answers a call it has seen without running the routine again.
#
# Before the fix the trait was accepted and did nothing: `pow2(10)`, which
# recurses twice per level, made 2,047 calls where Rakudo makes 11. As
# Rakudo's own trait does, a call is keyed on the gist of its Capture, so
# `f(1)` and `f("1")` are different calls, and so is an Array after a push.
# Every closure of one declaration shares one cache.
#
# Contract: exit 0 + last line PASS.
use experimental :cached;
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $calls = 0;
sub pow2($a) is cached { $calls++; $a == 0 ?? 1 !! pow2($a - 1) + pow2($a - 1) }
check 'pow2(10)', pow2(10), 1024;
check 'one call per level (rakudo#4665)', $calls, 11;
pow2(10);
check '…and none the second time', $calls, 11;

my $n = 0;
sub f($x, :$k) is cached { $n++; "$x/{$k // '-'}" }
check 'values', (f(1), f(1), f("1"), f(1.0), f(1, :k(2)), f(1, :k(2)), f(1, :k(3))),
               ('1/-', '1/-', '1/-', '1/-', '1/2', '1/2', '1/3');
check 'calls: 1, "1", 1.0 and the named ones apart', $n, 5;

my $m = 0;
my @a = 1, 2;
sub q(@x) is cached { $m++; @x.sum }
check 'an Array', q(@a), 3;
@a.push(3);
check '…pushed to, is another call', q(@a), 6;
check '…two calls', $m, 2;

my $an = 0;
my &g = sub ($x) is cached { $an++; $x * 2 };
g(1); g(1);
check 'an anonymous sub', $an, 1;

my $in = 0;
sub outer { sub inner($x) is cached { $in++ }; inner(1); inner(1) }
outer(); outer();
check 'a nested sub keeps its cache across calls of its outer one', $in, 1;

my $died = 0;
sub d($x) is cached { $died++; die "no" if $x; 1 }
try d(1); try d(1);
check 'a call that dies is not cached', $died, 2;

my $z = 0;
sub zero() is cached { ++$z }
check 'no arguments', (zero(), zero(), $z), (1, 1, 1);

# Raku++ only: Rakudo 2026.09 passes these by its own bugs' measure
if $*RAKU.compiler.name eq 'Raku++' {
    my $s = 0;
    sub viaSamewith($a) is cached { $s++; $a == 0 ?? 1 !! samewith($a - 1) + samewith($a - 1) }
    viaSamewith(10);
    check 'samewith goes through the cache (Rakudo: 2047)', $s, 11;
    my $sets = 0;
    sub setOf($s) is cached { $sets++ }
    setOf(<1 2 3>.Set) for ^100;
    check 'a Set is one key, whatever its order (rakudo#4666; Rakudo: 6)', $sets, 1;
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
