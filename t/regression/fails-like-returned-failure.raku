# Regression: `fails-like` on a Failure a routine RETURNS, with named matchers.
#
# String::Utils' shorten answers `"Target length 2 is too short".Failure`, and
# its suite asks `fails-like { shorten("foobarbaz", 2) }, X::AdHoc,
# :payload("Target length 2 is too short")`. Such a Failure carries only the
# bare X::AdHoc TYPE with message/payload beside it, and the matcher read the
# attribute by method call on the type — never a match. A `fail` inside the
# bare block itself is a THROW to fails-like (Rakudo's verdict: "expected code
# to fail but it threw"), and rakupp let that unwind the test file's mainline.
#
# Every expectation was checked against Rakudo.

use Test;

plan 5;

sub short-of($n) { "Target length $n is too short".Failure }
sub soft-fail()  { fail "b" }

fails-like { short-of(2) }, X::AdHoc, 'a returned coerced Failure';
fails-like { short-of(2) }, X::AdHoc, :payload("Target length 2 is too short"), '…with its payload';
fails-like { short-of(1) }, X::AdHoc, :message("Target length 1 is too short"), '…and its message';
fails-like { soft-fail() }, X::AdHoc, :payload("b"), 'a routine that fails returns its Failure';
fails-like { soft-fail() }, X::AdHoc, :payload(* eq "b"), '…judged by a code matcher too';

say "PASS";
