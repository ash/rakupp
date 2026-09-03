# Regression: two general grammar/regex engine bugs found under CSS::Grammar.
# Rakudo 2026.08 is the oracle for both.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

# --- an escaped punctuation char as a RANGE ENDPOINT in a character class ------
# `<[ \]..\~ ]>` is the range `]`..`~`. The class parser pushed `\]` as a single
# literal and never looked for a following `..`, so the range collapsed to its
# two endpoints plus a stray `.` — CSS::Grammar's stringchar-regular class
# (`<[ \x20 \! \# \$ \% \& \(..\[ \]..\~ ]>`) then matched no ordinary letter.
ok("h" ~~ /^<[ \]..\~ ]>$/, 'an escaped-bracket range start matches a letter');
ok("h" ~~ /^<[ \)..\~ ]>$/, 'an escaped-paren range start matches too');
ok(!("\c[1]" ~~ /^<[ \]..\~ ]>$/), '…and the range still excludes what it should');
ok("hi there!" ~~ /^<[ \x20 \! \# \(..\[ \]..\~ ]>+$/,
   'the full CSS stringchar-regular class matches ordinary text');
ok(("a" ~~ /^<[ \[..\] ]>$/) === Nil, 'a\'s outside `[`..`]` do not match');
ok("\\" ~~ /^<[ \[..\] ]>$/, '…and `\\` (0x5C, inside `[`..`]`) does');

# --- a proto used as the parse ENTRY POINT fires its candidate ONCE ------------
# `.parse(:rule<string>)` where `string` is a proto records the winning candidate
# in the node and the proto in its name; build fired BOTH the candidate action and
# the proto's `{*}` method — and an explicit `proto method string {*}` invoked
# directly with just `$/` dies "No matching multi candidate". CSS::Grammar parses
# `string` as its entry rule.
grammar G {
    proto token stringchar {*}
    token stringchar:sym<esc>   { '\\' \w }
    token stringchar:sym<ascii> { <[a..z]>+ }
    proto token string {*}
    token string:sym<sq> { \' [ <stringchar> ]* \' }
    token string:sym<dq> { \" [ <stringchar> ]* \" }
}
class A {
    method stringchar:sym<esc>($/)   { make "E" }
    method stringchar:sym<ascii>($/) { make ~$/ }
    proto method string {*}
    method string:sym<sq>($/) { make [~] $<stringchar>>>.ast }
    method string:sym<dq>($/) { make [~] $<stringchar>>>.ast }
}
ok(G.parse("'hi'", :rule<string>, :actions(A.new)).ast eq 'hi',
   'a proto entry rule fires its candidate action, not the bare proto');
ok(G.parse('"ab"', :rule<string>, :actions(A.new)).ast eq 'ab',
   '…for each candidate');

say $fails ?? "FAIL ($fails)" !! "PASS";
