# t/regression/exe-pair-containers-through-a-module.raku: Getopt::Long's
# shape (issue #255) — the caller hands `"name" => $var` Pairs, the module
# binds each Pair's container into a Hash, and an object writes through it.
unit module RakuppOptionStore;
my class Store {
    has Hash $.values is required;
    has $.key;
    method put($v) { $!values{$!key} = $v }
    method get()   { $!values{$!key} }
}
sub store-options(@args, *@pairs) is export {
    my %bound;
    for @pairs { %bound{.key} := .value }
    for @args.kv -> $i, $v { Store.new(values => %bound, key => @pairs[$i].key).put($v) }
    Store.new(values => %bound, key => @pairs[0].key).get
}
sub args-seen() is export { @*ARGS.raku }
sub args-via-capture(|c) is export { store-options(@*ARGS, |c) }
