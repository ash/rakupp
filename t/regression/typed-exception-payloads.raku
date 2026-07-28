# Regression: an exception's payload is its TYPE OBJECT, everywhere.
#
# Seventeen throw sites stored something else — a Str naming the class
# (`Value::str("X::Multi::NoMatch")`), a Str that was never a class name at all
# (`"op"`, `"Cannot assign"`, `"Not callable"`), or `Value::nil()`. The payload
# reaches `$!` through exceptionFor, which only promotes a Str to a real
# exception when it starts with "X::" — so the rest surfaced as bare values:
#
#   try { nosuchsub(1) }; $!.^name        was Str
#   try { nosuchsub(1) }; $!.message      DIED, "No such method 'message' for
#                                         invocant of type 'Str'"
#
# The two `Value::nil()` sites were in the CODE GENERATOR, so a multi with no
# matching candidate could not be caught by class under --exe at all.
#
# A `die` with a user payload is untouched — that is a different question, and
# the early-out for a non-Type payload is what makes it work.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# every internal throw is catchable by class and answers .message
try { nosuchsub(1) }
check($!.^name,  'X::Undeclared::Symbols',      'an undeclared routine is typed');
check($!.message, "Undefined routine 'nosuchsub'", 'and has a message');

multi rgm(Int $x) { 1 }
try { rgm("s") }
check($!.^name, 'X::Multi::NoMatch', 'a multi with no candidate');
check(($!.message.chars > 0).gist, 'True', 'with a message');

try { 42.nosuchmethod }
check($!.^name, 'X::Method::NotFound', 'an unknown method');
check($!.message, "No such method 'nosuchmethod' for invocant of type 'Int'", 'names it');

try { (1+2i).Int }
check($!.^name, 'X::Numeric::Real', 'a Complex with a nonzero imaginary part');

try { sub rgs() { ... }; rgs() }
check($!.^name, 'X::StubCode', 'a stub body');

# …and each is catchable by its class, which is the point
my $caught = 'no';
try { 42.nosuchmethod; CATCH { when X::Method::NotFound { $caught = 'yes' } } }
check($caught, 'yes', 'catchable with `when X::Method::NotFound`');

# a user `die` payload is unchanged
try { die "plain" }
check($!.^name,  'X::AdHoc', 'die with a string');
check($!.message, 'plain',   'keeps its text');
try { die "X::Whatever went wrong" }
check($!.^name,  'X::AdHoc', 'even when the text looks like a class name');
check($!.message, 'X::Whatever went wrong', 'with the text intact');
class RgE is Exception { method message { 'custom' } }
try { die RgE.new }
check($!.^name,  'RgE',    'die with an exception object');
check($!.message, 'custom', 'uses its own message');
try { die 42 }
check($!.^name, 'X::AdHoc', 'and a non-string payload is still AdHoc');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
