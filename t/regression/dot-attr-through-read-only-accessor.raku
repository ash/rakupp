# Regression (RAKUDO-ISSUES-PLAN R3; rakudo#5908, rakudo#6113, rakudo#4463):
# `$.x` in a method is `$(self.x)`, so through a READ-ONLY accessor it is a
# copy of the value.
#
# Before the fix `$.x` reached the attribute itself, whatever its accessor:
# `has $.b = 42; method bb { $.b = 666 }` set 666 where Rakudo dies "Cannot
# modify an immutable Int (42)", and `$.n++`, `$.n += 1` and `$.h<k> = 1`
# changed the attribute where Rakudo changes only the copy. Assigning from
# outside (`$o.b = 5`) refused, but said "Cannot modify an immutable 'b'".
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub dies-with(Str $what, &code, Str $msg) {
    my $died = False;
    try { code(); CATCH { default { $died = True;
        check "$what: type", .^name, 'X::Assignment::RO';
        check "$what: message", .message, $msg } } }
    @fail.push("$what: did not die") unless $died;
}

# a plain assignment refuses, naming the value
class A { has $.b = 42; method bb { $.b = 666 } }
dies-with '$.b = 666 (rakudo#5908)', { A.new.bb }, 'Cannot modify an immutable Int (42)';
class B { has $.a; method test { $.a = 5 } }
dies-with '$.a = 5 on an undefined attribute (rakudo#6113)', { B.new.test },
    "Cannot modify an immutable 'Any' type object";
role R { has $.a; method t { $.a = 7 } }
class RC does R { }
dies-with 'a role method', { RC.new.t }, "Cannot modify an immutable 'Any' type object";
dies-with 'from outside', { A.new.b = 5 }, 'Cannot modify an immutable Int (42)';
class U { has $.a = 1; method a { $!a * 10 }; method t { $.a = 5 } }
dies-with 'through a user method that is not rw', { U.new.t }, 'Cannot modify an immutable Int (10)';

# any other write reaches the copy, and the attribute keeps its value
class N {
    has $.n = 1; has $.h; has $.s = 'x';
    method incr  { $.n++; ++$.n; $.n += 1; $.n ~= 'x'; $!n }
    method viv   { $.h<k> = 1; $.h.push(2) if False; $!h }
    method subst { $.s ~~ s/x/y/; $!s }
}
check '$.n++, ++$.n, $.n += 1, $.n ~= "x"', N.new.incr, 1;
check '$.h<k> = 1 on an undefined attribute', N.new.viv, Any;
check '$.s ~~ s///', N.new.subst, 'x';
class C { has $.c = 4; method t { my $r := $.c; $r = 3; $!c } }
check 'a binding of $.c', C.new.t, 4;
# a container inside the value is still the object's own (rakudo#4463's raw method aside)
class L { has $.list = []; method t { $.list.push(5); $.list[1] = 6; $!list } }
check '$.list.push and $.list[1]', L.new.t, [5, 6];

# writable accessors still write: `is rw`, the class's `is rw`, an inherited
# `is rw`, a user method that is rw, and the @ and % attributes
class W1 { has $.a is rw; method t { $.a = 5; $.a++; $!a } }
check 'is rw', W1.new.t, 6;
class W2 is rw { has $.a; method t { $.a = 7; $!a } }
check 'class is rw', W2.new.t, 7;
class P { has $.a is rw }
class W3 is P { method t { $.a = 8; $.a } }
check 'an inherited is rw', W3.new.t, 8;
class W4 { has $.a; method a is rw { $!a }; method t { $.a = 9; $!a } }
check 'a user method that is rw', W4.new.t, 9;
class W5 { has @.a; has %.h; method t { @.a = 1, 2; %.h = k => 3; (@!a, %!h) } }
check '@.a and %.h', W5.new.t, ([1, 2], {k => 3});

# the reads are what they were
class Rd { has $.a = 1; method t { ($.a, $.a + 1, "{$.a}") } }
check 'reads', Rd.new.t, (1, 2, '1');

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
