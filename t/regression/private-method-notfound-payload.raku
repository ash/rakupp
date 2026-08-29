# BROKE: a missing PRIVATE method reported its dispatch KEY. Class method tables
# key a private method as `!name` (`md->isPrivate ? "!" + name : name`), and the
# not-found throw passed that key straight through — so the exception's `.method`
# was `!wrong`, not `wrong`, and the message read "No such method '!wrong'"
# where Rakudo says "No such private method '!wrong'". `.private`, which Rakudo's
# X::Method::NotFound carries and roast's S12-methods/private.t matches with
# `private => &so`, did not exist at all: reading it died with its own
# X::Method::NotFound.
#
# FIXED: the one typed throw site strips the `!` for the reported name, sets
# `private`, and names the kind in the message. A Raku method name cannot begin
# with `!`, so the key's prefix is unambiguous. The KEY is untouched — dispatch
# still looks up `!name`.
#
# NOT fixed here, and not what this case is about: Rakudo raises this at COMPILE
# time (`X::Method::NotFound+{X::Comp}`) when the invocant is `self`, so an inner
# `try` does not catch it. rakupp raises it at run time, which is why
# S12-methods/private.t still fails its 13th test.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

class Priv {
    method !known() { 'here' }
    method miss()   { self!nope() }
    method caught() { try { self!nope() }; $! }
}

# the private miss: name as written, the kind named, and the flag set
my $e = Priv.new.caught;
check $e.^name,   'X::Method::NotFound', 'a missing private method throws X::Method::NotFound';
check $e.method,  'nope',                'the exception reports the name as written, not the `!name` key';
check $e.private, True,                  '.private is True for a private call';
check $e.typename, 'Priv',               '.typename is the invocant type';
check $e.message, "No such private method '!nope' for invocant of type 'Priv'",
      'the message names the kind of call, as Rakudo does';

# the public miss keeps the wording it had, and answers the flag rather than dying
my $p = (try { Priv.new.nope; Nil } // $!);
check $p.method,   'nope',  'a public miss still reports its name';
check $p.private,  False,   '.private is False for a public call';
check $p.message,  "No such method 'nope' for invocant of type 'Priv'",
      'a public miss keeps the plain wording';

# the private method that DOES exist is unaffected
check (try { Priv.new!known(); 'no-throw' } // $!.message),
      "Private method call to 'known' outside the defining class",
      'a private call from outside the class is still refused, before any lookup';
class Ok { method !known() { 'here' }; method go() { self!known() } }
check Ok.new.go(), 'here', 'an existing private method still dispatches through the `!name` key';

# the two attributes coexist on one exception class however it was first thrown
class Order { method go() { try { self!a() }; my $x = $!; try { self.b() }; ($x, $!) } }
my ($first, $second) = Order.new.go;
check ($first.private, $second.private), (True, False),
      'both a private and a public miss answer .private on the same exception class';

# …including after a BARE-TYPE throw of the same exception has already put the
# class in the registry with no attributes at all. It used to keep whatever
# attribute set the first throw registered, so `$!.method` after this died with
# an X::Method::NotFound of its own.
class Late { method go() { my $m = 'nope'; self!"$m"() } }
try { Priv.new!known() };                # bare-type X::Method::NotFound, no attrs
check (try { Late.new.go(); 'no-throw' } // $!.method), 'nope',
      'a later payload-carrying throw still declares its own attributes';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
