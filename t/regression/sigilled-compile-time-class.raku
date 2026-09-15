# Regression: `$?CLASS` — the SIGILLED spelling of the compile-time enclosing
# type — resolves to that type. Only the `::?CLASS` spelling was handled, so
# inside a `unit class` body `$?CLASS` read as an undeclared variable and
# answered Any. A class that keys a registry on `$?CLASS.^name` then registers
# itself under "Any" and cannot be looked up: Log and Logger both do exactly
# that, and `Log.get` was dead under Raku++ while working under Rakudo.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class Plain {
    method who      { $?CLASS.^name }
    method build-me { $?CLASS.new }
    method colons   { ::?CLASS.^name }
    method pkg      { $?PACKAGE.^name }
}

ck(Plain.who,             'Plain', '$?CLASS names the enclosing class');
ck(Plain.colons,          'Plain', '::?CLASS agrees with it');
ck(Plain.pkg,             'Plain', '$?PACKAGE too');
ck(Plain.build-me.^name,  'Plain', 'and it can be constructed through');

# a subclass sees the class the METHOD was written in, not the invocant's
class Sub is Plain { }
ck(Sub.who,   'Plain', 'an inherited method keeps the declaring class');
ck(Sub.^name, 'Sub',   'while the invocant is still the subclass');

# in a ROLE, $?CLASS is the CONSUMING class, resolved per invocant
role Marked { method mine { $?CLASS.^name } }
class Taker does Marked { }
class Other does Marked { }
ck(Taker.new.mine, 'Taker', 'in a role it is the consuming class');
ck(Other.new.mine, 'Other', '…and follows the consumer');

# the registry shape the logging modules use
class Registry {
    my %pool;
    method add($obj) { %pool{$?CLASS.^name} = $obj; $obj }
    method get       { %pool{$?CLASS.^name} }
}
Registry.add('the one');
ck(Registry.get, 'the one', 'a registry keyed on $?CLASS.^name round-trips');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
