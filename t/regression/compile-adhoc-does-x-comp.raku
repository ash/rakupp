# An X::AdHoc the compiler raises does X::Comp, as Rakudo's is an
# `X::AdHoc+{X::Comp}`: `try EVAL q[sub f(%h is rw) {}]; $! ~~ X::Comp` was
# False. It keeps its line and its payload. And `class GLOBAL {}` is refused
# in the words every other pseudo-package gets, X::PseudoPackage::InDeclaration
# "Cannot use pseudo package GLOBAL in package name", where it was an X::AdHoc
# of its own; `class GLOBAL::B {}` still declares B.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

try EVAL q[sub f(%h is rw) {}];
ck ($!.^name, $! ~~ X::Comp, $! ~~ X::AdHoc, $!.line), ('X::AdHoc+{X::Comp}', True, True, 1), 'is rw on a %';
ck $!.message.lines[0], "For parameter '\%h', '\%' sigil containers don't need 'is rw' to be writable", 'its message';
ck $!.payload.lines[1], "Can only use 'is rw' on a scalar ('\$' sigil) parameter, not '\%h'", 'its payload';
try EVAL qq[\nsub g(\@a is rw) \{}];
ck ($!.^name, $!.line), ('X::AdHoc+{X::Comp}', 2), 'on its line';
try EVAL q[class GLOBAL {}];
ck ($!.^name, $!.message), ('X::PseudoPackage::InDeclaration', 'Cannot use pseudo package GLOBAL in package name'),
   'class GLOBAL';
try EVAL q[class OUR {}];
ck $!.message, 'Cannot use pseudo package OUR in package name', 'class OUR, as before';
class GLOBAL::B { method m { 1 } }
ck B.m, 1, 'class GLOBAL::B';

say 'PASS';
