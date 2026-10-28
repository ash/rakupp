# A TYPED variable's alias holds its cell through a Proxy the variable's own
# scope keeps for reuse (scalarAliasOf's cache), so a scope whose typed
# variable holds an object pointing back at the scope — here a closure over
# it — is one more cycle through that cache. The collector walks the cache as
# it walks the scope's variables; unwalked, it would count as a holder from
# outside, and every such frame would leak.
use Test;
my atomicint $freed = 0;
class N {
    has &.f is rw;
    has @.pad;
    submethod DESTROY { $freed⚛++ }
}
sub touch($x is rw) { $x }
sub frame() {
    my N $n = N.new(pad => (^20).list);
    $n.f = { $n };    # the object holds a closure over this scope
    touch($n);        # …and the scope keeps the alias touch's parameter took
    my $m := $n;      # (and one more, through `:=`)
    $m.pad.elems
}
frame() for ^2000;
$*VM.request-garbage-collection for ^3;
ok $freed >= 1900, "a frame whose typed variable was aliased is freed ($freed of 2000)";

# …and one still reachable is not
my N $keep = N.new;
$keep.f = { $keep };
touch($keep);
$*VM.request-garbage-collection for ^3;
ok $keep.f.() === $keep, 'a live one keeps its object';
done-testing;
