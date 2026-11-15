# CONTRACT (Codex review 2026-11-14, finding 01): an object the cycle
# collector tracks may be released on several threads at once. A release that
# leaves it alive makes it a collector candidate; it used to drop its count
# FIRST and then buffer the body, so another thread's last release could free
# the body in between and the candidate buffer was handed freed memory. Now
# the candidate is noted while the releasing thread still holds its count.
#
# Each round shares one tracked object (a class whose attribute can hold an
# object, so it is not classified acyclic) with workers that take and drop
# copies while the owner drops its own: the last two releases land on
# different threads, again and again. Run it under an ASan build to see the
# old window: heap-use-after-free in bufferAdd. Dying is not allowed.
#
# In parallel mode a latch lines the releases up: every worker holds its copy
# before the owner lets go, and all of them drop at the same signal. Under the
# GIL no two releases overlap, and a spin would only wait on the lock.
my $R = (@*ARGS[0] // 3000).Int;
my $parallel = (%*ENV<RAKUPP_GIL> // '0') eq '0' && (%*ENV<RAKUPP_PARALLEL> // '1') ne '0';
class Node { has $.next is rw }
my $kept = 0;
for ^$R -> $r {
    my $o = Node.new;
    $o.next = Node.new if $r %% 2;          # some with an edge, some without
    # A body already waiting in the candidate buffer is untracked: its
    # releases skip the note. A collection takes it out alive and tracks it
    # again, so the workers' releases below are the ones that note it.
    $*VM.request-garbage-collection;
    my atomicint $ready = 0;
    my atomicint $go = 0;
    my @w = (^3).map: -> $k {
        start {
            my $c = $o;
            my $n = 0;
            if $parallel { atomic-fetch-inc($ready); Nil until atomic-fetch($go) }
            for ^20 { my $d = $c; $n++ if $d.defined }
            $c = Nil;                         # a worker's release…
            $n
        }
    };
    if $parallel { Nil until atomic-fetch($ready) == 3; atomic-assign($go, 1) }
    $o = Nil;                                 # …races the owner's
    $kept += [+] await @w;
}
say "released: {$kept} copies";
say 'PASS';
