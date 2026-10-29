# Regression: passing a Blob or a Buf to a routine costs nothing whatever its
# size, and a write to a Buf parameter still reaches the caller (issue #177).
#
# setupRwLinks (src/InterpreterCore.cpp) snapshots a plain `$` parameter bound
# to a Buf or a Blob, and copyOutRw compares the parameter with the snapshot
# when the call returns, to copy a write back. valueEqv compared the two byte
# by byte: 1.2 ms a call for a 16 MB Blob, which made a scan calling a helper
# once per hit with its 16 MB window take 18 minutes. A Blob is immutable and
# is no longer snapshotted, and a Buf the callee did not write still has the
# body it was bound with, which is told by pointer before any byte is read.
# `subbuf-rw` refuses a Blob, as in Rakudo, rather than write one in place.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub take-blob(Blob:D $x) { 1 }
sub take-any($x) { 1 }
sub poke($x) { $x[0] = 66 }
sub splice-in($x) { $x.subbuf-rw(1, 1) = Buf.new(7, 8) }
sub splice-blob($x) { $x.subbuf-rw(0, 1) = Blob.new(9) }

# the cost: a 16 MB argument against a 16-byte one, 300 calls each
sub per-call(&f, $arg) { my $t = now; f($arg) for ^300; now - $t }
my $small-blob = Blob.new(65 xx 16);
my $big-blob   = Blob.new(buf8.allocate(16 * 1048576, 65));
my $small-buf  = buf8.allocate(16, 65);
my $big-buf    = buf8.allocate(16 * 1048576, 65);
for 'Blob', &take-blob, $small-blob, $big-blob,
    'Buf',  &take-any,  $small-buf,  $big-buf -> $what, &f, $small, $big {
    per-call(&f, $small);   # (warm)
    my $s = per-call(&f, $small);
    my $b = per-call(&f, $big);
    # before the fix the big one took ~0.36 s here, 1,000 times the small one
    unless $b < max(10 * $s, 0.05) {
        say "FAIL: 300 calls with a 16 MB $what took {$b.round(0.001)} s, with a 16-byte one {$s.round(0.001)} s";
        exit 1;
    }
}

# a write to a Buf parameter reaches the caller, in place and through subbuf-rw
my $buf = Buf.new(1, 2, 3);
poke($buf);
ck $buf.list, (66, 2, 3), 'an element write to a Buf parameter reaches the caller';
splice-in($buf);
ck $buf.list, (66, 7, 8, 3), 'a subbuf-rw write to a Buf parameter reaches the caller';
poke($big-buf);
ck $big-buf[0], 66, '…a 16 MB one too';

# a Blob stays as it was, and says so
my $blob = Blob.new(1, 2);
ck (try { poke($blob); 1 }), Nil, 'an element write to a Blob parameter dies';
ck $!.^name, 'X::Assignment::RO', '…as X::Assignment::RO';
ck (try { splice-blob($blob); 1 }), Nil, 'subbuf-rw of a Blob parameter dies';
ck $!.^name, 'X::Method::NotFound', '…as X::Method::NotFound';
ck (try { $blob.subbuf-rw(0, 1) = Blob.new(9); 1 }), Nil, 'subbuf-rw of a Blob variable dies';
ck $blob.list, (1, 2), 'the Blob is unchanged';

say 'PASS';
