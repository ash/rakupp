# Regression: the `:i`/`:ignorecase` adverb on the Str predicates, and what
# .indices actually answers.
#   * .contains/.starts-with/.ends-with silently IGNORED :i — every
#     case-insensitive check came back False. (.index/.rindex/.substr-eq already
#     honoured it; these three did not.) A bare `:i` means True, so the pair may
#     carry no value.
#   * .indices did not know the adverb at all, and answered BYTE offsets where
#     Rakudo answers CHARACTER positions — the two only agree on ASCII.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# :i on the three predicates
check("Hello".starts-with("hell", :i), 'True',  'starts-with-i');
check("Hello".ends-with("LO", :i),     'True',  'ends-with-i');
check("Hello".contains("ELL", :i),     'True',  'contains-i');
check("Hello".starts-with("hell", :ignorecase), 'True', 'starts-with-long-name');
# … and without it they stay case-SENSITIVE
check("Hello".starts-with("hell"), 'False', 'starts-with-is-sensitive-by-default');
check("Hello".ends-with("LO"),     'False', 'ends-with-is-sensitive-by-default');
check("Hello".contains("ELL"),     'False', 'contains-is-sensitive-by-default');
check("Hello".contains("ell"),     'True',  'contains-exact-still-works');
check("Hello".starts-with("H"),    'True',  'starts-with-exact-still-works');
# non-ASCII folds too
check("ÄÖ".contains("äö", :i),     'True',  'ignorecase-is-unicode-aware');

# .indices — the adverb, and CHARACTER positions
check("Hello".indices("L", :i).raku,   '(2, 3)', 'indices-i');
check("Hello".indices("L").raku,       '()',     'indices-is-sensitive-by-default');
check("banana".indices("a").raku,      '(1, 3, 5)', 'indices-plain');
check("banana".indices("ana").raku,    '(1,)',      'indices-non-overlapping');
check("banana".indices("ana", :overlap).raku, '(1, 3)', 'indices-overlap');
# a multi-byte prefix must not shift the answers
check("Ünïcödé ïs fun".indices("ï").raku, '(2, 8)', 'indices-are-characters-not-bytes');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
