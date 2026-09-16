# Regression: `Buf[int8]` reads back SIGNED.
#
# Two halves, both of which forced unsigned:
#
#   * construction wrote the element type as `"uint" ~ bits` whatever the
#     parameter said, so a `Buf[int8]` became a `Buf[uint8]` before any byte was
#     stored; and the parameter may arrive either as the ofType (`int8`) or
#     inside the type NAME (`Buf[int8]`), which the first attempt at this missed.
#   * blobWordAt assembles the bytes low-first and zero-extends, which is right
#     for every unsigned width and wrong for every signed one narrower than the
#     machine word.
#
# So `Buf[int8].new(255)[0]` answered 255 where Rakudo answers -1, and a module
# decoding signed fields out of a buffer read every negative value as its
# magnitude.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# signed parameters read back signed
check Buf[int8].new(255)[0],         -1, 'Buf[int8] of 255 is -1';
check Buf[int8].new(127)[0],        127, '…and 127 stays 127';
check Buf[int8].new(128)[0],       -128, '…and 128 is the low bound';
check Buf[int16].new(65535)[0],      -1, 'Buf[int16]';
check Buf[int32].new(4294967295)[0], -1, 'Buf[int32]';
check Buf[int8].new(200, 100).List, (-56, 100), 'each element independently';

# unsigned parameters, and the named spellings, are unchanged
check Buf[uint8].new(255)[0],  255, 'Buf[uint8] is untouched';
check blob8.new(255)[0],       255, 'blob8 is unsigned by its own spelling';
check buf8.new(255)[0],        255, 'and so is buf8';
check Buf.new(255)[0],         255, 'a bare Buf is unsigned';
check blob64.new(2**63)[0], 9223372036854775808, 'a wide unsigned still promotes';

# STILL OPEN, and separate from the value question: the INSTANCE reports its
# type as `Buf`, where Rakudo says `Buf[int8]`. That is the type name, which
# dispatch and `.raku` both read, so it is not a one-line change to make here.

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
