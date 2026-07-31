# Regression (issue #15): a variable interpolated into a regex INSIDE ANGLE
# BRACKETS is compiled as a REGEX; a bare one matches its text LITERALLY.
# rakupp quote-escaped both, so `/<$p>/` with $p = "^bar.?" matched the empty
# string and `/<@arr>/` matched a single "b" instead of "barc".
#
#   /<$p>/    → the string is a pattern      /$p/    → the string is literal
#   /<@arr>/  → each element is a pattern    /@arr/  → literal alternation
#
# (NOT asserted: captures inside an interpolated pattern. Rakudo does not
# expose them as $0 of the OUTER regex; rakupp does. Both match the same text,
# so this is a numbering difference, tracked separately.)
# Contract: exit 0 + last line PASS.
my @fail;

# the issue's two cases, verbatim
my $p = "^bar.?";
@fail.push('<$p> as regex')  unless ("barc" ~~ /<$p>/).Str eq 'barc';
my @arr = ("foo", "^bar.?");
@fail.push('<@arr> as regex') unless ("barc" ~~ /<@arr>/).Str eq 'barc';

# the bare forms stay LITERAL — a metacharacter must not gain meaning
my $lit = "a.c";
@fail.push('bare $p literal match')  unless ("a.c" ~~ /$lit/).Str eq 'a.c';
@fail.push('bare $p is not a regex') if     "abc" ~~ /$lit/;
my @la = ("a.c", "x");
@fail.push('bare @a literal match')  unless ("a.c" ~~ /@la/).Str eq 'a.c';
@fail.push('bare @a is not a regex') if     "abc" ~~ /@la/;

# an interpolated pattern is GROUPED: its alternation cannot swallow what
# follows, and a quantifier applies to the whole thing
my $alt = "a|b";
@fail.push('grouped alternation') unless ("xb" ~~ /x<$alt>/).Str eq 'xb';
my $ab = "ab";
@fail.push('quantified group')    unless ("abab" ~~ /[<$ab>]+/).Str eq 'abab';

# <@arr> keeps the longest-first ordering the bare form has
my @lf = ("b", "bc");
@fail.push('longest first') unless ("bcd" ~~ /<@lf>/).Str eq 'bc';

# a named assertion is still a rule call, not a variable
grammar G { token TOP { <w> }; token w { \w+ } }
@fail.push('named rule intact') unless G.parse('hi');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
