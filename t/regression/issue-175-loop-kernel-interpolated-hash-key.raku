# Regression: issue #175 (2026-11-10).
#
# `my %h; my $i = 0; while $i < 4 { %h{"k$i"} = $i; $i++ }` crashed (SIGSEGV)
# in the interpreter's loop kernel on a GCC 15 -O3 build: it read an
# interpolated part's type before computing it (fixed in 079427c0). The shapes
# of the issue's shrink matrix, each run by the kernel and checked here.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}
sub pairs(%h) { %h.sort.map({ .key ~ '=' ~ .value }).join(',') }

my %h; my $i = 0; while $i < 4 { %h{"k$i"} = $i; $i++ }
check pairs(%h), 'k0=0,k1=1,k2=2,k3=3', '%h{"k$i"} = $i in a while';
my %a; $i = 0; while $i < 4 { %a{"$i"} = $i; $i++ }
check pairs(%a), '0=0,1=1,2=2,3=3', '%h{"$i"} = $i';
my %b; $i = 0; while $i < 4 { %b{"k$i"} = "v$i"; $i++ }
check pairs(%b), 'k0=v0,k1=v1,k2=v2,k3=v3', '%h{"k$i"} = "v$i"';
my %c; $i = 0; while $i < 3 { %c{"k{$i}x$i"} = $i * 10; $i++ }
check pairs(%c), 'k0x0=0,k1x1=10,k2x2=20', 'two interpolated parts';
my %d; $i = 0; while $i < 3 { %d{'k' ~ $i} = $i; $i++ }
check pairs(%d), 'k0=0,k1=1,k2=2', Q[%h{'k' ~ $i} = $i];
my %e; loop (my $j = 0; $j < 2000; $j++) { %e{"k$j"} = $j }
check (%e.elems, %e<k0>, %e<k1999>), (2000, 0, 1999), '2000 keys through a loop (…)';
my %f; $i = 0; while $i < 4 { %f{"k$i"} += $i; %f{"k$i"} += 1; $i++ }
check pairs(%f), 'k0=1,k1=2,k2=3,k3=4', '%h{"k$i"} += $i';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
