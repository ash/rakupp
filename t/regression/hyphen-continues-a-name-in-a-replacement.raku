# A hyphen or apostrophe continues an identifier when a letter or digit
# follows, everywhere in Raku — including the replacement half of s///.
# Stopping at the hyphen interpolated an undeclared `$commit` (the empty
# string) and left `-sep` as literal text, so Git::Log's record separator
# became the four characters `-sep` and two commits came back glued into one.
use Test;
plan 5;

my $commit-sep = '|';
my $t = "a|\nb|\nc";
$t ~~ s:g/$commit-sep\n/$commit-sep/;
is $t, 'a|b|c', 'a hyphenated name interpolates in the replacement';

my $x = 'ab';
my $one-two = 'Z';
$x ~~ s/b/$one-two/;
is $x, 'aZ', '…in the simple case too';

my @one-two = 'p', 'q';
my $y = 'ab';
$y ~~ s/b/@one-two[1]/;
is $y, 'aq', 'an array with a hyphenated name too';

# a plain name is unchanged, and a trailing hyphen is still literal
my $plain = 'Z';
my $z = 'ab';
$z ~~ s/b/$plain/;
is $z, 'aZ', 'a plain name is unchanged';

my $w = 'ab';
$w ~~ s/b/$plain-/;
is $w, 'aZ-', 'a trailing hyphen stays literal';
