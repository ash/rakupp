# A brace tight against a routine's name subscripts what the call answers, as
# in Rakudo (rakudo#5144's `T{"max-rss"}`): it passed the block as an argument.
# With a space between them the block is still the argument.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my %h = a => 1, b => 2;
sub G() { %h }
check G{"a"}, 1, 'name{…} subscripts (rakudo#5144)';
check G{"a", "b"}.join(','), '1,2', '…a slice too';
sub F { %h }
check F{"b"}, 2, '…of a routine with no signature';
sub K(&b) { b() }
check (K {"blk"}), 'blk', 'name {…} with a space passes the block';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
