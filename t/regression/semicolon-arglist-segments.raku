# A `;` inside an argument list makes it MULTI-DIMENSIONAL: each segment is one
# POSITIONAL argument and is always a List, a trailing `;` adds a final empty
# segment, and a named argument inside a segment is absorbed by that segment's
# own list construction and vanishes.
#
# Every expectation here is Rakudo 2026.08's own answer.
use lib $?FILE.IO.parent(2).add('t');
my $fail = 0;
sub check($got, $want, $desc) {
    if $got eq $want { return }
    note "FAIL: $desc\n  got:  $got\n  want: $want";
    $fail++;
}
sub f(|c) { c.raku }

# one item per segment is still a LIST — the rule the first implementation missed
check f(1; 2),        '\((1,), (2,))',      'f(1; 2) — each segment a one-element List';
check f(1, 2; 3, 4),  '\((1, 2), (3, 4))',  'f(1,2; 3,4) — multi-item segments';
check f(1; 2; 3),     '\((1,), (2,), (3,))','three segments';

# a trailing `;` is a further, EMPTY segment
check f(1;),          '\((1,), ())',        'f(1;) — trailing ; adds an empty segment';

# a named argument inside a segment vanishes (List.new(x => 1, 2) is (2,))
check f(x => 1;),     '\((), ())',          'f(x => 1;) — the named argument vanishes';
check f(x => 1, 2; 3),'\((2,), (3,))',      'a named argument is dropped, positionals stay';
check f(1, x => 2; 3),'\((1,), (3,))',      '…wherever it sits in the segment';
check f(:a, :b; :c),  '\((), ())',          'colonpairs vanish the same way';

# …but only a SYNTACTIC named argument: parens and a quoted key make it positional
check f((x => 1); 2), '\((:x(1),), (2,))', 'a PARENNED pair stays positional';
check f('a' => 1; 2), '\((:a(1),), (2,))',  'a QUOTED key stays positional';

# no semicolon: ordinary argument-list rules, named args stay named
check f(x => 1),      '\(:x(1))',           'without a `;` a pair is still a named argument';
check f(1, 2),        '\(1, 2)',            'without a `;` a comma list spreads';

# the consequence that started this: two positionals and NO named `tr`
{
    my class C { has $.tr }
    my $r = 'ok';
    try { C.new(tr => 1;); CATCH { default { $r = 'threw' } } }
    check $r, 'threw', 'C.new(tr => 1;) hands the default constructor positionals';
}

# and the zip/cross consequence. `.List.raku` because whether zip returns a Seq
# or a List is a SEPARATE, pre-existing divergence (rakupp's zip answers a List,
# its cross a Seq; Rakudo answers a Seq for both) — not what this file is about.
my @a = 1, 2;
my @b = 3, 4;
check zip(@a; @b).List.raku,   '(([1, 2], [3, 4]),)', 'zip(@a; @b) zips two ONE-element lists';
check zip(1,2; 3,4).List.raku, '((1, 3), (2, 4))',    'zip(1,2; 3,4) zips element-wise';
check cross(@a; @b).List.raku, '(([1, 2], [3, 4]),)', 'cross(@a; @b) likewise';

say $fail == 0 ?? 'PASS' !! "FAIL ($fail)";
exit $fail ?? 1 !! 0;
