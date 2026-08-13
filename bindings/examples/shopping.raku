# The example grammar every binding guide uses: a shopping list, one
# `name=quantity` per line. The grammar and its actions class live in this
# one .raku file; each host names them ('Shopping', 'ShoppingActions') when
# it compiles the file. The actions run INSIDE the parse, in Raku — the host
# reads what they computed through `.made`.

grammar Shopping {
    token TOP  { <item>+ }
    token item { <name> '=' <qty> "\n" }
    token name { \w+ }
    token qty  { \d+ }
}

class ShoppingActions {
    method item($/) { make $<qty>.Int }
    method TOP($/)  { make $<item>.map(*.made).sum }
}
