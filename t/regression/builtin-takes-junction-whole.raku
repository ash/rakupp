# The built-in methods whose parameters are slurpies or Mu take a Junction
# whole, as Rakudo's do: `@b.append(1|2)` threaded, and appended 1 and then 2,
# `@c.unshift(1&2)` the same, `Set.new(1|2)` was a junction of two Sets and
# `.splice(0, 1, 5|6)` a junction of two splices. A Junction is one item where
# a list is flattened, and an Array holding one alone is `[any(1, 2)]`, no
# trailing comma. A method that takes a plain value still threads.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @b; @b.append(1|2);
ck @b.raku, '[any(1, 2)]', '.append';
my @c; @c.unshift(1&2);
ck @c.raku, '[all(1, 2)]', '.unshift';
my @p; @p.prepend(1|2);
ck @p.raku, '[any(1, 2)]', '.prepend';
my @a; @a.push(1|2);
ck @a.raku, '[any(1, 2)]', '.push';
my @m; @m.append(1, 2|3);
ck @m.raku, '[1, any(2, 3)]', 'among others';
ck Set.new(1|2).raku, 'Set.new(any(1, 2))', 'Set.new';
ck Bag.new(1|2).raku, '(any(1, 2)=>1).Bag', 'Bag.new';
ck Mix.new(1|2).raku, '(any(1, 2)=>1).Mix', 'Mix.new';
ck [1, 2].splice(0, 1, 5|6).raku, '[1]', '.splice answers';
my @s = 1, 2; @s.splice(0, 1, 5|6);
ck @s.raku, '[any(5, 6), 2]', 'and splices it in';
ck [1|2].raku, '[any(1, 2)]', 'an Array of one junction';
ck (1|2,).raku, '(any(1, 2),)', 'a List keeps its comma';
ck [[1, 2],].raku, '[[1, 2],]', 'an Array of an Array too';
ck ("abc".contains("a"|"z")).so, True, 'a plain parameter threads';

say 'PASS';
