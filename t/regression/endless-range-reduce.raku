# Regression (issue #19, "Infinity a little small"): an ENDLESS operand — an
# infinite Range (1..Inf / 1..*) or a lazy list with no end — must never be
# folded as the ten-thousand-element prefix Value::flatten hands back. That
# silently answered a different question: `[+] 1..Inf` came out 50005000, the
# sum of 1..10000, with nothing to say it was not the total.
#
# `+` is answered by the limit of the arithmetic series, which is what `.sum`
# gives; min/max by the range's own bounds; every other operator, and every
# endless LAZY list (whose elements are unknowable), says it cannot be reduced.
# Endless ranges also render with Inf, not with the long long their endpoint is
# parked in.
# Contract: exit 0 + last line PASS.
my @fail;

sub is-it($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub dies-it(&c, $what) {
    my $died = False;
    { c(); CATCH { default { $died = True } } }
    @fail.push("$what: did not throw") unless $died;
}

# --- what Rakudo answers too --------------------------------------------
is-it((1..Inf).sum,   Inf,  '(1..Inf).sum');
is-it((1..*).sum,     Inf,  '(1..*).sum');
is-it((-Inf..0).sum, -Inf,  '(-Inf..0).sum');
is-it((1.5..Inf).sum, Inf,  '(1.5..Inf).sum');
@fail.push('(-Inf..Inf).sum') unless (-Inf..Inf).sum.isNaN;

# a range whose endpoint does not fit a long long carries the same sentinel an
# endless one does — it is still finite, and sums by Gauss instead of by walking
my $googol = 10 ** 100;
is-it((1..$googol).sum, $googol * ($googol + 1) div 2, '(1..10**100).sum');
is-it(sum(1..$googol),  $googol * ($googol + 1) div 2, 'sum(1..10**100)');
is-it((1..Inf).min,   1,   '(1..Inf).min');
is-it((1..Inf).max,   Inf, '(1..Inf).max');

# an endless range renders with Inf, not with LLONG_MAX
is-it((1..*).gist,    '1..Inf',  '(1..*).gist');
is-it((1..Inf).gist,  '1..Inf',  '(1..Inf).gist');
is-it((1..*).raku,    '1..Inf',  '(1..*).raku');
is-it((^Inf).gist,    '0..^Inf', '(^Inf).gist — not abbreviated');
is-it((-Inf..0).gist, '-Inf..0', '(-Inf..0).gist');
is-it((1..*).Str,     '1..*',    '(1..*).Str');
is-it((-Inf..0).Str,  '*..0',    '(-Inf..0).Str');
is-it(~(1.5..Inf),    '1.5..*',  '~(1.5..Inf)');

# lazy views over an endless source still run lazily
is-it((^Inf).grep(*.is-prime).head(5).List, (2, 3, 5, 7, 11), 'grep of an endless range');
is-it((1..Inf).head(3).List, (1, 2, 3), '.head of an endless range');
is-it((1..Inf).map(* + 1).head(3).List, (2, 3, 4), '.map over an endless range');

# finite reduces are untouched
is-it(([+] 1..10),     55,    '[+] 1..10');
is-it(([*] 1..5),      120,   '[*] 1..5');
is-it(([+] ()),        0,     '[+] ()');
is-it(([~] 1..4),      '1234', '[~] 1..4');
is-it(([min] 3, 1, 2), 1,     '[min] over a list');
is-it((1..10).sum,     55,    '(1..10).sum');
is-it((1..10).reduce(&[+]), 55, '(1..10).reduce(&[+])');
is-it(~(1..5),      '1 2 3 4 5', '~(1..5) still expands to its elements');
is-it((^5).gist,       '^5',  '(^5).gist keeps the short form');

# --- rakupp-only: where Rakudo has no answer at all ----------------------
# Reducing an endless operand makes Rakudo fold forever — `[+] 1..Inf` never
# returns. Spinning is no better than the wrong number was, so rakupp answers
# the operators that can be answered without the elements and refuses the rest.
# (These lines would hang the suite under Rakudo, hence the gate.)
if $*RAKU.compiler.name eq 'Raku++' {
    is-it(([+] 1..Inf),  Inf, '[+] 1..Inf');
    is-it(([+] 1..*),    Inf, '[+] 1..*');
    is-it(([+] 1..Inf, 5), Inf, '[+] with an endless operand among others');
    is-it((1..Inf).reduce(&[+]), Inf, '(1..Inf).reduce(&[+])');
    is-it(([min] 1..Inf), 1,   '[min] 1..Inf');
    is-it(([max] 1..Inf), Inf, '[max] 1..Inf');

    # the partial products grow without bound (sign decided by how many negative
    # elements there are), unless a 0 among them pins every partial to 0
    is-it(([*] 1..Inf),     Inf,  '[*] 1..Inf');
    is-it(([*] 1.5..Inf),   Inf,  '[*] 1.5..Inf');
    is-it(([*] -0.5..Inf), -Inf,  '[*] -0.5..Inf — one negative element');
    is-it(([*] -2.5..Inf), -Inf,  '[*] -2.5..Inf — three of them');
    is-it(([*] ^Inf),       0,    '[*] ^Inf — zero is absorbing');
    is-it(([*] -3..Inf),    0,    '[*] -3..Inf — it walks over zero');
    is-it(([-] 1..Inf),    -Inf,  '[-] 1..Inf');
    @fail.push('[*] -Inf..0') unless ([*] -Inf..0).isNaN;   # sign never settles
    @fail.push('[-] -Inf..Inf') unless ([-] -Inf..Inf).isNaN;

    dies-it({ [~] 1..Inf },                '[~] 1..Inf');
    is-it((1..Inf).reduce(&[*]), Inf,      '(1..Inf).reduce(&[*])');
    dies-it({ (1..Inf).reduce(&[~]) },     '(1..Inf).reduce(&[~])');
    dies-it({ [+] (1, 2, 3 ... *) },       '[+] over an endless sequence');
    dies-it({ my @a = 1..Inf; [+] @a },    '[+] over an endless lazy array');
    dies-it({ [+] (1..Inf).grep(* %% 2) }, '[+] over a grep of an endless range');
    dies-it({ [+] (1..Inf).map(* + 1) },   '[+] over a map of an endless range');
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
