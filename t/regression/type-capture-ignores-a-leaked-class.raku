# A bare `::T $x` constrains nothing: T names whatever type arrived. The
# binder checked the value against a type CALLED T whenever one was
# registered, and a `my grammar T` declared inside an earlier EVAL stays
# registered after it, so Definitely's `multi method new(::T $value)` refused
# 42 in any process that had run such an EVAL first (a rakuglaze chunk).
use Test;
use MONKEY-SEE-NO-EVAL;
plan 5;

EVAL q[my grammar T { token TOP { x } }; T.parse('x')];

my class Some {
    has $.value;
    has $.type;
    multi method new(::T $value) { self.bless(:$value, type => T) }
}
is Some.new(42).type.^name, 'Int', 'the capture binds an Int';
is Some.new('x').type.^name, 'Str', '…and a Str';

sub same(::T $x, T $y) { 'ok' }
is same(1, 2), 'ok', 'a later T $y takes the captured type';
throws-like { same(1, 'two') }, X::TypeCheck::Binding::Parameter, '…and refuses another';

sub typed(::T Int $x) { T.^name }
throws-like { typed(my $ = 's') }, X::TypeCheck::Binding::Parameter,
    'a capture that also names a type still checks that type';
