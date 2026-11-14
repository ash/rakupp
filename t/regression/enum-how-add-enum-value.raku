# The EnumHOW protocol, as in Rakudo (rakudo#2165): `E.HOW.add_enum_value(E,
# key => value)` was "No such method", and `.pick` of an enum whose values are
# not Ints answered members whose `.value` was 0. `.^enum_values` is a Hash.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

enum Chain ( A => "T", G => "C", T => "A" );
check (Chain.pick.value ~~ any <T C A>), True, 'a picked member keeps its Str value (rakudo#2165)';
check Chain.pick(*)».value.sort.join, 'ACT', 'every member, by pick(*)';
check Chain.roll(3)».value.grep(* eq "0").elems, 0, '…and by roll';

enum Chain2 ();
Chain2.HOW.add_enum_value(Chain2, 'A' => 'T');
Chain2.HOW.add_enum_value(Chain2, 'C' => 'G');
check Chain2.enums.raku, 'Map.new((:A("T"),:C("G")))', 'add_enum_value adds members';
check Chain2.pick(*).map({ .key ~ .value }).sort.join(','), 'AT,CG', 'the added members pick';
check Chain2.HOW.enum_values(Chain2).^name, 'Hash', 'enum_values is a Hash';
enum N <x y>;
check N.^enum_values.^name, 'Hash', '…and so is .^enum_values';
check N.enums.^name, 'Map', '…where .enums is a Map';
check N.HOW.enum_from_value(N, 1), 'y', 'enum_from_value';
check N.HOW.enum_from_value(N, 7).raku, 'Mu', '…and Mu for no member';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
