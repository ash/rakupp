# Regression: `.WHICH` is a canonical IDENTITY, and a set operator's result carries
# its elements rather than their lookup keys.
#   * `.WHICH` was `TypeName|Str`, which is wrong for anything whose stringification
#     is not its identity: an allomorph has to carry BOTH halves
#     (`IntStr|Int|42|Str|42`), a Rat its exact numerator/denominator
#     (`Rat|421/10`, not `Rat|42.1`), a Complex its two parts, a Bool 1/0.
#   * a quanthash keeps the element in the count's pairKey, but the SET OPERATORS
#     built their own weight maps and threw the element away, so a result rendered
#     its keys as strings: `set(1,2) ∪ set(2,3)` was `Set.new("1","2","3")`.
#     The element now travels with the key through the operator.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# identity
check(<42>.WHICH,    'IntStr|Int|42|Str|42',     'an allomorph carries both halves');
check(<42.5>.WHICH,  'RatStr|Rat|85/2|Str|42.5', 'including the exact rational half');
check(42.WHICH,      'Int|42',                   'a plain Int');
check("42".WHICH,    'Str|42',                   'a plain Str');
check((1/2).WHICH,   'Rat|1/2',                  'a Rat is numerator/denominator');
check(42.1.WHICH,    'Rat|421/10',               'not its decimal rendering');
check((1+2i).WHICH,  'Complex|1|2',              'a Complex is its two parts');
check(True.WHICH,    'Bool|1',                   'a Bool is 1');
check(False.WHICH,   'Bool|0',                   'or 0');
check(1e0.WHICH,     'Num|1',                    'a Num');
# and it is stable
check((1/2).WHICH eq (2/4).WHICH ?? 'same' !! 'differ', 'same', 'equal Rats share an identity');

# a set operator result renders its elements
check((set(1,2) ∩ set(2,3)).raku,       'Set.new(2)',        'intersection');
# (element ORDER in a quanthash is arbitrary — Rakudo's differs from ours — so
#  these compare the elements themselves, sorted, rather than the rendering)
check((bag(1,1,2) ⊎ bag(2)).kv.sort.gist, '(1 2 2 2)', 'bag addition keeps Int elements');
check((bag(1,1,2) ⊎ bag(2)).keys.map(*.^name).sort.gist, '(Int Int)', 'as Ints, not strings');
check(((1/2, 1/3).Set ∪ set(1/4)).keys.sort.map(*.raku).gist,
      '(0.25 <1/3> 0.5)', 'Rat elements survive the operator');
check(((Set) (|) (Set)).raku,           'Set.new(Set)',      'a type object stays a type object');
check((set(1,2) ∪ set(2,3)).elems,      '3',                 'and the arithmetic is unchanged');
check((<a b> ∪ <b c>).elems,            '3',                 'string elements too');
check((set(1,2) ∪ set(2,3)).keys.sort.gist, '(1 2 3)',       'keys come back as their values');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
