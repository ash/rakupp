# The REVIEW-3.7 cooldown, batch 1: eight silent wrong answers, each verified
# against Rakudo before the fix —
#
#   * `sleep-till` was a rakupp-only stub that returned True without sleeping;
#     the real routine is `sleep-until`, which already existed. The stub is
#     gone: the name is now an undeclared routine, exactly as in Rakudo.
#   * `.parse-base` (and `.UInt`, `.base-repeating`) refused with the bare
#     Failure TYPE OBJECT, which slid through arithmetic as 0 instead of
#     throwing; refusals are armed Failures now (X::Str::Numeric /
#     X::Syntax::Number::RadixOutOfRange / X::OutOfRange payloads).
#   * `1, 4, 9 ... 100` invented a step (the last difference) where Rakudo
#     throws X::Sequence::Deduction with `from` naming the seeds.
#   * Rakudo::Internals::JSON.to-json concatenated hash KEYS raw and escaped
#     only five characters in values — a quote in a key emitted invalid JSON.
#   * `let` inside a METHOD did not restore on unwind (the sub path did).
#   * a runtime negative subscript read answered a soft Failure where Rakudo
#     (and rakupp's own .AT-POS) throws X::OutOfRange.
#   * `"/nope".IO.open` handed back a live handle where open("/nope") threw:
#     the method now delegates to the one open() implementation.
#   * `slurp($p, :bin)` returned a CRLF-squeezed Str instead of the raw Blob;
#     the sub now delegates to the method. Method .spurt gains the sub's
#     :createonly/:x refusal.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# -- sleep-till is gone; sleep-until answers its contract ---------------------
try { EVAL 'sleep-till 1' }
check(?$!, True, 'sleep-till is no longer a routine');
check(sleep-until(now - 10), False, 'sleep-until with a past instant answers False without waiting');
check(sleep-until(now + 0.05), True, 'sleep-until with a future instant waits and answers True');

# -- parse-base and friends refuse with ARMED Failures ------------------------
{
    my $f = "z9".parse-base(2);
    check($f.^name, 'Failure', 'parse-base refusal is a Failure');
    try { my $x = $f + 1 }
    check($!.^name, 'X::Str::Numeric', 'using the refusal throws X::Str::Numeric');
    check("ff".parse-base(16), 255, 'parse-base still parses what it should');
    my $r = "Raku".parse-base(42);
    try { my $x = $r + 1 }
    check($!.^name, 'X::Syntax::Number::RadixOutOfRange', 'radix 42 refusal carries RadixOutOfRange');
    my $u = (-5).UInt;
    try { my $x = $u + 1 }
    check($!.^name, 'X::OutOfRange', '(-5).UInt refusal carries X::OutOfRange');
}

# -- underivable sequences throw, deducible ones still run --------------------
{
    try { (1, 4, 9 ... 100).elems }
    check($!.^name, 'X::Sequence::Deduction', '1,4,9...100 throws X::Sequence::Deduction');
    check($!.from, '1,4,9', 'and .from names the seeds');
    try { (1, 2, 6 ... *)[5] }
    check($!.^name, 'X::Sequence::Deduction', 'the endless form throws too');
    check((1, 2, 4 ... 64).join(','), '1,2,4,8,16,32,64', 'geometric deduction still works');
    check((10, 8, 6 ... 0).join(','), '10,8,6,4,2,0', 'descending arithmetic still works');
    check((1, 2 ... 5).join(','), '1,2,3,4,5', 'two seeds still mean an arithmetic step');
}

# -- to-json escapes keys and controls ----------------------------------------
check(Rakudo::Internals::JSON.to-json({ 'a"b' => 1 }), '{"a\\"b":1}',
      'a quote in a hash key is escaped');
check(Rakudo::Internals::JSON.to-json("x\x[1]"), '"x\\u0001"',
      'a C0 control in a value is escaped');

# -- let in a method restores on unwind, keeps on success ---------------------
{
    my $x = 1;
    class LetM {
        method boom { let $x; $x = 5; die 'unwind' }
        method calm { let $x; $x = 7; 'ok' }
    }
    try { LetM.boom }
    check($x, 1, 'let in a method restores when the method dies');
    LetM.calm;
    check($x, 7, 'let in a method keeps the value on a successful exit');
    sub sboom { let $x; $x = 9; die 'unwind' }
    try { sboom }
    check($x, 7, 'let in a sub still restores (unchanged)');
}

# -- a negative subscript read THROWS, like AT-POS and like Rakudo ------------
{
    my @a = 1, 2, 3;
    my $i = -1;
    try { my $v = @a[$i] }
    check($!.^name, 'X::OutOfRange', 'a runtime negative subscript read throws');
    try { my $v = @a[*-5] }
    check($!.^name, 'X::OutOfRange', 'a *-N below zero throws');
    try { my $v = @a.AT-POS(-1) }
    check($!.^name, 'X::OutOfRange', '.AT-POS(-1) still throws');
    check(@a[5].defined, False, 'a past-the-end read is still a quiet undefined');
    check(@a[$i + 2], 2, 'a legal computed subscript still reads');
    my @empty;
    my $last = @empty[*-1];
    check($last.^name, 'Failure', '@empty[*-1] is the one SOFT case (the last-chunk-if-any idiom)');
    check($last.defined, False, 'and it answers undefined, not a throw');
}

# -- .IO.open agrees with open() ----------------------------------------------
# (The TYPE is rakupp's established X::IO::DoesNotExist — Rakudo says X::AdHoc
#  here, and its spurt :createonly answers a Failure where ours answers False;
#  those two conventions predate this batch. Everything else in this file is
#  engine-neutral: 28 of 30 checks pass verbatim under Rakudo.)
{
    try { "/nonexistent-batch1-$*PID".IO.open }
    check($!.^name, 'X::IO::DoesNotExist', '.IO.open on a missing file throws like open()');
    my $tmp = $*TMPDIR.add("batch1-open-$*PID.txt");
    $tmp.spurt("hi");
    my $fh = $tmp.IO.open;
    check($fh.get, 'hi', 'the delegated .IO.open still reads');
    $fh.close;
    $tmp.unlink;
}

# -- slurp/spurt: the sub and the method agree --------------------------------
{
    my $tmp = $*TMPDIR.add("batch1-bin-$*PID.bin");
    $tmp.spurt(Blob.new(65, 13, 10, 66));            # "A\r\nB"
    check(slurp($tmp, :bin).^name.starts-with('B'), True, 'sub slurp :bin answers a Blob');
    check(slurp($tmp, :bin).elems, 4, 'and it keeps all four raw bytes');
    check(slurp($tmp).chars, 3, 'text slurp still translates CRLF');
    check($tmp.slurp(:bin).elems, 4, 'the method form agrees');
    check($tmp.spurt("x", :createonly), False, 'method spurt :createonly refuses to clobber');
    check($tmp.slurp(:bin).elems, 4, 'and the file is untouched');
    $tmp.unlink;
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
