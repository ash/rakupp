# Signal's package stash was empty, so `Signal.WHO<SIGPIPE>` resolved to Any
# and numified to 0 — and the `sigpipe` pragma, whose whole body is
# signal(Signal.WHO<SIGPIPE>, SIG_DFL), quietly called signal(0, 0) instead.
# A no-op nobody could see.
use Test;
plan 5;

ok Signal.WHO.elems > 10,        'the Signal stash holds its names';
ok Signal.WHO<SIGPIPE>.defined,  'SIGPIPE is one of them';
is Signal.WHO<SIGPIPE>.Int, 13,  '…with the right number';
is Signal.WHO<SIGINT>.Int,  2,   'and so is SIGINT';
is Signal::SIGPIPE.Int, Signal.WHO<SIGPIPE>.Int,
   'the stash agrees with the direct spelling';
