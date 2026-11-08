# Issues #223 and #224: two declarations in PDF::Content 0.9.11 that the parser
# refused, so `use PDF::Content`, and PDF::Lite above it, could not compile.
#
# #223: `sub STORE($, $!text) { … }` inside a method was "Variable $!text used
# where no 'self' is available". The check refused every attributive parameter
# of a named sub; a sub nested in a method sees that method's `self`, as an
# anonymous `sub ($!x)` there already did. PDF::Content::Text::Box's `text`
# accessor hands such a FETCH and STORE to a Proxy. A sub with no method
# around it (at the top level, or in a class body) is still refused.
#
# #224: `my subset S of ::?CLASS:D is export(:Tag)` was "Undefined routine
# 'export'", and `… of ::?CLASS:D where { … }` "Undefined routine 'where'".
# The `of` read only a plain type name, so `::?CLASS:D` became a statement of
# its own and the trait or `where` after it a routine call. It now reads the
# enclosing class or role, as `has ::?CLASS $.x` does
# (PDF::Content::FontObj's SubsetFontLike and CoreFontLike).
#
# Contract: exit 0 + last line PASS. Every answer is Rakudo 2026.09's — run
# this file under `rakudo` too.
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }
sub refusal(Str $code) { try EVAL $code; $! ?? $!.message !! 'compiled' }

# #223 — the issue's program
class C223 {
    has $.text = "hi";
    method m {
        sub STORE($, $!text) { $!text }
        STORE(0, "bound")
    }
}
my $c = C223.new;
check 'a named sub in a method binds $!text', $c.m, 'bound';
check '…into the invocant', $c.text, 'bound';

# …PDF::Content::Text::Box's accessor, a Proxy over the two subs
class Box {
    has $.text = "x";
    has @.chunks;
    method text(::?CLASS:D $obj:) is rw {
        sub FETCH($) { $!text };
        sub STORE($, $!text) { $obj!layup: $!text.comb(/\w+/) }
        Proxy.new: :&FETCH, :&STORE;
    }
    method !layup(@c) { @!chunks = @c }
}
my $b = Box.new;
check 'the Proxy reads the attribute', $b.text, 'x';
$b.text = 'hello big world';
check '…and its STORE writes it', $b.text, 'hello big world';
check '…and runs its body', $b.chunks, ['hello', 'big', 'world'];

# a sub two levels down still sees the method's self
class Deep { has $.v = 1; method m { sub inner { sub deeper($!v) { $!v }; deeper(7) }; inner(); $!v } }
check 'a sub nested in a sub in a method', Deep.new.m, 7;

# …and with no method around it, there is still no self
check 'a top-level sub is refused', refusal('sub f($!x) { }'), "Variable \$!x used where no 'self' is available";
check 'a sub in a class body is refused', refusal('class A { has $.x; sub f($!x) { } }'),
      "Variable \$!x used where no 'self' is available";

# #224 — the issue's two programs
check 'of ::?CLASS:D is export(:Tag)',
      refusal('class C1 { my subset S of ::?CLASS:D is export(:Tag); method m { 1 } }'), 'compiled';
check 'of ::?CLASS:D where { … }',
      refusal('class C2 { my subset S of ::?CLASS:D where { .^name }; method m { 1 } }'), 'compiled';

# …and the subsets they declare refine the enclosing type
class K {
    has $.v;
    my subset Big of ::?CLASS:D where { .v > 1 };
    method big { self ~~ Big }
}
check 'a subset of ::?CLASS:D takes a matching instance', K.new(v => 2).big, True;
check '…refuses one its where refuses', K.new(v => 0).big, False;

role FontObj {
    method font-name { 'ABCDEF+Times' }
    my subset SubsetFontLike of ::?ROLE:D is export(:SubsetFontLike) where { .font-name ~~ m/^<[A..Z]>**6"+"/ }
    method subsetted { self ~~ SubsetFontLike }
}
class Font does FontObj { }
class Plain does FontObj { method font-name { 'Times' } }
check 'a subset of ::?ROLE:D takes a consumer', Font.new.subsetted, True;
check '…refuses one its where refuses', Plain.new.subsetted, False;

.say for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
