# Regression: `flatmap` is an Any method, and a Match answers `flat`.
#
# Two one-element-list gaps in the same family, both found by distributions in
# the 2026-09-16 random fifty:
#
#   * Algorithm::HierarchicalPAM calls `.flatmap` on a Str ("No such method
#     'flatmap' for invocant of type 'Str'"). Rakudo puts flatmap on Any, where
#     a lone scalar is a one-element list, so `42.flatmap({…})` is `(42,).Seq`.
#     This engine had it on the list arms only, next to `map` and `grep`, and
#     the scalar whitelist beside them had every neighbour of flatmap but not
#     flatmap itself.
#
#   * Email::Valid asks a Match for `.flat` before walking the captures. A Match
#     is Positional over its captures and already routed the other list methods
#     that way; `flat` was missing from that set.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# flatmap over a lone scalar: the invocant is the one element.
check 42.flatmap({ $_ }).raku,      '(42,).Seq',      'Int.flatmap';
check "x".flatmap({ $_ }).raku,     '("x",).Seq',     'Str.flatmap';
check True.flatmap({ $_ }).raku,    '(Bool::True,).Seq', 'Bool.flatmap';
check (1.5).flatmap({ $_ }).raku,   '(1.5,).Seq',     'Rat.flatmap';
check Any.flatmap({ $_ }).raku,     '(Any,).Seq',     'a type object is one element to iterate';

# It is map-that-flattens-one-level, not map: the block's list result spreads.
check 42.flatmap({ ($_, $_) }).raku,      '(42, 42).Seq',     'a scalar flatmap flattens';
check (1, 2).flatmap({ ($_, $_) }).raku,  '(1, 1, 2, 2).Seq', 'and so does the list form';
check (1, 2).flatmap({ $_ }).raku,        '(1, 2).Seq',       'a non-list result is passed through';

# The neighbours it was missing from — these already worked and must keep working.
check 42.map({ $_ * 2 }).raku,  '(84,).Seq', 'Int.map still answers a one-element Seq';
check 42.grep({ $_ }).raku,     '(42,).Seq', 'Int.grep too';

# A Match is Positional over its captures, so `.flat` is the capture list.
my $m = ("ab" ~~ /a/);
check $m.flat.elems,   0, 'a Match with no captures flattens to nothing';
my $c = ("ab" ~~ /(a)(b)/);
check $c.flat.elems,   2, 'a Match with two captures flattens to both';
check $c.flat.map(*.Str).join(','), 'a,b', 'and they are the captures themselves';
check $c.flatmap({ $_ }).elems, 2, 'flatmap over the same Match agrees';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
