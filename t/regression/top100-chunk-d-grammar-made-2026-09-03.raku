# Regression: a rule reached through a NON-capturing `<.rule>` call must still
# make its children's `.made` values available to its own action. The action was
# already firing (a prior fix); what was lost is that the parent's replay-build
# rebuilt the children with their actions suppressed, so `$/<child>.made` came
# back empty. HTTP::Parser reads its header NAME exactly that way, and the whole
# key vanished ("" => value). Rakudo 2026.08 is the oracle.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

# The HTTP::Parser shape, minimised: a non-capturing parent in a quantifier,
# whose child's action sets a made value the parent reads.
grammar G {
    token TOP { [ <.pair> ',' ]* }
    token pair { <name> '=' <val> }
    token name { <.word> }
    token val  { <.word> }
    token word { \w+ }
}
class A {
    has %.seen;
    method name($_) { .make: .Str.uc }      # sets $<name>.made via a method call
    method pair($/) { %!seen{$/<name>.made} = ~$/<val> }
}
my $a = A.new;
G.parse("foo=1,bar=2,", :actions($a));
ok($a.seen<FOO> eq '1', 'a non-captured pair reads its child name.made (FOO)');
ok($a.seen<BAR> eq '2', '…for every iteration of the quantifier (BAR)');
ok($a.seen.elems == 2, '…and both survive (no empty-key collapse)');

# A captured rule over the SAME span as a non-captured sibling must NOT inherit
# its made — the reason the store is name-qualified (CSS::Grammar's token-over-
# comment relied on this).
grammar H {
    token TOP { <thing> }
    token thing { <.note> <core> }
    token note { <?> }         # zero-width, same span as the start of thing
    token core { \w+ }
}
class B {
    method note($_) { .make: 'NOTE-MADE' }
    method core($/) { make ~$/ }
    method thing($/) { make $<core>.made }   # must be the core text, not NOTE-MADE
    method TOP($/) { make $<thing>.made }
}
ok(H.parse("hi", :actions(B)).made eq 'hi',
   'a captured rule does not inherit a same-span non-captured sibling made');

say $fails ?? "FAIL ($fails)" !! "PASS";
