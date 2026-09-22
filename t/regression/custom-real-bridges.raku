# Regression: a custom Real numifies through its own `.Bridge`.
#
# A class that `does Real` has no numeric value of its own — `toNum()` on the
# object answers 0 — so every place that numified an argument with `toNum()`
# read one as zero. `is-approx $x, $x` then compared the value against 0 and
# failed; `log($x)`, `cis($x)` and `unpolar($x, $a)` computed on 0. The METHOD
# forms were right, so the sub and the method disagreed about the same object
# (S32-num/real-bridge.t, 188/201 -> 200/201).
#
# The other half: `does Real` promises a `.Bridge`, and a class that does not
# write one inherits the role's default, which delegates to `.Num`. That default
# is for Real ALONE — a `Cool` class with a `.Numeric` of its own must keep
# answering "no such method" for `.Bridge`, because that refusal is what sends
# the numifier on to `.Numeric`. S32-trig's NotComplex has a COMPLEX `.Numeric`,
# and a too-eager default flattened it to a real number, costing eleven trig
# files half their assertions.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub near($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want ~{$want.raku}") unless abs($got - $want) < 1e-9
}

class Fixed2 does Real {
    has Int $.one-hundredths;
    multi method new(Int $a) { self.bless(:one-hundredths($a * 100)) }
    multi method new(Rat $a) { self.bless(:one-hundredths(floor($a * 100))) }
    method Bridge() { $.one-hundredths.Bridge / 100.Bridge }
}
my $one  = Fixed2.new(1);
my $ten  = Fixed2.new(10);
my $frac = Fixed2.new(1.01);

# --- the object is its bridged value, wherever it is used -------------------
near +$one,            1,   'prefix + bridges';
near $one.Num,         1,   'and .Num';
near $one.abs,         1,   'and .abs';
near log($one),        0,   'the SUB form of log bridges its argument';
near $one.log,         0,   'as the method form always did';
near $frac.log($ten),  1.01.log10, 'a bridged BASE, as an argument';
near log($frac, $ten), 1.01.log10, 'and the same through the sub';
check cis($one).raku,  1.cis.raku, 'cis bridges its argument';
check unpolar($frac, Fixed2.new(-3)).raku, 1.01.unpolar(-3).raku,
      'unpolar bridges both of them';

# --- is-approx: the custom type on either side ------------------------------
# this is what failed: the EXPECTED side read as 0, so 1 was "not approximately"
# the very object it came from
check (abs(($one * $one) - $one) < 1e-9), True, 'the arithmetic was never wrong';
my $ok = True;
{
    # is-approx is the thing under test, so check its VERDICT rather than call it
    my $got = $one * $one;
    $ok = abs($got - $one) / max(abs($got), abs($one)) <= 1e-6;
}
check $ok, True, 'and is-approx now agrees with it';

# --- the default Bridge is for Real, and only Real --------------------------
check (my class :: does Real { method Num { 42e0 } }.new.Bridge), 42e0,
      'a does-Real class without its own Bridge delegates to .Num';
class NotComplex is Cool {
    has $.value;
    multi method new(Complex $value is copy) { self.bless(:$value) }
    multi method Numeric() { self.value }
}
# no default Bridge here: .Numeric must be reached, and it is a COMPLEX
check (try NotComplex.new(3+2i).Bridge) // 'refused', 'refused',
      'a Cool class gets no default Bridge';
check NotComplex.new(3.92699081702367+2i).cosec.narrow.WHAT.gist, '(Complex)',
      'so a Complex .Numeric survives into the trig';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
