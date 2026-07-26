# Regression: what a TYPE OBJECT means in list context, and adverbs the SUB
# forms were dropping.
#   * a type object has no ELEMENTS, so `.pairs`/`.antipairs`/`.kv`/`.keys`/
#     `.values`/`.invert` are empty and `.reduce` is Nil — but it is still one
#     THING to iterate, so `.map`/`.sort`/`.grep`/`.unique` see a one-element
#     list. Every one of those was a dispatch failure before.
#   * `minmax` takes a &mapper (or `:by`), which orders the comparison while the
#     endpoints stay the original elements. The SUB form now delegates to the
#     method, as `min`/`max` already did, so the adverb reaches it.
#   * the `classify`/`categorize` SUBS were treating their adverbs as list
#     elements, so `categorize(&f, @list, :as(&g))` classified the ADVERB.
#   * `.repeated` takes `:as` (compare the mapped value) and `:with` (supply the
#     comparison), the latter needing a linear scan rather than a set.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a type object has no elements…
check(Num.pairs.gist,       '()',  'pairs of a type object');
check(Range.antipairs.gist, '()',  'antipairs');
check(Sub.kv.gist,          '()',  'kv');
check(Int.keys.gist,        '()',  'keys');
check(Int.values.gist,      '()',  'values');
check(Int.invert.gist,      '()',  'invert');
check(Range.reduce(&infix:<+>).gist, 'Nil', 'reduce is nil');
check(Str.reduce(&infix:<~>).gist,   'Nil', 'for any type');
# …but is still one thing to iterate
check(Int.map({ $_ }).gist, '((Int))', 'map sees one element');
check(Int.sort.gist,        '((Int))', 'and so does sort');
check(Int.grep({ True }).gist, '((Int))', 'and grep');
check(Int.unique.gist,      '((Int))', 'and unique');
check(Int.first({ True }).gist, '(Int)', 'first answers the element itself');
check(Int.head.gist,        '(Int)',   'and so does head');
# an INSTANCE is unaffected
check(5.pairs.gist,         '(0 => 5)', 'a real value still pairs up');
check(5.map({ $_ + 1 }).gist, '(6)',    'and maps');

# minmax takes a mapper
check((1, 7, 3).minmax.gist,          '1..7', 'minmax');
check((1, 7, 3).minmax({ -$_ }).gist, '7..1', 'minmax with a mapper');
check(minmax(1, 7, 3).gist,           '1..7', 'the minmax sub');
check(minmax(1, 7, 3, :by( -* )).gist, '7..1', 'the sub takes :by');
check((1..5).minmax.gist,             '(1 5)', 'a range answers the pair');

# the classify/categorize subs forward their adverbs
check(categorize(* %% 3, -5..5, as => &abs).gist,
      '{False => [5 4 2 1 1 2 4 5], True => [3 0 3]}', 'categorize with :as');
check(classify(* %% 3, -5..5, as => &abs).gist,
      '{False => [5 4 2 1 1 2 4 5], True => [3 0 3]}', 'classify with :as');
check(classify(*.chars, <a bb ccc>).gist,
      '{1 => [a], 2 => [bb], 3 => [ccc]}', 'and still work without one');

# .repeated takes :as and :with
check(<1 -1 2 -2 3>.repeated(:as(&abs), :with(&[==])).gist, '(-1 -2)', 'repeated with :as and :with');
check((3+3i, 3+2i, 2+1i).repeated(as => *.re).gist,        '(3+2i)',  'repeated with :as');
check(<a b a c b>.repeated.gist,                           '(a b)',   'plain repeated');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
