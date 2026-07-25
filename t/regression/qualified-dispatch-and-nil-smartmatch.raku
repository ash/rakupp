# Regression: two general fixes surfaced implementing `zef install` under rakupp.
#   1. `$obj.Class::method` dispatches to Class's method, reaching PAST the
#      invocant's own override. The qualifier was stripped, so `self.Parent::meth`
#      from within an override called the override again → infinite recursion.
#      (zef: `self.Zef::Distribution::meta` inside Local.meta.)
#   2. `$x ~~ Nil` is true only for Nil ITSELF — a defined-but-empty hash/list/str
#      does NOT match Nil (it used to, so `when <falsy-expr>` fired wrongly on a
#      hash topic). (zef: the license-filter `when .<blacklist>.?chars && …`.)
# Contract: exit 0 + last line PASS.
my @fail;

# 1. qualified parent-method dispatch (class inheritance)
class P { method m { 'parent' } }
class C is P { method m { self.P::m ~ '+child' } }
@fail.push('class-qual') unless C.new.m eq 'parent+child';
# with attribute access in the parent method
class P2 { method describe { self.x } }
class C2 is P2 { has $.x = 42; method describe { 'C2:' ~ self.P2::describe } }
@fail.push('class-qual-attr') unless C2.new.describe eq 'C2:42';
# an unknown/builtin qualifier falls through to ordinary dispatch
@fail.push('builtin-qual') unless (1, 2, 3).Any::elems == 3;

# 2. ~~ Nil is Nil-only
@fail.push('emptyhash-nil') if (my %h) ~~ Nil;
@fail.push('emptylist-nil') if () ~~ Nil;
@fail.push('emptystr-nil')  if '' ~~ Nil;
@fail.push('nil-nil')       unless Nil ~~ Nil;
@fail.push('int-nil')       if 5 ~~ Nil;
# the case that bit zef: `when <falsy-expr>` must not fire on a hash topic
my $r = do given %h {
    when .<blacklist>.?chars && any(|.<blacklist>) ~~ any('*') { 'fired' }
};
# a `given` whose `when` never matches yields False — which IS defined, so the
# check is that the block did not FIRE, not that $r is undefined
@fail.push("when-falsy-on-hash ({$r.raku})") unless $r === False;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
