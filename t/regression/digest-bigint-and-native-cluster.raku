# Regression: seven general faults found by running Digest's own suite
# (md5 / sha / ripemd / hmac — the dist ends 4/4). All checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- polymod with a lazy divisor list on a BigInt ----------------------------
my $big = parse-base('d41d8cd98f00b204e9800998ecf8427e', 16);
ck $big.polymod(256 xx *).reverse.List,
   (212, 29, 140, 217, 143, 0, 178, 4, 233, 128, 9, 152, 236, 248, 66, 126),
   'polymod(256 xx *) divides in BigInt';

# --- prefix +^ at full width -------------------------------------------------
my $x = 0xF123456789ABCDEF;
ck +^$x, -17375808098319191536, 'prefix +^ is -(x+1), exact at any width';
ck (+^$x) +& 0x1234567890ABCDEF, 149764582167019520, 'and feeds +& correctly';
my &notf = +^*;
ck notf(0x0F), -16, '+^* curries';

# --- statement-modifier given collects placeholders --------------------------
sub ROL64 { ($^a +> (64 - $_) +| $a +< $_) % (1 +< 64) given $^n % 64 }
ck ROL64(107176545, 1), 214353090, 'placeholders bind through a modifier given';
ck ROL64(2**63, 1), 1, 'including the wraparound bit';

# --- Buf element writes survive op= and slice assignment ---------------------
my buf8 $state .= new: 0 xx 8;
$state[3] +^= 0x06;
ck $state.^name.substr(0,3), 'Buf', 'op= keeps the buffer a Buf';
ck $state[3], 6, 'and writes the byte';
my buf8 $ns .= new: 0 xx 16;
given 8 { $ns[$_ ..^ $_ + 8] = 8, 7, 6, 5, 4, 3, 2, 1 }
ck $ns.list.List, (0,0,0,0,0,0,0,0,8,7,6,5,4,3,2,1), 'a range-slice assign lands in place';

# --- a native-int parameter truncates on bind --------------------------------
sub trunc(uint32 $n) { $n }
ck trunc(2**40 + 5), 5, 'uint32 param truncates its argument';
sub rotl(uint32 $n, $b) { $n +< $b +| $n +> (32 - $b) }
ck rotl(2**40 + 0x80000001, 1) % 2**32, 3, 'so rotl sees 32 bits';

# --- multi dispatch: a typed buffer beats a bare sigil ------------------------
multi K(@lanes) { 'lanes' }
multi K(blob8 $state) { 'blob8' }
my buf8 $b .= new: 1, 2;
ck K($b), 'blob8', 'blob8 $x outranks @lanes for a Buf';
ck K([1, 2]), 'lanes', 'and an Array still reaches the sigil candidate';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
