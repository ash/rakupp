# Regression: `::T:U` was read as a type CAPTURE, and captured the name.
#
# A smiley decides which of two things `::T` is, and the difference shows up in
# the BODY, not in the binding:
#
#   sub f(::T $x)    — a type CAPTURE. T binds to whatever arrived, and that
#                      binding shadows any outer T. (This one was fixed at
#                      v4.0.0 and is asserted in type-capture-constrains.raku.)
#   sub f(::T:U $x)  — an ordinary type CONSTRAINT wearing a definedness
#                      smiley. It captures nothing, so an outer T stays visible.
#
# rakupp took the first reading for both, so a signature that merely constrained
# a parameter silently rebound the type's name for the whole body.
#
# That is not a corner: YAMLish declares
#
#     our sub load-yaml(Str $input, ::Grammar:U :$schema = ::Schema::Core) {
#         my $match = Grammar.parse($input);   # the module's OWN grammar
#
# and `Grammar.parse` there means the grammar the module declares — the one
# whose `method parse` override attaches the actions. Capture that name and the
# call goes to $schema's type instead, whose parse is the plain built-in: the
# match still succeeds, no action method ever runs, and `.ast` comes back as the
# document's own source text. Every load-yaml died with "No such method
# 'concretize' for invocant of type 'Str'" — a message naming neither the
# signature nor the grammar.
#
# Found by raku.online's sites/spec/verify.sh during the v4.0.0 release, on a
# documentation example that had passed at v3.25.0.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub threw(&c, $what) {
    my $r = try { c(); Nil };
    my $e = $!;
    return @fail.push("$what: returned {$r.raku} instead of throwing") unless $e;
    @fail.push("$what: threw {$e.^name}") unless $e ~~ X::TypeCheck::Binding::Parameter;
}
sub bound(&c, $want, $what) {
    my $r = try { c() };
    return @fail.push("$what: threw {$!.^name}") if $!;
    check $r, $want, $what;
}

class Foo { method who { "Foo" } }
class Bar is Foo { method who { "Bar" } }

# ---- the smiley forms leave the outer name alone ----------------------------
sub pos-smiley(::Foo:U $x)   { Foo.^name }
sub named-smiley(::Foo:U :$x) { Foo.^name }
bound { pos-smiley(Bar)      }, "Foo", 'a positional `::Foo:U` leaves the outer Foo visible';
bound { named-smiley(x => Bar) }, "Foo", 'and so does a named one';

# ---- the bare form still captures, and still shadows ------------------------
# This is the half that must NOT regress: `::Foo` with no smiley is a capture,
# and Rakudo lets it shadow an outer type of the same name.
sub bare-capture(::Foo $x) { Foo.^name }
bound { bare-capture(Bar) }, "Bar", 'a bare `::Foo` capture still shadows the outer Foo';

# ---- what the smiley does to BINDING is not asserted here -------------------
# Only the two engines' common ground is: a `:U` parameter takes a type object
# and a `:D` one takes an instance.
sub defined-only(::Foo:D $x) { Foo.^name }
bound { pos-smiley(Foo)       }, "Foo", 'a `:U` parameter accepts a type object';
bound { defined-only(Bar.new) }, "Foo", 'and a `:D` one accepts an instance';

# The other direction is a DIVERGENCE, and an old one: rakupp enforces the
# smiley (`pos-smiley(Bar.new)` and even `pos-smiley(42)` throw
# X::Parameter::InvalidConcreteness) where Rakudo binds them without complaint,
# apparently reading `::Foo:U` as a capture and so as constraining nothing.
# v3.25.0 threw on exactly the same five cases, so this predates the type
# capture work and is not what this file is about — it is left alone
# deliberately, and not asserted, so that the file runs on both engines.

# ---- the shape that found it -----------------------------------------------
# A grammar whose `parse` override attaches actions, reached through a sub whose
# signature constrains a parameter to that grammar's own name.
grammar Gram {
    token TOP  { <word> }
    token word { \w+ }

    class Actions {
        method TOP($/) { make "MADE:" ~ $<word> }
    }

    method parse($string, *%args) {
        nextwith($string, :actions(Actions), |%args);
    }
}
grammar Gram2 is Gram { }

sub load(Str $in, ::Gram:U :$schema = Gram) { Gram.parse($in).ast }

bound { load("hello") }, "MADE:hello",
      'the body reaches the grammar the module declares, not the parameter type';
bound { load("hello", schema => Gram2) }, "MADE:hello",
      'and still does when a different (conforming) type is actually passed';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
