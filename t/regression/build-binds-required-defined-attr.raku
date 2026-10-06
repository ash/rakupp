# Regression: `has Int:D $.rows is required` in a class whose own BUILD binds
# it (`submethod BUILD(:$!rows!)`) died "Type check failed on attribute
# '$!rows'; expected Int:D but got Int". The `:D` check ran before BUILD and
# counted the constructor argument as having filled the slot — but with a
# BUILD of its own the class binds its arguments THERE, later. Graph::Grid is
# written exactly so (issue #47: Graph would not install).
#
# Answers below are rakudo 2026.09's.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
sub err(&code) { try { code(); return 'lived' }; $!.^name }

class Grid {
    has Int:D $.rows is required;
    submethod BUILD(:$!rows!) { }
    multi method new(Int:D $rows) { self.bless(:$rows) }
}
check Grid.new(3).rows, 3, 'BUILD binds the :D attribute';

class Sets {
    has Int:D $.x is required;
    submethod BUILD() { $!x = 7 }
}
check Sets.new.x, 7, 'BUILD assigns it';

# …and what BUILD leaves undefined is still refused, after BUILD
class Ignores {
    has Int:D $.x is required;
    submethod BUILD() { }
}
check err({ Ignores.new(:x(1)) }), 'X::Attribute::Required', 'BUILD ignores the argument';

class BindsTypeObj {
    has Int:D $.x is required;
    submethod BUILD(:$!x) { }
}
check err({ BindsTypeObj.new(:x(Int)) }), 'X::TypeCheck::Assignment', 'BUILD binds a type object';

class Defaulted {
    has Int:D $.x = 3;
    submethod BUILD() { }
}
check Defaulted.new(:x(1)).x, 3, 'a default still fills what BUILD skips';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say "PASS";
