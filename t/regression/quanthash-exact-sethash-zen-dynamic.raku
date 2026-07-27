# Regression: exact Mix weights, SetHash mutators, the adverbed `<>` zen slice,
# and what `.dynamic` asks.
#   * makeBaggy's fractional-Mix merge summed weights as C DOUBLES, so the exact
#     Rats 1/10 and 1/50 came out as 0.12000000000000001 — and, being a Num, the
#     result then printed at full Num precision as well. It goes through the
#     exact tower now. Bag/Set counts were never affected; they take the integer
#     `add` path.
#   * `.set`/`.unset` did not exist. They are SetHash-only in Rakudo (`Set.^can`
#     and `BagHash.^can` are both False), take one possibly-listy positional,
#     and return Nil.
#   * an EMPTY `<>` was dropped by the parser, so a following `:k`/`:v`/`:kv`
#     adverb had no Index to attach to and was silently discarded — `%h<>:k`
#     printed the whole hash. `%h{}` and `@a[]` already synthesised an Index for
#     exactly this case.
#   * `.dynamic` was answered from the variable NAME for every sigil. A bare
#     `$s.dynamic` DECONTAINERIZES first and asks the held value, so it is the
#     Hash's or Array's own flag (False); only `$s.VAR.dynamic` asks the Scalar.
#     For `@` and `%` the value IS the container, so there the name answer is the
#     right one and still fires.
# NOT asserted here: the KEY ORDER of a multi-key zen slice. rakupp iterates
# sorted and Rakudo's is its own hash order — this same expression gave `(b a)`
# on one run and `(a b)` on another. Every check below is order-independent.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# exact Mix weights
check(Mix.new-from-pairs('sugar' => 0.1, 'sugar' => 0.02).gist, 'Mix(sugar(0.12))',
      'fractional weights merge exactly');
check((sugar => 0.1, sugar => 0.02).Mix<sugar>, '0.12', 'and the subscript reads the exact value');
check((a => 1/3, a => 1/3).Mix<a>, '0.666667', 'a repeating Rat stays a Rat');
check(bag(<a a b>).gist,  'Bag(a(2) b)',  'Bag counts are unaffected');
check(set(<a a b>).gist,  'Set(a b)',     'and so are Set members');
check((a => 0.5, a => -0.5).Mix.elems, '0', 'weights cancelling to zero drop the key');

# SetHash.set / .unset
my $f = <peach>.SetHash; $f.set('apple');
check($f.keys.sort.join(','), 'apple,peach', 'set adds an element');
my $g = <a>.SetHash; $g.set(<b c>);
check($g.keys.sort.join(','), 'a,b,c', 'and takes a list');
$g.unset(<b c>);
check($g.keys.sort.join(','), 'a', 'unset removes them again');
check($g.set('z').defined.gist, 'False', 'both return Nil');
check((<a>.SetHash).^can('unset').Bool.gist, (<a>.SetHash).^can('set').Bool.gist,
      'set and unset are equally visible');

# the adverbed zen slice
my %h1 = a => 1;
check((%h1<>:k).gist,  '(a)',   'an adverb after <> is no longer dropped');
check((%h1<>:v).gist,  '(1)',   ':v too');
check((%h1<>:kv).gist, '(a 1)', 'and :kv');
my %h2 = a => 1, b => 2;
check((%h2<>:k).sort.join(','), 'a,b', 'over several keys');
check((%h2{}:k).sort.join(','), 'a,b', 'the brace form still agrees');
my $x = 5;
check($x<>, '5', 'a bare <> still decontainerizes');

# what .dynamic asks
my $sh is dynamic = %('apples' => 5);
my $sa is dynamic = [1, 2, 3];
my @c is dynamic;
my %g2 is dynamic;
my $t is dynamic = 5;
check($sh.dynamic.gist, 'False', 'a $-sigil variable asks the held Hash');
check($sa.dynamic.gist, 'False', 'and the held Array');
check(@c.dynamic.gist,  'True',  'an @-sigil variable IS the container');
check(%g2.dynamic.gist, 'True',  'and so is a %-sigil one');
check($t.VAR.dynamic.gist, 'True', '.VAR still asks the Scalar');
my $nd = 5;
check($nd.VAR.dynamic.gist, 'False', 'and answers False when it is not dynamic');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
