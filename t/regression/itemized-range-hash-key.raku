# `(1..2).item` is itemized, so `%h{(1..2).item} = 1` stores one key, "1 2",
# as `%h{$(1..2)}` and Rakudo do: `.item` itemized only an Array or a Hash,
# and the slice branch of the assignment took the Range for two keys.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my %x; %x{(1..2).item} = 1;
ck %x, { '1 2' => 1 }, '%x{(1..2).item} = 1';
ck %x{(1..2).item}, 1, '…and read back';
my %y; %y{$(1..2)} = 1;
ck %y, { '1 2' => 1 }, '$(1..2) (as before)';
my %z; %z{1..2} = 3, 4;
ck %z, { 1 => 3, 2 => 4 }, 'a bare Range is a slice (as before)';
ck (1..2).item.elems, 2, '.elems';
my @seen;
for (1..3).item { @seen.push($_) }
ck @seen.elems, 1, 'for over it runs once';

say 'PASS';
