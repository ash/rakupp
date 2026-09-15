# Every scalar answers Any's one-element list view — `42.cache` is `(42)` — but
# a Code object (a sub, a block, a Regex) fell through to "No such method", so
# `rx/a/.cache` threw where Rakudo answers `(rx/a/,)`. Testo caches the regex it
# was handed before matching with it, and stopped on its first assertion.
use Test;
plan 5;

my $rx = rx/a/;
is $rx.cache.elems, 1,          'a Regex caches as one element';
ok $rx.cache[0] === $rx,        '…and that element is the regex itself';
is $rx.list.elems, 1,           '.list is the same one-element view';

my $s = sub { 42 };
is $s.cache.elems, 1,           'a Sub, too';
is $s.List.elems,  1,           '…and .List with it';
