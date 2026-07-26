# Regression: what a `$` container does to a list, and a batch of List routines.
#   * a `$` variable ITEMIZES what it stores, so `my $t = (1,2)` is ONE thing:
#     it does not flatten into a later list (`my @b = $t` has one element) and it
#     renders with the `$` marker. The marker is dropped for an ARRAY element,
#     whose slot itemizes anyway — Rakudo prints `[[1, 2],]` but `($(1, 2),)`.
#   * `list(…)` builds a List of its ARGUMENTS without flattening them; only a
#     lone non-itemized Positional spreads (the one-arg rule).
#   * `leg` is the STRING comparison, so `-4 leg -1` is More where `cmp` is Less.
#   * `.lsb`/`.msb`/`.base` used to truncate a big integer to 64 bits.
#   * a Whatever in a list PATTERN matches one element, a HyperWhatever a run of
#     them — and a HyperWhatever on the right of `~~` is a pattern, not a curry.
#   * `.head`/`.tail`/`.skip` accept any Callable count, not just a WhateverCode.
#   * `reduce`/`produce` as SUBS delegate to the methods, which is where an
#     operator's associativity is read off the callable's name. Over an empty
#     list an operator answers its identity.
#   * a matcher may be a Regex inside a JUNCTION (`.grep(none /<[aeiou]>/)`) —
#     the generic `~~` cannot match a regex, so those need the engine.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a $ container itemizes
my $t = (1, 2);
check($t.raku,        '$(1, 2)', 'a-list-in-a-scalar-is-itemized');
check($t.elems,       '2',       'but-it-still-knows-its-length');
my @b = $t;
check(@b.elems,       '1',       'and-does-not-flatten-into-a-list');
check(($t,).raku,     '($(1, 2),)', 'the-marker-shows-inside-a-list');
my $arr = [1, 2];
check($arr.raku,      '$[1, 2]',    'an-array-in-a-scalar-too');
check([$arr,].raku,   '[[1, 2],]',  'but-not-as-an-array-element');
my %h = a => 1;
my $hh = %h;
check($hh.raku,       '${:a(1)}',   'a-hash-in-a-scalar');
check(($hh,).raku,    '(${:a(1)},)', 'a-hash-marker-inside-a-list');
check([$hh,].raku,    '[{:a(1)},]',  'no-hash-marker-as-an-array-element');
# an itemized hash is one element in list context
my @d = $%h;
check(@d.elems,       '1',       'an-itemized-hash-does-not-spread');
my @e = %h;
check(@e.elems,       '1',       'a-plain-hash-still-spreads-its-pairs');
# the plain nesting cases are unchanged
check([[1, 2],].raku,     '[[1, 2],]',      'a-nested-array-literal');
check([1, [2, 3]].raku,   '[1, [2, 3]]',    'an-array-inside-an-array');
check(((1, 2), (3, 4)).raku, '((1, 2), (3, 4))', 'nested-lists');

# list()
check(list(1, 2).raku,          '(1, 2)',         'list-of-two-values');
check(list($t).raku,            '($(1, 2),)',     'list-does-not-open-an-itemized-argument');
check(list(|$t).raku,           '(1, 2)',         'but-a-slip-does');
check(list((1, 2), (3, 4)).raku, '((1, 2), (3, 4))', 'list-does-not-flatten');
check(list([1, 2]).raku,        '(1, 2)',         'one-arg-rule-spreads-a-lone-array');
check(list().raku,              '()',             'list-of-nothing');
check(list(1, 2).^name,         'List',           'list-is-a-list');

# leg is stringwise
check((-4 leg -1).gist, 'More', 'leg-compares-as-strings');
check((10 leg 9).gist,  'Less', 'leg-again');
check((-4 cmp -1).gist, 'Less', 'cmp-is-still-numeric');
check(('a' leg 'b').gist, 'Less', 'leg-on-real-strings');
check((3, -4, 7, -1, 2, 0).sort({ $^b leg $^a }).gist, '(7 3 2 0 -4 -1)', 'sorting-by-leg');

# big integers know all their bits
check((2 ** 81).lsb,            '81', 'lsb-of-a-big-int');
check((2 ** 81).msb,            '81', 'msb-of-a-big-int');
check((2 ** 81).base(2).chars,  '82', 'base-2-of-a-big-int');
check((10 ** 30).base(10),      '1' ~ '0' x 30, 'base-10-of-a-big-int');
check(8.lsb,                    '3',  'lsb-of-a-small-int');
check(255.base(16),             'FF', 'base-16-of-a-small-int');

# Whatever and HyperWhatever in a list pattern
check(((1, 2, 3) ~~ (1, *, 3)).Bool,  'True',  'a-whatever-matches-one-element');
check(((1, 2, 3) ~~ (9, *, 5)).Bool,  'False', 'the-others-must-still-match');
check(((1, 2, 3) ~~ (**, 3)).Bool,    'True',  'a-hyperwhatever-matches-a-run');
check(((1, 2, 3) ~~ (**, 5)).Bool,    'False', 'and-still-has-to-reach-the-end');
check(((1, 2, 4, 5, 6) ~~ (**)).Bool, 'True',  'a-lone-hyperwhatever-matches-anything');
check((() ~~ (**)).Bool,              'True',  'including-nothing');
my $hw = **;
check(((1, 2, 3) ~~ $hw).Bool,        'True',  'a-hyperwhatever-value-is-a-pattern-not-a-curry');

# a Callable count
check(<a b c d e>.tail({ $_ - 2 }).gist, '(c d e)', 'tail-with-a-block-count');
check(<a b c d e>.tail(* - 2).gist,      '(c d e)', 'tail-with-a-whatevercode-count');
check(<a b c d e>.head({ $_ - 2 }).gist, '(a b c)', 'head-with-a-block-count');
check(<a b c d e>.skip({ $_ - 2 }).gist, '(d e)',   'skip-with-a-block-count');
check(<a b c d e>.tail(2).gist,          '(d e)',   'a-plain-count-still-works');

# reduce / produce as subs
check((produce &[**], (2, 3, 4)).gist, '(4 81 2417851639229258349412352)', 'the-produce-sub-folds-right');
check(reduce(&infix:<->, (10,)),  '10', 'a-one-element-reduce-is-that-element');
check(reduce(&infix:<->, (10, 3)), '7', 'a-two-element-reduce-still-folds');
check(reduce(&[+], (1, 2, 3)),     '6', 'the-reduce-sub');
check((produce &[+], (1, 2, 3)).gist, '(1 3 6)', 'the-produce-sub');
check(().reduce(&[+]), '0',  'an-empty-plus-reduce-is-zero');
check(().reduce(&[*]), '1',  'an-empty-times-reduce-is-one');
check(().reduce(&[~]), '',   'an-empty-concat-reduce-is-the-empty-string');
check(reduce(&[+], ()), '0', 'the-sub-form-agrees');

# a regex matcher inside a junction
check(<a b c d e f>.grep(none /<[aeiou]>/).gist, '(b c d f)', 'grep-none-of-a-regex');
check(<a b c>.grep(none /a/).gist,        '(b c)', 'grep-none-of-a-simple-regex');
check(<a b 6 d 8 0>.grep(none Int).gist,  '(a b d)', 'grep-none-of-a-type');
check(<a b c>.grep(any(/a/, /c/)).gist,   '(a c)', 'grep-any-of-two-regexes');
check(<a b c>.grep(/b/).gist,             '(b)',   'a-plain-regex-matcher');
check(<a b c>.first(none /a/),            'b',     'first-with-a-junction-matcher');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
