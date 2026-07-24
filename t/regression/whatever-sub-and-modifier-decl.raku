# Regression: three general fixes surfaced running the zef installer under rakupp.
#   1. `*.&sub` curries into a WhateverCode instead of eagerly calling `sub(*)`.
#      An identity-ish sub used to yield a bare Whatever (`sub($_)` ran on the `*`),
#      so `@a.map(*.&sub)` came back empty. (zef: `@wants .= map: *.&str2identity`.)
#   2. `my $x = EXPR if COND` declares $x in the ENCLOSING scope regardless of COND
#      (compile-time declaration); only the initializer is gated. rakupp used to
#      leave $x undeclared when the whole statement was skipped, or block-scoped
#      when it ran. (zef: `my @uri-candidates = ... if +@uris`.)
#   3. a Slip argument always flattens into a `grep`'s value list, even among
#      several args (the single-arg rule doesn't apply to a Slip). (zef:
#      `grep *.defined, (…).Slip, (…).Slip, (…).Slip` in list-dependencies.)
# Contract: exit 0 + last line PASS.
my @fail;

# 1. *.&sub currying
sub ident($x) { $x }
sub twice($x) { $x * 2 }
@fail.push('and-sub-identity') unless <a b c>.map(*.&ident) eqv ('a', 'b', 'c');
@fail.push('and-sub-arith')    unless (1, 2, 3).map(*.&twice) eqv (2, 4, 6);
@fail.push('and-sub-scalar')   unless (*.&ident)(42) == 42;

# 2. my … if
sub decl-true  { my @x = (1, 2, 3) if True;  @x.elems }
sub decl-false { my @x = (1, 2, 3) if False; @x.elems }
@fail.push('my-if-true')  unless decl-true()  == 3;
@fail.push('my-if-false') unless decl-false() == 0;
# init side effect runs only when the condition holds
my $n = 0;
sub bumped { $n++; 5 }
sub run-it { my $v = bumped() if True;  my $w = bumped() if False; ($v // -1, $w.defined) }
@fail.push('my-if-sideeffect') unless run-it() eqv (5, False) && $n == 1;

# 3. grep flattens Slip args
@fail.push('grep-slip')       unless (grep *.defined, (1, 2).Slip, (3, 4).Slip) eqv (1, 2, 3, 4);
@fail.push('grep-empty-slip') unless (grep *.defined, ().Slip, ().Slip).elems == 0;
@fail.push('grep-single-arg') unless (grep *.so, [1, 2], [3, 4]).elems == 2; # single-arg rule intact

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
