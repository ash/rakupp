# Issue #43: `self!"$m"()` called a method spelled `!$m`.
#
# A quoted method name is computed at run time — `."$name"()` already was — but
# the private-call branch took the STRING-INTERPOLATION token's raw text as the
# name, so `self!"$m"()` looked for `!$m` instead of the method `$m` names. The
# branch now parses the interpolation as an expression into `methodExpr`, the
# way the public path does; a plain `"foo"` literal keeps taking the token text.
#
# Deferring the name to run time also moved the "private call needs a self in
# scope" check: with a `methodExpr` there is no name to check until the
# expression has been evaluated, so that check now runs after the name resolves
# (and, for the plain form, exactly where it did before).
#
# And a quoted private name now takes the parentheses S12 requires, the way the
# public path already did. Bare `self!"$m"` parsed as a no-argument call, so
# `self!"$m" = 5` assigned into nothing and said nothing.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

class Foo {
    method !foo()      { 'private-foo' }
    method !add($a, $b) { $a + $b }
    method  self-interp()  { my $m = 'foo'; self!"$m"() }
    method  self-literal() { self!'foo'() }
    method  self-args()    { my $m = 'add'; self!"$m"(2, 3) }
    method  self-expr()    { my $m = 'f';   self!"{ $m }oo"() }
    method  other(Foo $o)  { my $m = 'foo'; $o!"$m"() }
}

check Foo.new.self-interp(),  'private-foo', 'self!"$m"() resolves the interpolated name';
check Foo.new.self-literal(), 'private-foo', 'self!\'foo\'() still takes the literal name';
check Foo.new.self-args(),    5,             'a computed private name takes arguments';
check Foo.new.self-expr(),    'private-foo', 'the name may be any interpolation, not just a bare $var';
check Foo.new.other(Foo.new), 'private-foo', '$obj!"$m"() works on another instance of the class';

# the same call site, two different names — the resolution is per evaluation
class Two {
    method !a() { 'A' }
    method !b() { 'B' }
    method go() { (<a b>.map: -> $m { self!"$m"() }).join }
}
check Two.new.go(), 'AB', 'one call site resolves a fresh name each time';

# inherited, and composed from a role
class Kid is Foo { method reach() { my $m = 'foo'; self!"$m"() } }
check Kid.new.reach(), 'private-foo', 'a computed name finds an inherited private method';

role R { method !r() { 'role-priv' }; method go() { my $m = 'r'; self!"$m"() } }
class Doer does R {}
check Doer.new.go(), 'role-priv', 'a computed name finds a private method composed from a role';

# a sub in the class body has no `self`, and reaches another instance's private
# method through the routine's own package — that path must survive the name
# now being resolved later than the check
class Pkg {
    method !secret() { 'pkg-priv' }
    my sub peek(Pkg:D $p) { my $m = 'secret'; $p!"$m"() }
    method go() { peek(self) }
}
check Pkg.new.go(), 'pkg-priv', 'a computed name works from a sub in the class body';

# and outside the class it is still refused, naming the RESOLVED method
my $out = Foo.new;
my $m = 'foo';
check (try { $out!"$m"(); 'no-throw' } // $!.message),
      "Private method call to 'foo' outside the defining class",
      'a computed private call outside the class reports the resolved name';

# an unknown name reports what it resolved to, not the source text
# (the wording and the `.method`/`.private` payload are their own case —
# private-method-notfound-payload.raku)
class Miss { method go() { my $m = 'nope'; self!"$m"() } }
check (try { Miss.new.go(); 'no-throw' } // $!.method),
      'nope',
      'an unknown computed private name reports the resolved name';

# a quoted name must be immediately called, as it must on the public path (S12)
# — `self!"$m"` used to parse as a no-argument call, so `self!"$m" = 5` wrote
# into nothing at all
for 'self!"$m"', "self!'foo'" -> $form {
    my $src = 'class Q { method !foo() { 1 }; method go() { my $m = "foo"; ' ~ $form ~ ' } }';
    check (try { EVAL $src; 'no-throw' } // ($!.message.contains('requires parentheses') ?? 'refused' !! $!.message)),
          'refused', "$form without parentheses is refused";
}

# the public quoted form, which shares the parse, is untouched
class Pub { method hi() { 'pub-hi' }; method go() { my $m = 'hi'; self."$m"() } }
check Pub.new.go(), 'pub-hi', 'the public ."$m"() form still works';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
