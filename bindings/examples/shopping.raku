# The example grammar every binding guide uses: a shopping list of
# `name=quantity` pairs. TOP and item are `rule`s, not tokens, so they match
# :sigspace — every space in the pattern matches `<.ws>` instead of itself.
# That is what makes the list free-form: items may be one per line or several
# to a line, and the `=` may be surrounded by spaces or by nothing at all.
# The leaves stay `token`s, because a name and a quantity have no whitespace
# inside them. The grammar and its actions class live in this one .raku file;
# each host names them ('Shopping', 'ShoppingActions') when it compiles the
# file. The actions run INSIDE the parse, in Raku — the host reads what they
# computed through `.made`.

grammar Shopping {
    rule  TOP  { <item>+ }
    rule  item { <name> '=' <qty> }
    token name { \w+ }
    token qty  { \d+ }
}

class ShoppingActions {
    method item($/) { make $<qty>.Int }
    method TOP($/)  { make $<item>.map(*.made).sum }
}
