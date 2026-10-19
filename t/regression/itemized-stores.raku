# Regression: plain `=` into a `$` variable or a hash or array element
# stores a List, Array or Hash as ONE item, and some other stores did not:
# `//=`, `||=`, `.push` of a Hash onto a new key, and a `Hash()` append
# merge stored the bare value, so `my @a = %h<a>` flattened it. webraku's
# examples/wavelet/Analysis.rakumod does `%h<a> //= …`, then
# `my @a = %h<a>`.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my %h; %h<a> //= (1, 2, 3); my @a = %h<a>;
ck(@a.elems, 1, '%h<a> //= LIST is one item');
ck(%h<a>.raku, '$(1, 2, 3)', 'and shows as one');
my $s; $s //= (1, 2, 3); my @b = $s;
ck(@b.elems, 1, '$s //= LIST is one item');
my $t; $t ||= [1, 2]; my @t = $t;
ck(@t.elems, 1, '$t ||= ARRAY is one item');
my %o; %o<x> //= {a => 1}; my @o = %o<x>;
ck(@o.elems, 1, '%h<x> //= HASH is one item');
my @e; @e[0] //= (1, 2);
ck(@e[0].raku, '$(1, 2)', '@a[0] //= LIST was already one');
my %p; %p.push: (b => {z => 1, y => 2}); my @c = %p<b>;
ck(@c.elems, 1, '.push of a Hash onto a new key is one item');
ck(%p<b>.raku, '${:y(2), :z(1)}', 'and shows as one');
%p.push: (c => [1, 2]);
ck(%p<c>.raku, '$[1, 2]', 'an Array pushed onto a new key was already one');
my Hash() %m = a => {x => 1}; %m.append: %(a => {y => 2}); my @d = %m<a>;
ck(@d.elems, 1, 'a Hash() append merge is one item');
ck(%m<a>.keys.sort.List, ('x', 'y'), 'holding both');
my %k; %k<n> //= 5; ck(%k<n>, 5, 'a plain value is unchanged');
my %v; %v<l> = (1, 2); ck(%v<l>.raku, '$(1, 2)', 'plain = is unchanged');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
