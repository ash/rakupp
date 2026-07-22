# BROKE: redispatch floor (eb7e875) — of five redispatch-frame push sites,
# four got the new ownFrame marker; the grammar parse/subparse user-override
# path did not, so its pre-pushed frame (next = built-in parse) sat below the
# invoked method's floor: nextwith saw an exhausted chain and returned Nil.
# Every YAMLish Grammar.parse silently failed (user-reported via
# raku-course/raku-pages.raku, not caught by roast).
# FIXED: f2ce475 (ownFrame=true at that invokeMethod).
grammar G {
    token TOP { \w+ }
    method parse($s, *%args) { nextwith($s, |%args) }
    method subparse($s, *%args) { nextwith($s, |%args) }
}
die 'parse override + nextwith lost the built-in target' unless so G.parse("abc");
die 'parse result should be a match of the whole string' unless ~G.parse("abc") eq 'abc';
die 'subparse override + nextwith broken' unless so G.subparse("abc");
say 'PASS';
