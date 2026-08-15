# Regression: two things Rakugrid found across ~460 of its grid points.
#
# 1. `.raku` on an enum VALUE must be QUALIFIED — `Order::Less`, not `Less`.
#    .raku has to round-trip through EVAL and the bare key need not be in scope.
#    Bool was special-cased and so looked right; every other enum answered the
#    bare key, which is what `(3 cmp 5).raku` showing `Less` came from. Junctions
#    tag themselves with an enum NAME too (any/all/one/none) and are not enums —
#    they carry no enum TYPE, which is what tells the two apart.
#
# 2. `div`, `mod` and `%` by zero THROW; they used to hand back a Failure, so
#    `try { 3 div 0 }` caught nothing and the Failure surfaced later as an
#    uncatchable death. `%%` already threw. `/` by zero is genuinely different
#    and stays lazy: `1/0` is the Rat <1/0>.
# Contract: exit 0 + last line PASS.
my @fail;

# ---- 1. enum .raku is qualified -------------------------------------------
enum Colour <Red Green Blue>;
enum Size ( small => 1, big => 2 );

@fail.push("cmp ({(3 cmp 5).raku})")   unless (3 cmp 5).raku eq 'Order::Less';
@fail.push("<=> ({(5 <=> 3).raku})")   unless (5 <=> 3).raku eq 'Order::More';
@fail.push("leg ({('a' leg 'a').raku})") unless ('a' leg 'a').raku eq 'Order::Same';
@fail.push("word enum ({Red.raku})")   unless Red.raku eq 'Colour::Red';
@fail.push("pair enum ({small.raku})") unless small.raku eq 'Size::small';
# …and through containers
@fail.push("list ({[Red, Blue].raku})") unless [Red, Blue].raku eq '[Colour::Red, Colour::Blue]';
@fail.push("hash ({%(a => Red).raku})") unless %(a => Red).raku eq '{:a(Colour::Red)}';
# Bool keeps its own spelling
@fail.push("Bool ({True.raku})") unless True.raku eq 'Bool::True';
# .gist and .Str stay UNqualified — only .raku changes
@fail.push("gist ({Red.gist})") unless Red.gist eq 'Red';
@fail.push("Str ({(3 cmp 5).Str})") unless (3 cmp 5).Str eq 'Less';
# a junction is not an enum and must not grow a `::`
@fail.push("junction ({any(1,2).raku})") if any(1, 2).raku.contains('::');

# ---- 2. division by zero: which operators SOFT-FAIL and which THROW ---------
# Rakudo is not uniform, and Roast pins both shapes: `div` and `%` return a
# Failure (S03-operators/div.t asserts `fails-like`), while `mod` and `%%` throw
# on the spot. Making them all throw breaks div.t; making them all soft-fail
# loses the two that really do throw.
sub threw(&c) { my $ok = False; try { c(); CATCH { when X::Numeric::DivideByZero { $ok = True } } }; $ok }

my $dv = 3 div 0;
@fail.push("div soft-fails ({$dv.^name})") unless $dv ~~ Failure;
@fail.push('div Failure type') unless $dv.exception ~~ X::Numeric::DivideByZero;
my $pc = 3 % 0;
@fail.push("% soft-fails ({$pc.^name})") unless $pc ~~ Failure;
my $zz = 0 div 0;
@fail.push("0 div 0 soft-fails ({$zz.^name})") unless $zz ~~ Failure;

@fail.push('mod throws')  unless threw { 3 mod 0 };
@fail.push('%% throws')   unless threw { 3 %% 0 };

# ---- 2b. Inf/NaN in the integer operators ----------------------------------
# These take integers: a non-finite value has NO Int, and saturating it to
# 2**63-1 (what a plain toInt does) made `0 gcd Inf` answer 9223372036854775807.
sub threwCC(&c) { my $ok = False; try { c(); CATCH { when X::Numeric::CannotConvert { $ok = True } } }; $ok }
@fail.push('gcd Inf') unless threwCC { 0 gcd Inf };
@fail.push('gcd NaN') unless threwCC { 0 gcd NaN };
@fail.push('lcm Inf') unless threwCC { 1 lcm Inf };
# the DIVISOR decides first: a non-finite divisor has no Int and counts as zero,
# which is why `1 div Inf` is a divide-by-zero exactly as `1 div (1/3)` is
@fail.push("div Inf ({(1 div Inf).^name})") unless (1 div Inf) ~~ Failure;
@fail.push("div 1/3 ({(1 div (1/3)).^name})") unless (1 div (1/3)) ~~ Failure;
@fail.push("div 0e0 ({(1 div 0e0).^name})") unless (1 div 0e0) ~~ Failure;

# `/` by zero is NOT this: it yields a Rat, and only complains when used
my $r = 1 / 0;
@fail.push("1/0 type ({$r.^name})") unless $r.^name eq 'Rat';
@fail.push("1/0 nu/de") unless $r.numerator == 1 && $r.denominator == 0;
# ordinary division still works
@fail.push('7 div 2') unless 7 div 2 == 3;
@fail.push('7 mod 2') unless 7 mod 2 == 1;
@fail.push('-7 div 2') unless -7 div 2 == -4;   # floors, not truncates

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
