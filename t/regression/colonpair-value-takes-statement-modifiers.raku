# A statement modifier inside a colonpair's value parentheses. The plain-paren
# path has always taken the whole chain — `(5 given 1)` parses — but a named
# argument's value stopped at the modifier keyword and reported "expected )".
# Data::Dump::Tree writes every title that way:
# `:title( S/(' ')$// given @element[0] ~ @element[1] )`.
use Test;
plan 4;

sub f(:$t)     { $t }
sub g(:$title) { $title }

is f(:t(5 given 1)),    5, 'given inside a colonpair value';
is f(:t(5 if 1)),       5, 'if inside a colonpair value';
is f(:t(5 if 0)).elems, 0, '…and a false one yields nothing';

my $str = "ab ";
is g(:title( S/(' ')$// given $str )), 'ab', 'the shape Data::Dump::Tree writes';
