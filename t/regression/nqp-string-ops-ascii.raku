# Regression: the nqp string ops take an ASCII fast path that skips the UTF-8
# decode, because they are what a tokenizer written in Raku calls once per
# character — always handing over the WHOLE text. Decoding it per call made an
# O(n) scan cost O(n^2): JSON::Fast spent 1.9 s on one 68 KB META6.json.
#
# The fast path is only correct while a byte index and a codepoint index agree,
# so what has to be pinned is the hand-off: every op must give the same answer
# on ASCII, on non-ASCII, on a string that turns non-ASCII partway through, and
# at offsets past the end.
use nqp;

my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $ascii = 'abc';
my $lead  = "\c[LATIN SMALL LETTER A WITH RING ABOVE]bc";   # non-ASCII first
my $tail  = "ab\c[LATIN SMALL LETTER A WITH RING ABOVE]";   # non-ASCII last
my $wide  = "aaa\c[GRINNING FACE]bbb";                      # 4-byte codepoint
my $mark  = "\c[COMBINING ACUTE ACCENT]x";                  # combining mark

# ordat — the hottest of them; -1 for anything out of range, either sign.
ck((^3).map({ nqp::ordat($ascii, $_) }).List, (97, 98, 99),   'ordat over ASCII');
ck((^3).map({ nqp::ordat($lead,  $_) }).List, (229, 98, 99),  'ordat past a leading non-ASCII');
ck((^3).map({ nqp::ordat($tail,  $_) }).List, (97, 98, 229),  'ordat up to a trailing non-ASCII');
ck((^7).map({ nqp::ordat($wide,  $_) }).List, (97, 97, 97, 128512, 98, 98, 98), 'ordat across a 4-byte codepoint');
ck(nqp::ordat($ascii, 3), -1,  'ordat past the end of an ASCII string');
ck(nqp::ordat($wide, 7),  -1,  'ordat past the end (byte length exceeds char length)');
ck(nqp::ordat($ascii, -1), -1, 'ordat at a negative index');
ck(nqp::ordat('', 0), -1,      'ordat on the empty string');

# chars — a byte count is only the answer while every byte is ASCII.
ck(nqp::chars($ascii), 3, 'chars of ASCII');
ck(nqp::chars($wide),  7, 'chars counts a 4-byte codepoint once');
ck(nqp::chars($mark),  2, 'chars counts a combining mark separately');
ck(nqp::chars(''),     0, 'chars of the empty string');

# eqat — the offset is in characters, so a non-ASCII prefix shifts it.
ck(nqp::eqat($ascii, 'bc', 1), 1, 'eqat matches at an ASCII offset');
ck(nqp::eqat($ascii, 'bc', 0), 0, 'eqat rejects a wrong offset');
ck(nqp::eqat($lead,  'bc', 1), 1, 'eqat offset counts characters, not bytes');
ck(nqp::eqat($wide,  'bbb', 4), 1, 'eqat offset past a 4-byte codepoint');
ck(nqp::eqat($ascii, 'abcd', 0), 0, 'eqat with a needle longer than the haystack');
ck(nqp::eqat($ascii, '', 3), 1,   'eqat with an empty needle at the end');
ck(nqp::eqat($ascii, 'a', 5), 0,  'eqat at an offset past the end');

# index — byte search is valid only when both sides are ASCII.
ck(nqp::index($ascii, 'bc'),   1, 'index finds an ASCII needle');
ck(nqp::index($ascii, 'zz'),  -1, 'index reports a miss');
ck(nqp::index($tail, "\c[LATIN SMALL LETTER A WITH RING ABOVE]"), 2, 'index returns a character offset');
ck(nqp::index($wide, 'bbb'),   4, 'index counts a 4-byte codepoint as one');
ck(nqp::index($ascii, 'a', 1), -1, 'index honours its start offset');
ck(nqp::index($ascii, ''),     0, 'index of an empty needle');

# substr — offset AND length are in characters.
ck(nqp::substr($ascii, 1),    'bc',  'substr from an ASCII offset');
ck(nqp::substr($ascii, 1, 1), 'b',   'substr with a length');
ck(nqp::substr($lead, 1),     'bc',  'substr skips one non-ASCII character');
ck(nqp::substr($wide, 3, 1),  "\c[GRINNING FACE]", 'substr cuts a 4-byte codepoint whole');
ck(nqp::substr($ascii, 3),    '',    'substr at the end');
ck(nqp::substr($ascii, 0, 99), 'abc', 'substr with an over-long length clamps');

# iscclass / findnotcclass — the index is a character index here too.
ck(nqp::iscclass(nqp::const::CCLASS_ALPHABETIC, $ascii, 0), 1, 'iscclass on ASCII');
ck(nqp::iscclass(nqp::const::CCLASS_ALPHABETIC, $lead,  0), 1, 'iscclass on a non-ASCII letter');
ck(nqp::iscclass(nqp::const::CCLASS_ALPHABETIC, $wide,  3), 0, 'iscclass on an emoji');
ck(nqp::iscclass(nqp::const::CCLASS_ALPHABETIC, $ascii, 3), 0, 'iscclass past the end');
ck(nqp::iscclass(nqp::const::CCLASS_NUMERIC, '7', 0), 1,      'iscclass numeric');
ck(nqp::findnotcclass(nqp::const::CCLASS_ALPHABETIC, $ascii, 0, 3), 3, 'findnotcclass runs off an all-alpha ASCII string');
ck(nqp::findnotcclass(nqp::const::CCLASS_ALPHABETIC, $wide,  0, 7), 3, 'findnotcclass stops at the emoji');
ck(nqp::findnotcclass(nqp::const::CCLASS_ALPHABETIC, 'ab1c', 0, 4), 2, 'findnotcclass finds the digit');

# The shape that made this worth fixing: a whole-string scan, one op per
# character. Quadratic decoding showed up here as seconds, not milliseconds.
my $doc = 'x' x 20000;
my $pos = 0;
$pos++ while $pos < nqp::chars($doc) && nqp::ordat($doc, $pos) == 120;
ck($pos, 20000, 'a per-character scan of a 20k string completes');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
