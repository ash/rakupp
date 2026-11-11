# Regression: issue #163 (2026-11-10).
#
# In an `--exe` binary, `$b[*-1]` on a Buf or a Blob died "Index out of
# range. Is: -1, should be in 0..^Inf", and so did a store `$b[*-1] = 30`,
# where the interpreter and Rakudo answer the last element. The compiled
# read (Interpreter::idxW) and the place a store resolves (rtStarKey) counted
# an Array's elements only, so a Buf had none and `*-1` was -1; they count a
# Buf's now, and a store or an `op=` through `*-1` takes the element's own
# store (rtElemStoreN), as `$b[*-1]--` did since 9b0073c8. Found on the way:
# the INTERPRETER's `$b[*-1] += 10` changed element 0.
#
# Meant to be compiled: t/run.raku builds it natively and its binary must
# print PASS, and t/exe/run.raku compares its -O binary with the interpreter.
# Expected answers are Rakudo 2026.09's (2026-11-10).
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

sub arr() { my @a = 1, 2, 3; @a[*-1] }
check arr(), 3, '@a[*-1]';
sub buf-last() { my $b = Buf.new(1, 2, 3); $b[*-1] }
check buf-last(), 3, 'Buf $b[*-1]';
sub blob-last() { my $b = Blob.new(4, 5, 6); $b[*-1] }
check blob-last(), 6, 'Blob $b[*-1]';
sub typed() { my Buf $b = Buf.new(7, 8, 9); ($b[*-2], $b[*-3]) }
check typed(), (8, 7), 'my Buf $b, [*-2] and [*-3]';
sub param(Blob $b) { $b[*-1] }
check param(Blob.new(1, 255)), 255, 'a Blob parameter';
sub store() { my $b = Buf.new(1, 2, 3); $b[*-1] = 30; $b.list }
check store(), (1, 2, 30), 'a store through [*-1]';
sub store-wraps() { my $b = Buf.new(1, 2); $b[*-1] = 300; $b.list }
check store-wraps(), (1, 44), 'a store through [*-1] wraps at the byte';
sub op-assign() { my $b = Buf.new(1, 2, 3); $b[*-1] += 10; $b[*-2] *= 3; $b.list }
check op-assign(), (1, 6, 13), '`op=` through [*-1] and [*-2]';
sub blob-store() { my $b = Blob.new(1, 2); try { $b[*-1] = 3 }; $! ?? 'refused' !! 'took it' }
check blob-store(), 'refused', 'a Blob refuses a store through [*-1]';
sub native() { my int @n = 1, 2, 3; @n[*-1] = 9; @n[*-2] += 5; @n.List }
check native(), (1, 7, 9), 'a native int array through [*-1]';
sub plain() { my @p = 1, 2, 3; @p[*-1] = 9; @p[*-2] += 5; @p.List }
check plain(), (1, 7, 9), 'a plain array through [*-1], as before';
sub held() { my $a = [1, 2]; $a[*-1] = 7; $a.List }
check held(), (1, 7), 'an Array a `$` holds';

my $top = Buf.new(1, 2, 3);
check $top[*-1], 3, 'at the top level';
$top[*-1] = 33;
check $top.list, (1, 2, 33), 'a store at the top level';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
