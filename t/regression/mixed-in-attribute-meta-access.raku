# Regression (found by the module battery, AttrX::Mooish): an Attribute
# meta-object with a role mixed in answers `.^get_attribute_for_usage` for
# the role's attributes, and their `.get_value` reads and writes them.
#
# Before the fix `$attr.^get_attribute_for_usage('$!lazy')` was "No such
# method", so AttrX::Mooish's trait died on its first option
# (`self.^get_attribute_for_usage('$!' ~ $name).get_value(self) = $value`).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

role Opts {
    has $.lazy = False;
    has $!secret = 'hidden';
    method set($n, $v) { self.^get_attribute_for_usage('$!' ~ $n).get_value(self) = $v }
    method get($n) { self.^get_attribute_for_usage('$!' ~ $n).get_value(self) }
    method secret { $!secret }
}
class C { has $.x }
my $attr = C.^attributes[0];
$attr does Opts;

my $meta = $attr.^get_attribute_for_usage('$!lazy');
check 'the role attribute it names', ($meta.^name, $meta.name), ('Attribute', '$!lazy');
check 'get_value reads it', $meta.get_value($attr), False;
$attr.set('lazy', True);
check 'get_value(…) = v writes it', $attr.lazy, True;
check 'a private one reads', $attr.get('secret'), 'hidden';
$attr.set('secret', 'told');
check '…and writes', $attr.secret, 'told';
my $msg = 'nothing';
try { $attr.^get_attribute_for_usage('$!nope'); CATCH { default { $msg = .message } } }
check 'an attribute no role has', $msg, 'No $!nope attribute in Attribute+{Opts}';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
