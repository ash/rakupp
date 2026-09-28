# Companion to t/regression/import-is-lexical.raku: no package of its own, so
# its `our sub` is GLOBAL's.
our sub lexg-global() { "global" }
