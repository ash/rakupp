# Regression: the Hash::Agnostic chain (2026-08-30). Five gaps, each found by
# walking one dist's store path: `my %h is MyHash` did not make %h a MyHash;
# a routine's `is rw`/`is raw`/`return-rw` result was not an assignment target;
# `.rakuseen`, `List.from-iterator` and a Pair-blind `.are` were missing or
# wrong. Rakudo 2026.08 is the oracle for every expectation.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- 1. the container trait decides what the variable IS ---------------------
class MyH is Hash  { method marker() { 'H' } }
class MyA is Array { method marker() { 'A' } }
my %top is MyH;
ok(%top.^name eq 'MyH' && %top.marker eq 'H', 'a mainline declaration takes the trait');
{ my %blk is MyH; ok(%blk.^name eq 'MyH', '…so does one in a block'); }
sub inner() { my %s is MyH; %s.^name }
ok(inner() eq 'MyH', '…and one in a sub');
my @arr is MyA;
ok(@arr.^name eq 'MyA', 'arrays take it too');
my %s1 is Set;
ok(%s1.^name eq 'Set', 'the QuantHash family still works at mainline');
my %b1 is Bag = <a b a>;
ok(%b1.^name eq 'Bag' && %b1<a> == 2, '…and with an initialiser');

# LIMIT, stated rather than asserted: a container type with NO STORE method is
# still replaced by an assignment (`my %h is MyH = (a => 1)` leaves a plain
# Hash). Pre-existing, and separate from the declaration paths above.

# --- 2. a routine's result can BE a container --------------------------------
class C {
    has @!a = 1, 2, 3;
    has %!h = (x => 1);
    method araw($i)  is raw { @!a[$i] }
    method arw($i)   is rw  { @!a[$i] }
    method aret($i)          { return-rw @!a[$i] }   # no trait: return-rw alone
    method hraw($k)  is raw { %!h{$k} }
    method viaself($k, $v)   { self.hraw($k) = $v }  # …reached through `self`
    method dump()            { @!a.raku ~ ' ' ~ %!h.raku }
}
my $c = C.new;
$c.araw(0) = 91;
$c.arw(1)  = 92;
$c.aret(2) = 93;
$c.hraw('x') = 94;
ok($c.dump eq '[91, 92, 93] {:x(94)}', "all four spellings assign (got {$c.dump})");
my $d = C.new;
$d.viaself('x', 7);
ok($d.dump eq '[1, 2, 3] {:x(7)}', 'and through `self` inside a method');

# --- 3. `.are(T)` asks the smartmatch question -------------------------------
my @pairs = (a => 1), (b => 2);
ok(@pairs.are(Pair), 'a list of Pairs are Pairs');
ok((1, 2, 3).are(Int), '…and Ints are Ints');

# --- 4. `X.from-iterator($it)` drains into the named container ---------------
ok(List.from-iterator((1, 2, 3).iterator).raku eq '(1, 2, 3)', 'List.from-iterator');
ok(Array.from-iterator((1, 2).iterator).raku eq '[1, 2]', 'Array.from-iterator');

# --- 5. `.rakuseen` runs the block, and guards a cycle -----------------------
class R { method raku() { self.rakuseen('R', { 'R.new(' ~ 'ok' ~ ')' }) } }
ok(R.new.raku eq 'R.new(ok)', '.rakuseen runs its block');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
