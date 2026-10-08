# Regression (issue #135): a class with nothing but Any and Mu above it has
# none of Cool's methods. `class A { }; my A $a; $a.index` answered 0, and
# `.uc`, `.chars`, `.sqrt`, `.lines`, `.IO` and the rest answered on the
# object's stringification or numification, while `.^can('index')` said there
# was no such method. Each is X::Method::NotFound, as any unknown name is, so a
# FALLBACK, a `.^add_fallback` or a `handles *` delegation gets it first;
# `split`, `fmt` and `chrs` (multis Any declares) and `.Int`/`.Numeric`/`.Real`
# on an instance are X::Multi::NoMatch. A class that is Cool, a Str or a Real,
# and a class's own method or delegation, still answer.
# Contract: exit 0 + last line PASS.
my @fail;

sub dies-with(Str $label, Mu $type, &code, :$method) {
    my $ok = False;
    my $got = 'lived';
    try { code(); CATCH { default { $got = .^name; $ok = $_ ~~ $type && (!$method || .method eq $method) } } }
    @fail.push("$label: got $got, want {$type.^name}") unless $ok;
}
sub is-eq(Str $label, Mu $got, Mu $want) {
    @fail.push("$label: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

class A { }
class B is A { }
role R { }

# the issue's program
my A $a;
is-eq 'A.^can(index)', $a.^can('index').elems, 0;
dies-with 'type object .index()', X::Method::NotFound, { $a.index() }, :method<index>;
dies-with 'type object .index(…)', X::Method::NotFound, { $a.index('ca', 'be', 'any', 'anything') }, :method<index>;

for <index rindex substr chars uc lc tc fc flip trim chomp comb words lines ords contains
     starts-with ends-with subst trans abs sqrt floor ceiling round sign exp log sin succ pred
     base is-prime IO path Rat Num wordcase samecase uniname NFC encode> -> $m {
    dies-with "A.new.$m", X::Method::NotFound, { A.new."$m"('x') }, :method($m);
    dies-with "A.$m", X::Method::NotFound, { A."$m"('x') }, :method($m);
    dies-with "B.new.$m", X::Method::NotFound, { B.new."$m"('x') }, :method($m);
}
dies-with 'role pun .uc', X::Method::NotFound, { R.uc }, :method<uc>;
dies-with 'instance .Num', X::Method::NotFound, { A.new.Num }, :method<Num>;
is-eq '.?index', A.new.?index('x'), Nil;

for <split fmt chrs> -> $m {
    dies-with "A.new.$m", X::Multi::NoMatch, { A.new."$m"() };
    dies-with "A.$m", X::Multi::NoMatch, { A."$m"() };
}
for <Int Numeric Real> -> $m {
    dies-with "A.new.$m", X::Multi::NoMatch, { A.new."$m"() };
}

# what an unknown name gets, these get
class F { method FALLBACK($name, |) { "fb:$name" } }
is-eq 'FALLBACK .index', F.new.index('a'), 'fb:index';
is-eq 'FALLBACK .uc', F.new.uc, 'fb:uc';
is-eq 'FALLBACK on the type', F.chars, 'fb:chars';
is-eq 'FALLBACK .sqrt', F.new.sqrt, 'fb:sqrt';
dies-with 'FALLBACK does not take .split', X::Multi::NoMatch, { F.new.split('a') };
class G { }
G.^add_fallback(-> $, $name { $name eq 'uc' }, -> $, $name { method { "added:$name" } });
is-eq '.^add_fallback', G.new.uc, 'added:uc';

# delegation and a method of the class's own still answer
class H  { has $.s handles <uc chars> }
class H2 { has $.s handles * }
class H4 { has $.s handles /^ind/ }
class M  { method index($x) { "mine:$x" } }
is-eq 'handles <uc>', H.new(s => 'ab').uc, 'AB';
is-eq 'handles <chars>', H.new(s => 'ab').chars, 2;
dies-with 'handles <uc chars> leaves .index', X::Method::NotFound, { H.new(s => 'ab').index('b') }, :method<index>;
is-eq 'handles *', H2.new(s => 'ab').index('b'), 1;
is-eq 'handles * .uc', H2.new(s => 'ab').uc, 'AB';
is-eq 'handles /^ind/', H4.new(s => 'ab').index('b'), 1;
dies-with 'handles /^ind/ leaves .uc', X::Method::NotFound, { H4.new(s => 'ab').uc }, :method<uc>;
is-eq 'own method', M.new.index(1), 'mine:1';
is-eq 'own method on the type', M.index(2), 'mine:2';
dies-with 'own .index leaves .rindex', X::Method::NotFound, { M.new.rindex(1) }, :method<rindex>;

# a Cool, a Str and a Real keep theirs
class D is Str { }
class E does Real { method Bridge { 4e0 } }
role RR does Real { }
class E2 does RR { method Bridge { 9e0 } }
is-eq 'is Str', D.new(value => 'dd').uc eq 'DD', True;
is-eq 'does Real .sqrt', E.new.sqrt, 2e0;
is-eq 'role that does Real', E2.new.sqrt, 3e0;
is-eq '.Str first', A.new.Str.index('A'), 0;

if @fail { .say for @fail; say "FAIL" }
else { say "PASS" }
