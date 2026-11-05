# A hash's first chunk of entries may be any size now (2026-11-05, V6-PLAN
# P3 phase 0): an object's attribute table reserves exactly its class's
# attribute count, and a copied hash exactly its size, where both were rounded
# up to a power of two (five attributes held eight 120-byte entries). Later
# chunks still double, so an element's place is found from where the first
# chunk ends. The same batch moved a hash's rare fields (the aliases'
# generation, an object-keyed hash's original keys) and an object's boxed value
# and monitor lock behind one pointer each, and folded an entry's dead flag
# into its hash. Every check walks one of those: order and lookups across the
# chunk boundaries of odd first sizes, a copy that grows, attributes a mixin
# adds to a full table, deletes, object keys, Pairs that outlive a clear.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# classes whose tables are sized 3, 5, 6 and 9: every attribute read back
class A3 { has $.a = 1; has $.b = 2; has $.c = 3 }
class A5 { has $.a = 1; has $.b = 2; has $.c = 3; has $.d = 4; has $.e = 5 }
class A6 is A3 { has $.d = 4; has $.e = 5; has $.f = 6 }
class A9 is A5 { has $.f = 6; has $.g = 7; has $.h = 8; has $.i = 9 }
ck(A3.new.raku, 'A3.new(a => 1, b => 2, c => 3)', 'three attributes');
my ($a5, $a6, $a9) = A5.new, A6.new, A9.new(i => 90);
ck(($a5.a, $a5.b, $a5.c, $a5.d, $a5.e), (1, 2, 3, 4, 5), 'five attributes');
ck(($a6.a, $a6.b, $a6.c, $a6.d, $a6.e, $a6.f), (1, 2, 3, 4, 5, 6), 'six, three of them inherited');
ck(($a9.a, $a9.e, $a9.f, $a9.i), (1, 5, 6, 90), 'nine, one set by .new');
my $c = A9.new.clone(g => 70);
ck(($c.g, $c.h, $c.a), (70, 8, 1), 'a clone of nine with one changed');

# a role mixed in adds attributes past the table's exact size
role Extra { has $.x = 10; has $.y = 20 }
my $m = A3.new but Extra;
ck(($m.a, $m.c, $m.x, $m.y), (1, 3, 10, 20), 'a mixin adds two attributes to a full table of three');
my $o = A5.new;
$o does Extra;
ck(($o.e, $o.x), (5, 10), '`does` on an object of five');

# a copied hash is sized to its size exactly, then grows through later chunks
my %src = (^5).map({ "k$_" => $_ });
my %h = %src;
%h{"n$_"} = $_ * 10 for ^40;
ck(%h.elems, 45, 'a copy of five, grown by forty');
ck((%h<k0>, %h<k4>, %h<n0>, %h<n39>), (0, 4, 0, 390), '…every key found');
ck(%h.keys.grep(*.starts-with("n")).map({ .substr(1).Int }).sort.List, (^40).List, '…every new key iterated');
%h{"k$_"}:delete for ^5;
%h{"n$_"}:delete for ^20;
ck((%h.elems, %h<n20>, %h<k1>:exists), (20, 200, False), 'deletes across the chunks');
%h<k1> = "back";
ck((%h.elems, %h<k1>), (21, "back"), 'a deleted key stored again');
my %small = a => 1, b => 2, c => 3;
my %copy = %small;
%copy<d> = 4; %copy<e> = 5;
ck((%small.elems, %copy.elems, %copy<e>), (3, 5, 5), 'a copy of three grown to five, its source unchanged');

# the moved fields: object keys, Pairs over entries, a hash of hashes
my %ok{Any};
my $key = [1, 2];
%ok{$key} = "list";
%ok{42} = "int";
ck(%ok.keys.map(*.^name).sort.List, ("Array", "Int"), 'an object hash keeps its keys');
my %ok2{Any} = %ok;
ck(%ok2{42}, "int", '…and a copy of it');
my %p = x => 1, y => 2;
my @pairs = %p.pairs.sort(*.key);
@pairs[1].value = 20;
ck(%p<y>, 20, 'a Pair writes through to its entry');
%p = ();
ck(@pairs».value.List, (1, 20), '…and keeps it past a clear');
my %shared = (^20).map({ $_ => $_ });
ck(await((^4).map({ start { %shared.pairs.map(*.value).sum } })).List, (190, 190, 190, 190),
   'four threads take Pairs from one hash');
my %hoh;
%hoh{$_}{"v"} = $_ for ^30;
ck((%hoh.elems, %hoh{29}<v>), (30, 29), 'a hash of thirty hashes');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
