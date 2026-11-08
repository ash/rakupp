# Regression: a method `handles` delegation did not satisfy a role's stub
# requirement. `class PDF::Lite does PDF::Content::API` died with
# "Method 'page' must be implemented by PDF::Lite because it is required by
# roles: PDF::Content::API", although PDF::Lite answers it with
# `method Pages returns Pages handles <page …> { self.Root.Pages }` — dispatch
# already honoured the delegation; only the composition check did not look at it.
# Attribute `handles` counted all along (Roast S14-roles/stubs.t: "composing stub
# implemented with attribute handles"), and Rakudo treats both alike.
# Contract: exit 0 + last line PASS.
my @fail;

role R { method page { ... } }
class Pages { method page { 'page!' } }

# the delegation sits on the class that composes the role
class C does R { method Pages returns Pages handles <page> { Pages.new } }
@fail.push("own delegation") unless (try { C.new.page } // '') eq 'page!';

# …or on a parent class (Roast: "stubbed method can come from parent class too")
class Base { method Pages returns Pages handles <page> { Pages.new } }
class D is Base does R { }
@fail.push("inherited delegation") unless (try { D.new.page } // '') eq 'page!';

# …or it arrives with a role the class composes
role W { method Pages returns Pages handles <page> { Pages.new } }
class E does W does R { }
@fail.push("delegation from a role") unless (try { E.new.page } // '') eq 'page!';

# several names from one delegation, as PDF::Lite's page tree declares them
role API2 {
    method pages { ... }
    method add-page { ... }
}
class P2 {
    method page { 'p!' }
    method pages { 'ps!' }
    method add-page { 'added!' }
}
class F does API2 { method Pages returns P2 handles <page pages add-page> { P2.new } }
my $f = F.new;
@fail.push("pages")    unless (try { $f.pages } // '') eq 'ps!';
@fail.push("add-page") unless (try { $f.add-page } // '') eq 'added!';

# a requirement nothing delegates still refuses
@fail.push('negative') unless
    (try { EVAL 'role Rq1 { method page { ... } }; class Cq1 does Rq1 { }'; 0 } // 1) == 1;

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
