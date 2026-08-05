# Regression: `PROCESS::{"\$$name"}` — a pseudo-package stash read with a
# COMPUTED key. The angle form `PROCESS::<$OUT>` is resolved at parse time into a
# plain variable; the brace form cannot be, so it read Any. Trap opens its tee
# with exactly this, and Test::Output goes through Trap.
# Also: a `:=` whose right-hand side has no lvalue at all must still bind.
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck PROCESS::{'$OUT'}.^name, $*OUT.^name, 'a literal key reaches the handle';
my $which = 'OUT';
ck PROCESS::{"\$$which"}.^name, $*OUT.^name, 'and so does a computed one';
$which = 'ERR';
ck PROCESS::{"\$$which"}.^name, $*ERR.^name, 'the key is read at run time';
ck PROCESS::<$OUT>.^name, $*OUT.^name, 'the angle form still works';

# binding it — the right-hand side is not a container, and that is fine
my $bound := PROCESS::{'$OUT'};
ck $bound.^name, $*OUT.^name, 'binding a stash read';

class Tee {
    has $.tee;
    submethod TWEAK() { with $!tee { $!tee := PROCESS::{"\$$_"} } }
}
ck Tee.new(tee => 'OUT').tee.^name, $*OUT.^name, 'and binding it to an attribute';
ck Tee.new(tee => 'ERR').tee.^name, $*ERR.^name, 'per instance';

# the hash-slot binding this shares a code path with is unchanged
my %h = k => 1;
my $slot := %h<k>;
$slot = 2;
ck %h<k>, 2, 'binding a hash slot still writes through';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
