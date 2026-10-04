# Regression: language behaviour from the second batch of audited mutsu gap
# files (2026-10-04).
#
# - `.self` decontainerizes; an Array is its own `.cache`.
# - `my $e = enum Ea <a b>` is valued as the enum's Map, as an anonymous one is.
# - `\qw[…]` / `\qqw[…]` inside a q string split into words; Q stays raw.
# - A CRLF grapheme can begin a `<-[\r]>` match; `(1..*).join` is `...`, and
#   routine join stops at a lazy argument; `[0, |(1...*)]` is a lazy Array.
# - Match.hash and `%($/)` are Maps, `.Hash` a Hash.
# - A `Z` timestamp takes :timezone; `$?NL` follows `use newline`, and a
#   written `\r\n` stays CRLF under it.
# - `(1 + 2)++` is X::Multi::NoMatch; a proto beside a plain sub of the name,
#   and a variable bound twice across nested sub-signatures, are redeclarations.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub err(&code) { CATCH { default { return $_ } }; code(); 'lived' }

my %h = a => 1;
my $h = %h;
ck($h.self.raku, '{:a(1)}', '.self decontainerizes');
my @a = 1, 2;
ck(@a.cache === @a, True, 'an Array is its own .cache');
my $e = enum Ea <ea-one ea-two>;
ck(($e.^name, $e<ea-two>), ('Map', 1), 'a named enum declaration is valued as its Map');
my $animal = 'quaggas';
ck('these are \qqw[$animal or  zebras]', 'these are quaggas or zebras', 'an embedded qqw escape in single quotes');
ck(q{a \qw[one   two] b}, 'a one two b', 'an embedded qw escape in q braces');
ck(Q{raw \qw[a b]}.contains(Q{\qw}), True, 'Q leaves the escape raw');
ck(so("\r\n" ~~ /^<-[\r]>$/), True, 'a CRLF can begin a <-[\r]> match');
ck((1..*).join(','), '...', 'joining an endless range');
ck(join(',', 1, (lazy 2, 3)), '1,...', 'routine join stops at a lazy argument');
ck([0, |(1...*)][4], 4, 'an Array with a slipped endless tail');
ck([0, |(1..*)].is-lazy, True, '... is lazy');
my $m = 'ab' ~~ /$<x>=(.)/;
ck(($m.hash.^name, $m.Hash.^name, %($m).^name), ('Map', 'Hash', 'Map'), 'Match.hash / .Hash / %()');
ck(DateTime.new('2020-01-01T00:00:00Z', :timezone(3600)).Str, '2020-01-01T00:00:00+01:00', 'Z with :timezone');
ck($?NL, "\n", '$?NL defaults to LF');
{
    use newline :crlf;
    ck($?NL, "\r\n", 'use newline :crlf sets $?NL');
    ck("\r\n".chars, 1, 'a written CR LF pair stays one CRLF');
}
ck(err({ (1 + 2)++ }).^name, 'X::Multi::NoMatch', '(1 + 2)++ has no container');
ck((try { EVAL 'proto f8($) {*}; sub f8($x) { 1 }'; 'ok' } // $!.message.contains('Redeclaration')), True,
   'proto beside a plain sub');
ck((try { EVAL 'sub dup((:key(($a)), :value(($a)))) { }'; 'ok' } // $!.message.contains('Redeclaration')), True,
   'a variable bound twice across nested sub-signatures');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
