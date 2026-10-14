# Regression: a call made at run time (through a variable, a slip, a pointy
# block, a method) never checked the arity of a sigilless parameter. The
# arity table counted `\x` as if it were a capture `|c`, unbounded and never
# required, so `sub h(\x) {…}; my &c = &h; c()` ran with x undefined, and
# `c(1, 2)` dropped the 2. The same table in the method binder did the same
# for `method m(\x)`.
#
# It surfaced as JSON: `to-json(:pretty)` with no value wrote `null` where
# Rakudo dies. That took two more fixes: the codec's hand-over to the
# module's own sub (JSON::Fast, JSON::Native::Raku) is now the caller's
# call, checked as one, and a builtin consumes the name the call was written
# with, so the hand-over's failure is the run-time kind. The
# Rakudo::Internals::JSON methods check their one positional themselves.
#
# Passes under Rakudo too.

use Test;

sub dies-saying(&code, Str $want, Str $desc) {
    my $got = (try { code(); 'no exception' }) // $!.message.lines.head;
    is $got, $want, $desc;
}
my $few1 = 'Too few positionals passed; expected 1 argument but got 0';

sub h(\x) { "got " ~ x }
sub dflt(\x = 5) { "dflt " ~ x }
sub two(\a, \b) { "two {a} {b}" }
sub mix($a, \b, :$n) { "mix $a {b}" }
sub typed(Int \x) { "typed " ~ x }
sub cap(|c) { "cap " ~ c.elems }
sub sub-sig(\x ($p, $q)) { "sub-sig $p $q" }
sub plus(+ints) { "plus " ~ ints.elems }

my &c = &h;
dies-saying { c() }, $few1, 'a missing sigilless positional dies';
dies-saying { c(:p) }, $few1, 'a named argument does not stand in for it';
dies-saying { c(1, 2) }, 'Too many positionals passed; expected 1 argument but got 2', 'one too many dies';
is c(1), 'got 1', 'one argument binds';
my @none;
dies-saying { h(|@none) }, $few1, 'through a slip';
dies-saying { my &t = &two; t(1) }, 'Too few positionals passed; expected 2 arguments but got 1', 'two sigilless';
dies-saying { my &m = &mix; m(1) }, 'Too few positionals passed; expected 2 arguments but got 1',
    'beside a sigilled one';
dies-saying { my &t = &typed; t() }, $few1, 'a typed sigilless parameter';
dies-saying { my $p = -> \x { x }; $p() }, $few1, 'a pointy block';

my &d = &dflt; is d(), 'dflt 5', 'a default still makes it optional';
my &k = &cap; is k() ~ ' ' ~ k(1, 2, 3), 'cap 0 cap 3', 'a capture still takes any number';
my &s = &sub-sig; is s((1, 2)), 'sub-sig 1 2', 'a sub-signature binds one positional';
my &pl = &plus; is pl() ~ ' ' ~ pl(1, 2), 'plus 0 plus 2', 'a +slurpy still takes any number';
is (1, 2, 3).map(-> \x { x * 2 }).join(','), '2,4,6', 'map over a sigilless block';
is (3, 1, 2).sort(-> \a, \b { a <=> b }).join(','), '1,2,3', 'sort with a sigilless comparator';

class C { method m(\x) { "m " ~ x } }
dies-saying { C.m() }, 'Too few positionals passed; expected 2 arguments but got 1', 'a method';
dies-saying { C.m(1, 2) }, 'Too many positionals passed; expected 2 arguments but got 3', 'a method, one too many';
is C.m(4), 'm 4', 'a method binds one';

my \J = ::('Rakudo::Internals::JSON');
dies-saying { J.to-json(a => 1) }, 'Too few positionals passed; expected 2 arguments but got 1',
    'Rakudo::Internals::JSON.to-json with only a named argument';
dies-saying { J.to-json(1, 2) }, 'Too many positionals passed; expected 2 arguments but got 3',
    'Rakudo::Internals::JSON.to-json with two values';
dies-saying { J.from-json() }, 'Too few positionals passed; expected 2 arguments but got 1',
    'Rakudo::Internals::JSON.from-json with no text';

# The engine answers `use JSON::Native` itself; under Rakudo it is an install.
if $*RAKU.compiler.name eq 'rakudo' {
    skip 'JSON::Native is answered by the Raku++ engine', 2;
}
else {
    dies-saying { EVAL 'use JSON::Native; to-json(:pretty)' }, $few1, 'JSON::Native to-json with no value';
    dies-saying { EVAL 'use JSON::Native; from-json()' }, $few1, 'JSON::Native from-json with no text';
}

done-testing;
