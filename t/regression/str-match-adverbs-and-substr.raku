# Regression: `.match`'s occurrence-selection adverbs, `.lines`/`.words`
# arguments, and the fuller `substr` signature.
#   * `.match` understood only `:g`. The whole family — `:continue`/`:c`,
#     `:pos`/`:p`, `:x(N)`, `:nth(N)` and the `:1st`/`:2nd`/`:3rd`/`:Nth` spellings
#     — is exactly what s/// already implements, so `.match` routes through that
#     same code. It routes there only when EVERY adverb is one that code knows:
#     it THROWS on an unknown name, and `:overlap`/`:exhaustive` are not
#     implemented, so sending them there aborted the caller instead of degrading
#     to a plain match.
#   * an ARRAY needle is its elements joined by a space.
#   * `.lines` takes a limit, `:count` and `:!chomp`; the `lines`/`words` SUBS
#     delegate to the methods, so the string may sit behind a named argument
#     (`lines(:!chomp, "a\nb")` used to lose it and read STDIN instead).
#   * `substr` takes a Range, and a Whatever START — `substr($s, *-3, *-1)`.
#     A Whatever LENGTH counts from the END of the string, not from the tail
#     that remains after the start.
#   * `substr-eq` takes `:ignoremark`; `.uniparse` works as a method.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# .match — needles
check("properly".match('perl').Str,     'perl',  'a-literal-needle');
check("properly".match(/p.../).Str,     'prop',  'a-regex-needle');
check("1 2 3".match([1,2,3]).Str,       '1 2 3', 'an-array-needle-joins-with-spaces');
check("abc".match(/b/).Str,             'b',     'a-plain-match-is-unchanged');
check("abc".match(/z/).^name,           'Nil',   'a-miss-is-nil');

# .match — occurrence selection
check("a1xa2".match(/a./, :continue(2)).Str,      'a2',   'continue-starts-the-search');
check('abcdef'.match(/.*/, :pos(2)).Str,          'cdef', 'pos-anchors-the-start');
check('several words here'.match(/\w+/, :global).gist,
      '(｢several｣ ｢words｣ ｢here｣)',                        'global');
check("foo[bar][baz]".match(/../, :1st).Str,      'fo',   'first');
check("foo[bar][baz]".match(/../, :2nd).Str,      'o[',   'second');
check("foo[bar][baz]".match(/../, :3rd).Str,      'ba',   'third');
check("foo[bar][baz]".match(/../, :4th).Str,      'r]',   'fourth');
check("foo[bar][baz]".match(/../, :nth(2)).Str,   'o[',   'nth');
check("foo[bar][baz]bada".match('ba', :x(2)).gist, '(｢ba｣ ｢ba｣)', 'x-counts-the-matches');
# an adverb the selection code does not implement degrades to a plain match
# rather than throwing (that abort is what used to cut two roast files short)
check("abc".match(/b/, :overlap).defined, 'True', 'an-unknown-adverb-does-not-abort');

# .lines
check(lines("a\nb").raku,              '("a", "b").Seq', 'the-lines-sub-answers-a-seq');
check(lines("a\nb").elems,             '2',   'lines-sub-elems');
check("a\nb".lines.elems,              '2',   'lines-method-elems');
check("a\n".lines.elems,               '1',   'a-trailing-newline-is-a-terminator');
check(lines(:!chomp, "a\nb").raku,     '("a\n", "b").Seq', 'a-named-arg-does-not-hide-the-string');
check("a\n".lines(:!chomp).raku,       '("a\n",).Seq',     'no-chomp-keeps-the-terminator');
check(<not there yet>.join("\n").lines(2).gist,      '(not there)', 'a-line-limit');
check(<not there yet>.join("\n").lines(:count),      '3',           'count-answers-a-number');
check("a\r\nb".lines.raku,             '("a", "b").Seq', 'crlf-is-one-terminator');

# .words
check(words("I will be very brief here", 2).gist, '(I will)', 'a-word-limit-through-the-sub');
check("a b c d".words(2).gist,          '(a b)',   'a-word-limit-on-the-method');
check("a b c".words.gist,               '(a b c)', 'no-limit');

# substr
check(substr("Long string", 3..6),      'g st',   'a-range');
check(substr("Long string", 6, 3),      'tri',    'start-and-length');
check(substr("Long string", 6),         'tring',  'start-only');
check(substr("Long string", 6, *-1),    'trin',   'a-whatever-length');
check(substr("Long string", *-3, *-1),  'in',     'a-whatever-start-and-length');
check(substr("Long string", *-3),       'ing',    'a-whatever-start');
check("abcdef".substr(1, 2),            'bc',     'the-method-form');
check("abcdef".substr(0, *-2),          'abcd',   'the-method-with-a-whatever-length');

# substr-eq and uniparse
check("cliché".substr-eq("che", 3),               'False', 'marks-matter-by-default');
check("cliché".substr-eq("che", 3, :ignoremark),  'True',  'substr-eq-ignoremark');
check("abc".substr-eq("b", 1),                    'True',  'a-plain-substr-eq');
check("abc".substr-eq("B", 1, :i),                'True',  'substr-eq-ignorecase');
check('TWO HEARTS, BUTTERFLY'.uniparse,           '💕🦋',  'uniparse-as-a-method');
check(uniparse('LATIN SMALL LETTER A'),           'a',     'uniparse-as-a-sub');
check('LATIN SMALL LETTER A'.uniparse,            'a',     'both-spellings-agree');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
