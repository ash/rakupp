# DATA-PLAN P4, the cross-check that matters most: our DEFLATE against the real
# one, IN THIS PROCESS.
#
# `Compress::Zlib` is NativeCall over the system libz, and its one-shot subs
# work on this engine (re-probed 2026-09-05; what still fails there is its file
# wrappers, which reach for `nqp::p6definite`). So libz itself is available as
# an oracle here, and both directions can be asked of it on any input rather
# than on the fixed streams t/vectors/zlib.vec carries.
#
# Separate from data-native-zlib.raku because it `#?requires` that module, and
# the vectors must still run on a machine without it — which includes any
# machine without a system libz, the case this tag exists for.

#?requires Compress::Zlib

use Test;

# DELIBERATELY not named `compress`/`uncompress`. A module's imports reach its
# importer on this engine, and `use Compress::Zlib` in a block leaves
# Compress::Zlib::Raw's four-argument NATIVE `compress` in GLOBAL — calling
# THAT with two arguments hands libz a null pointer and segfaults. The names
# here are distinct so this file tests deflate rather than that.
my &pcompress   = &::('rakupp-compress');
my &puncompress = &::('rakupp-uncompress');

my (&zcompress, &zuncompress);
{ use Compress::Zlib; &zcompress = &compress; &zuncompress = &uncompress; }

my @bodies =
    ''.encode,
    'a'.encode,
    'hello world'.encode,
    ('ab' x 40_000).encode,                        # highly repetitive
    Buf.new(^256),
    'héllo — ünïcode ✓ 😀'.encode,
    (^500).map({ "line $_: the quick brown fox\n" }).join.encode,
    Buf.new((^40_000).map({ ($_ * 2654435761) +& 0xff })),
    $*PROGRAM.slurp(:bin),
    ;

# ---- both directions, on every body --------------------------------------

my $theirs-reads-ours = 0;
my $ours-reads-theirs = 0;
for @bodies -> $b {
    # Compress::Zlib cannot inflate to NOTHING — its `_internal-compression`
    # writes a sentinel at `$outdata[$bufsize - 1]` and dies with "Index out of
    # range for Buf" when the output is empty. Ours handles it (there is a
    # regression case for it next door); it is simply not a question this oracle
    # can be asked.
    next unless $b.elems;
    for 0, 1, 6, 9 -> $lvl {
        $theirs-reads-ours++ unless zuncompress(Buf.new(pcompress($b, $lvl).list)) eqv Buf.new($b.list);
    }
    $ours-reads-theirs++ unless puncompress(Buf.new(zcompress(Buf.new($b.list)).list)) eqv Buf.new($b.list);
}
is $theirs-reads-ours, 0,
   "libz inflates everything we deflate — {(@bodies.grep(*.elems)) * 4} streams over four levels";
is $ours-reads-theirs, 0,
   "and we inflate everything libz deflates — {+@bodies.grep(*.elems)} streams";

# ---- the ratio -----------------------------------------------------------
#
# Not a correctness property, and pinned loosely on purpose: libz is thirty
# years of tuned C and this is a clean-room implementation of the format. What
# the assertion is protecting against is a change that quietly turns dynamic
# Huffman back into fixed, or drops the match search — either of which shows up
# here as tens of percent, not as the few this allows.

my $worst = 0e0;
for @bodies -> $b {
    next if $b.elems < 1000;
    my $mine  = pcompress($b, 6).elems;
    my $libz  = zcompress(Buf.new($b.list), 6).elems;
    my $over  = 100 * ($mine - $libz) / $libz;
    $worst max= $over;
    diag sprintf("  %7d bytes: libz %7d, ours %7d, %+.1f%%", $b.elems, $libz, $mine, $over);
}
ok $worst < 10, "our level 6 is within 10% of libz's on every body (worst {$worst.fmt('%.1f')}%)";

done-testing;
