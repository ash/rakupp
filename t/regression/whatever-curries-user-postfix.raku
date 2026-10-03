# A `*` under a USER-DEFINED postfix is a Whatever-curry: `(1..10).map(*!)`
# is `.map({ $_! })`. It called `postfix:<!>` on the Whatever itself, so the
# factorial's `[*] 1..$n` reduced `1..*` to Inf and the map died with
# "Cannot map a Range using 'Inf'". User infixes already curried; postfixes
# did not, and neither did the built-in `postfix:<i>` (`*i`).
#
# Every expectation below was checked against Rakudo.
use Test;
plan 17;

sub postfix:<!>($n) { [*] 1..$n }

# --- the reported shape ---------------------------------------------------
is (1..10).map(*!).List, (1, 2, 6, 24, 120, 720, 5040, 40320, 362880, 3628800), '.map(*!) maps the factorial';
# (bound first: a method on `(*!)` would extend the curry, in Rakudo too)
my &fact = *!;
is &fact.^name, 'WhateverCode', '*! on its own is a WhateverCode';
is fact(5), 120, 'and calling it applies the postfix';
is &fact.arity, 1, 'with one parameter';

# --- the curry runs on through the postfix, and into it -------------------
is (*! + 1)(4), 25, '(*! + 1) — an infix on top extends the curry';
is (2 * *!)(3), 12, '(2 * *!) — on either side';
is (-*!)(3), -6, '(-*!) — a prefix on top';
is (*!.succ)(3), 7, '(*!.succ) — a method on top';
is (*.succ!)(3), 24, '(*.succ!) — the postfix over a method curry';
is ((* + 1)!)(3), 24, '((* + 1)!) — over a parenthesised infix curry';
is ((* + *)!)(1, 2), 6, '((* + *)!) — and keeps that curry\'s two parameters';

# --- other postfix names, and the candidate's signature does not matter ----
sub postfix:<foo>($n) { "<$n>" }
is (*foo)(3), '<3>', 'a wordy postfix curries';
multi sub postfix:<‼>(Int $n) { $n * 2 }
is (1..3).map(*‼).List, (2, 4, 6), 'a multi postfix curries';
sub postfix:<¡>(Whatever $w) { 'W' }
my &bang = *¡;
is &bang.^name, 'WhateverCode', 'even one whose candidate takes a Whatever (currying is syntax)';

# --- a WhateverCode the operand merely HOLDS is passed, not curried --------
sub postfix:<?!>($x) { $x.^name }
my $wc = * + 1;
is $wc?!, 'WhateverCode', '$wc?! calls the operator on the WhateverCode';

# --- the built-in postfix:<i> -----------------------------------------------
is (*i)(2), 0+2i, '(*i) curries';
is (1..3).map(*i).List, (0+1i, 0+2i, 0+3i), '.map(*i)';
