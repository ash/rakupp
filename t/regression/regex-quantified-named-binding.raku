# Regression, 2026-10-09: a named binding is itself an atom, so a quantifier
# after the bound atom's own quantifies the binding. `$<w>=.*? +%% [ … ]`
# binds `.*?` and repeats the binding, each repetition one more capture in
# $<w>. The parse-time check read the `+` as a second quantifier on `.*?` and
# refused it ("Quantifier quantifies nothing"). Base64::Native 0.0.10's own
# t/readme.t uses that regex, so `rakupp install Base64::Native` (and
# BSON::Simple, which needs it) was refused. Expected values are Rakudo
# 2026.09's.
use Test;
plan 17;

# Base64::Native's README regex
my $s = "intro\n```\ncode1\n```\nmiddle\n```\ncode2\n```\n";
my $m = $s ~~ /^ $<waffle>=.*? +%% ["```" \n? $<code>=.*? "```" \n?] $/;
ok $m, 'the README regex matches';
is $<waffle>.elems, 3, '…with three waffle captures';
is $<code>.map(*.Str.trim).join('|'), 'code1|code2', '…and two code captures';

sub cap($m) { $m ?? ($m<x> ~~ Positional ?? $m<x>.map(~*).join('|') !! ~$m<x>) !! 'no match' }
is cap("aaXaaaXa" ~~ /^ $<x>=a+ +% X $/), 'aa|aaa|a', 'a+ bound, the binding repeated';
is cap("1 22 333" ~~ /^ $<x>=\d+ +% ' ' $/), '1|22|333', 'an escape';
is cap("foo,bar," ~~ /^ $<x>=\w+ +%% ',' $/), 'foo|bar', 'with %%';
is cap("xyz" ~~ /^ $<x>=. + $/), 'xyz', 'an unquantified atom takes the + itself';
is cap("abXab" ~~ /^ $<x>=[ab] +% X $/), 'abXab', '…a group too';

# what compiles and what the parser still refuses
sub compiles($src) { (try EVAL "my \$re = rx$src; 1") ?? True !! False }
ok compiles(Q{/$<x>=a+ +/}), 'a quantified binding';
ok compiles(Q{/$<x>=[ab]+ +/}), '…of a group';
ok compiles(Q{/$<x>=\w+? +/}), '…of a frugal escape';
ok compiles(Q{/@<x>=\d+ +/}), '…an array binding';
ok compiles(Q{/$<x>='ab'* +/}), '…of a quoted literal';
ok compiles(Q{/$<x>=<alpha>+ +/}), '…of a subrule';
nok compiles(Q{/a+ +/}), 'a second quantifier on a plain atom is refused';
nok compiles(Q{/$<x>=a+ b+ +/}), '…and on the atom after a binding';
nok compiles(Q{/$<x>=.*? + +/}), '…and a third one on a binding';
