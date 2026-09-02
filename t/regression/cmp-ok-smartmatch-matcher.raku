# Regression: `cmp-ok $got, '~~', $matcher` applies the FULL smartmatch. A block
# on the right is CALLED with the value, a regex is matched, a junction of
# matchers is threaded, and `'!~~'` is the negation. cmp-ok handed `~~` to the
# plain value-level operator, which knows none of that and answered False for
# every block — Roast's Test::Util run-with-tty judges a child's STDOUT with
# `cmp-ok .out.slurp(:close), '~~', { .contains: "FOO" & "bar" }`, so
# S32-io/out-buffering.t's "prompt does not hang" failed with both words
# sitting in the captured output. (TAP-style on purpose: the thing under test
# IS the Test module.) The same file passes under Rakudo.
use Test;
plan 12;

my $s = "^D\b\bbar\r\nFOONil\r\n";
cmp-ok $s, '~~', { .contains: "FOO" & "bar" }, 'a block on the right is called with the value';
cmp-ok $s, '~~', -> $x { $x.contains("FOO") && $x.contains("bar") }, 'a pointy block too';
cmp-ok $s, '~~', { .contains: "FOO" & "bar" or do { diag "Got STDOUT: {.raku}"; False } },
       'the run-with-tty shape: a junction inside, an `or do` fallback';
cmp-ok $s, '!~~', { .contains("nope") }, '!~~ negates a block that answers False';
cmp-ok $s, '~~', /FOO/, 'a regex is matched';
cmp-ok $s, '!~~', /nope/, 'and !~~ negates a regex';
cmp-ok 5, '~~', any(3, 5, 7), 'a junction of values threads';
cmp-ok 5, '~~', Int, 'a type still matches';
cmp-ok "x", '!~~', Int, 'and !~~ a type';
cmp-ok 5, '~~', 1..10, 'a range contains';
cmp-ok 5, '==', 5, '== is untouched';
cmp-ok "a", 'eq', "a", 'so is eq';
