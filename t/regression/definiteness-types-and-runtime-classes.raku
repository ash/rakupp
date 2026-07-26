# Regression: the type SMILEY as a first-class type, and creating a class at
# runtime through the metamodel.
#   * `Foo:D` / `Foo:U` in TERM position parsed and then threw the smiley away,
#     so the constraint existed only inside a signature. It rides on the type
#     value now, which is what lets `.^name` report it, smartmatch test
#     definedness alongside the type, and `.^base_type` strip it back off.
#   * `Metamodel::ClassHOW.new_type(:name, :ver, :auth)` registers a class the
#     same way a declared one is registered, so `.^add_method` and `.new` work on
#     it afterwards.
#   * the HOW spellings take the type as their FIRST argument
#     (`$t.HOW.add_method($t, …)`) where `.^` passes it implicitly. Forwarding
#     them is named to the MOP operations explicitly: a metaclass also answers
#     ORDINARY methods (`.isa`, `.gist`), and forwarding those to their argument
#     cut S24-testing/1-basic.t from 39 emitted tests to 15 — which is why the
#     forwarding is an allow-list rather than "anything with a type argument".
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the smiley is part of the type
check(Any:D.^name, 'Any:D', 'a defined-only type names itself');
check(Any:U.^name, 'Any:U', 'and a type-object-only one');
check(Any:_.^name, 'Any',   'the explicit any-definiteness smiley adds nothing');
check(Int:D.^name, 'Int:D', 'it works on any type');
check(Any:D.^base_type.^name, 'Any', 'base_type strips it');
check(Int:D.^base_type.^name, 'Int', 'for any type');
check(Any.^name,   'Any',   'a plain type object is unaffected');

# smartmatch tests definedness as well as type
check((Any     ~~ Any:D).gist, 'False', 'a type object is not defined');
check((Any     ~~ Any:U).gist, 'True',  'but it is a type object');
check((Any     ~~ Any:_).gist, 'True',  'and matches either way');
check((Any.new ~~ Any:D).gist, 'True',  'an instance is defined');
check((Any.new ~~ Any:U).gist, 'False', 'and is not a type object');
check((Any.new ~~ Any:_).gist, 'True',  'and matches either way too');
check((5   ~~ Int:D).gist,     'True',  'a defined int');
check((Int ~~ Int:D).gist,     'False', 'an undefined one');
check((5   ~~ Str:D).gist,     'False', 'the type still has to match');
# the signature form, which already worked, keeps working
sub takes-defined(Int:D $x) { $x }
check(takes-defined(3),                 '3',     'a defined argument binds');
check((try takes-defined(Int)).defined, 'False', 'an undefined one does not');
my Int:D $constrained = 5;
check($constrained, '5', 'a constrained container holds a value');

# a class made at runtime
my $type = Metamodel::ClassHOW.new_type(name => "NewType", ver => v0.0.1, auth => 'github:raku');
check($type.^name, 'NewType', 'the new type is named');
$type.HOW.add_method($type, "hey", method { 'Hey' });
check($type.hey, 'Hey', 'a method added through the HOW is callable on the type');
$type.HOW.compose($type);
check($type.new.hey, 'Hey', 'and on an instance');
# a metaclass still answers its OWN ordinary methods rather than forwarding them
check(Int.HOW.^name.contains('ClassHOW'), 'True', 'a metaclass answers its own meta-methods');
check(Int.^name, 'Int', 'and the ordinary meta-methods are unaffected');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
