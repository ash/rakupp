# Regression: the adverbial forms of the list routines, and the container each
# one answers in.
#   * `.rotor(2, 3)` CYCLES its size arguments (2, 3, 2, 3, …); only the last one
#     used to take effect. A trailing window that is complete is still emitted.
#   * `.min`/`.max` take `:by(&code)` (the named spelling of the mapper) and
#     `:k`/`:v`/`:kv`/`:p`, which answer EVERY position attaining the extremum.
#     The SUB forms delegate to the method, so a lone Positional/Associative
#     argument is the list and several arguments are NOT flattened.
#   * `.first` grew `:v`/`:kv`/`:p` beside `:k`, and takes a Regex matcher —
#     `applyArith`'s `~~` does not know regexes, so it needs the engine directly.
#   * `.sort`/`.reverse`/`.unique`/`.squish`/`.head`/`.tail`/`.skip`/`.rotor` are
#     Seq in Rakudo, including their sub forms.
#   * `deepmap`/`duckmap` answer in the INVOCANT's container; `nodemap` is always
#     a List. All three are list operators, so they take a bare first argument.
#   * `&infix:«OP»` is the guillemet spelling of `&infix:<OP>`; `<=>` survives the
#     double-angle strip that `infix:<<∈>>` needs.
#   * a `Rat` parameter REJECTS an Int (`3 ~~ Rat` is False), which is what makes
#     `duckmap -> Rat $_ {…}` descend past the Ints.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# .rotor cycles its specs
check(<a b c d e f g>.rotor(2, 3).gist, '((a b) (c d e) (f g))', 'rotor-cycles-sizes');
check((1..10).rotor(3, 2).gist, '((1 2 3) (4 5) (6 7 8) (9 10))', 'rotor-cycles-over-a-range');
check(<a b c d e f g>.rotor(1, 2 => -1).gist,
      '((a) (b c) (c) (d e) (e) (f g) (g))', 'rotor-cycles-sizes-and-gaps');
# the single-spec forms are unchanged
check(<a b c d e f g>.rotor(3).gist,        '((a b c) (d e f))',           'rotor-plain');
check(<a b c d e f g>.rotor(2 => 1).gist,   '((a b) (d e))',               'rotor-with-a-gap');
check(<a b c d e f g>.rotor(3 => -1).gist,  '((a b c) (c d e) (e f g))',   'rotor-overlapping');
check(<a b c d e f g>.rotor(2 => 1, :partial).gist, '((a b) (d e) (g))',   'rotor-partial');
check(<a b c>.batch(2).gist,                '((a b) (c))',                 'batch-keeps-the-short-tail');

# .min/.max adverbs — every position attaining the extremum
check(<a b c a>.min(:k).gist,  '(0 3)',              'min-k');
check(<a b c a>.min(:v).gist,  '(a a)',              'min-v');
check(<a b c a>.min(:kv).gist, '(0 a 3 a)',          'min-kv');
check(<a b c a>.min(:p).gist,  '(0 => a 3 => a)',    'min-p');
check(<a b c c>.max(:k).gist,  '(2 3)',              'max-k');
check(<a b c c>.max(:p).gist,  '(2 => c 3 => c)',    'max-p');
check((1, 7, 3).min,           '1',                  'min-plain');
check((1, 7, 3).min({ 1 / $_ }), '7',                'min-with-a-mapper');
check(min(1, 7, 3, :by({ 1 / $_ })), '7',            'min-by-is-the-named-mapper');
check(max(1, 7, 3, :by({ 1 / $_ })), '1',            'max-by');
check(min(%(a => 3, b => 7)).gist, 'a => 3',         'min-of-a-hash-is-a-pair');
check(min((1, 2), (3, 4)).gist,    '(1 2)',          'min-does-not-flatten-its-arguments');
check(min(1, 7, 3),                '1',              'min-sub-plain');

# .first — the answer forms, and a Regex matcher
check(<a b c d>.first(*.uc eq 'C'),      'c',       'first-plain');
check(<a b c d>.first(*.uc eq 'C', :k),  '2',       'first-k');
check(<a b c d>.first(*.uc eq 'C', :v),  'c',       'first-v');
check(<a b c d>.first(*.uc eq 'C', :kv).gist, '(2 c)',   'first-kv');
check(<a b c d>.first(*.uc eq 'C', :p).gist,  '2 => c',  'first-p');
check((3..33).first(/\d\d/),             '10',      'first-with-a-regex');
check(<a bb>.first(/\w\w/),              'bb',      'first-regex-over-a-list');
check((1..20).first(* %% 7, :end),       '14',      'first-end');

