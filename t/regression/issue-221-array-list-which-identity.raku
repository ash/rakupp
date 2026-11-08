# Issue #221: `Array.WHICH` and `List.WHICH` were the type and the elements
# joined by a space: `@a.push(3)` changed `@a.WHICH`, two arrays holding 1, 2
# had one WHICH (and were one Set element), and `[1,2]`, `["1","2"]` and
# `["1 2"]` were all `Array|1 2` — while `===` already said the arrays were
# different objects. An Array, a List or a Seq now identifies by its payload,
# as `===` does: the same for the life of the object however it changes, and
# distinct for two objects.
#
# …and two temporaries are two objects too. The identity was the payload's
# address, which the next body took once the first was gone, so the issue's
# last row, `(1,2).WHICH eq ('1','2').WHICH`, stayed True — as did
# `Foo.new.WHICH eq Foo.new.WHICH`, two Hashes, two Bufs, two Instants. Every
# such WHICH is a serial now, assigned to the body when first asked for and
# dropped with it.
#
# Found beside it: `(elem)` flattened the list it searched, so
# `@b (elem) (@b.item,)` was False and `2 ∈ (1, (2, 3))` True; it walks the
# list's own elements now, as Rakudo does.
#
# Contract: exit 0 + last line PASS. Every answer is Rakudo 2026.09's — run
# this file under `rakudo` too.
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# the issue's program
my @a = 1, 2;
my $w = @a.WHICH;
@a.push(3);
check 'stable after push', $w eq @a.WHICH, True;
@a = 3, 4;
check 'stable after assignment', $w eq @a.WHICH, True;
@a.unshift(0); @a.splice(1, 1); @a.pop; @a.shift; @a.append(5, 6); @a[10] = 1;
check 'stable after every mutator', $w eq @a.WHICH, True;
my @b = 1, 2;
my @c = 1, 2;
check 'two equal arrays differ', @b.WHICH eq @c.WHICH, False;
check '…as === says', @b === @c, False;
check '…and WHICH agrees with ===', @b.WHICH === @c.WHICH, False;
check 'two Set elements', Set.new(@b.item, @c.item).elems, 2;
check 'two Bag elements', bag(@b.item, @c.item, @b.item).elems, 2;
check '…one of them twice', bag(@b.item, @c.item, @b.item){@b.item}, 2;
my $p = (1, 2);
my $q = ('1', '2');
my $r = ('1 2',);
check '(1,2) is no ("1","2")', $p.WHICH eq $q.WHICH, False;
check '…nor ("1 2",)', $p.WHICH eq $r.WHICH, False;
check 'the issue\'s last row, with temporaries', (1, 2).WHICH eq ('1', '2').WHICH, False;
check 'two temporary Arrays', [1, 2].WHICH eq [1, 2].WHICH, False;
check 'one temporary a loop makes each time', (^3).map({ [$_].WHICH }).unique.elems, 3;

# the other identities had the same hole
class Foo { }
check 'two temporary objects', Foo.new.WHICH eq Foo.new.WHICH, False;
check 'two temporary Hashes', {a => 1}.WHICH eq {b => 2}.WHICH, False;
check 'two temporary Bufs', Buf.new(1).WHICH eq Buf.new(2).WHICH, False;
check 'two Promises', Promise.new.WHICH eq Promise.new.WHICH, False;
my $obj = Foo.new;
check 'an object keeps its WHICH', $obj.WHICH eq $obj.WHICH, True;
my $buf = Buf.new(1, 2);
my $bw = $buf.WHICH;
$buf.push(3);
check 'a Buf keeps its WHICH', $bw eq $buf.WHICH, True;
my %hh = a => 1;
my $hw = %hh.WHICH;
%hh = b => 2;
check 'a Hash keeps its WHICH through a refill', $hw eq %hh.WHICH, True;

# a List holding an Array keeps its WHICH while the Array grows
my $l = (1, 2, @a);
my $lw = $l.WHICH;
@a.push(9);
check 'a List is stable', $lw eq $l.WHICH, True;

# every way of reaching the same array is the same object
check '.item', @b.WHICH eq @b.item.WHICH, True;
check 'a scalar holding it', (my $x = @b).WHICH eq @b.WHICH, True;
sub through(@p) { @p.WHICH }
check 'a parameter', through(@b), @b.WHICH;
check 'a list element', (@b.item,)[0].WHICH, @b.WHICH;
check 'Empty is one object', Empty.WHICH eq slip().WHICH, True;

check 'an Array is an ObjAt', @a.WHICH.^name, 'ObjAt';
check 'a List is an ObjAt', (1, 2).WHICH.^name, 'ObjAt';
check 'a Seq is an ObjAt', (1, 2).map(* + 1).WHICH.^name, 'ObjAt';
check 'a Range is still a value', (1..3).WHICH eq (1..3).WHICH, True;
check 'a Capture is still a value', \(1, 2) === \(1, 2), True;
check '…its Array parts are not', \([1]) === \([1]), False;
check 'a Junction is still a value', any(1, 2).WHICH eq any(1, 2).WHICH, True;
my Int @t = 1, 2;
check 'a typed Array names its type', @t.WHICH.Str.starts-with('Array[Int]|'), True;

# what keys on identity
check '.unique keeps two equal arrays', ([1, 2], [1, 2]).unique.elems, 2;
check '…and folds one array met twice', (@b, @b).unique.elems, 1;
check '.repeated', ([1, 2], [1, 2]).repeated.elems, 0;
my %h{Any};
my $kb = @b.item;
my $kc = @c.item;
%h{$kb} = 'b';
%h{$kc} = 'c';
check 'an object hash keeps both', %h.elems, 2;
check '…and finds each', (%h{$kb}, %h{$kc}), ('b', 'c');

# (elem) asks the list's own elements
check '@b (elem) (@b.item,)', @b (elem) (@b.item,), True;
check '[1,2] (elem) ([1,2],)', [1, 2] (elem) ([1, 2],), False;
check '2 ∈ (1, (2, 3))', 2 ∈ (1, (2, 3)), False;
check '2 ∈ [1, [2, 3]]', 2 ∈ [1, [2, 3]], False;
check '3 ∈ (1, slip(2, 3))', 3 ∈ (1, slip(2, 3)), True;
check '4 ∈ a Seq', 4 ∈ (1..5).map(* + 0), True;
check '2 ∈ an Array', 2 ∈ [1, 2, 3], True;

.say for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
