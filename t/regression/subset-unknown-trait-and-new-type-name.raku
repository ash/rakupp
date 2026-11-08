# A subset's trait that no `trait_mod:<is>` takes is a parent nobody
# declared, as Rakudo reads `subset S is no-such of Int`: X::Inheritance::
# UnknownParent, where the trait was dropped without a word. (Rakudo refuses
# it at compile time; here the declaration refuses it when it runs.) And a
# runtime subset made by `Metamodel::SubsetHOW.new_type` on a taken name
# answers the name it was given, `User`, where it said `User+1`.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

try EVAL q[subset S is no-such-trait of Int;];
ck $!.^name, 'X::Inheritance::UnknownParent', 'an unknown trait';
ck $!.message, "'S' cannot inherit from 'no-such-trait' because it is unknown.", '…its message';
my @seen;
multi trait_mod:<is>(Mu:U $t, :$mytrait!) { @seen.push($t.^name) }
subset S3 of Int is mytrait;
ck @seen, ['S3'], 'a user trait still runs';
subset S4 is export of Int;
subset S5 of Int is DEPRECATED;
ck (3 ~~ S4, 3 ~~ S5), (True, True), 'the built-in traits are taken';

class User { }
my $s = Metamodel::SubsetHOW.new_type(:name<User>, :refinee(User), :refinement(True));
ck $s.^name, 'User', 'new_type keeps its name';
ck $s.raku, 'User', '…and .raku';
ck User.new ~~ $s, True, 'it matches';
ck User.^name, 'User', 'the class keeps its own';

say 'PASS';
