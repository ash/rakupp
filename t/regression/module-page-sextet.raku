# Regression: the general fixes behind the six spec-site module divergences
# (JSON::Tiny, XML, UUID, Method::Also, URI::Encode) — each reduced to pure Raku.
#  1. Sigspace quantifiers: space before the quantifier puts <.ws> INSIDE the
#     repetition; the % separator gets <.ws> after it (and only there).
#  2. Inline :ignoremark/:m — literals compare base codepoints, consuming the
#     whole grapheme (both combining marks and NFC-precomposed forms).
#  3. \cNN decimal codepoint escapes: bare, class member, and class range
#     (JSON::Tiny's <-[\c32..\c126]> escaper).
#  4. Str.encode('utf-16') yields 16-bit code units with surrogate pairs.
#  5. <!["]> / <?["]> — the assertion inner is a CHARACTER CLASS, so a quote
#     member must not open a string literal (XML's value token).
#  6. token/rule parameter DEFAULTS bind when called without args — including
#     dynamic-var params ($*STOPPER) visible in rules the parameterised rule
#     calls, and via a :rule<…> entry point.
#  7. Assigning @-attr through its accessor list-assigns (`$obj.nodes = @list`
#     stores elements, not one itemized array) — XML::Element's tree build.
#  8. `$s ~= $obj` honours a user-defined Str method like binary `~` does.
#  9. :256[…] spills to big Int past 64 bits; sprintf-style integer PRECISION
#     zero-pads big values (`%32.32x` of a 128-bit UUID).
# 10. Match.join joins the positional captures (a Match is a Capture).
# 11. A named capture under a quantifier is list-valued — also inside a subst
#     callback (URI::Encode's $<bit> percent-decoder).
# 12. Method-level user `is` traits dispatch to trait_mod:<is> with the angle
#     word-list argument and $*PACKAGE bound; `T.HOW does R` mixins persist and
#     a mixed-in `compose` runs at class composition (Method::Also's machinery).
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}"; note "FAIL: $desc" }
}

# 1. sigspace quantifier semantics
grammar SigA {
    rule TOP { <num> * % \, }
    token num { \d+ }
}
grammar SigB {
    rule TOP { <num>* % \, }
    token num { \d+ }
}
grammar SigC {
    rule TOP { <num> + }
    token num { \d+ }
}
ck(?SigA.parse("1 , 2"), True,  'rule <num> * % \, allows ws around the separator');
ck(?SigA.parse("1, 2 "), True,  'trailing ws joins the last iteration');
ck(?SigA.parse("1 2"),   False, 'no comma still means no match');
ck(?SigB.parse("1, 2"),  True,  'tight quantifier: ws after separator only');
ck(?SigB.parse("1 , 2"), False, 'tight quantifier: no ws before the separator');
ck(?SigC.parse("1 2"),   True,  'space before + distributes <.ws> into the repetition');

# 2. inline ignoremark
ck(?("a\x[308]bc" ~~ /:ignoremark abc/), True, ':ignoremark matches through a combining mark');
ck(?("äbc" ~~ /:m abc/),                 True, ':m matches the precomposed form');
ck(?("xbc" ~~ /:ignoremark abc/),        False, ':ignoremark still compares base letters');

# 3. \c decimal escapes
ck("four".subst(/<-[\c32..\c126]>/, "X", :g), "four", '\c32..\c126 class range (nothing escapable)');
ck("fo\x[7]ur".subst(/<-[\c32..\c126]>/, "X", :g), "foXur", 'control char lands outside the range');
ck(?("A" ~~ /\c65/), True, 'bare \c65 is decimal codepoint 65');
ck(?("B" ~~ /<[\c65]>/), False, '<[\c65]> is a one-member class');

# 4. utf-16 code units
ck("é".encode("utf-16").values.List, (233,), 'utf-16 encodes code units, not UTF-8 bytes');
ck("😀".encode("utf-16").values.List, (55357, 56832), 'astral chars become a surrogate pair');

# 5. class assertions with quote members
ck(("7" ~~ /<!["]> ./).Str, "7", '<!["]> is a class assertion, not a string opener');
ck(("7" ~~ /<?["7]> ./).Str, "7", '<?["7]> positive class lookahead');
grammar QVal {
    token char  { <!["]> .+? <?["]> }
    token value { \" <char>+ \" }
}
ck(?QVal.parse(Q{"7"}, :rule<value>), True, 'the XML-style quoted-value token matches');

# 6. rule parameter defaults (plain + dynamic-var)
grammar PDef {
    token inner { <?{ $*S eq "x" }> \w+ }
    token value($*S = "x") { <inner> }
    token plain($p = "ok") { <?{ $p eq "ok" }> \w+ }
}
ck(?PDef.parse("abc", :rule<value>), True, '$*VAR param default binds and reaches callees');
ck(?PDef.parse("abc", :rule<plain>), True, 'plain param default binds via :rule entry');

# 7. @-attr accessor list-assign
class NodeBox {
    has @.n is rw;
    method kinds { @.n.map(*.^name).join(',') }
}
my @src = 1, 2;
my $box = NodeBox.new;
$box.n = @src;
ck($box.n.elems, 2, 'accessor assignment stores the elements');
ck($box.kinds, 'Int,Int', 'iteration sees the elements, not one Array');

# 8. ~= with an object RHS
class Stringy8 { method Str { "S!" } }
my $acc = "a";
$acc ~= Stringy8.new;
ck($acc, "aS!", '~= calls the user Str method');

# 9. big radix-list + big-int precision
my $b = buf8.new(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16);
ck(:256[$b.values], 1339673755198158349044581307228491536, ':256[16 bytes] is a 128-bit Int');
ck(:256[$b.values].fmt("%32.32x"), "0102030405060708090a0b0c0d0e0f10", '%32.32x zero-pads a big Int');

# 10. Match.join over captures
ck(("0102030405060708090a0b0c0d0e0f10" ~~ /(........)(....)(....)(....)(............)/).join("-"),
   "01020304-0506-0708-090a-0b0c0d0e0f10", 'Match.join joins the positional captures');

# 11. list-valued named capture in a subst callback
ck("a%20b".subst(/[\%$<bit>=[<[0..9A..Fa..f]>** 2]]+/, -> $m { $m<bit>.list.elems.Str }, :g),
   "a1b", 'quantified $<bit> arrives as a list in the callback');
ck("a%20b".subst(/[\%$<bit>=[<[0..9A..Fa..f]>** 2]]+/,
                 -> $m { Buf.new($m<bit>.list.map({:16($_.Str)})).decode }, :g),
   "a b", 'the URI::Encode decoder shape works end to end');

# 12. method trait dispatch + HOW mixin + compose (Method::Also in miniature)
my %aliases;
my role AliasHOW {
    method compose(Mu \o, |) {
        for %aliases{o.^name}[] { o.^add_method(.key, .value) if $_ }
        nextsame;
    }
}
multi sub trait_mod:<is>(Method:D \meth, :$also!) {
    $*PACKAGE.HOW does AliasHOW unless $*PACKAGE.HOW ~~ AliasHOW;
    %aliases{$*PACKAGE.^name}.push: Pair.new($also.Str, meth);
}
class P12 { method m() is also<mag> { 5 } }
ck(P12.new.mag, 5, 'is also<mag> installs the alias via HOW-mixin compose');
ck(P12.new.m, 5, 'the original method still answers');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
