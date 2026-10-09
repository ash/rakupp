# Regression: `.map`, `.grep` and `.skip` over a lazy source are Seqs.
#
# Over a finite list they were (the eager path), but the lazy views answered
# List: `(0..*).map(* * 2).^name` was List, and so was a sub returning one
# (found with the Python binding's object shim, 2026-10-05; fixed 2026-10-09).
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check (0..*).map(* * 2).^name,   'Seq', 'a map over an endless Range';
check (0..*).grep(* %% 2).^name, 'Seq', 'a grep';
check (0..*).skip(2).^name,      'Seq', 'a skip';
sub doubled { (0..*).map(* * 2) }
check doubled().^name,           'Seq', 'returned from a sub';
check (0..3).map(* * 2).^name,   'Seq', 'a finite one, as before';

# still read by position, more than once
my $s = (0..*).map(* * 2);
check ($s[3], $s[1]),            (6, 2), 'indexed out of order';
check $s.head(3).List,           (0, 2, 4), '…and its head';
my $g = (0..*).grep(* %% 3);
check ($g[2], $g[0]),            (6, 0), 'a grep indexed out of order';
check (1..*).map(* + 1).grep(*.is-prime).head(3).List, (2, 3, 5), 'chained';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
