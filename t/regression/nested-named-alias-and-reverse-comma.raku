# Regression: two parser features zef's CLI needs —
#   1. a NESTED named-parameter alias after a type: `Bool :h(:help($))` (both --h
#      and --help bind an anonymous scalar). The untyped form already worked; the
#      typed path was missing the nested-alias loop.
#   2. the reverse-comma metaop `a R, b` == the list `(b, a)` (zef's plugin-probe
#      loop writes `next() R, DEBUG(...)`).
# Contract: exit 0 + last line PASS.
my @fail;

# nested alias after a type — both keys answer, binds anon $
sub h1(Bool :h(:help($))) { 'ok' }
@fail.push('nested-h')    unless h1(:h) eq 'ok';
@fail.push('nested-help') unless h1(:help) eq 'ok';

# nested alias, named var, three layers
sub h2(:x(:y(:z($v)))) { $v }
@fail.push('three-x') unless h2(x => 5) == 5;
@fail.push('three-z') unless h2(z => 9) == 9;

# reverse comma
@fail.push("R-comma ({(1 R, 2).raku})") unless (1 R, 2) eqv (2, 1);
@fail.push('R-comma-expr') unless ('a' R, 'b') eqv ('b', 'a');

# the ordinary reverse metaop still works, and plain comma is unaffected
@fail.push('R-minus') unless (3 R- 5) == 2;
@fail.push('R-div')   unless (10 R/ 2) == 0.2;
@fail.push('comma')   unless (1, 2, 3) eqv (1, 2, 3);

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
