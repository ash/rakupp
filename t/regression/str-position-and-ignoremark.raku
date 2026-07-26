# Regression: the start-position argument and `:ignoremark` on the Str search
# routines, plus two smaller Str gaps.
#   * `.contains($needle, $pos)` and `.indices($needle, $pos)` begin at CHARACTER
#     position $pos. Both silently ignored it, so a search that should have run
#     past its target still found it. (`.index` already honoured the position.)
#   * `:ignoremark` compares BASE characters — "ä" matches "a". The fold is
#     grapheme-for-grapheme, so an answered position still indexes the original
#     string, which is what makes `"tête-à-tête".indices("te", :ignoremark)`
#     answer 0, 2, 7, 9 rather than the folded coordinates.
#   * `.unival` of a character with no numeric value is NaN, not Nil — so
#     `.univals` interleaves them with the real values.
#   * `.encode($enc, :replacement)` substitutes for every character the encoding
#     cannot represent; a bare `:replacement` means "?".
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a start position
check('Hello, World'.contains('Hello'),      'True',  'contains-plain');
check('Hello, World'.contains('Hello', 0),   'True',  'contains-from-zero');
check('Hello, World'.contains('Hello', 1),   'False', 'contains-past-the-match');
check('Hello, World'.contains(',', 3),       'True',  'contains-before-the-match');
check('Hello, World'.contains(',', 10),      'False', 'contains-past-the-end-of-the-match');
check(<Hello, World>.contains('Hello', 1),   'False', 'contains-position-on-a-list');
check('Hello, World'.contains(/\w <?before ','>/),    'True',  'contains-regex');
check('Hello, World'.contains(/\w <?before ','>/, 5), 'False', 'contains-regex-with-a-position');
check('banana'.indices('a').gist,      '(1 3 5)', 'indices-plain');
check('banana'.indices('ana', 2).gist, '(3)',     'indices-from-a-position');
check('banana'.indices('a', 3).gist,   '(3 5)',   'indices-from-a-position-again');
check('banana'.index('a', 2),          '3',       'index-position-is-unchanged');

# :ignoremark
check('abc'.contains('ä'),                    'False', 'marks-matter-by-default');
check('abc'.contains('ä', :ignoremark),       'True',  'contains-ignoremark');
check('abc'.index('ä', :ignoremark),          '0',     'index-ignoremark');
check('abc'.index('ä').defined,               'False', 'index-marks-matter-by-default');
check('tête-à-tête'.indices('te').gist,              '(2 9)',     'indices-marks-matter');
check('tête-à-tête'.indices('te', :ignoremark).gist, '(0 2 7 9)', 'indices-ignoremark-keeps-positions');
check('Hello'.contains('ELL', :i),            'True',  'ignorecase-still-works');
check('Hello'.starts-with('hell', :i),        'True',  'starts-with-ignorecase-unchanged');

# unival / univals
check('4'.unival,        '4',    'unival-digit');
check('¾'.unival,        '0.75', 'unival-fraction');
check('a'.unival.gist,   'NaN',  'unival-of-a-non-numeric-is-nan');
check('4a¾'.univals.gist, '(4 NaN 0.75)', 'univals-interleaves-nan');

# encode :replacement
my $str = 'Þor is mighty';
check($str.encode('ascii', :replacement('Th')).decode('ascii'), 'Thor is mighty', 'encode-replacement-string');
check($str.encode('ascii', :replacement).decode('ascii'),       '?or is mighty',  'encode-bare-replacement');
check('abc'.encode('ascii').decode('ascii'),                    'abc',            'encode-without-replacement');
check('abc'.encode.decode,                                      'abc',            'encode-utf8-round-trip');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
