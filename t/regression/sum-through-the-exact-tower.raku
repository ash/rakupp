# Regression: `.sum` folded through a `double` and cast the result back.
#   * that SATURATED at int64 — `(2**70, 1).sum` came out as 9223372036854775807
#     instead of 1180591620717411303425;
#   * it lost Rat exactness, since the running total was binary floating point;
#   * and it flattened a junction element to a number, where Rakudo autothreads
#     (`(1|2, 3).sum` is `any(4, 5)`).
# Folding with applyArith gets all three right, because the exact numeric tower
# and junction autothreading already live there.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

check((1, 2, 3).sum,            '6',  'the ordinary case');
check([1, 2, 3].sum,            '6',  'over an Array');
check(().sum,                   '0',  'an empty list sums to zero');
check((2**70, 1).sum, '1180591620717411303425', 'a big integer does not saturate');
check((2**70, 2**70).sum, '2361183241434822606848', 'nor does adding two');
check((1/3, 1/3, 1/3).sum,      '1',  'Rats stay exact');
check((1/3, 1/3, 1/3).sum.^name,'Rat','and stay a Rat even when the total is whole');
# …and folding through applyArith means .sum INHERITS the Rat spill rule rather
# than sidestepping it: a Rat degrades to Num once its denominator passes 64 bits,
# at exactly the point `+` degrades. The old double-based fold never produced a
# Rat at all, so it could not have shown this either way.
check((1..30).map({ 1 / $_ }).sum.^name,          'Rat', 'a sum whose lcm still fits stays exact');
check((1..25).map({ 1 / (10**$_ + 1) }).sum.^name,'Num', 'and spills to Num when it does not');
my $hand = 0; $hand += $_ for (1..25).map({ 1 / (10**$_ + 1) });
check($hand.^name, 'Num', 'the hand-written fold spills at the same point');
check(([+] (1..30).map({ 1 / $_ })).^name, 'Rat', 'and so does the reduce metaop');
check((1|2, 3).sum.gist, 'any(4, 5)', 'a junction element autothreads');
check((1, 3, pi).sum,   '7.141592653589793', 'a Num makes it a Num');
check((1, "0xff").sum,  '256', 'a numeric string numifies');
check(sum(0b1111, 5),   '20',  'the sub form agrees');
check((1e0, 2).sum,     '3',   'and a Num literal');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
