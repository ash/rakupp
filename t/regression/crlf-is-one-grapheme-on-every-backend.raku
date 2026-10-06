# CR LF is ONE grapheme (UAX #29 GB3): "a\r\nb".chars is 3. The --target=js
# runtime's fast paths took "ASCII" to mean "one UTF-16 unit per grapheme",
# so under node .chars, .comb, .flip, .substr and .index all split the pair
# (Unicode's GraphemeBreakTest failed on `÷ 000D × 000A ÷` alone). This file
# runs in t/js's corpus too, where the interpreter is the oracle.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
my $s = "a\r\nb";

check $s.chars,                 3,           '.chars';
check $s.comb.elems,            3,           '.comb';
check $s.flip.ords.join(','),   '98,13,10,97', '.flip keeps the pair in order';
check $s.substr(1, 1).ords.join(','), '13,10', '.substr takes the pair whole';
check $s.substr(2),             'b',         '…and indexes past it';
check $s.index('b'),            2,           '.index';
check ($s ~~ /b/).from,         2,           'a match position';
check "\r\n".chars,             1,           'the pair alone';
check "\r\r\n".chars,           2,           'only CR LF pairs, not CR CR';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
