# Regression: the Cro-family cluster — eight fixes that make Cro::Core parse
# URIs byte-identically and Cro::HTTP build/serialize messages.
#  1. `|%hash` in a LIST slips its pairs (Cro: `%parts = scheme => …, |$<hier-part>.ast`).
#  2. `self.bless(...)` / `$obj.new(...)` on an INSTANCE builds a fresh object.
#  3. `<alias=.rule>` — the dot suppresses only the rule-name capture; the alias
#     still captures (Cro::MediaType `<attribute=.token>`).
#  4. A positional capture under a quantifier in a GRAMMAR rule is an Array of
#     every occurrence in the action's $/ (`(...)+` → @$0) — Cro::Uri pchars.
#  5. %( $hash, pair ) merges the hash (was: stringified it into a KEY).
#  6. A sub in a class body is callable from methods (was parsed-and-discarded).
#  7. subset traits before where: `subset X of Str is export where /…/`.
#  8. Private-method colon listop: `self!setup: { }, :$enc`; and `\x21..\xFF`
#     hex-escape RANGES in char classes (Cro::HTTP header validation).
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 1. hash slip in a list
my %inner = host => 'h', port => 9;
my @l = 'x', |%inner;
ck(@l.elems, 3, '|%hash slips pairs into a list');
my %parts = scheme => 's', |%inner;
ck(%parts<scheme host port>.join(','), 's,h,9', 'slipped pairs land in a hash assign');

# 2. instance bless
class B1 { has $.x; method mk($v) { self.bless(x => $v) } }
ck(B1.new(x => 1).mk(9).x, 9, 'self.bless on an instance');

# 3. <alias=.rule>
grammar GA { token TOP { <p> }; token p { <x=.tok> '=' \w+ }; token tok { \w+ } }
ck(GA.parse('a=b')<p><x>.Str, 'a', '<alias=.rule> captures under the alias');

# 4. quantified capture list in grammar actions
grammar GQ { token TOP { <pc> }; token pc { (<[a..z]>+ | '%' <[0..9A..F]>**2 | $<broken>=<[!]>)+ } }
class AQ { method pc($/) { my $r = ''; $r ~= ~$_ for @$0; make $r } }
ck(GQ.parse('p%20q', :actions(AQ))<pc>.ast, 'p%20q', 'grammar @$0 lists every iteration');

# 5. %( hash, pair ) merge
my %m = %( %inner, path => '/z' );
ck(%m<host port path>.join(','), 'h,9,/z', '%( $hash, pair ) merges');

# 6. class-body sub
class B2 {
    sub helper($v) { $v * 2 }
    method go($x) { helper($x) }
}
ck(B2.new.go(21), 42, 'class-body sub callable from a method');

# 7. subset with traits before where
my subset SN of Str is export where /^ab/;
my SN $sv = "abc";
ck($sv, 'abc', 'subset ... is export where ... parses and gates');

# 8a. private-method colon listop (incl. leading block arg)
class B3 {
    method !setup($cb, :$enc) { $cb() ~ "-$enc" }
    method go() { self!setup: { "blk" }, :enc<u8> }
}
ck(B3.new.go, 'blk-u8', 'self!method: { block }, :named');
# 8b. hex-escape ranges in char classes
ck(('example.com' ~~ /^ <[\x21..\xFF]>+ $/).Bool, True, 'hex range \x21..\xFF');
ck((' ' ~~ /<[\x21..\x7E]>/).Bool, False, 'space outside \x21..\x7E');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
