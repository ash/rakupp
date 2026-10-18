# Regression: two places `rakupp --target=js` died where the interpreter answers,
# found building the Webraku demos (2026-10-18). Under the interpreter these
# lines hold the expected values; t/js/run.raku is where they earn their keep.
#
# Expectations checked against Rakudo v2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- pick and roll on an Int range, by arithmetic --------------------------------
# The JS runtime built the range's array before picking, so `(^2**40).pick`
# died with "RangeError: Invalid array length". An empty range rolled
# undefined values. The random values themselves are not printed; what they
# must be is.
my $p = (^2**40).pick;
ck ($p ~~ Int && 0 <= $p < 2**40), True, '(^2**40).pick is an Int in the range';
my @r = (^10**30).roll(3);
ck (@r.elems == 3 && @r.map({ $_ ~~ Int && 0 <= $_ < 10**30 }).all.so), True, '(^10**30).roll(3): three Ints in the range';
my @k = (^2**40).pick(5);
ck (@k.elems, @k.unique.elems), (5, 5), '(^2**40).pick(5): five distinct values';
ck @k.map({ 0 <= $_ < 2**40 }).all.so, True, '…all in the range';
ck (^2**40).roll(5).elems, 5, '(^2**40).roll(5)';
my @h = (^100).pick(49);
ck (@h.elems, @h.unique.elems), (49, 49), 'fewer than half of a small range: distinct';
my @w = (^100).pick(60);
ck (@w.elems, @w.unique.elems), (60, 60), 'more than half: distinct too';
ck (^0).pick, Nil, 'an empty range picks Nil';
ck (^0).pick(3).elems, 0, '…and no values for a count';
ck (^0).roll, Nil, 'an empty range rolls Nil';
ck (^0).roll(2).elems, 0, '…and no values for a count';
ck (5..5).roll(3).List, (5, 5, 5), 'a one-value range rolls that value';
ck (1^..^4).pick(*).sort.List, (2, 3), 'pick(*) honours excluded ends';
ck (-3..-1).roll(4).map({ -3 <= $_ <= -1 }).all.so, True, 'negative ranges';
ck ('a'..'c').pick(*).sort.join, 'abc', 'a string range still picks from its values';

# --- an undeclared dynamic variable is a Failure -------------------------------
# The JS runtime threw "Dynamic variable … not found" at the lookup, so the
# usual `$*X // $default` died where the interpreter and Rakudo answer the
# default. Used as a value, it still throws.
ck $*WEBRAKU-NOWHERE // 5, 5, 'an undeclared $*var falls through //';
ck $*WEBRAKU-NOWHERE.defined, False, '…is not defined';
ck so($*WEBRAKU-NOWHERE), False, '…is false';
ck (with $*WEBRAKU-NOWHERE { 'with' } else { 'without' }), 'without', '…takes the else of a with';
ck $*WEBRAKU-NOWHERE ~~ Failure, True, '…is a Failure';
ck (try { $*WEBRAKU-NOWHERE + 1 } // $!.^name), 'X::Dynamic::NotFound', '…and throws when used in arithmetic';
ck (try { $*WEBRAKU-NOWHERE.foo } // $!.message), 'Dynamic variable $*WEBRAKU-NOWHERE not found',
    'a method a Failure lacks throws its exception';
ck (try { say $*WEBRAKU-NOWHERE; 1 } // $!.message), 'Dynamic variable $*WEBRAKU-NOWHERE not found',
    'say of an unchecked Failure throws it';
{
    my $f = $*WEBRAKU-NOWHERE;
    $f.defined;
    # Rakudo adds the backtrace after the message
    ck $f.gist.starts-with('(HANDLED) Dynamic variable $*WEBRAKU-NOWHERE not found'), True,
        'a checked Failure gists as handled';
}
sub reads() { $*WEBRAKU-SOMETIMES // 'unset' }
ck reads(), 'unset', 'a sub reading an undeclared $*var';
{
    my $*WEBRAKU-SOMETIMES = 'set';
    ck reads(), 'set', '…and the same sub once a caller declares it';
}

say $fails ?? "FAILED $fails" !! 'PASS';
