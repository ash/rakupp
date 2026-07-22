# rakupp --lint: new-arg-matches-no-attribute
#
# The default .new binds named arguments to PUBLIC attributes and silently
# ignores everything else — so a typo'd attribute name costs nothing at
# runtime except an attribute that stays undefined. Both lines below run
# clean under Rakudo and rakupp; only the lint sees the problem.

class Cafe {
    has $.na;         # oops — meant $.name
    has @!orders;
}

my $cafe = Cafe.new(
    name => "Paris",  # <-- flagged: matches no public attribute (has: na)
);

say $cafe.na;         # (Any) — the constructor argument went nowhere
