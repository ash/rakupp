# Regression: `role R['x'] does R` — a parameterized role declared over the
# bare role of the same name — was refused as a self-composition, so
# PDF::Content's `role PDF::Content::XObject['Form'] does PDF::Content::XObject`
# could not be declared at all. The exemption covered only the mirror spelling
# (`role R does R['x']`, where the PARENT carries the arguments); a role's own
# `[...]` list makes it a new candidate of the group, declared only after its
# parent resolves, so the `does` names the role declared before it.
# Contract: exit 0 + last line PASS.
my @fail;

# the declaration, and a class composing the specialization
role W { method w { 5 } }
role W['X'] does W { method more { 6 } }
class CW does W['X'] { }
@fail.push("CW.w")        unless CW.w == 5;
@fail.push("CW.more")     unless CW.more == 6;
@fail.push("CW name")     unless CW.^name eq 'CW';
@fail.push("W['X'] name") unless W['X'].^name eq 'W[Str]';
@fail.push("CW keeps base") unless CW.^methods.map(*.name).grep('w');

# the bare role's attributes reach the composed class too
role A2 { has $.a = 7 }
role A2['X'] does A2 { has $.b = 8 }
class CA2 does A2['X'] { }
my $o = CA2.new;
@fail.push("attr a") unless $o.a == 7;
@fail.push("attr b") unless $o.b == 8;

# a RUN-TIME mix-in of the specialization keeps what the bare role declares:
# PDF::Content mixes its XObjects in this way, `$xobject does …XObject[$sub-type]`
my $m = class { }.new;
$m does W['X'];
@fail.push("mixin w")    unless (try { $m.w } // 0) == 5;
@fail.push("mixin more") unless (try { $m.more } // 0) == 6;

# an empty parameter list is a parameterization as well
role V { method v { 1 } }
role V[] does V { }
@fail.push("V[] name") unless V.^name eq 'V';

# the mirror direction still composes the parameterization
role R[::T] { }
role R does R[Int] { }
@fail.push("mirror") unless R.^name eq 'R';

# genuine self-composition still refuses
@fail.push('role Zz1 does Zz1') unless (try { EVAL 'role Zz1 does Zz1 { }'; 0 } // 1) == 1;
@fail.push('class Aa1 is Aa1')  unless (try { EVAL 'class Aa1 is Aa1 { }'; 0 } // 1) == 1;
@fail.push('role Yy1[$x] does Yy1') unless (try { EVAL 'role Yy1[$x] does Yy1 { }'; 0 } // 1) == 1;

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
