# Regression: the `use nqp` compatibility subset (docs/dev/MODULE-FINDINGS.md
# #4b), added for the v2 ecosystem campaign. MUST be zero-cost when unused.
# 2026-07-22.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { say "FAIL: $label — got {$got.raku}, want {$want.raku}"; $ok = False }
}

# zero-cost invariant: without `use nqp`, nqp::* is an ordinary (undefined)
# qualified call — the parser never builds an NqpOp node.
my $bare = qqx{$*EXECUTABLE -e 'say nqp::add_i(1,2)' 2>&1};
unless $bare.contains('Undefined routine') {
    say 'FAIL: nqp:: leaked without use nqp';
    $ok = False;
}

# with `use nqp`, the subset evaluates (run in a child so this file stays clean)
my $src = q:to/NQP/;
use nqp;
print nqp::add_i(20, 22), " ";
print nqp::iseq_i(5, 5), " ";
print nqp::substr("hello world", 6, 5), " ";
print nqp::chars("héllo"), " ";
print nqp::concat("a", "b"), " ";
my @a := nqp::list_i();
nqp::push_i(@a, 7); nqp::push_i(@a, 9);
print nqp::elems(@a), " ", nqp::atpos_i(@a, 1), " ";
my $i := 0; my $s := 0;
nqp::while(nqp::islt_i($i, 5),
  nqp::stmts(($s := nqp::add_i($s, $i)), ($i := nqp::add_i($i, 1))));
print $s, " ";
print nqp::if(nqp::iseq_i(1,1), "Y", "N"), " ";
print nqp::findnotcclass(nqp::const::CCLASS_NUMERIC, "123abc", 0, 6);
NQP
my $tmp = 'temp_nqp_' ~ $*PID ~ '.raku';
spurt $tmp, $src;
my $out = qqx{$*EXECUTABLE $tmp 2>&1}.trim;
unlink $tmp;
check($out, '42 1 world 5 ab 2 9 10 Y 3', 'nqp subset evaluation');

say 'PASS' if $ok;
