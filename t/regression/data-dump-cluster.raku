# Regression: six general faults found by running Data::Dump's own suite
# (the dist ends 9/9). Every expectation checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- a quoted brace inside a string's code block ----------------------------
sub sym($x) { $x }
ck "A{sym('{')}B", 'A{B', Q[a quoted open brace inside an interpolated call stays text];
ck "A{sym('}')}B", 'A}B', Q[and a close brace];
ck "A{ '{' }B", 'A{B', 'a bare quoted brace too';

# --- reflection: methods, attributes, signatures ----------------------------
class E {
    has $.public;
    has Int $!private = 5;
    method r(Str $a) { }
    method !p(Int $x) { }
}
my @names = E.^methods.map(*.name).sort;
ck ('public' ∈ @names), True, 'a public attr accessor is in ^methods';
ck ('p' ∈ @names || '!p' ∈ @names), False, 'a private method is not';
ck ('r' ∈ @names), True, 'declared methods are';

# a method's reflected params carry the implicit invocant and *%_
my @ps = E.^lookup('r').signature.params;
ck @ps[0].invocant, True, 'params[0] is the invocant';
ck @ps[*-1].name, '%_', 'params[*-1] is the implicit *%_';
ck @ps[1 .. *-2].map(*.name).List, ('$a',), 'the middle is the real signature';

# an Attribute stringifies as its declaration, not its guts
my $attr = E.^attributes.grep(*.name eq '$!private').head;
ck (~$attr).contains('$!private'), True, '~$attr names the attribute';

# get_value on a TYPE object throws (Data::Dump relies on the try-chain)
ck (try { $attr.get_value(E); True } // False), False, 'get_value(TypeObject) dies';

# --- .^methods/.^attributes on a builtin type answer, not die ---------------
ck (try { Any.^methods; True } // False), True, 'Any.^methods does not die';

# --- .^can on a builtin value probes ----------------------------------------
my $m = 'hello world' ~~ /'o w'/;
ck (?$m.^can('orig')), True, 'a Match can orig';
ck (?$m.^can('nosuchmethod')), False, 'but not an invented name';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
