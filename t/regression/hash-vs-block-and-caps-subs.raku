# Regression: when `{…}` is a Hash composer and when it is a Block, and the
# all-caps introspection subs.
#   * a capitalized bareword before `{` is a type with a block body (`if Mu {…}`),
#     which is right for types and wrong for WHAT/WHO/HOW/VAR/WHICH/WHY — those
#     are ROUTINES, so `WHAT {3 => 4}` was parsing as the bareword `WHAT` followed
#     by an unrelated block and answering (Any).
#   * a brace that LOOKS like a composer (empty, or opening with a Pair or a
#     %-var) is still a Block if it uses the topic: `{3 => 4, :b}` is a Hash,
#     `{3 => 4, :b($_)}` and `{3 => 4, :b(.Num)}` are Blocks.
#   * that test looks at the composer's OWN level only. A nested block owns its
#     own topic, so `{ :out{ .contains: 'x' } }` is a Hash whose value is a Block
#     — scanning into it demoted every such hash to a block and cost seven
#     assertions in t/test-util/01-is-eqv.t.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the all-caps subs take a brace argument
check((WHAT {3 => 4}).gist,  '(Hash)',  'WHAT of a hash composer');
check((WHAT {3 => 4, :b}).gist, '(Hash)', 'with a colon-pair too');
check((WHAT [1, 2]).gist,    '(Array)', 'a bracket argument still works');
check((WHAT 5).gist,         '(Int)',   'and a plain one');
check((WHICH 5).Str.contains('Int'), 'True', 'WHICH takes an argument as well');
# a real type before a block is still a condition + body, not a listop call
my $r1 = 'none'; if Mu   { $r1 = 'then' } else { $r1 = 'else' }
my $r2 = 'none'; if True { $r2 = 'then' } else { $r2 = 'else' }
check($r1, 'else', 'a type before a block is a block body, and Mu is falsy');
check($r2, 'then', 'a true capitalized term takes the then branch');

# topic use makes a composer a block
check({3 => 4, :b}.^name,        'Hash',  'no topic: a Hash');
check({}.^name,                  'Hash',  'and an empty brace');
check({a => 1}.^name,            'Hash',  'and a fat-arrow pair');
check(do { given 3 { {3 => 4, :b($_)}.^name } },   'Block', '$_ makes it a Block');
check(do { given 3 { {3 => 4, :b(.Num)}.^name } }, 'Block', 'and so does a bare .method');
check({ 'a', :b(3), 'c' }.^name, 'Block', 'a non-composer stays a Block');

# …but only at the composer's own level
my %nested = :out{ .contains('x') }, :err{ .chars };
check(%nested.^name,        'Hash',  'a nested block does not demote the composer');
check(%nested.keys.sort.gist, '(err out)', 'and its keys are intact');
check(%nested<out>.^name,   'Block', 'the value is the Block');
check(%nested<out>('axb').gist, 'True', 'which still closes over the topic');

# the ordinary uses of both are untouched
check((1, 2).map({ $_ * 2 }).gist, '(2 4)', 'a map block');
my %h = a => 1, b => 2;
check(%h.raku, '{:a(1), :b(2)}', 'an ordinary hash');
check(%h.map({ .key }).sort.gist, '(a b)', 'a block over its pairs');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
