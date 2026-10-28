# A Buf is a reference-semantics container: what a routine writes into a Buf
# it was passed, its caller's Buf holds (issue #186). A native body works on a
# copy of each parameter, so its `write-uint16` and `$b[i] = v` reached nobody;
# both now write the shared storage in place. (A write past the end still
# grows a buffer of its own, which reaches an INTERPRETED caller only, by the
# frame's copy-back: rtAotParamBack.)
unit module Aot::Buf;

sub poke(Buf $b) is export {   #aot: native
    $b.write-uint16(0, 0x0201, LittleEndian);
    $b[2] = 3
}
sub set-two(Buf $b, $x, $y) is export {   #aot: native
    $b[0] = $x;
    $b[1] = $y;
    $b
}
sub stamp($b, $v) is export {   #aot: native
    $b.write-uint32(4, $v, BigEndian)
}
sub own-copy(Buf $b is copy) is export {   #aot: native
    $b[0] = 255;
    $b[0]
}
