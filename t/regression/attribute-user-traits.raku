# Regression: a user-defined `trait_mod:<is>` on an ATTRIBUTE must actually be
# CALLED. Sub-, class- and method-level traits all dispatched; attribute traits
# did not — their names and payloads were merely recorded on the ClassAttr and
# answered by a hardcoded table of the role names the JSON:: ecosystem happens to
# use. Anything not in that table silently did nothing: META6's `is customary`
# left two attributes without MetaAttribute, and its 020-basic.t failed.
#
# The trait body's shape, which is what makes this work: it mixes a role into the
# Attribute meta-object and then writes the role's state through it —
#   multi sub trait_mod:<is>(Attribute $a, :$customary!) {
#       $a does MetaAttribute::Customary;
#       $a.where = 'unknown';
#   }
# so the meta-object must be built ONCE and cached (`.^attributes` hands back the
# same object), `does` on it must mix in place rather than boxing a copy nobody
# can reach, and a mixed-in role's accessors must read and write it.
# Contract: exit 0 + last line PASS.
my @fail;

role Marker { }
role Tagged does Marker {          # a role composing a role: the attributes are
    has Str $.where is rw;         # on the OUTER one, and the trait assigns to
    has Int $.rank is rw = 7;      # them the line after the mixin
}

my @called;
multi sub trait_mod:<is> (Attribute $a, :$tagged!) {
    @called.push($a.name ~ '/tagged=' ~ $tagged.^name);
    $a does Tagged;
    $a.where = $tagged ~~ Str ?? $tagged !! 'unknown';
}
multi sub trait_mod:<is> (Attribute $a, Int :$ranked!) {
    @called.push($a.name ~ '/ranked=' ~ $ranked);
    $a does Tagged;
    $a.rank = $ranked;
}

class C {
    has Str $.plain is rw;
    has Str $.bare  is rw is tagged;              # bare trait: the value is True
    has Str $.named is rw is tagged('here');      # …or whatever it was given
    has Int $.deep  is rw is ranked(3);           # a typed candidate
}

my %attr = C.^attributes.map({ .name => $_ });

# the trait ran, once per attribute that carried one
@fail.push("called: {@called.sort}") unless @called.elems == 3;

# .^does sees the role the trait mixed in — and the role that role does
for <$!bare $!named $!deep> -> $n {
    @fail.push("$n does Tagged") unless %attr{$n}.^does(Tagged);
    @fail.push("$n does Marker") unless %attr{$n}.^does(Marker);
}
@fail.push('plain must not do Tagged') if %attr<$!plain>.^does(Tagged);

# the role's accessors read what the trait wrote
@fail.push("bare.where ({%attr<$!bare>.where})")   unless %attr<$!bare>.where eq 'unknown';
@fail.push("named.where ({%attr<$!named>.where})") unless %attr<$!named>.where eq 'here';
@fail.push("deep.rank ({%attr<$!deep>.rank})")     unless %attr<$!deep>.rank == 3;
# …including an attribute the trait never touched, which keeps its default
@fail.push("bare.rank ({%attr<$!bare>.rank})")     unless %attr<$!bare>.rank == 7;

# `.^attributes` is the SAME object each time — a fresh one would have forgotten
# the trait ever ran
@fail.push('identity') unless C.^attributes.first(*.name eq '$!named').where eq 'here';

# the ordinary Attribute surface still answers
@fail.push('name')  unless %attr<$!plain>.name eq '$!plain';
@fail.push('rw')    unless %attr<$!plain>.rw;
@fail.push('acc')   unless %attr<$!plain>.has_accessor;
@fail.push('type')  unless %attr<$!deep>.type === Int;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
