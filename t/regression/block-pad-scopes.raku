# Guard: an inline block's own `my`s live in a pad of the block's own
# (Block::padLayout), put on the block's scope at each entry. Before that, a
# `my` declared in an `if`, loop, `given`/`when` or bare block body was a map
# entry, and a loop over such variables ran 4-5x slower than the same loop
# over a sub's top-level `my`s. These are the scoping rules the block pads must
# keep: a closure keeps its own iteration's variable, nothing leaks from one
# iteration into the next, each recursive activation has its own block
# variables, shadowing works at every level, and `temp`, `MY::`, `EVAL`,
# `:=`, typed and native declarations, CATCH and LEAVE see them as before.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub closures {
    my @c;
    for 1..3 -> $i { my $x = $i * 10; @c.push({ $x }) }
    my $j = 0;
    while $j < 3 { my $y = $j; @c.push(-> { $y + 100 }); $j++ }
    @c.map({ .() }).join(',')
}
ck(closures(), '10,20,30,100,101,102', 'a closure keeps the loop iteration it was made in');

sub shadow-nested {
    my $x = 'outer';
    my @seen;
    if True { @seen.push($x); { my $x = 'inner'; @seen.push($x) }; @seen.push($x) }
    @seen.push($x);
    @seen.join(',')
}
ck(shadow-nested(), 'outer,inner,outer,outer', 'an inner block shadows; the outer name is back after it');

sub fresh-per-iteration {
    my @r;
    for 1..3 { my $acc; $acc //= 0; $acc += $_; @r.push($acc) }
    my $k = 0;
    while $k < 3 { my @a; @a.push($k); @r.push(@a.elems); $k++ }
    @r.join(',')
}
ck(fresh-per-iteration(), '1,2,3,1,1,1', 'nothing leaks from one iteration into the next');

sub nest($n) {
    if $n > 0 { my $here = $n; my $below = nest($n - 1); return $here ~ '[' ~ $below ~ ']' }
    'z'
}
ck(nest(3), '3[2[1[z]]]', 'each recursive activation has its own block variables');

our $g = 'G';
sub show { $g }
sub temps {
    my @r;
    for 1..2 { my $tag = "i$_"; temp $g = $tag; @r.push(show()) }
    @r.push(show());
    @r.join(',')
}
ck(temps(), 'i1,i2,G', '`temp` in a block with its own pad is undone at each exit');

sub given-when {
    my $v = 1;
    my @r;
    given 'a' {
        when 'a' {
            my $v = 2;
            { my $v = 3; @r.push($v) }
            @r.push($v);
            if True { my $w = $v + 10; @r.push($w) }
        }
    }
    @r.push($v);
    @r.join(',')
}
ck(given-when(), '3,2,12,1', 'given/when bodies: shadowing at three levels');

sub introspect {
    my @r;
    if True {
        my $p = 42;
        @r.push(MY::<$p>);
        @r.push(EVAL '$p + 1');
        @r.push(MY::.keys.grep(* eq '$p').elems);
    }
    @r.join(',')
}
ck(introspect(), '42,43,1', '`MY::` and `EVAL` see a block\'s own lexicals');

sub containers {
    my @r;
    if True { my @a = 1, 2; my $s := @a[0]; $s = 9; @r.push(@a.join('-')) }
    if True { my %h; %h<k> = 1; my $c = %h; $c<j> = 2; @r.push(%h.keys.sort.join) }
    @r.join(',')
}
ck(containers(), '9-2,jk', 'a block lexical bound with `:=` and shared through a reference');

sub typed {
    my @r;
    loop (my $i = 0; $i < 2; $i++) { my Int $t = $i * 2; @r.push($t) }
    my $q = 0;
    repeat { my int $n = $q; @r.push($n); $q++ } while $q < 2;
    if True { my Int $z; @r.push($z.^name) }
    @r.join(',')
}
ck(typed(), '0,2,0,1,Int', 'typed and native block lexicals in loop and repeat bodies');

sub phasers {
    my @r;
    {
        my $m = 'leave-saw';
        LEAVE @r.push($m);
        die 'boom';
        CATCH { default { my $e = .message; @r.push("caught $e") } }
    }
    @r.join(',')
}
ck(phasers(), 'caught boom,leave-saw', 'a CATCH and a LEAVE in a block with its own pad');

my $top = 'T';
my @mainline;
for 1..2 -> $i { my $mm = $top ~ $i; @mainline.push($mm) }
if True { my $only = 'block'; @mainline.push($only) }
ck(@mainline.join(','), 'T1,T2,block', 'block lexicals in the mainline');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
