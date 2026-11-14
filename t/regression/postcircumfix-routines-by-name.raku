# `&postcircumfix:<[ ]>` and `&postcircumfix:<{ }>` are routines (rakudo#2000):
# called by name or through a variable they subscript, the named arguments are
# the adverbs, and a third positional is assigned, as Rakudo's assign candidate.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my %h = a => 1, b => 2, c => 3;
my @a = 10, 20, 30, 40;

check (&postcircumfix:<{ }> ~~ Callable), 'True', 'the hash subscript is Callable';
check (&postcircumfix:<[ ]> ~~ Callable), 'True', 'the array subscript is Callable';
check &postcircumfix:<{ }>.name, 'postcircumfix:<{ }>', 'its name';
check &postcircumfix:<{ }>(%h, 'a'), '1', 'a key';
check &postcircumfix:<[ ]>(@a, 1), '20', 'a position';
check &postcircumfix:<{ }>(%h, <a b>).raku, '(1, 2)', 'a key slice';
check &postcircumfix:<[ ]>(@a, 1..2).raku, '(20, 30)', 'a Range slice';
my $r = 1..2;
check &postcircumfix:<[ ]>(@a, $r), '30', 'an itemized Range is one position';
check &postcircumfix:<[ ]>(@a, *-1), '40', 'a WhateverCode position';
check &postcircumfix:<{ }>(%h, 'a', :exists), 'True', ':exists';
check &postcircumfix:<{ }>(%h, 'zz', :!exists), 'True', ':!exists';
check &postcircumfix:<{ }>(%h, <b c>, :kv).raku, '("b", 2, "c", 3)', ':kv';
check &postcircumfix:<{ }>(%h, 'c', :delete), '3', ':delete answers the value';
check %h.keys.sort.join(','), 'a,b', '…and deletes it from the caller\'s hash';
check &postcircumfix:<[ ]>(@a).elems, '4', 'one operand is the zen slice';

my &pc = &postcircumfix:<[ ]>;
check pc("abc".comb, 1), 'b', 'through a variable';
check (%h, %h).map({ &postcircumfix:<{ }>($_, 'b') }).join(','), '2,2', 'inside a block';

check &postcircumfix:<[ ]>(@a, 1, 77), '77', 'three operands assign';
check @a.raku, '[10, 77, 30, 40]', '…into the caller\'s array';
&postcircumfix:<{ }>(%h, 'z', 5);
check %h<z>, '5', '…and into the caller\'s hash';

check postcircumfix:<{ }>(%h, 'a', :exists), 'True', 'the bare name takes adverbs too';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
