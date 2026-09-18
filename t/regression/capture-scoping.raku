# Regression: every CAPTURE is a capture scope. `( … )` and `$<x>=( … )` hold
# what they matched — their own positional captures numbered from 0 again, and
# the names matched inside them — instead of everything being flattened onto the
# whole match. `"ab" ~~ /(a(b))/` answered `$0[0]` as Nil, `$<f>=((a)(a))` put
# all three groups in the top-level list and left `$<f>.list` empty, and `$0`
# used as a BACKREFERENCE inside a group read the group itself rather than the
# capture beside it.
#
# This file exists because the Roast files that specify the shape —
# S05-capture/dot.t, array-alias.t, subrule.t — are PARTIAL passes, so the
# release gate (the fully-passing file list) cannot see them move. The work also
# came within one t/run.raku check of silently breaking a rule it did not touch:
# capture numbering restarts in every ALTERNATIVE too, which is pinned in
# regex-alt-captures-and-cursor.raku and repeated here for the nested case.
#
# Every expectation below is the Rakudo 2026.08 answer, byte for byte.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- a positional capture nests inside a positional capture -----------------
ok("ab" ~~ /(a(b))/);
check(~$0,    "ab", 'the outer capture is its whole span');
check(~$0[0], "b",  'and the inner one is reached through it');

ok("ab" ~~ /((a)(b))/);
check($/.list.elems, 1,   'a nested capture takes no number of its own');
check(~$0[0],        "a", "the inner captures number from 0 inside their scope");
check(~$0[1],        "b", '…and on');

# --- a NAME takes the parens' capture, so the parens take no number ----------
ok("ab" ~~ /$<x>=(a) (b)/);
check($/.list.elems, 1,   '`$<x>=(a)` leaves the numbering to the next capture');
check(~$0,           "b", 'so `(b)` is $0');
check(~$<x>,         "a", 'and the named capture answers to its name');

ok("aaaa" ~~ m/$<f>=((a)(a))/);
check($/.list.elems, 0,   'a named capture publishes nothing positionally');
check(~$<f>[0],      "a", "its parens' contents are ITS captures");
check(~$<f>[1],      "a", '…and on');

# `$<x>=[ … ]` only GROUPS — it captures nothing, so what it holds stays where
# it was written (this is the one form that does not scope).
ok("a" ~~ /$<x>=[ (a) ]/);
check($/.list.elems, 1,   'the bracket form does not scope');
check(~$0,           "a", '…so the group inside it is the match\'s own $0');

# --- names scope into a POSITIONAL capture just as they do into a named one --
ok("a" ~~ /( $<n>=(a) )/);
check(~$0<n>,        "a",   'a name matched inside a group belongs to the group');
check($/<n>.defined, False, '…and not to the whole match');

grammar G { token TOP { ( <word> ) '!' }; token word { \w+ } }
my $g = G.parse("hi!");
check(~$g[0]<word>,    "hi",  'a SUBRULE capture scopes into its group too');
check($g<word>.defined, False, '…and is not published at the top');

# --- a quantified capture: each occurrence carries what IT matched -----------
ok("aa" ~~ /((a)+)/);
check($0[0].elems, 2,   'the quantified capture inside is a list of occurrences');
check(~$0[0][1],   "a", '…each one its own Match');

ok("abab" ~~ /( (a) (b) )+/);
check($0.elems,  2,   'the OUTER capture collates the occurrences');
check(~$0[1][0], "a", 'and each occurrence holds its own captures');
check(~$0[1][1], "b", '…not a list collated across all of them');

# --- `$0` as a backreference is this scope's $0 ------------------------------
ok("aab" ~~ / ( (a) $0 b ) /);
check(~$/, "aab", '`$0` inside a group is the capture beside it, not the group');

# --- …and numbering still restarts in every alternative ----------------------
ok("xy" ~~ / (a)(b) | (x)(y) /);
check($/.list.map(*.Str).join(","), "x,y", "a branch's groups are \$0 and \$1 too");

# --- the sigilled capture forms read the capture's list / hash ---------------
ok("aaaa" ~~ m/$<fee>=a $<fie>=((a)(a)) $<foe>=($<fum>=(a))/);
check(@<fie>.map(*.Str).join(","), "a,a", '`@<x>` is the capture\'s positional list');
check(@<fee>.elems,                0,     '…and empty when it captured nothing');
check(%<foe>.keys.sort.join(","),  "fum", '`%<x>` is the capture\'s named captures');
check(~%<foe><fum>,                "a",   '…with the sub-match as the value');

# every Match reports the WHOLE subject as .orig, captures included
check($<fee>.orig, "aaaa", 'a capture shares the subject as its .orig');

sub ok($cond) { @fail.push("a match that must succeed did not") unless $cond }

note @fail.join("\n") if @fail;
say @fail ?? "FAIL" !! "PASS";
exit @fail ?? 1 !! 0;
