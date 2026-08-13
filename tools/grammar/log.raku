# The gate grammar (GRAMMAR-PLAN G0): an access-log line format, the same
# shape as the plan's measurements. Parsed identically by the Raku driver and
# every host binding — this file IS the deliverable's format: the grammar
# stays a .raku file, and a host names the types it wants ('Log',
# 'LogActions') when it compiles it.
#
# \x22 rather than a literal quote inside the char class: a `"` inside
# <-[...]> is a known parser trap (docs/dev/findings — quirks from the spec
# generator).

grammar Log {
    token TOP    { <line>+ }
    token line   { <ip> ' - - [' <ts> '] "' <req> '" ' <status> ' ' <size> "\n" }
    token ip     { \d+ '.' \d+ '.' \d+ '.' \d+ }
    token ts     { <-[ \] ]>+ }
    token req    { <-[ \x22 ]>+ }
    token status { \d+ }
    token size   { \d+ }
}

class LogActions {
    # ($/,)».Int.sum == $/.Int — the » is deliberate: the Python driver runs
    # under a UTF-8 locale (CPython coerces LC_CTYPE at startup), so this
    # line fails loudly from Python if the tokenizer's character
    # classification ever goes locale-dependent again (src/AsciiCtype.h).
    method size($/) { make ($/,)».Int.sum }
}
