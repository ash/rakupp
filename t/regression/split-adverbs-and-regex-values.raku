# Regression: `.split`'s separator-keeping adverbs, `.words`' limit, and where a
# bare regex literal is a VALUE rather than an immediate match.
#   * `:v`/`:k`/`:kv`/`:p` interleave the separators with the pieces. `:k` is the
#     index of the DELIMITER that matched — its position in the delimiter list,
#     not its offset in the string — so a single delimiter always reports 0.
#     A regex delimiter comes back as a Match, a literal one as a Str.
#   * `.words($n)` takes at most $n words; `*`/Inf means all of them.
#   * inside an ARRAY LITERAL a bare `/pat/` is the Regex itself. It used to be
#     evaluated as a match against $_ and collapse to Nil, which silently dropped
#     regex delimiters out of `split(["a", /b+/, 4], …)`.
#   * a Regex GISTS as the literal that makes it (`.Str` stays the bare pattern —
#     that is what the engine consumes).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# separator-keeping adverbs, regex delimiter
check('abc'.split(/b/, :v).gist,  '(a ｢b｣ c)',        'split-regex-v');
check('abc'.split(/b/, :k).gist,  '(a 0 c)',          'split-regex-k');
check('abc'.split(/b/, :kv).gist, '(a 0 ｢b｣ c)',      'split-regex-kv');
check('abc'.split(/b/, :p).gist,  '(a 0 => ｢b｣ c)',   'split-regex-p');
check('a1b2c'.split(/\d/, :v).gist, '(a ｢1｣ b ｢2｣ c)', 'split-regex-v-repeats');
# … and a literal delimiter, where the separator is a plain string
check('abc'.split('b', :v).gist,  '(a b c)',          'split-str-v');
check('abc'.split('b', :p).gist,  '(a 0 => b c)',     'split-str-p');
# with SEVERAL delimiters `:k` tells you which one matched
check('a1b2c'.split(['1', '2'], :k).gist,  '(a 0 b 1 c)',            'split-list-k');
check('a1b2c'.split(['1', '2'], :kv).gist, '(a 0 1 b 1 2 c)',        'split-list-kv');
check('a1b2c'.split(['1', '2'], :p).gist,  '(a 0 => 1 b 1 => 2 c)',  'split-list-p');
# plain splits are unchanged
check('a,b,c'.split(',').gist,           '(a b c)',   'split-plain');
check('a,b,c'.split(',', 2).gist,        '(a b,c)',   'split-with-a-limit');
check('a,,b'.split(',', :skip-empty).gist, '(a b)',   'split-skip-empty');
check('abc'.split('').gist,              '( a b c )', 'split-on-the-empty-string');

# a regex delimiter inside a delimiter LIST survives to the split
check(split(['a', /b+/, 4], '1a2bb345').raku, '("1", "2", "3", "5").Seq', 'split-mixed-delimiters');
check(['a', /b/][1].^name,  'Regex', 'a-regex-in-an-array-literal-is-a-regex');
check([/x/, 1].elems,       '2',     'the-array-literal-still-has-both-elements');
check([1, 2].gist,          '[1 2]', 'ordinary-array-literals-are-unaffected');

# .words takes a limit
check('a b c d'.words(2).join('|'),        'a|b',         'words-limit');
check(<The quick brown fox>.words(2).join('|'), 'The|quick', 'words-limit-over-a-list');
check('a b c'.words.join('|'),             'a|b|c',       'words-without-a-limit');
check('a b c'.words(*).join('|'),          'a|b|c',       'words-star-is-all');
check('a b c'.words(Inf).join('|'),        'a|b|c',       'words-inf-is-all');
check(('easy come, easy goes' ~~ m:g/(ea\w+)/).words(Inf).gist, '(easy easy)', 'words-over-a-match');

# a Regex gists as its literal
check(rx/a/.gist,        'rx/a/', 'regex-gist');
check((rx/a/, 1).gist,   '(rx/a/ 1)', 'regex-gist-inside-a-list');
check(('abc' ~~ /b/).Str, 'b',    'matching-still-yields-the-match');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
