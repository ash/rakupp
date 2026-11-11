# Regression: `Blob.new` and `Buf.new` read a Seq's elements without
# pulling it first, so a Seq nothing had read yet — the `.map` a sub
# returns, a `gather` — made an empty buffer: `Blob.new(f())` was
# `Blob:0x<>` where Rakudo has `Blob:0x<01 02 03>`. Found when a test
# helper's `Blob.new(le($v, 4))` built short binary records (issue #241,
# a 5.6.0 regression).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub f { (^3).map({ $_ + 1 }) }
sub g { gather { take 1; take 2 } }
sub le($v, $n) { (^$n).map({ ($v +> (8 * $_)) +& 0xFF }) }
check Blob.new(f()).list, (1, 2, 3), 'Blob.new of a sub\'s map';
check Buf.new(f()).elems, 3, 'Buf.new';
check Blob.new(g()).list, (1, 2), 'a gather';
check Blob.new(f(), 9).list, (1, 2, 3, 9), 'among other arguments';
check buf8.new(f()).list, (1, 2, 3), 'buf8';
check blob16.new(f()).list, (1, 2, 3), 'blob16';
check Blob.new(le(0x01020304, 4)).list, (4, 3, 2, 1), 'the little-endian helper';
check Blob.new((^3).map({ $_ + 1 })).list, (1, 2, 3), 'inline, as before';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
