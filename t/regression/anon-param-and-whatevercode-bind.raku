# Regression: two defects in parameter binding, both found by Crane (issue #69),
# both of which changed which multi candidate ran rather than raising anything.
#
# 1. Every ANONYMOUS parameter is spelled as the bare sigil, so one signature can
#    hold several called `$` — `sub f($ where .so, $)`, or the `:k($) :v($) :p($)`
#    trio Crane builds its signatures from. They all bound the SAME env slot and
#    whichever bound last owned the name. Nothing reads an anonymous parameter,
#    so that was invisible until the `where` enforcement looked its value up BY
#    NAME: a `where` on the first `$` then tested the LAST `$`'s value.
#    `f(:k(True), :v(False))` died on :k's own `where .so` while
#    `f(:k(True), :v(True))` passed it, neither having anything to do with :k.
#
# 2. The binder's type matcher never named WhateverCode, so `sub f(WhateverCode:D
#    $x)` called with `*-0` failed its own bind with "expected WhateverCode but
#    got WhateverCode" — `~~` answers that from another path, and the two
#    disagreed. Crane classifies its "append here" step on exactly that
#    signature, so `*-0` fell through to the OTHER branch and threw.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- 1. anonymous parameters do not share a slot ----------------------------
multi sub f(Bool :k($), Bool:D :v($)! where .so) { 'V' }
multi sub f(Bool:D :k($)! where .so, Bool :v($)) { 'K' }
multi sub f(Bool :k($), Bool :v($))              { 'D' }
check f(:k(True)),            'K', 'a where on the FIRST anon named reads its own value';
check f(:k(True), :v(False)), 'K', '…and is not decided by a later anon named';
check f(:k(True), :v(True)),  'V', '…while a genuinely matching :v candidate still wins';
check f(:v(True)),            'V', 'the other candidate still wins on its own named';
check f(:k(False)),           'D', 'a where that genuinely fails still falls through';
check f(),                    'D', 'and so does a missing required named';

# positional anonymous parameters collide the same way
sub two-anon($ where .so, $) { 'POS' }
check two-anon(1, 0), 'POS', 'a where on the first anon POSITIONAL reads its own value';
sub three-anon($ where * > 5, $, $) { 'POS3' }
check three-anon(9, 0, 0), 'POS3', 'three anonymous positionals, where on the first';

# a one-character SIGILLESS parameter is NOT anonymous — `\c`, `\n`, `\v` are
# real names, and mangling them hid the caller's value (roast's Test::Util
# `sub TEST-ITER-OPT (\iter, \data, \n, $desc)` broke exactly there)
sub sigilless(\c, \v, $d) { "{c}-{v}-{$d}" }
check sigilless(1, 2, 'x'), '1-2-x', 'one-character sigilless parameters keep their names';

# --- 2. WhateverCode binds to a WhateverCode parameter ----------------------
sub wc(WhateverCode:D $x) { 'WC' }
check wc(*-0), 'WC', 'a curried * binds to WhateverCode:D';
sub wcu(WhateverCode $x) { 'WCU' }
check wcu(*-1), 'WCU', '…and to an unsmileyed WhateverCode';
my $smart = *-0 ~~ WhateverCode:D;
check $smart, True, 'and ~~ agrees, as it always did';

# the dispatch Crane actually does: Int / WhateverCode / everything else
enum Kind <NUM WHEN OTHER>;
multi sub classify(Int:D $ where * >= 0 --> Kind:D) { NUM }
multi sub classify(WhateverCode:D $     --> Kind:D) { WHEN }
multi sub classify($                    --> Kind:D) { OTHER }
check classify(8),     NUM,   'an Int index classifies as NUM';
check classify(*-0),   WHEN,  'a WhateverCode index classifies as WHEN';
check classify('8'),   OTHER, 'a Str that looks numeric is still OTHER';
check classify(-3),    OTHER, 'a negative Int misses the >= 0 candidate';

if @fail {
    note $_ for @fail;
    die "{+@fail} check(s) failed";
}
say 'PASS';
