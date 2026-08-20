# Regression: three more from the Weekly Challenge sweep.
#
# 1. `.Array` (and `.Hash`) on a type object or an undefined value: `Any.Array`
#    is [(Any)], the same one-element reading `.List` already gave. Coercing an
#    absent hash lookup that way — `%graph{$k}.Array` — died with "No such
#    method 'Array' for invocant of type 'Any'".
#
# 2. `.head` / `.tail` with no argument hand back the ELEMENT'S container, so
#    they are assignable: `@stack.tail = …` replaces the top of a stack.
#
# 3. A LIST on the right of a smartmatch compares element by element, and a
#    non-Positional left side is not one: `Any ~~ Empty` and `1 ~~ (1,)` are
#    False. rakupp compared through valueEq, which called an undefined value and
#    an empty list equal — so `while @stack.tail ~~ Empty` looped on a stack
#    whose top was merely undefined.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. .Array / .Hash on the undefined ---
check Any.Array.raku,  '[Any]',  'Any.Array is a one-element Array';
check Int.Array.raku,  '[Int]',  '…of whatever type object it was';
check Any.Hash.raku,   '{}',     'Any.Hash is an empty Hash';
my %g;
check %g{9}.Array.raku, '[Any]', 'an absent hash lookup coerces the same way';
check Any.List.raku,   '(Any,)', 'and .List is unchanged';
check "x".Array.raku,  '["x"]',  'a defined scalar still boxes itself';

# --- 2. .head / .tail as targets ---
my @s = ([1,2],[3]);
@s.tail = (5,6);
check @s.raku, '[[1, 2], (5, 6)]', '.tail is assignable';
my @a = 1, 2;
@a.head = 9;
check @a.raku, '[9, 2]', '…and so is .head';
check @a.tail, 2, 'reading them still works';
check (1,2).tail, 2, '…on a List too';
my @stack = [[1],[2]];
@stack.tail.push(9);
check @stack.raku, '[[1], [2, 9]]', 'and mutating through them still works';

# --- 3. a list on the right of ~~ ---
check (Any ~~ Empty),   False, 'an undefined value is not an empty list';
check (1 ~~ (1,)),      False, 'nor is a number a one-element list';
check ("" ~~ Empty),    False, 'nor is the empty string';
check (Nil ~~ Empty),   False, 'nor Nil';
check (() ~~ Empty),    True,  'an empty list IS one';
check ([] ~~ Empty),    True,  '…and so is an empty Array';
check ((1,2) ~~ (1,2)), True,  'and a matching list still matches';
check ([1,2] ~~ (1,2)), True,  '…whichever container it is in';
check ((1..2) ~~ (1,2)), True, '…a Range too';
my @empty;
check (@empty.tail ~~ Empty), False, 'the tail of an empty array is undefined, not empty';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
