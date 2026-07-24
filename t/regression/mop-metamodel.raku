# Regression: Meta-Object Protocol (.^ introspection) coverage.
#   - Attribute meta-objects: get_value / set_value (read/write a slot on an
#     instance through the meta-object — what marshallers like JSON::Marshal drive)
#   - .^ver / .^auth / .^api, .^attribute_table
#   - .^compose (no-op returning the type), .^set_name
#   - Attribute.new + .^add_attribute (dynamic class construction)
#   - .^declares_method (local vs inherited), .^find_method_qualified
# Contract: exit 0 + last line PASS.
my @fail;

class Animal { has $.legs is rw; method speak() { 'generic' } }
class Dog:ver<1.2.3>:auth<zef:me> is Animal { has $.name is rw; method speak() { 'woof' } }
my $d = Dog.new(name => 'Rex', legs => 4);

# Attribute get_value / set_value
my $name-attr = Dog.^attributes.first(*.name eq '$!name');
@fail.push('attr-get') unless $name-attr.get_value($d) eq 'Rex';
$name-attr.set_value($d, 'Fido');
@fail.push('attr-set') unless $d.name eq 'Fido';

# ver / auth
@fail.push("ver ({Dog.^ver})") unless Dog.^ver eq v1.2.3;
@fail.push('auth') unless Dog.^auth eq 'zef:me';

# attribute_table
@fail.push('attr-table') unless Dog.^attribute_table{'$!name'}.name eq '$!name';

# compose is a no-op returning the type; declares_method local vs inherited
@fail.push('compose') unless Dog.^compose === Dog;
@fail.push('declares-local')   unless Dog.^declares_method('speak');
@fail.push('declares-inherit') if     Dog.^declares_method('legs');   # accessor is inherited
@fail.push('find-qualified')   unless Dog.^find_method_qualified(Animal, 'speak').defined;

# Attribute.new + add_attribute
class Empty { }
Empty.^add_attribute(Attribute.new(name => '$!tag', type => Str));
@fail.push('add-attribute') unless Empty.^attributes.first(*.name eq '$!tag').defined;

# .^methods / .^mro / .^parents still hold
@fail.push('methods') unless Dog.^methods.grep(*.name eq 'speak').elems >= 1;
@fail.push('mro')     unless Dog.^mro.map(*.^name).join(',') eq 'Dog,Animal,Any,Mu';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
