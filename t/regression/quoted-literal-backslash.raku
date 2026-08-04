# Regression: a single-quoted literal inside a regex keeps its backslashes,
# 2026-08-04. Found taking JSON::Tiny through its own suite, where
# `<utf16_codepoint>+ % '\u'` never saw its separator, so each half of a UTF-16
# surrogate pair was decoded on its own and an astral character came out as
# replacement bytes. Every expectation was checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

my $bs-n = 'a' ~ '\\' ~ 'n' ~ 'b';    # a, backslash, n, b
my $nl   = "a\nb";                     # a, newline, b

# Single quotes: the ONLY escapes are \\ and \' — `'\n'` is backslash-then-n.
ck(?($bs-n ~~ / 'a' '\n' 'b' /), True,  "'\\n' matches a literal backslash-n");
ck(?($nl   ~~ / 'a' '\n' 'b' /), False, "…and NOT a newline");
ck(?('a\\b' ~~ / 'a' '\\' 'b' /), True, "'\\\\' is one backslash");
ck(?("a'b"  ~~ / 'a' '\'' 'b' /), True, "'\\'' is a quote");

# Double quotes keep the usual escapes.
ck(?($nl   ~~ / 'a' "\n" 'b' /), True,  '"\\n" is a real newline');
ck(?($bs-n ~~ / 'a' "\n" 'b' /), False, '…and not a backslash-n');

# The shape that found it: a separated quantifier whose separator is `'\u'`.
{
    grammar G {
        token TOP { 'u' <hex>+ % '\u' }
        token hex { <xdigit> ** 4 }
    }
    my $m = G.parse('uD835\uDCB7');
    ck(?$m, True, 'the separator matches inside a grammar token');
    ck(($m ?? $m<hex>.elems !! 0), 2, 'and both halves land in ONE match');
    # …which is what lets the pair decode as one astral codepoint
    ck(utf16.new(0xD835, 0xDCB7).decode().ords.List, (119991,), 'the surrogate pair decodes');
}

say $ok ?? 'PASS' !! 'FAIL';
