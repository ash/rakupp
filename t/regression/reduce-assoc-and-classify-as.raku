# Regression: two List defects the documentation surfaced.
#   1. `.reduce`/`.produce` folded LEFT even for a RIGHT-associative operator, so
#      `(2,3,4).reduce(&[**])` gave (2**3)**4 instead of 2**(3**4). The metaop
#      forms `[**]` / `[\**]` were already right — only the METHOD forms, which
#      are handed an `&[OP]` callable, were not. That callable's name is the only
#      place the associativity is recorded.
#   2. `.classify`/`.categorize` ignored `:as`, which maps the STORED value while
#      the key still comes from the classifier.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. right-associative folds
check((2, 3, 4).reduce(&[**]),  '2417851639229258349412352', 'reduce-pow-is-right-assoc');
check((2, 3, 4).produce(&[**]).gist, '(4 81 2417851639229258349412352)', 'produce-pow-is-right-assoc');
check((1, 2, 3, 4).reduce(&[=>]).gist, '1 => 2 => 3 => 4', 'reduce-fatarrow-is-right-assoc');
# the metaop spellings agree with the method ones
check(([**] 2, 3, 4),      '2417851639229258349412352', 'metaop-pow-reduce');
check(([\**] 2, 3, 4).gist, '(4 81 2417851639229258349412352)', 'metaop-pow-scan');
# LEFT-associative operators are unchanged
check((1, 2, 3).reduce(&[-]),   '-4',        'reduce-minus-is-left-assoc');
check((1, 2, 3).produce(&[-]).gist, '(1 -1 -4)', 'produce-minus-is-left-assoc');
check((1, 2, 3).reduce(&[+]),   '6',         'reduce-plus');
check((1, 2, 3).produce(&[+]).gist, '(1 3 6)', 'produce-plus');
# a plain closure still folds left
check((1, 2, 3, 4).reduce(-> $a, $b { ($a, $b) }).gist, '(((1 2) 3) 4)', 'closure-folds-left');

# 2. `:as` maps what is stored, not what is classified by
check(<Moe Innie Minnie>.classify(*.chars, :as(*.lc)).gist,
      '{3 => [moe], 5 => [innie], 6 => [minnie]}', 'classify-as');
check(<a bb ccc>.categorize(*.chars, :as(*.uc)).gist,
      '{1 => [A], 2 => [BB], 3 => [CCC]}',         'categorize-as');
check(<a bb ccc>.classify(*.chars).gist,
      '{1 => [a], 2 => [bb], 3 => [ccc]}',         'classify-without-as-is-unchanged');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
