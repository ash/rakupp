# Regression: an `our` declaration written with a QUALIFIED name is absolute.
#
# The publish prepended the enclosing package's prefix to whatever name the
# declaration carried, so `our $Foo::v` inside `class Foo` published
# $Foo::Foo::v — the doubled name — and left $Foo::v, the name every reader
# asks for, empty. `our $Bar::v` inside `class Foo` went to $Foo::Bar::v for
# the same reason. Rakudo treats a qualified name as naming its package
# outright, whatever package the declaration is written in.
#
# Found via `rakupp test Gnome::N` on Linux, where its other three test files
# pass: lib/Gnome/N/X.rakumod declares `our $Gnome::N::x-debug` and
# `our &Gnome::N::debug` inside `class Gnome::N`, so the module set a debug
# flag that neither its own test suite nor any caller could see.
#
# The rows that could not come from a broken engine are the ones asserting the
# DOUBLED name is empty: an engine with the bug puts the value exactly there.
# The unqualified rows are the other half of the pair — a fix that reads every
# name as absolute would break `our $v` inside a package, which must still
# publish $Pkg::v.
#
# Checked against Rakudo v2026.08, which agrees on every row below except the
# one marked DIVERGENCE: for `our %Pkg::h = a => 1`, Rakudo publishes a List
# (`.keys` gives 0,1) while the unqualified `our %h` in the same position gives
# a Hash. The `%` sigil should impose hash context however the name is spelled,
# so rakupp keeps the Hash rather than copying that.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape: same-named package ------------------------------
{
    class Foo { our $Foo::v = False; }
    ck($Foo::v, False, 'a qualified `our` publishes the name it spells');
    ck($Foo::Foo::v, Any, '…and NOT the doubled name');
}

# --- the contrast: an unqualified `our` still takes the prefix -----------
{
    class Unq { our $v = 'plain'; }
    ck($Unq::v, 'plain', 'an unqualified `our` still publishes under the package');
}

# --- a qualified name is absolute, not relative --------------------------
{
    class Outer { our $Elsewhere::v = 'x'; }
    ck($Elsewhere::v, 'x', 'a qualified name names its OWN package');
    ck($Outer::Elsewhere::v, Any, '…not one nested under the enclosing package');
}

# --- nested packages, full path ------------------------------------------
{
    module Deep { module Inner { our $Deep::Inner::v = 'deep'; } }
    ck($Deep::Inner::v, 'deep', 'a full path from a nested package resolves to itself');
}

# --- every sigil ---------------------------------------------------------
{
    class Sig { our @Sig::list = 1, 2, 3; }
    ck(@Sig::list.elems, 3, 'a qualified `our @` publishes its elements');
    ck(@Sig::list.join(','), '1,2,3', '…in order');
}
{
    class Cod { our &Cod::f = sub { 'called' }; }
    ck(&Cod::f(), 'called', 'a qualified `our &` is callable under the name it spells');
}
{
    # DIVERGENCE (see header): Rakudo publishes a List here, rakupp a Hash.
    class Hsh { our %Hsh::map = a => 1, b => 2; }
    ck(%Hsh::map.keys.sort.join(','), 'a,b', 'a qualified `our %` stays a Hash');
    ck(%Hsh::map<b>, 2, '…and indexes associatively');
}

# --- no package at all: the prefix is empty, nothing to double -----------
{
    our $Top::v = 7;
    ck($Top::v, 7, 'a qualified `our` at file scope publishes itself');
}

# --- one container, not two (the property f2e1439 installed) ------------
{
    class Ident { our $Ident::v = 'init'; method peek { $Ident::v }; method poke { $Ident::v = 'inside' } }
    $Ident::v = 'outside';
    ck(Ident.new.peek, 'outside', 'a write through the qualified name reaches the package');
    Ident.new.poke;
    ck($Ident::v, 'inside', 'and a write inside the package reaches the qualified name');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
