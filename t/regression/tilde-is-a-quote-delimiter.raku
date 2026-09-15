# `~` is a legal delimiter for every quote form, and code that quotes text full
# of slashes reaches for it: Apache::LogFormat::Compiler builds its formatter
# with `q~…~`, and Data::Dump::Tree renames accessors with `s~^(.).~$0.~`. The
# lexer took `/ | !` for bare forms and nothing else, so both distributions died
# at their first such line — "Missing required term after infix" for the quote,
# "unexpected operator in term position" for the substitution.
use Test;
plan 6;

my $q = q~tilde quoted~;
is $q, 'tilde quoted',                  'q~…~ is a quote, not an infix';

my $name = "hello";
is qq~say $name~, 'say hello',          'qq~…~ interpolates';

my $s = "abc";
$s ~~ s~b~X~;
is $s, 'aXc',                           's~…~…~ substitutes';

my $t = "one two";
is (S~two~three~ given $t), 'one three', 'S~…~…~ answers the new string';

ok "abc" ~~ m~b~,                       'm~…~ matches';
is Q~no $interpolation here~, 'no $interpolation here', 'Q~…~ quotes literally';
