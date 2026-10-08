# `my $x = 'a'; @d.push: ($x, 6); ~@d` — a variable's container inside a
# nested list.
#
#   `($x, 6)` keeps $x's container, a Proxy over the variable's shared cell,
#   so that the list sees later assignments to $x. Stringifying the OUTER
#   array read containers only at its own level: `~@d`, `@d.Str`, "@d[]",
#   `put @d` and Test's `is` all rendered the inner Proxy as its FETCH/STORE
#   pair where Rakudo says "a 6". iz4's write-scaffolds returns exactly such a
#   list, and its evidence test compared it with `is`.
# Contract: exit 0 + last line PASS.
use Test;
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my $x = 'a';
my @d; @d.push: ($x, 6);
check ~@d,          'a 6', '~@d';
check @d.Str,       'a 6', '.Str';
check "@d[]",       'a 6', 'interpolated';
check @d.Stringy,   'a 6', '.Stringy';
check @d.join(','), 'a 6', '.join (already right)';
my @e; for 1 { my ($rel, $text) = 'p', 'q'; @e.push: ($rel, 6) }
check ~@e,          'p 6', 'from a destructuring declaration in a loop';
my @deep; @deep.push: (($x, 1), 2);
check ~@deep,       'a 1 2', 'two levels down';
check (is @d, (('a', 6),), 'is reads the container'), True, 'Test is';
$x = 'b';
check ~@d,          'b 6', 'the container is still shared';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
