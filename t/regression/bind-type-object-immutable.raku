# `my $x := Int; $x = 5` succeeded.
#
# Binding a name to a type object binds the VALUE, with no container behind
# it, exactly like `my $x := 42`; Rakudo refuses the later assignment. Only
# literals were marked immutable here, so the assignment quietly made a
# container and stored 5. Rakudo's wording depends on the operator: plain `=`
# asks for a concrete object, and every other form — `~=`, `+=`, a sigilless
# name — says the type object is immutable.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }
sub refusal(&code) { try { code(); return 'lived' }; $!.^name ~ ': ' ~ $!.message }

check(refusal({ my $x := Int; $x = 5 }),
      'X::AdHoc: assign requires a concrete object (got a Int type object instead)', ':= Int, then =');
check(refusal({ my $x := Any; $x = 5 }),
      'X::AdHoc: assign requires a concrete object (got a Any type object instead)', ':= Any, then =');
class C { }
check(refusal({ my $x := C; $x = 5 }),
      'X::AdHoc: assign requires a concrete object (got a C type object instead)', ':= a class, then =');
check(refusal({ my $x; $x := Int; $x = 5 }),
      'X::AdHoc: assign requires a concrete object (got a Int type object instead)', 'a later := Int, then =');
check(refusal({ my $x := Str; $x ~= 'a' }),
      "X::Assignment::RO: Cannot modify an immutable 'Str' type object", ':= Str, then ~=');
check(refusal({ my $x := Int; $x += 1 }),
      "X::Assignment::RO: Cannot modify an immutable 'Int' type object", ':= Int, then +=');
check(refusal({ my \v = Int; v = 1 }),
      "X::Assignment::RO: Cannot modify an immutable 'Int' type object", 'a sigilless name bound to Int');
check(refusal({ my \w = 42; w = 1 }),
      'X::Assignment::RO: Cannot modify an immutable Int (42)', 'a sigilless name bound to 42 keeps its wording');

# a container is still a container
check(do { my $x = Int; $x = 5; $x }, 5, '= Int makes a container');
check(do { my $y = Int; my $x := $y; $x = 5; $y }, 5, ':= a variable holding Int binds its container');
check(do { my $x := Int; $x := 5; $x }, 5, 'rebinding is not assigning');
check(do { my $x := Int; $x.^name }, 'Int', 'reading the bound type object');

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
