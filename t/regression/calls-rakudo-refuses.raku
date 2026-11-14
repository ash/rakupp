# Calls Rakudo refuses, refused here too: a Regex given to `substr`, a
# private name in a computed method call (rakudo#3802), a Hash given to
# `Lock.protect` (rakudo#1644) and `.chrs` of an infinite list (rakudo#2742).

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

try 'x'.substr(/x/, 'x');
check $!.message, "You cannot use a Regex on 'substr', did you mean 'subst'?", 'substr of a Regex';
class C { method !p { 1 }; method m { my $n = 'p'; self."!$n"() } }
try C.new.m;
check $!.^name, 'X::Method::NotFound', 'self."!$name"() (rakudo#3802)';
check $!.message.contains(q[did you mean 'self!"p"()']), True, '…naming the call to write';
class D { method !q { 2 }; method m { self!q } }
check D.new.m, 2, 'a private method named q, as before';
try Lock.new.protect({});
check $!.^name, 'X::Multi::NoMatch', 'Lock.protect of a Hash (rakudo#1644)';
check Lock.new.protect({ 42 }), 42, '…of a block, as before';
try (0...∞).chrs;
check $!.^name, 'X::Cannot::Lazy', '.chrs of an infinite list (rakudo#2742)';
check (65...*).head(3).chrs, 'ABC', '…of a finite one, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
