# An empty `my int @a` / `my num @a` grown by `push` or by index keeps its
# elements as machine words (2026-11-05, SPEED-BACKLOG 49). Only a list
# assignment (`my int @a = ^N`) made the packed form; the declaration made an
# ordinary empty Array, and the packed `push` and index-store lanes take only
# an array that is packed already, so an array built by a loop stayed 80-byte
# Values: 1M pushed Ints 87 MB peak, against 16 MB for `= ^N`. The empty
# array is words from the start now, and an `=` at or past its end grows them,
# a gap holding 0 (0e0). Every answer below must be the one the boxed array
# gave; the memory is in tools/run-memory.raku's `int-array` row.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my int @p;
@p.push($_) for ^5;
ck(@p.raku, 'array[int].new(0, 1, 2, 3, 4)', 'pushed one at a time');
ck((@p.elems, @p.sum, @p.min, @p.max), (5, 10, 0, 4), '…its elems, sum, min and max');
@p.push(5, 6);
push @p, 7;
@p.append(8, 9);
ck(@p.join(","), "0,1,2,3,4,5,6,7,8,9", 'push of two, sub push, append');
@p.unshift(-1);
ck((@p[0], @p[*-1], @p.elems), (-1, 9, 11), 'unshift, then the ends');

my int @g;
@g[$_] = $_ * 2 for ^4;
ck(@g.raku, 'array[int].new(0, 2, 4, 6)', 'grown by index, at its end');
@g[7] = 1;
ck(@g.raku, 'array[int].new(0, 2, 4, 6, 0, 0, 0, 1)', 'a store past the end: the gap is 0');
@g[2] += 10;
@g[3]++;
ck((@g[2], @g[3]), (14, 7), '`+=` and `++` on a grown element');

my num @n;
@n.push($_ + 0.5e0) for ^3;
@n[5] = 2.5e0;
ck(@n.raku, 'array[num].new(0.5e0, 1.5e0, 2.5e0, 0e0, 0e0, 2.5e0)', 'a num array, pushed and grown: the gap is 0e0');
ck(@n.sum, 7e0, '…and its sum');

my int @f;
for 1 .. 3 -> $i { @f.push($i * $i) }
my $t = 0;
$t += $_ for @f;
ck($t, 14, 'iterated after a pushing loop');
ck(@f.sort({ $^b <=> $^a }).List, (9, 4, 1), 'sorted');
ck(@f.reverse.List, (9, 4, 1), 'reversed');
ck(@f.map(* + 1).List, (2, 5, 10), 'mapped');

my int @c = @f;
@c.push(100);
ck((@f.elems, @c.elems), (3, 4), 'a copy grows apart from its source');
sub takes(int @x) { @x.push(7); @x.elems }
ck(takes(@f), 4, 'a pushed array passed to an `int @` parameter, which pushes');
ck(@f[*-1], 7, '…and the caller sees the push');

my int @r;
@r.push(1, 2, 3);
@r = 5, 6;
ck(@r.raku, 'array[int].new(5, 6)', 'refilled by a list assignment after pushes');

my int @e;
ck((@e.elems, @e.raku, @e.of.^name), (0, 'array[int].new()', 'int'), 'an empty one');
my int @x;
my $err = (try { @x.push("seven"); "no error" }) // $!.^name;
ck($err, 'X::TypeCheck', 'a Str pushed is refused');
ck(@x.elems, 0, '…and nothing was added');

my int @s[3];
@s[1] = 5;
my $serr = (try { @s[5] = 1; "no error" }) // $!.^name;
ck(($serr ne "no error", @s.raku), (True, 'array[int].new(:shape(3,), [0, 5, 0])'),
   'a shaped array does not grow');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
