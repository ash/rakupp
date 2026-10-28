# Regression: `for` over a value in no container walks what its `.list`
# gives (issue #146).
#
# `sub drain(Channel $in) { for $in -> $v { … } }` ran once, with the Channel
# itself as $v, where Rakudo receives the Channel's values. Rakudo binds a
# typed read-only `$` parameter to its value with no Scalar around it, and a
# `for` over a value in no container iterates its `.list` (Any.iterator is
# `self.list.iterator`) — a Channel's or a Supply's values, a Match's
# positional captures, a class's own `list`. rakupp ran all of them once.
# A `$` variable is still one item, and so is a typed parameter holding
# something Iterable (the binder itemizes that).

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub chan(*@v) { my $c = Channel.new; $c.send($_) for @v; $c.close; $c }
sub count(&run) { my $n = 0; run({ $n++ }); $n }

# the issue's shape: a typed Channel parameter, in a worker
sub drain(Channel $in) { my @got; for $in -> $v { @got.push: $v }; @got }
ck drain(chan(1, 2)), [1, 2], 'for over a Channel parameter';
sub drain-d(Channel:D $in) { my @got; @got.push($_) for $in; @got }
ck drain-d(chan(3, 4)), [3, 4], '…a Channel:D one, statement-modifier form';

# a value from a call, or bound with `:=`, is in no container either
ck count(-> &f { for chan(1, 2, 3) { f() } }), 3, 'for over a call returning a Channel';
ck count(-> &f { my $c := chan(1, 2); for $c { f() } }), 2, 'for over a Channel bound with :=';
ck count(-> &f { my $c = chan(1, 2); for $c<> { f() } }), 2, 'for over a decontainerized Channel';
ck count(-> &f { for Supply.from-list(1, 2, 3) { f() } }), 3, 'for over a Supply';
ck count(-> &f { for 'ab' ~~ /(a)(b)/ { f() } }), 2, 'for over a Match: its positional captures';
class L { method list { (1, 2, 3) } }
ck count(-> &f { for L.new { f() } }), 3, "for over an object: its class's own list";
sub typed(L $l) { my $n = 0; $n++ for $l; $n }
ck typed(L.new), 3, '…through a typed parameter';

# …while a container is one item
ck count(-> &f { my $c = chan(1, 2); for $c { f() } }), 1, 'a $ variable holding a Channel is one item';
ck count(-> &f { my $c = chan(1, 2); for $($c) { f() } }), 1, 'so is an itemized Channel';
sub untyped($in) { my $n = 0; $n++ for $in; $n }
ck untyped(chan(1, 2)), 1, 'an untyped parameter gets the Scalar';
sub any-typed(Any $in) { my $n = 0; $n++ for $in; $n }
ck any-typed(chan(1, 2)), 1, '…and so does an Any one';
sub copy(Channel $in is copy) { my $n = 0; $n++ for $in; $n }
ck copy(chan(1, 2)), 1, '…and an `is copy` one';
sub range(Range $r) { my $n = 0; $n++ for $r; $n }
ck range(1..3), 1, 'a typed parameter holding an Iterable is one item';
sub list(List $l) { my $n = 0; $n++ for $l; $n }
ck list((1, 2, 3)), 1, '…a List as well';
ck count(-> &f { my $m = 'ab' ~~ /(a)(b)/; for $m { f() } }), 1, 'a $ variable holding a Match is one item';

say 'PASS';
