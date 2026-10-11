# Regression: what `rakupp --target=js` got wrong when Math::Wavelet was first
# compiled into a browser page (2026-10-11). Each was a divergence between the
# interpreter and the JavaScript program, so t/js/run.raku is where these
# lines earn their keep; under the interpreter they hold the expected values.
#
# Expectations checked against Rakudo v2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- a hyphenated attribute read through its twigil ---------------------------
# The emitter spelled `$!my-x` as the escaped property, the runtime stored the
# raw name, and `@!dec-lo` read as empty inside TWEAK.
class H {
    has @.dec-lo;
    has $.my-x = 5;
    has $.n;
    submethod TWEAK { $!n = +@!dec-lo }
    method both { "{+@!dec-lo} $!my-x" }
}
my $h = H.new(dec-lo => [1, 2, 3]);
ck $h.both, '3 5', 'hyphenated attributes read through the ! twigil';
ck $h.n, 3, '…and inside TWEAK';
ck $h.clone(my-x => 7).both, '3 7', '…and after a clone that sets one';

# --- one-letter constants are distinct ------------------------------------------
constant N = 512;
constant H2 = 260;
constant W = 1000;
ck (N, W), (512, 1000), 'one-letter constants each keep their value';

# --- a hyper method answers a List when the method is nodal -------------------
my @c = [1, 2], [3];
ck @c».elems.WHAT.^name, 'List', '@array».elems is a List';
my @a = 1, -2;
ck @a».abs.WHAT.^name, 'Array', '@array».abs stays an Array';

# --- list assignment iterates a single item on the right ----------------------
my @bands = 1, (2, 3, 4);
my ($p, $q, $r) = @bands[1];
ck ($p, $q, $r), (2, 3, 4), 'my (…) = @a[i] unpacks the element';
my $item = (5, 6);
my ($s, $t) = $item;
ck ($s, $t), (5, 6), 'my (…) = $item unpacks the item';
my ($u, $v) = @bands;
ck $u, 1, 'an Array on the right is iterated, not flattened';

# --- a nested unnamed sub-signature ------------------------------------------
sub split-bands(($a, ($h, $v, $d))) { "$a|$h|$v|$d" }
ck split-bands((1, (2, 3, 4))), '1|2|3|4', 'an unnamed sub-signature inside a sub-signature binds';

# --- an open-ended slice on the left stops at the end -------------------------
my @w = 1, 2, 3, 4;
@w[1..*] = @w[1..*].map(* * 10);
ck @w, [1, 20, 30, 40], '@a[1..*] = … assigns to the existing elements';
@w[2..*] = 7, 8, 9, 10;
ck @w, [1, 20, 7, 8], '…and drops what does not fit';

# --- .map(|*) is a WhateverCode that slips -------------------------------------
ck ((1, 2), (3, 4)).map(|*).List, (1, 2, 3, 4), '.map(|*) flattens one level';

# --- Range min/max with a key --------------------------------------------------
my @vals = 3, 9, 4;
ck (^@vals).max(:by({ @vals[$_] })), 1, '(^@a).max(:by(…)) compares the keys';
ck (^@vals).min(:by({ @vals[$_] })), 0, '(^@a).min(:by(…)) too';
ck (^3).max, 3, 'a bare (^3).max is still the endpoint';

# --- Map, cis, Lock ----------------------------------------------------------
my $m = (b => 2, a => 1).Map;
ck $m.WHAT.^name, 'Map', '.Map makes a Map';
ck $m<a>, 1, '…that answers its keys';
ck (try { $m<c> = 3; 'set' } // $!.message), "Cannot add key 'c' to an immutable Map", '…and refuses a new one';
ck cis(0).re, 1e0, 'cis(0) is 1';
ck (cis(pi / 2).im - 1).abs < 1e-12, True, 'cis(π/2) is i';
my $lock = Lock.new;
ck $lock.protect({ 6 * 7 }), 42, 'Lock.protect answers its block';

say $fails ?? "FAILED $fails" !! 'PASS';
