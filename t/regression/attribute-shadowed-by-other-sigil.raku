# Regression: a subclass declaring an attribute its parent also declares, with
# ANOTHER sigil, lost its accessor.
#
#   class A { has @.data }
#   class B is A { has %.data }
#   B.new.data          # Rakudo: {} (a Hash); rakupp: (Any)
#
# A user's module with exactly this pair failed to install until %.data was
# renamed. Construction keyed a non-$ attribute by "%data" whenever ANY class
# in the chain declared the name with another sigil (the "sigil twin" rule,
# meant for one class's `has Digest $.digest; has &!digest`), while the
# accessor read the bare "data". Across classes the parent's slot is already
# class-qualified, so twins are now decided per class, and the accessor reads
# the key its attribute was actually stored under (ClassInfo::findAccessorAttr).
#
# Neighbours that broke the same way are here too: a same-class twin whose
# PUBLIC one is the non-$ attribute (`has $!x; has @.x` answered "No such
# method 'x'"); a subclass's PRIVATE attribute hiding its parent's public
# accessor and named argument (`has &!d` over a parent's `has $.d`); the
# qualified `$o.A::data`, `.raku` and `Attribute.get_value`, which read the
# bare name; and a parent's attributive parameter (`submethod BUILD(:$!x)`,
# `method set($!x)`), which wrote the SUBCLASS's attribute of that name.
#
# Every expectation was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku} want {$want.raku}" }
}

# --- the report: @.data in the parent, %.data in the child --------------------
class A { has @.data; method a-data { @!data } }
class B is A { has %.data; method b-data { %!data } }

ck B.new.data.^name, 'Hash', "the child's accessor answers the child's Hash";
ck B.new(data => (a => 1,)).data, {a => 1}, 'a named argument reaches the child attribute';
ck B.new(data => (a => 1,)).a-data, [a => 1], '…and the parent attribute, as its own Array';
ck B.new(data => (a => 1,)).A::data, [a => 1], "the qualified accessor reads the parent's";
ck B.new(data => (a => 1,)).raku, 'B.new(data => {:a(1)}, data => [:a(1)])', '.raku shows each one';
{
    my $b = B.new(data => (a => 1,));
    my ($ah, $aa) = B.^attributes.sort(*.name);   # %!data (B's), @!data (A's)
    ck $ah.get_value($b), {a => 1}, "get_value through the child's Attribute";
    ck $aa.get_value($b), [a => 1], "get_value through the parent's Attribute";
}
{
    my $b = B.new;
    $b.data<x> = 1;
    $b.a-data.push: 5;
    ck $b.data, {x => 1}, 'writing through the accessor reaches the child attribute';
    ck $b.b-data, {x => 1}, "…which the child's own methods see";
    ck $b.a-data, [5], "and the parent's storage stays its own";
}

# --- a private parent attribute of the name -----------------------------------
class A1 { has @!data = 1, 2; method a1 { @!data } }
class B1 is A1 { has %.data = k => 'v' }
ck B1.new.data, {k => 'v'}, 'the accessor over a private parent attribute';
ck B1.new.a1, [1, 2], "the parent's private attribute keeps its default";

# --- a $ parent, an @ child -----------------------------------------------------
class A2 { has $.data = 'scalar' }
class B2 is A2 { has @.data = 1, 2 }
ck B2.new.data, [1, 2], 'an @ child over a $ parent';
ck B2.new.A2::data, 'scalar', "…and the parent's through its qualified accessor";
{
    my $b2 = B2.new;
    $b2.data.push: 3;
    ck $b2.data, [1, 2, 3], 'pushing through the @ accessor';
}

# --- twins in ONE class, the public one not a $ ---------------------------------
class C { has @.x = 1, 2; has %!x = a => 1; method both { @!x, %!x } }
ck C.new.x, [1, 2], 'the public @.x beside a private %!x';
ck C.new.both, ([1, 2], {a => 1}), 'each twin keeps its own storage';

class D { has $!x = 9; has @.x = 1, 2; method both { $!x, @!x } }
ck D.new.x, [1, 2], 'the public @.x beside a private $!x';
ck D.new(x => (3, 4)).both, (9, [3, 4]), 'a named argument sets the public twin only';
{
    my $d = D.new;
    $d.x.push: 7;
    ck $d.both, (9, [1, 2, 7]), 'writing through the accessor reaches the public twin';
}

# --- a subclass's PRIVATE attribute does not hide the parent's accessor -------
class P { has $.d = 7; method pd { $!d } }
class Q is P { has &!d = { 42 }; method q { &!d() } }
ck Q.new.d, 7, "the parent's accessor under the child's private &!d";
ck Q.new.q, 42, "the child's private attribute";
ck Q.new(d => 9).pd, 9, "a named argument reaches the parent's public attribute";
ck Q.new(d => 9).q, 42, "…and not the child's private one";

class P2 { has &!d = { 42 }; method q { &!d() } }
class Q2 is P2 { has $.d = 7 }
ck Q2.new.d, 7, 'a public child attribute over a private parent one';
ck Q2.new.q, 42, "the parent's private attribute";
ck Q2.new(d => 8).d, 8, 'a named argument reaches the public child attribute';

class P3 { has $.v is rw = 1 }
class Q3 is P3 { has @!v }
{
    my $q3 = Q3.new;
    $q3.v = 5;
    ck $q3.v, 5, "an `is rw` parent accessor stays assignable under a private child @!v";
}

# --- a parent's attributive parameters write the parent's own attribute ------
class F { has $.x = 1; submethod BUILD(:$!x = 10) { }; method fx { $!x } }
class G is F { has $.x = 2 }
ck G.new.fx, 10, "a parent BUILD's :\$!x default lands in the parent's slot";
ck G.new.x, 2, "…and the child's keeps its own default";
ck G.new(x => 4).fx, 4, "a parent BUILD's :\$!x takes the named argument";
ck G.new(x => 4).x, 4, '…and so does the child attribute';

class F2 { has $.x = 1; method setx($!x) { }; method fx { $!x } }
class G2 is F2 { has $.x = 2 }
{
    my $g = G2.new;
    $g.setx(9);
    ck ($g.fx, $g.x), (9, 2), "a parent method's positional \$!x writes the parent's slot";
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
