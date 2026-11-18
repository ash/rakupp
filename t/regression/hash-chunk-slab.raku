# Regression guard: a hash's entry chunks come from the slab pool.
#
# Every object's attribute table and every small hash allocated its first
# chunk of entries with malloc and freed it with free: two calls and, on
# macOS, a clock read in the free, for each `Point.new` and each `my %h`.
# ChunkList (ValueHash.h) takes a chunk of up to the pool's largest block from
# SlabPool, by the chunk's own size, so a block freed by one hash is the next
# one's. What must stay true is that nothing else about a hash changed: it
# grows past its first chunk and past the pooled sizes, deletes and re-adds,
# copies, clears and nests as before, objects of one to many attributes keep
# their own, and blocks handed back by one size of hash are not another's.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }
# every size from empty to well past the pooled chunk sizes, built, shrunk and rebuilt
my @sums;
for 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 33, 64, 100, 257, 1000 -> $n {
    my %h;
    %h{"k$_"} = $_ for ^$n;
    my $total = [+] %h.values;
    %h{"k$_"}:delete for (^$n).grep(* %% 2);
    %h{"again$_"} = $_ * 10 for ^($n div 2);
    @sums.push: "$n:{$total}:{%h.elems}:{[+] %h.values}";
}
o @sums.join(' ');
# many short-lived small hashes, of mixed sizes, so freed blocks are reused
my $acc = 0;
for ^2000 -> $i {
    my %a = a => $i, b => 2;
    my %b = (^($i % 7)).map({ "x$_" => $_ });
    my %c = %a, c => 3;
    $acc += %a<a> + %b.elems + %c.elems;
    %a<z> = 1 if $i %% 3;
    $acc += %a.elems;
}
o $acc;
# copies are separate, nested hashes survive their parent
my %orig = one => 1, two => {inner => [1, 2, 3]};
my %copy = %orig;
%copy<one> = 100;  %copy<three> = 3;
my $inner = %orig<two>;
%orig = ();
o %orig.elems, ' ', %copy<one>, ' ', %copy.elems, ' ', $inner<inner>.sum;
# objects of one to many attributes, kept and dropped
class One { has $.a }
class Three { has $.a; has $.b; has $.c }
class Nine { has $.a; has $.b; has $.c; has $.d; has $.e; has $.f; has $.g; has $.h; has $.i }
class Late { has $.a; has %.extra }
my @kept;
for ^500 -> $i {
    my $o1 = One.new(a => $i);
    my $o3 = Three.new(a => $i, b => $i + 1, c => $i + 2);
    my $o9 = Nine.new(a => $i, e => $i * 2, i => $i * 3);
    my $l = Late.new(a => $i);
    $l.extra{$_} = $_ for ^($i % 5);
    @kept.push: $o3, $l if $i %% 50;
    $acc += $o1.a + $o3.c + $o9.i + ($o9.b // 0) + $l.extra.elems;
}
o $acc, ' ', @kept.elems, ' ', @kept.map({ $_ ~~ Three ?? .b !! .extra.elems }).join(',');
# a clone has its own attributes
my $t = Three.new(a => 1, b => 2, c => 3);
my $u = $t.clone(b => 20);
o $t.b, ' ', $u.b, ' ', $u.c;
# from several threads at once: a block freed on one thread is that thread's
my @per-thread = await (^4).map: -> $w { start { my $s = 0; for ^500 { my %h = a => $_, b => $w; $s += %h<a> + Three.new(a => $_).a }; $s } };
o @per-thread.join(' ');

my @want = q:to/WANT-END/.lines;
0:0:0:0 1:0:0:0 2:1:2:1 3:3:2:1 4:6:4:14 5:10:4:14 7:21:6:39 8:28:8:76 9:36:8:76 15:105:14:259 16:120:16:344 17:136:16:344 31:465:30:1275 33:528:32:1456 64:2016:64:5984 100:4950:100:14750 257:32896:256:97664 1000:499500:1000:1497500
2015662
0 100 3 6
2641412 20 1,0,51,0,101,0,151,0,201,0,251,0,301,0,351,0,401,0,451,0
2 20 3
249500 249500 249500 249500
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
