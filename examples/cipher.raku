#!/usr/bin/env raku
# Three classic substitution ciphers, and a proof that each one round-trips:
# encrypting a message and then decrypting it returns the original text.
#
# ROT13 and the Caesar shift are one-liners with `.trans`, which maps ranges
# of characters onto other ranges. The Vigenere cipher needs a running key,
# so it drops down to per-character modular arithmetic with `.ord`/`.chr`.

my $message = 'Attack at Dawn!';

# ROT13: rotate each letter halfway round the alphabet. It is its own inverse,
# so the same transform both hides and reveals the text.
sub rot13($s) { $s.trans('a..zA..Z' => 'n..za..mN..ZA..M') }

# Caesar shift by N: build the shifted alphabets and map onto them. Decrypting
# is just a shift by 26 - N (equivalently, -N modulo 26).
sub caesar($s, $n) {
    my $lo = ('a' .. 'z').join;
    my $up = ('A' .. 'Z').join;
    my $rl = $lo.comb.rotate($n).join;
    my $ru = $up.comb.rotate($n).join;
    $s.trans("$lo$up" => "$rl$ru");
}

# Vigenere: each letter is shifted by the next letter of a repeating key. The
# key index only advances on letters, so spaces and punctuation pass through.
sub vigenere($text, $key, $dir) {
    my @k = $key.uc.comb.map({ .ord - 65 });
    my $ki = 0;
    my $out = '';
    for $text.comb -> $c {
        if $c ~~ /<[A..Za..z]>/ {
            my $base  = $c ~~ /<[A..Z]>/ ?? 65 !! 97;
            my $shift = @k[$ki % @k.elems] * $dir;
            $out ~= (($c.ord - $base + $shift) % 26 + $base).chr;
            $ki++;
        }
        else {
            $out ~= $c;
        }
    }
    $out;
}

say "message: $message";
say '';

my $r = rot13($message);
say 'ROT13';
say "  encrypt: $r";
say "  decrypt: {rot13($r)}";
say "  round-trips: {rot13($r) eq $message}";
say '';

my $c = caesar($message, 3);
say 'Caesar (shift 3)';
say "  encrypt: $c";
say "  decrypt: {caesar($c, -3)}";
say "  round-trips: {caesar($c, -3) eq $message}";
say '';

my $v = vigenere($message, 'LEMON', 1);
say 'Vigenere (key LEMON)';
say "  encrypt: $v";
say "  decrypt: {vigenere($v, 'LEMON', -1)}";
say "  round-trips: {vigenere($v, 'LEMON', -1) eq $message}";
