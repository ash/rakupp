# A `handles` delegation written on a METHOD — `method Pages handles <page>`
# — implements a role's `method page { ... }`, as an attribute's always did:
# the class died "Method 'page' must be implemented by C because it is
# required by roles: R", on its own delegation, an inherited one, or one a
# composed role brings. PDF::Lite answers PDF::Content::API's stubs that way.
# A delegation from a role in a later `does` slot was not dispatched either,
# and one from a second parent (`class D is B is A`) was not dispatched,
# whether on a method or on an attribute, though an attribute's counted for
# the role. Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class Pg { method page { 'p' }; method pages { 'ps' }; method add-page { 'added' } }
role R { method page { ... } }

class Own does R { method Pages handles <page> { Pg.new } }
ck Own.new.page, 'p', "the class's own method delegation implements the stub";

class Base { method Pages handles <page> { Pg.new } }
class Inh is Base does R { }
ck Inh.new.page, 'p', '…an inherited one';

class B { }
class A { method Pages handles <page> { Pg.new } }
class Second is B is A does R { }
ck Second.new.page, 'p', "…one from a second parent";
class SecondPlain is B is A { }
ck (SecondPlain.new.page, SecondPlain.page), ('p', 'p'), '…which is dispatched, on an instance and the type';

class AA { has $.pg handles <page> = Pg.new }
class SecondAttr is B is AA does R { }
ck SecondAttr.new.page, 'p', "an attribute's from a second parent is dispatched";

role W { method Pages handles <page> { Pg.new } }
class First does W does R { }
class Later does R does W { }
ck (First.new.page, Later.new.page), ('p', 'p'), "a composed role's, in any `does` slot";
role Z { }
class LaterPlain does Z does W { }
ck LaterPlain.new.page, 'p', '…which is dispatched without a stub too';
role Via does W { }
class Deep does R does Via { }
ck Deep.new.page, 'p', '…and through a role that does it';

class Par { method page { 'parent' } }
class OverParent is Par does W { }
ck OverParent.new.page, 'p', "a role's delegation outranks an inherited method";

role API { method pages { ... }; method add-page { ... } }
class Several does API { method Pages handles <pages add-page> { Pg.new } }
ck (Several.new.pages, Several.new.add-page), ('ps', 'added'), 'several names from one delegation';

my $refused = (try { EVAL q[role Rq { method page { ... }; method other { ... } }
                            class Cq does Rq { method Pages handles <page> { Pg.new } }]; 'composed' })
              // $!.message;
ck $refused, "Method 'other' must be implemented by Cq because it is required by roles: Rq.",
   'a requirement nothing delegates is still refused';

say 'PASS';
