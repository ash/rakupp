# BROKE (long-standing, found via S06-signature/unpack-object.t in the
# no-TAP campaign): multi-element `for` loops with a sub-signature param
# (`-> $ (:$key, :$value)`) took the single-var FAST PATH, which binds
# the topic directly and never runs real signature binding — the inner
# names were undeclared. Single-element iterations worked (different
# branch), which masked it.
# FIXED: the fast path requires fs->params to be empty (nu7 gate).
my @r;
for (a => 1, b => 2) -> $ (:$key, :$value) { @r.push("$key=$value") }
die "pair unpack broken: {@r.raku}" unless @r.join(',') eq 'a=1,b=2';
class A { has $.x }
my @x;
for A.new(x => 4), A.new(x => 2) -> $ (:$x) { @x.push($x) }
die "object unpack broken" unless @x.join('') eq '42';
my @p;
for [1,2], [3,4] -> $ ($a, $b) { @p.push("$a-$b") }
die "positional destructure broken" unless @p.join(' ') eq '1-2 3-4';
say 'PASS';
