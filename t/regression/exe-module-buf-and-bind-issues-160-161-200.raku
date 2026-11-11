# Regression: issues #160, #161, #200 (2026-11-10).
#
#   #160 in an --exe binary, bytes a native function wrote into a Buf from a
#        natively compiled module routine were lost: the Buf stayed zeroed.
#   #161 `my $b = Buf.new(1,2,3); $b[0] = 9` in a routine made $b `Array [9]`.
#   #200 `my @a := aoa()[0]` in a native module routine bound a one-element
#        list wrapping the element: `1 Nil` where 5.4.1 gave `3 20`.
# Built with --exe (and -O) from lib/RakuppIssues160To200.rakumod; the
# interpreter must agree. Expected answers are Rakudo 2026.09's (2026-11-10).

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppIssues160To200;

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

# --- #160
my $b = buf-from-c();
check $b.list, (1, 2, 3, 4), '#160: memcpy into a typed Buf in a module routine';
check $b.gist, 'Buf:0x<01 02 03 04>', '#160: its gist';
check buf-untyped-from-c(), (7, 7, 7), '#160: memset into an untyped Buf';
my $arg = Buf.allocate(2);
check buf-arg-from-c($arg), (0xAB, 0xAB), '#160: memset into a Buf parameter';
check $arg.list, (0xAB, 0xAB), "#160: ...and the caller's Buf holds it";

# --- #161
check buf-store(), ('Buf', (9, 2, 3)), '#161: $b[0] = 9 keeps a Buf';
check buf-from-blob(), ('Buf', (4, 5)), '#161: Buf.new(Blob)';

# --- #200
check v1(), '3 20', '#200: my @a := aoa()[0]';
check v2(), '3 20', '#200: my @a := state-aoa()[0]';
check v3(), '3 20', '#200: my @a := @outer[0]';
check v4(), '3 20', '#200: my $x = aoa()[0]';
check (^3).map({ sbox($_) }).List, (10, 20, 30), '#200: lookups through the bound table';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
