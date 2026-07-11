#!/usr/bin/env raku
# Roman numerals in both directions: integer to numeral and back again,
# round-tripping a set of numbers so that both conversions must agree.
#
# The forward direction is table-driven: a list of (value, symbol) pairs in
# descending order, including the subtractive forms (CM, XL, IV, ...), is
# greedily subtracted from the number. The reverse walks the letters and uses
# a `given`/`when` to decide whether each one adds or subtracts.

# Ordered largest-first so the greedy loop always takes the biggest chunk.
my @table =
    1000 => 'M', 900 => 'CM', 500 => 'D', 400 => 'CD',
     100 => 'C',  90 => 'XC',  50 => 'L',  40 => 'XL',
      10 => 'X',   9 => 'IX',   5 => 'V',   4 => 'IV',
       1 => 'I';

sub to-roman($n is copy) {
    my $out = '';
    for @table -> $pair {
        while $n >= $pair.key {
            $out ~= $pair.value;
            $n   -= $pair.key;
        }
    }
    $out;
}

sub value-of($letter) {
    given $letter {
        when 'M' { 1000 }
        when 'D' {  500 }
        when 'C' {  100 }
        when 'L' {   50 }
        when 'X' {   10 }
        when 'V' {    5 }
        when 'I' {    1 }
    }
}

# A smaller letter placed before a larger one is subtracted (IV = 5 - 1),
# otherwise it is added. One left-to-right pass is enough.
sub from-roman($s) {
    my @letters = $s.comb;
    my $total   = 0;
    for ^@letters.elems -> $i {
        my $here = value-of(@letters[$i]);
        if $i + 1 < @letters.elems && $here < value-of(@letters[$i + 1]) {
            $total -= $here;
        }
        else {
            $total += $here;
        }
    }
    $total;
}

my @numbers = 4, 9, 14, 40, 90, 444, 1984, 2026, 3888;

say '  n     roman              back   ok';
say '  ----  -----------------  -----  --';
for @numbers -> $n {
    my $roman = to-roman($n);
    my $back  = from-roman($roman);
    say sprintf('  %4d  %-17s  %4d   %s', $n, $roman, $back, $n == $back);
}
