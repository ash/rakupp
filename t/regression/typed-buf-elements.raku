# Regression: typed Buf/Blob (buf16/32/64, blob32…) addressed their contents
# as BYTES where Rakudo addresses ELEMENTS. Three faces of one gap, all found
# continuing the zef-bar batch-3 Digest::MD5 thread:
#
# 1. `my buf8 $b .= new` died "No such method 'new' for invocant of type
#    'Any'" — the lowercase Buf/Blob aliases are TYPE names, but the declared
#    default treated them as native scalars and seeded Any. (Uppercase `Buf`,
#    `Int`, `Array` all worked; this is Digest::MD5's very first line.)
# 2. `.push`/`.unshift`/`.append` appended ONE BYTE per value, so 14 pushes
#    onto a buf32 gave 3 elements. Now one little-endian ELEMENT per value.
#    `.pop`/`.shift` likewise take a whole element.
# 3. `.write-uintN(pos, …)` on a typed buf takes `pos` in ELEMENTS (the value
#    lands at byte pos*W) and grows to (pos + N) elements — verified against
#    Rakudo: buf32.new(1,2,3).write-uint64(3, v) leaves .elems == 11.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. typed declaration seeds a usable type object
# (.^name differs by engine — Rakudo says Buf[uint8], rakupp Buf; what both
# must agree on is that the declaration yields a WORKING, empty buffer)
my buf8 $b8 .= new;
@fail.push("buf8 .= new: {$b8.^name}")   unless $b8.defined && $b8.elems == 0;
my blob32 $bl .= new;
@fail.push("blob32 .= new: {$bl.^name}") unless $bl.defined && $bl.elems == 0;
my int $ni; @fail.push('native int intact') unless $ni == 0;      # natives unchanged
my num32 $nn; @fail.push('native num intact') unless $nn == 0;

# 2. element-wise push/pop
my $b = buf32.new;
$b.push(0); $b.push(0); $b.push(128);
@fail.push("push elems: {$b.elems}") unless $b.elems == 3;
@fail.push("push vals: {$b.list}")   unless $b.list eqv (0, 0, 128);
my $p = buf32.new(1, 300, 70000);
@fail.push("pop: {$p.pop}")          unless $p.elems == 3 && $p.list[2] == 70000;
my $q = buf32.new(1, 300, 70000);
@fail.push("pop val")                unless $q.pop == 70000 && $q.elems == 2;
my $s = buf32.new(5, 6);
@fail.push("shift val")              unless $s.shift == 5 && $s.elems == 1;
my $b8b = buf8.new;                                    # plain buf8 stays byte-wise
$b8b.push(65); $b8b.push(300);
@fail.push("buf8 push: {$b8b.list}") unless $b8b.list eqv (65, 44);

# 3. write-uintN addresses elements on a typed buf
my $w = buf32.new(1, 2, 3);
$w.write-uint64(3, 0x0102030405060708, LittleEndian);
@fail.push("write elems: {$w.elems}") unless $w.elems == 11;
@fail.push("write vals: {$w.list[3]}/{$w.list[4]}")
    unless $w.list[3] == 0x05060708 && $w.list[4] == 0x01020304;
my $w8 = buf8.new;                                     # buf8 keeps byte offsets
$w8.write-uint32(0, 7, LittleEndian);
@fail.push("buf8 write: {$w8.list}") unless $w8.list eqv (7, 0, 0, 0);

# the shape Digest::MD5 builds: pad a message into 16-element blob32 groups
my $msg = ''.encode;
my @groups = |map { blob32.new: @$_ },
  { my $bits = 8 * $msg.elems;
    $^bb.push($_) for (@$msg, 0x80, 0x00 xx (-($bits div 8 + 1 + 8) % 64))
        .flat.rotor(4).map({ :256[@^a.reverse] });
    $bb.write-uint64: $bb.elems, $bits, LittleEndian;
    $bb }(buf32.new).rotor(16);
@fail.push("md5 pad: {@groups.elems}/{@groups[0].elems // 0}")
    unless @groups.elems == 1 && @groups[0].elems == 16;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
