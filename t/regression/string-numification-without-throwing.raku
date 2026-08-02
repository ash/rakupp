# Regression: `Value::toNum()` numified a Str with `std::stod`, which THROWS
# `std::invalid_argument` when the string does not start with a number. The throw
# was caught and turned into 0.0 — the right answer, reached by raising and
# unwinding a C++ exception. Speculative numification of a string that turns out
# not to be one is routine (the invocant of every `"ab".method` call reaches
# toNum), so that was one C++ throw per method call: ~7us each, measured with a
# breakpoint on __cxa_throw as exactly one throw per call and none for an empty
# loop. `std::strtod` reports the same failure through its end pointer for free.
#
# This test pins the SEMANTICS, which must be identical either way — the speed is
# in docs/internals/OPTIMIZATION.md.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a leading number is taken, the rest ignored; no leading number is 0
check((+"12abc").defined.gist, 'False', 'a trailing-garbage string is a Failure, not a silent number');
check("12".Int,   '12',  'a clean integer');
check("3.5".Rat,  '3.5', 'a clean rational');
check(1 + "2",    '3',   'numeric context on a numeric string');
check("5" * "4",  '20',  'both sides');
check(" 12 ".Int, '12',  'surrounding space is allowed');
check("+3".Int,   '3',   'a leading sign');
check((+".5"),    '0.5', 'a leading dot');
check("1e2".Num,  '100', 'exponent form');

# the cases that used to reach the throw: a non-numeric string in a context that
# numifies WITHOUT raising
check("abc".Bool.gist,  'True',  'boolification does not numify');
check(("ab" cmp "ac").gist, 'Less', 'string comparison is stringwise');
check("abc".chars,      '3',     'and .chars is unaffected');
check("12abc".chars,    '5',     'even with digits in it');
check(<a b c>.sort.gist,'(a b c)', 'sorting strings');
check(("ab", "a").sort(*.chars).gist, '(a ab)', 'sorting by an extracted numeric key');

# a Match numifies the same way
"abc42" ~~ /\d+/;
check(+$/, '42', 'a Match of digits numifies');
"abc" ~~ /\w+/;
check((+$/).defined.gist, 'False', 'and a non-numeric Match is a Failure');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
