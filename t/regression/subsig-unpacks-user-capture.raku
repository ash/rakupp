# Regression (issue #119): a destructuring sub-signature binds its argument as
# the argument's `.Capture`. A class that writes its own `.Capture` must be
# asked for it — Cro's form bodies (WWWFormUrlEncoded, MultiPartFormData) hand
# their fields over that way, and `request-body -> (:$tags, :$description) {…}`
# saw both as undefined.
# Contract: exit 0 + last line PASS.
my @fail;

class Form does Associative {
    has @!pairs;
    has $!hashed;
    submethod BUILD(:@pairs) { @!pairs := @pairs }
    method !hashed() {
        without $!hashed {
            my %h;
            for @!pairs -> $p { %h{$p.key} = $p.value }
            $!hashed := %h;
        }
        $!hashed
    }
    method Capture() { Capture.new(hash => self!hashed) }
}
my $f = Form.new(pairs => [tags => 'a,b', description => 'hello']);

# a pointy block's sub-signature, called directly and through a sigilless param
my &h = -> (:$tags?, :$description?) { "{$tags // 'U'}|{$description // 'U'}" };
@fail.push("direct: {h($f)}") unless h($f) eq 'a,b|hello';
sub relay(\body) { &h.(body) }
@fail.push("relayed: {relay($f)}") unless relay($f) eq 'a,b|hello';

# a sub's sub-signature, positional and named parts from the Capture
class Pt { method Capture() { \(3, 4, :label<p>) } }
sub show(($x, $y, :$label)) { "$label=$x,$y" }
@fail.push("positional: {show(Pt.new)}") unless show(Pt.new) eq 'p=3,4';

# a Capture's nameds are all it carries: a method of the same name on the
# object is not consulted for a key the Capture lacks
class Both { method colour { 'from-method' }; method Capture() { \(:size<L>) } }
my &b = -> (:$size, :$colour) { "{$size // 'U'}/{$colour // 'U'}" };
@fail.push("capture-only: {b(Both.new)}") unless b(Both.new) eq 'L/U';

# a plain object (no .Capture of its own) still binds nameds from accessors
class Plain { has $.x = 1; has $.y = 2 }
my &p = -> (:$x, :$y) { "$x$y" };
@fail.push("plain: {p(Plain.new)}") unless p(Plain.new) eq '12';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
