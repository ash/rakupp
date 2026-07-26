# Regression: a Pair as an Associative container, as a smartmatch PATTERN, and
# how its pieces render.
#   * a Pair is Associative on its ONE key: `$p<a>` is the value, `$p<a>:exists`
#     is True, and any other key is Nil — not the `(Any)` a Hash answers, since a
#     Pair has no element type to default to. `:exists` did not know about Pairs
#     at all and always said False.
#   * `$val ~~ :method` calls that method on $val and tests the result against the
#     pair's value, so `3 ~~ :is-prime` is True and `4 ~~ :!is-prime` is too.
#     Matching a Pair or Hash against a Pair stays an ordinary value compare.
#   * `.fmt` on a Pair formats its KEY and VALUE as the two arguments.
#   * `.invert`/`.antipairs` carried a redundant key VALUE for a Str key, which
#     made the result render as `"bar" => "foo"` instead of `:bar("foo")`.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a Pair is associative on its key
my $pair = a => 5;
check($pair<a>,           '5',     'reading-the-key');
check($pair<a>:exists,    'True',  'the-key-exists');
check($pair<no-such-key>.gist, 'Nil', 'another-key-is-nil');
check($pair<no-such-key>:exists, 'False', 'and-does-not-exist');
check($pair.key,          'a',     'key');
check($pair.value,        '5',     'value');
# a Hash still answers (Any) for a missing key
my %h = a => 1;
check(%h<zz>.gist,        '(Any)', 'a-missing-hash-key-is-any');
check(%h<a>:exists,       'True',  'hash-exists-is-unchanged');
check(%h<zz>:exists,      'False', 'hash-missing-exists');

# a Pair as a smartmatch pattern names a method
check((3 ~~ :is-prime).gist,                'True',  'pair-pattern-calls-the-method');
check((4 ~~ :is-prime).gist,                'False', 'and-reports-a-false-answer');
check((4 ~~ :!is-prime).gist,               'True',  'a-negated-pair-pattern');
check((3 ~~ (is-prime => 'truthy')).gist,   'True',  'any-truthy-value-does');
check(('abc' ~~ :chars).gist,               'True',  'a-non-boolean-method-is-tested-for-truth');
# an Associative left side is a plain value compare
check(((a => 1) ~~ (a => 1)).gist,          'True',  'pair-against-pair-compares-values');
check((%h ~~ (a => 1)).gist,                'True',  'hash-against-pair-compares-too');
# a regex right side is untouched (it must not be evaluated early)
check(('abc' ~~ /b/).Str,                   'b',     'a-regex-pattern-still-matches');
check(('abc' ~~ 'abc').gist,                'True',  'a-string-pattern-still-matches');

# .fmt formats key and value
check(:Earth(1).fmt("%s is %.3f AU away from the sun"),
      'Earth is 1.000 AU away from the sun', 'pair-fmt-with-two-directives');
check((a => 1).fmt("%s=%s"), 'a=1',   'pair-fmt');
check((a => 1).fmt,          "a\t1",  'pair-fmt-default-is-tab-separated');

# .invert / .antipairs render as colonpairs for a Str key
check(:foo<bar>.invert.raku,  '(:bar("foo"),).Seq', 'invert-renders-as-a-colonpair');
check(:foo<Raku is great>.invert.raku,
      '(:Raku("foo"), :is("foo"), :great("foo")).Seq', 'invert-over-a-list-value');
check((a => 1).invert.raku,    '(1 => "a",).Seq', 'a-non-str-key-keeps-its-value');
check((a => 1).antipairs.raku, '(1 => "a",).Seq', 'antipairs-agrees');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
