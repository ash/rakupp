# Regression: a List, Array, Hash or Pair renders a Version, a Date, a
# DateTime, $*KERNEL / $*DISTRO / $*VM or a Uni through that value's own
# `.raku`, nested as at the top, and a Pair keeps a Version, IO::Path or Buf
# KEY as what it is.
#
# Container `.raku` rendered these values itself: `(v1.2,).raku` was
# `("1.2",)`, a Date in a Hash was the hash of its fields
# (`{:d(${:day(3), :month(2), :year(2026)})}`), `($*KERNEL,).raku` was
# `(${:name("darwin")},)`, a NFC was `$[97, 98]`, where Rakudo writes `(v1.2,)`,
# `{:d(Date.new(2026,2,3))}`, `(Kernel.new(…),)` and `(Uni.new(…).NFC,)`.
# `[Set.new(1),]` and `[Date.new(…),]` had a trailing comma that Rakudo gives
# only a lone Hash or Map. And `v1.2 => 1` stored its key as the Str "1.2", so
# `.key.WHAT` was (Str) and the Pair's `.raku` was `"1.2" => 1`.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($ok, $what) { @fail.push($what) unless $ok }
sub is($got, $want, $what) { check $got eq $want, "$what: got {$got.raku}, want {$want.raku}" }

my $d  = Date.new(2026, 2, 3);
my $dt = DateTime.new(2026, 1, 1, 0, 0, 0);
is (v1.2,).raku, '(v1.2,)', 'a Version in a List';
is [v1.2].raku, '[v1.2]', 'a Version in an Array';
is (a => v1.2).raku, ':a(v1.2)', 'a Version as a Pair value';
is {a => $d}.raku, '{:a(Date.new(2026,2,3))}', 'a Date in a Hash';
is (v1.2, $d).raku, '(v1.2, Date.new(2026,2,3))', 'a Version and a Date in a List';
is ($dt,).raku, "({$dt.raku},)", 'a DateTime in a List';
is ("ab".NFC, Uni.new(0x61)).raku, '(Uni.new(0x0061, 0x0062).NFC, Uni.new(0x0061))', 'a NFC and a Uni in a List';
is ((v1.2,),).raku, '((v1.2,),)', 'a Version two lists down';
is (Version.new("1a"),).raku, '(v1.a,)', 'a Version.new in a List';
my @a = v1.2, $d;
is @a.raku, '[v1.2, Date.new(2026,2,3)]', 'an Array variable';
for $*KERNEL, $*DISTRO, $*VM -> $o {
    is ($o,).raku, "({$o.raku},)", "{$o.^name} in a List";
    is [$o].raku, "[{$o.raku}]", "{$o.^name} in an Array";
}
# a one-element Array marks a lone Hash or Map with a trailing comma, and
# nothing else that is a hash underneath: [Set.new(1)], [Date.new(…)]
is [set(1)].raku, '[Set.new(1)]', 'a Set alone in an Array';
is [bag(1)].raku, '[(1=>1).Bag]', 'a Bag alone in an Array';
is [$d].raku, '[Date.new(2026,2,3)]', 'a Date alone in an Array';
is [${a => 1},].raku, '[{:a(1)},]', 'a Hash alone in an Array keeps its comma';
is [$(Map.new((a => 1))),].raku, '[Map.new((:a(1))),]', 'a Map alone in an Array keeps its comma';
is $*KERNEL.values.raku, "({$*KERNEL.raku},)", '$*KERNEL.values';

# a Pair keeps a Version, IO::Path or Buf key as it is
check (v1.2 => 1).key ~~ Version, 'a Version key stays a Version';
check (v1.2 => 1).key eqv v1.2, 'a Version key eqv the Version';
my $k = v1.2;
check ($k => 1).key ~~ Version, 'a Version key from a variable';
is (v1.2 => 1).raku, 'v1.2 => 1', 'a Version-keyed Pair';
check ("/tmp".IO => 1).key ~~ IO::Path, 'an IO::Path key stays an IO::Path';
check ("x".encode => 1).key ~~ Blob, 'a Buf key stays a Buf';
check ("a" => 1).key ~~ Str && ("a" => 1).raku eq ':a(1)', 'a Str key is still a Str';
my %h = v1.2 => 1;
check %h<1.2> == 1, 'a Hash keys a Version Pair by its string';
check (:{ v1.2 => 1 }).keys[0] ~~ Version, 'an object Hash keeps the Version';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
