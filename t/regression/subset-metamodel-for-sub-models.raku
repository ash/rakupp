# Regression: the subset metamodel Red's sub-models stand on (issue #77,
# FCO's "subset new_type" under metamodels and traits).
#
# Red makes a sub-model two ways, and neither worked:
#
#   (Metamodel::SubsetHOW but MetamodelX::Red::SubModelHOW).new_type:
#       :$name, :refinee(model), :&refinement;
#   subset Administrator is sub-model of User where *.role eq "admin";
#
# There was no `Metamodel::SubsetHOW.new_type`, no `.^refinee` or
# `.^refinement`, a subset's `.HOW` was one shared type object (so `$s.HOW
# does Role` died "Cannot use 'does' operator on a type object"), and a
# subset's user traits were parsed and dropped. SubModelHOW's own methods
# (`method all(\model, |c) { model.^refinee.^all(|c).grep: model.^refinement }`)
# are then reached as `Admin.^all`.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

# the introspection
subset Pos of Int where * > 0;
ck Pos.^refinee.^name, 'Int', '.^refinee';
ck Pos.^refinement.^name, 'WhateverCode', '.^refinement is the WhateverCode';
ck Pos.^refinement.(5), True, '…and it answers';
subset Blocky of Int where { $_ > 0 };
ck Blocky.^refinement.^name, 'Block', 'a where block is a Block';
subset Thunked of Int where $_ > 2;
ck (Thunked.^refinement.(3), Thunked.^refinement.(1)), (True, False), 'a where expression is a thunk';
subset Bare of Int;
ck Bare.^refinement.^name, 'Mu', 'no where clause: Mu';
ck Pos.HOW.refinee(Pos).^name, 'Int', 'the HOW form';
ck Pos.HOW.^name, 'Perl6::Metamodel::SubsetHOW', 'its metaobject';
ck Pos.HOW ~~ Metamodel::SubsetHOW, True, '…is a SubsetHOW';
ck Pos.HOW === Bare.HOW, False, '…of its own';
ck UInt.^refinee.^name, 'Int', 'UInt refines Int';

# a subset made at run time
my \Neg = Metamodel::SubsetHOW.new_type(:name<Neg>, :refinee(Int), :refinement(* < 0));
ck (-1 ~~ Neg, 1 ~~ Neg, 'x' ~~ Neg), (True, False, False), 'new_type makes a subset';
ck Neg.^name, 'Neg', '…with its name';

# …through a metaclass with a role mixed in, whose methods it then answers
role SubModel {
    method all(\model) { (1, 2, 3, -4).grep({ $_ ~~ model.^refinee }).grep(model.^refinement).List }
}
my \Big = (Metamodel::SubsetHOW but SubModel).new_type(:name<Big>, :refinee(Int), :refinement(* > 1));
ck Big.^all, (2, 3), 'a mixed-in metaclass\'s method through .^';
ck Big.HOW.^name, 'Perl6::Metamodel::SubsetHOW+{SubModel}', 'the subset\'s HOW is that metaclass';

# a user trait on a subset runs, and what it mixes into the HOW sticks
multi trait_mod:<is>($s where { .HOW ~~ Metamodel::SubsetHOW }, Bool :$sub-model!) {
    $s.HOW does SubModel;
    $s
}
class User { has $.role }
subset Administrator is sub-model of User where *.role eq 'admin';
ck User.new(:role<admin>) ~~ Administrator, True, 'the sub-model accepts an admin';
ck User.new(:role<user>) ~~ Administrator, False, '…and refuses a user';
ck Administrator.HOW.^name, 'Perl6::Metamodel::SubsetHOW+{SubModel}', 'the trait mixed into its HOW';

my @seen;
multi trait_mod:<is>($s where { .HOW ~~ Metamodel::SubsetHOW }, :$tagged!) { @seen.push: $s.^name => $tagged }
subset Tagged is tagged(42) of Int;
ck @seen, [Tagged => 42], 'a trait with an argument';

say 'PASS';
