# Regression: eight one-assertion Roast losses from the 2026-09-17 ceiling board.
#
#   *²(4)                    — the superscript power followed by a call was lexed
#                              flat, `* ** 2(4)`, and invoked the Int 2. It is a
#                              call of the curried power. (S32-num/power.t)
#   True but False           — ~ answered "True" and + answered 1: the mixed-in
#                              Bool is what the value now IS in every coercion,
#                              as Rakudo has it. (integration/advent2010-day19.t)
#   redo with `is copy`      — the parameter is bound afresh on redo (Rakudo), so
#                              a body that decrements it and redoes while it is
#                              positive only ends through its own guard. The
#                              signature-binding loop path kept the decremented
#                              copy. (S04-statements/redo.t)
#   duckmap and `next`       — `next` in the block drops the element; it was read
#                              as "does not quack" and the element kept.
#                              (S32-list/duckmap.t)
#   put/print of an object   — a user `method Str` is honoured through a file
#                              handle and through the sub form. (S16-io/put.t)
#   spurt($fh, …)            — the sub form on an OPEN handle is the method; it
#                              took the handle for a path. (S32-io/spurt.t)
#   ('OH' => 'HAI').Capture  — a Pair unpacks into :key and :value, not "cannot unpack".
#                              (S02-types/capture.t)
#   f(|(a => 42))            — a named argument is not credited toward a required
#                              positional: too few positionals, as Rakudo says,
#                              where $a used to run undefined. (S02-literals/pairs.t)
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }
sub threw(&c, $what) {
    my $r = try { c() };
    @fail.push("$what: returned {$r.raku} instead of throwing") unless $!;
}

check *⁰(0), 1, '*⁰(0)';
check *²(4), 16, '*²(4)';
check *³²(2), 2 ** 32, '*³²(2)';
check 3³, 27, '3³ stays a plain power';

{ my $v = True but False; check ~$v, 'False', '~(True but False)'; check ($v ?? 1 !! 0), 0, '?(True but False)'; check +$v == 0, True, '+(True but False)' }

{
    my $guard = 100; my $sum = 0;
    for 1..10 -> $i is copy { $sum += $i; $i -= 1; last if !$guard--; if $i > 0 { redo } }
    check $sum, 201, 'redo re-binds an is-copy parameter';
}

check <a b c>.duckmap({ next if $_ eq "b"; $_ }).join(' '), 'a c', 'duckmap honours next';
check duckmap({ .elems }, ["a", ["bb", "cc", "d"], []]), [1, 3, 0], 'duckmap applies where the block quacks';

{
    my $o = class { method Str { "pass" } }.new;
    my $f = $*TMPDIR.add("put-obj-$*PID");
    my $fh = $f.open(:w); $fh.put($o); $fh.print($o); $fh.close;
    check $f.slurp, "pass\npass", 'file handle put/print use the user Str';
    { temp $*OUT = $f.open(:w); put(|$o); $*OUT.close }
    check $f.slurp, "pass\n", 'sub-form put of a slipped object';
    $fh = $f.open(:w, :bin); $fh.spurt(Buf.new: 200); spurt($fh, Buf.new: 201); $fh.close;
    check $f.slurp(:bin), Buf[uint8].new(200, 201), 'spurt sub form on an open handle appends';
    $f.unlink;
}

{
    my $c = ('OH' => 'HAI').Capture;
    check $c.^name, 'Capture', 'Pair.Capture is a Capture';
    check $c<key>, 'OH', 'Pair.Capture<key>';
    check $c<value>, 'HAI', 'Pair.Capture<value>';
    sub named(:$key, :$value) { "$key=$value" }
    check named(|$c), 'OH=HAI', 'the Capture carries key and value as nameds';
    sub one($a) { $a }
    threw { one(|(a => 42)) }, 'one(|(a => 42))';
    threw { one(|\(:a(1))) }, 'one(|\(:a(1)))';
    sub two($a, :$b) { "$a/$b" }
    check two(1, |(b => 2)), '1/2', 'a positional beside a slipped named still binds';
}

.say for @fail;
say @fail ?? "FAIL" !! "PASS";
exit +?@fail;