# containers
check(<b c a>.sort.^name,      'Seq', 'sort-is-a-seq');
check((9, 8).reverse.^name,    'Seq', 'reverse-is-a-seq');
check([9, 8].reverse.^name,    'Seq', 'reverse-of-an-array-is-a-seq-too');
check((1, 1).unique.^name,     'Seq', 'unique-is-a-seq');
check((1, 2).head(1).^name,    'Seq', 'head-is-a-seq');
check((sort <b a>).^name,      'Seq', 'the-sort-sub-is-a-seq');
check((reverse 1, 2).^name,    'Seq', 'the-reverse-sub-is-a-seq');
check(<b c a>.sort.join,       'abc', 'sort-still-sorts');
check((9, 8, 7).reverse.gist,  '(7 8 9)', 'reverse-still-reverses');

# deepmap/duckmap keep the invocant's container; nodemap is a List
check([[1, 2], [3]].deepmap(* + 1).^name, 'Array', 'deepmap-keeps-an-array');
check(((1, 2), (3)).deepmap(* + 1).^name, 'List',  'deepmap-keeps-a-list');
check([1, 2].nodemap(* + 1).^name,        'List',  'nodemap-is-always-a-list');
check([1, 2].duckmap(* + 1).^name,        'Array', 'duckmap-keeps-an-array');
# … and they are list operators, so the first argument needs no parentheses
check((deepmap * + 1, [[1, 2, 3], [[4, 5], 6, 7]]).gist,
      '[[2 3 4] [[5 6] 7 8]]', 'deepmap-as-a-listop');
check((duckmap *², [[1, 2, 3], [[4, 5], 6, 7]]).gist, '[9 9]', 'duckmap-as-a-listop');
# a Rat parameter rejects an Int, so duckmap descends past the whole numbers
check((duckmap -> Rat $_ { $_² }, [[1, 2, 3], [[4, 5], 6.1, 7.2]]).gist,
      '[[1 2 3] [[4 5] 37.21 51.84]]', 'duckmap-quacks-on-the-rats-only');
check((try sub (Rat $x) { $x }(3)).defined, 'False', 'an-int-does-not-bind-a-rat-param');
check(sub (Rat $x) { $x }(3.5),             '3.5',   'a-rat-still-binds');
check(sub (Real $x) { $x }(3),              '3',     'an-int-still-binds-real');

# operator names
check(&infix:«+»(1, 2),               '3',    'guillemet-operator-name');
check(&infix:«<=>»(1, 2).gist,        'Less', 'guillemet-name-of-an-angle-shaped-op');
check(&[<=>](1, 2).gist,              'Less', 'bracket-form-of-the-same-op');
check('231'.comb.sort(&infix:«<=>»).join, '123', 'the-op-value-works-as-a-comparator');
check(&infix:<+>(1, 2),               '3',    'the-plain-angle-name-still-works');

# odds and ends the documentation demonstrates
check(Whatever.elems, '1',  'elems-of-a-type-object');
check(Any.elems,      '1',  'elems-of-any');
check(Int.elems,      '1',  'elems-of-int');
check((42).elems,     '1',  'elems-of-a-scalar-is-unchanged');
check('food'.serial,  'food', 'serial-is-the-identity');
check((a => 1, 'b', 'c').pairup.raku, '(:a(1), :b("c")).Seq', 'pairup-a-pair-stands-alone');
check((1, 2, 3, 4).pairup.raku,       '(1 => 2, 3 => 4).Seq', 'pairup-reads-pairwise');
check(((1, 2), (3), %(:42a)).flat.gist, '(1 2 3 a => 42)', 'flat-opens-a-hash-into-pairs');
check((1, 2).Capture.gist,            '\(1, 2)',      'list-capture');
check((a => 1, 2).Capture.raku,       '\(2, :a(1))',  'list-capture-sorts-nameds-last');
check(\(1, :a(2)).gist,               '\(1, :a(2))',  'a-capture-gists-as-its-literal');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
