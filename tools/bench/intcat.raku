# Build strings by appending Ints — how output, reports and documents get built
# piece by piece, in Raku as in Perl. Two shapes: the one-line `$s ~= $_ for
# ^$n` (issue #130, where every append copied the whole string — 40,000 took
# 0.7 s compiled), and a loop body appending a number and a separator. Its Perl
# 5 twin (intcat.pl) is line-for-line the same program, because building a
# string is the comparison Perl is usually invoked to win.
my $s = '';
$s ~= $_ for ^1_000_000;
my $t = '';
for ^1_000_000 -> $i { $t ~= $i; $t ~= ',' }
say $s.chars, ' ', $t.chars;
