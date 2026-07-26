# Regression: a batch of routines the documentation demonstrates that had no
# implementation, so every example touching them died on dispatch.
#   * the POSITIONAL PROTOCOL spelled out — `.AT-POS`, `.EXISTS-POS`, `.slice` —
#     answered by a Range as the list it stands for, and by an Array/List too.
#   * `$range.in-range($v)` is True inside the range, and otherwise THROWS
#     X::OutOfRange naming the offending value. Rakudo throws here rather than
#     handing back a soft Failure — even `.defined` on the result explodes.
#   * the ROLE coercers `.Setty`/`.Baggy`/`.Mixy` name the immutable member of
#     each QuantHash family.
#   * a Code is a one-element list for `.kv`/`.pairs`/`.keys`/`.values`, and a
#     NAMED routine gists as its `&`-reference rather than a generic `sub {…}`.
#   * `Date.new-from-daycount`, `.first-date-in-month`, `Instant.to-posix`.
#   * `.subst-mutate` substitutes IN PLACE and answers the Match — it needs the
#     invocant's slot, so it cannot live in the value-only method dispatcher.
#   * the `comb($matcher, $input)` SUB, whose arguments are the other way round
#     from the method.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the positional protocol
check((1..5).AT-POS(2),        '3',     'at-pos-on-a-range');
check(<a b c>.AT-POS(1),       'b',     'at-pos-on-a-list');
check((1..5).EXISTS-POS(2),    'True',  'exists-pos-inside');
check((1..5).EXISTS-POS(9),    'False', 'exists-pos-outside');
check((1..10).slice(2,4).gist, '(3 5)', 'slice-of-a-range');
check(<a b c d>.slice(0,2).gist, '(a c)', 'slice-of-a-list');
check((1,2,3).Seq.slice(0,2).gist, '(1 3)', 'slice-of-a-seq');
# these already worked and must keep working
check((1..5).antipairs.gist, '(1 => 0 2 => 1 3 => 2 4 => 3 5 => 4)', 'antipairs-of-a-range');
check((1..5).reduce(&[+]),   '15',      'reduce-over-a-range');

# .in-range
check((1..5).in-range(3),          'True',    'in-range-inside');
check((try (1..5).in-range(9)).defined, 'False', 'in-range-outside-throws');
try { (1..5).in-range(9) };
check($!.^name, 'X::OutOfRange', 'and-it-is-an-out-of-range');

# the QuantHash role coercers
check(Set.new(1,2).Baggy.^name, 'Bag', 'set-to-baggy');
check(Set.new(1,2).Mixy.^name,  'Mix', 'set-to-mixy');
check(Bag.new(1,2).Setty.^name, 'Set', 'bag-to-setty');
check(Set.new(1,2).Set.^name,   'Set', 'set-to-set-is-unchanged');

# a Code as a one-element list, and how a named routine gists
check((&say).kv.gist,        '(0 &say)', 'kv-of-a-code');
check(&say.gist,             '&say',     'a-named-sub-gists-as-its-reference');
check(&[+].gist,             '&infix:<+>', 'an-operator-too');
sub named-thing { 42 }
check(&named-thing.gist,     '&named-thing', 'a-user-sub');
check((1,2).Capture.Capture.gist, '\(1, 2)', 'capture-of-a-capture-is-itself');

# Date / Instant
check(Date.new(2016,6,3).first-date-in-month.Str, '2016-06-01', 'first-date-in-month');
check(Date.new(2016,6,3).last-date-in-month.Str,  '2016-06-30', 'last-date-in-month');
check(Date.new-from-daycount(49700).Str,          '1994-12-14', 'new-from-daycount');
check(Instant.from-posix(1).to-posix.gist,        '(1 False)',  'instant-to-posix');

# .subst-mutate
my $some-string = "Some foo";
my $match = $some-string.subst-mutate(/foo/, "string");
check($some-string,  'Some string', 'subst-mutate-changes-the-invocant');
check($match.Str,    'foo',         'and-answers-the-match');
my $t = "aeiou";
$t.subst-mutate(/<[oe]>/, '', :g);
check($t,            'aiu',         'subst-mutate-takes-adverbs');

# the comb sub
check(comb(/a/, "banana").gist,          '(a a a)',           'the-comb-sub');
check(comb(3, [3,33,333,3333]).join('*'), '3 3*3 3*33 *333*3', 'comb-with-a-chunk-size');
check("banana".comb(/a/).gist,           '(a a a)',           'the-comb-method-is-unchanged');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
