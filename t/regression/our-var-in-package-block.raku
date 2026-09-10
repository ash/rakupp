# Regression: `our $v` declared in a `module`/`package` BLOCK is one container,
# the same as one declared in a `class` body.
#
# f18b71c made the published name a VIEW onto the declaring scope's slot, so
# `$Pkg::VAR = 4` written outside is a write to the variable the package itself
# reads. A braced `module Foo { … }` then republished every symbol of its body
# by VALUE after running it, copying straight over that view — so the class form
# was one container and the module form still two, from the same declaration.
# Reported as issue #76 (rakupp answered `foo foo foo` where Rakudo answers
# `foo bar baz`); the class half of it was fixed before v3.26.0 shipped, hours
# after the tag, and the module half is this.
#
# A BARE `our $v;` was two containers in a module too: the declaration publish
# lives in evalAssign, and a declaration with no initialiser never assigns.
# Each row gets its own package, so no row can be carried by another's write.
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { note "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 1. module block, outside write -> the module's own reader
module M1 { our $v = 'm1-in'; our sub peek { $v } }
$M1::v = 'm1-out';
ck M1::peek(), 'm1-out', 'a module block sees a write through its qualified name';

# 2. package block, the same
package P2 { our $v = 'p2-in'; our sub peek { $v } }
$P2::v = 'p2-out';
ck P2::peek(), 'p2-out', 'a package block sees it too';

# 3. class body — the half fixed first, which must stay fixed
class C3 { our $v = 'c3-in'; our sub peek { $v } }
$C3::v = 'c3-out';
ck C3::peek(), 'c3-out', 'a class body still sees it';

# 4. the reporter's own direction: the package writes, the outside reads
module M4 {
    our $v = 'm4-in';
    our sub setit { $v = 'm4-set' }
}
M4::setit();
ck $M4::v, 'm4-set', "a package's own write reaches the qualified name";

# 5. through a method, which is how issue #76 was written
class C5 {
    our $v = 'c5-in';
    method setit { $v = 'c5-set' }
}
C5.new.setit;
ck $C5::v, 'c5-set', "a method's write reaches the qualified name";

# 6. a BARE `our $v;` — no initialiser, so no assignment to publish from
module M6 { our $v; our sub peek { $v // 'unset' } }
$M6::v = 'm6-out';
ck M6::peek(), 'm6-out', 'a bare `our $v;` in a module is one container';

# 7. bare `our @a` / `our %h` in a package, written whole from outside
module M7 {
    our @a;
    our %h;
    our sub peek { @a.join(',') ~ '/' ~ (%h<k> // 'unset') }
}
@M7::a = <m7-x m7-y>;
%M7::h = k => 'm7-z';
ck M7::peek(), 'm7-x,m7-y/m7-z', 'bare `our @a` / `our %h` are one container';

# 8. binding an alias to the qualified name reaches the same variable
module M8 { our $v = 'm8-in'; our sub peek { $v } }
my $alias := $M8::v;
$alias = 'm8-alias';
ck M8::peek(), 'm8-alias', 'a binding to the qualified name aliases the slot';

# 9. `our &f` stays a callable published by name, not a view onto one
module M9 { our &f = sub { 'm9-called' }; our sub g { 'm9-sub' } }
ck &M9::f(), 'm9-called', 'an `our &f` is still callable by qualified name';
ck M9::g(),  'm9-sub',    'an `our sub` is still callable by qualified name';

# 10. a `my` in a package body is NOT published — the view must not widen that
module M10 { my $hidden = 'm10'; our $shown = 'm10-shown'; our sub peek { $hidden } }
ck M10::peek(), 'm10', 'a `my` package var is still readable inside';
ck M10::.EXISTS-KEY('$hidden'), False, '…and is still not published';
ck $M10::shown, 'm10-shown', '…while the `our` beside it is';

# 11. the view survives a second write, in both directions
module M11 { our $v = 'm11-a'; our sub peek { $v }; our sub setit($x) { $v = $x } }
$M11::v = 'm11-b';
ck M11::peek(), 'm11-b', 'first write through the name';
M11::setit('m11-c');
ck $M11::v, 'm11-c', 'then the package writes back';
$M11::v = 'm11-d';
ck M11::peek(), 'm11-d', 'and the name writes again';

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
exit $ok == $n ?? 0 !! 1;
