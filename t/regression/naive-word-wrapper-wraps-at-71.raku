# Rakudo carries a `naive-word-wrapper` on Str, and retired distributions use
# it for their farewell note: Tie::StdArray, Tie::StdHash and auto-dynamic all
# have a BEGIN block that wraps a deprecation message and exits 1. rakupp had
# no such method, so the note died with X::Method::NotFound before printing and
# the `exit 1` never ran — a user on rakupp saw a missing-method error for a
# method they had never called, and never the message telling them what to do.
use Test;
plan 5;

is "short".naive-word-wrapper, "short", 'a string under the width is unchanged';
is "a b c".naive-word-wrapper, "a b c", '…and so is a short sentence';
is ("x" x 80).naive-word-wrapper, "x" x 80, 'a single long word is never split';

my $wrapped = ("word " x 30).naive-word-wrapper;
ok $wrapped.lines > 1,                      'a long run of words wraps';
ok $wrapped.lines.map(*.chars).max <= 71,   '…with no line wider than 71';
