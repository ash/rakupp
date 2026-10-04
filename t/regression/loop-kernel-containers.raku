# Loop kernels over containers (KERNEL-PLAN task 1): `@a[EXPR]` and
# `%h{EXPR}` read, stored, `op=` and ++/-- in place, `@a.push`, `.elems`.
# A kernel writes into the containers themselves, so a later bail has to take
# those writes back before the loop runs again the ordinary way: each case
# below that bails does so AFTER it has written, with writes that are not
# idempotent, so a missing or partial undo shows as a doubled effect.
# Every answer is Rakudo's, and the same with RAKUPP_NO_KERNELS=1.
use Test;
plan 28;

# reads, stores, push, elems
my @a;
for 1 .. 1000 -> $i { @a.push($i * 3 % 100) }
is @a.elems, 1000,                         'push in a loop';
my $s = 0;
for 0 ..^ @a.elems -> $i { $s += @a[$i] }
is $s, 49500,                              'read by index';
for 0 ..^ @a.elems -> $i { @a[$i] = @a[$i] + 1 }
is @a[0] + @a[999], 5,                     'store by index';
for 0 ..^ 10 { @a[$_] *= 2; @a[$_]++; --@a[$_] }
is @a[0..2], (8, 14, 20),                  'op= and ++/-- on elements';

# holes: a store past the end fills the gap with Any; op= and ++ count from
# the identity
my @h;
for 1 .. 3 { @h[$_ * 2] = $_; @h[7]++; @h[8] *= 3; @h[9] -= 1 }
is @h.raku, '[Any, Any, 1, Any, 2, Any, 3, 3, 27, -3]', 'holes';

# strings
my @w = <a b c>;
my $t = '';
for 0 .. 2 { $t ~= @w[$_]; @w[$_] ~= '!' }
is $t, 'abc',                              'Str elements read';
is @w.join(','), 'a!,b!,c!',               '…and appended to';
my @p;
for 1 .. 3 { @p.push("n$_") }
is @p.join(','), 'n1,n2,n3',               'push of a Str';

# hashes
my %c;
for 1 .. 1000 { %c{$_ % 7}++ }
is %c.elems, 7,                            'hash ++ on new keys';
is %c{0}, 142,                             '…counted';
my %f;
for 1 .. 100 -> $i { %f{"key$i"} = $i * 2 }
is %f<key50>, 100,                         'interpolated Str keys';
my $sum = 0;
for 1 .. 100 -> $i { $sum += %f{"key$i"} }
is $sum, 10100,                            'hash read';
my %m = a => 5;
for 1 .. 3 { %m<a> += 2; %m<b> -= 1; %m<c> *= 2; %m<d> ~= 'x' }
is %m.sort.map({ .key ~ '=' ~ .value }).join(' '), 'a=11 b=-3 c=8 d=xxx', 'hash op= on new and old keys';

# bails after writes: an Int that outgrows int64 on the last iterations
my @u = 0 xx 5;
my $big = 2 ** 60;
for 0 .. 4 { @u[$_] += 1; @u.push($_); $big *= 2 }
is @u.join(','), '1,1,1,1,1,0,1,2,3,4',    'an overflow after writes: the writes are taken back';
is $big, 2 ** 65,                          '…and the loop runs again';
my %v = a => 1;
my $grow = 2 ** 61;
for 1 .. 4 { %v{"k$_"} = $_; %v<a>++; $grow *= 2 }
is %v.sort.map({ .key ~ '=' ~ .value }).join(' '), 'a=5 k1=1 k2=2 k3=3 k4=4', '…for a hash too';

# …and after the log has outgrown the containers and been folded into a
# saved copy of each (thousands of writes to ten elements, then the overflow)
my %c2;
my $g2 = 2 ** 40;
for 1 .. 5000 { %c2{$_ % 10}++; %c2{"x$_"} = 1 if $_ %% 1000; $g2 *= 2 if $_ > 4970 }
is %c2.elems, 15,                          'a bail after the log was folded: hash';
is (%c2{3}, %c2<x5000>, $g2 == 2 ** 70), (500, 1, True), '…counted once';
my @c3 = 0 xx 10;
my $g3 = 2 ** 40;
for 1 .. 5000 { @c3[$_ % 10] += 1; @c3.push($_) if $_ %% 1000; $g3 *= 2 if $_ > 4970 }
is @c3.elems, 15,                          'a bail after the log was folded: array';
is @c3[0..2], (500, 500, 500),             '…counted once';

# a value of another kind halfway: a Str element numifies on the generic path
my @mixed = 1, 2, '3', 4;
my @seen;
my $total = 0;
for 0 .. 3 { @seen.push($_); $total += @mixed[$_] }
is $total, 10,                             'a Str element in an Int read';
is @seen.join(','), '0,1,2,3',             '…the pushes before it taken back';

# the value of a postfix ++ or -- on a hole is 0, as for `my $x; $x++`
my %z;
my @olds;
for 1 .. 2 { my $o = %z<n>++; my $p = %z<m>--; @olds.push($o); @olds.push($p) }
is @olds.join(','), '0,0,1,-1',            'a postfix ++ whose value is read';

# containers a plain store would not honour run the ordinary way
my Int @typed = 1, 2;
for 0 .. 1 { @typed[$_] += 10 }
is @typed.join(','), '11,12',              'a typed array';
my @dflt is default(7);
for 0 .. 1 { @dflt[$_] += 1 }
is @dflt.join(','), '8,8',                 'is default';
my $x = 10;
my @bound = 1, 2;
@bound[0] := $x;
for 0 .. 1 { @bound[$_] += 1 }
is "$x @bound[1]", '11 3',                 'a bound element';

# a negative index is the generic path's error
throws-like { my @n = 1, 2; for 0 .. 1 { @n[$_ - 1] = 0 }; 1 }, Exception, 'a negative index';
my @e = 1, 2, 3;
my $cnt = 0;
for 0 .. 5 { $cnt += 1 if @e[$_] }
is $cnt, 3,                                'a read past the end is the generic path\'s';
