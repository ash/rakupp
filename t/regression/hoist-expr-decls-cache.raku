# Regression: hoistExprDecls caches its decision per body, and must still hoist.
#
# A `my` declared inside ONE ternary/nqp branch has to be visible to the SIBLING
# branch — that is the whole reason the walk exists. But it ran on every sub call
# and every block entry, walking the body's entire expression tree to look for
# declarations that are usually not there. Profiling fib against the 1.0.0 binary
# put ~6% of runtime in that lambda (a std::function, so it allocated too).
#
# Whether a body holds such a declaration is a STATIC property of its AST, so it
# is now decided once and remembered on the owning Block / Callable. The dynamic
# half — whether the variable needs defining on THIS call — must stay per-call,
# which is what these cases pin: the same body is entered repeatedly and has to
# behave identically every time, not just the first.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the sibling-branch case, entered twice — the cached body must hoist both times
sub f($c) { $c ?? (my $x = 1) !! ($x // 'unset') }
check(f(True).gist,  '1',     'a my in the taken branch');
check(f(False).gist, 'unset', 'and the sibling branch still sees it declared');
check(f(True).gist,  '1',     'second entry to the same (now cached) body');
check(f(False).gist, 'unset', 'and again');

# per-iteration freshness: the declaration must NOT leak across loop iterations
my @seen;
for 1..3 -> $i { @seen.push: ($i > 1 ?? (my $y = $i) !! ($y // 'none')).gist }
check(@seen.join(','), 'none,2,3', 'a hoisted my stays fresh per iteration');

# a body with nothing to hoist is the cached-negative path — it must still run
sub plain($n) { $n * 2 }
check(plain(21).gist, '42', 'a body with nothing to hoist still works');
check(plain(1).gist,   '2', 'and on repeat entry');

# nested: an inner block's decision must not be confused with the outer one
sub g() { my $n = 0; for ^3 { $n = $_ ?? (my $w = $_) !! ($w // 0) }; $n }
check(g().gist, '2', 'a nested block keeps its own decision');
check(g().gist, '2', 'repeatably');

# top-level, outside any sub
my $q = 1 ?? (my $deep = 'd') !! 0;
check($deep, 'd', 'hoisting at file scope');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
