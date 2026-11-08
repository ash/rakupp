# `%h{@b.item} = 1` stores ONE key, as a read of `%h{@b.item}` and a store
# through `%h{$x}` already did: the assignment's slice branch took any list a
# method call answered for many keys, itemized or not, so it stored two. An
# object hash keeps the array itself as the key.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @b = 1, 2;
my %h; %h{@b.item} = 1;
ck %h.elems, 1, '%h{@b.item} = 1 is one key';
ck %h.keys.List, ('1 2',), '…its Str';
my %o{Any}; %o{@b.item} = 1;
ck %o.keys[0] === @b, True, 'an object hash keeps the array';
my %u; %u{(1, 2).item} = 5;
ck %u, { '1 2' => 5 }, 'an itemized List';
my @a; @a[@b.item] = 1;
ck @a, [Any, Any, 1], 'an array index takes its count (as before)';

# a list a method answers is still a slice
my %s; %s{@b.list} = 7, 8;
ck %s, { 1 => 7, 2 => 8 }, '.list';
my %t; %t{@b.Array} = 3, 4;
ck %t, { 1 => 3, 2 => 4 }, '.Array';
my %v; %v{@b.map(* + 1)} = 1, 2;
ck %v, { 2 => 1, 3 => 2 }, '.map';
my %k; %k{$@b} = 3;
ck %k, { '1 2' => 3 }, '$@b (as before)';

say 'PASS';
