# Regression: `ValueList` is no longer `std::vector<Value>` but `RVec<Value>`
# (src/ValueVec.h), a container that grows by RELOCATING its elements — one
# memcpy of the whole buffer — instead of move-constructing them one at a time.
# The licence is that a `Value` is trivially relocatable; the risk is that a
# hand-written container gets the corner cases wrong where `std::vector`'s
# twenty years of use got them right. Every case below is one of those corners,
# reached from Raku:
#
#   * a push whose ARGUMENT lives in the array being pushed to (`@a.push(@a[0])`)
#     — the reallocation frees the argument's storage unless the new element is
#     built into the NEW buffer first;
#   * splice/unshift/shift, which open and close holes in the middle;
#   * growth across many reallocations, so the memcpy path runs repeatedly;
#   * arrays of Str, whose inline `std::string` is the one member that is NOT
#     bitwise-relocatable on every standard library (libstdc++ points a short
#     string at its own buffer) — `bitwiseRelocOk()` picks the safe path there,
#     and this case must pass identically either way;
#   * nested arrays and hashes, so the relocated bytes carry live refcounts.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($name, $got, $want) {
    @fail.push("$name: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# 1. self-referential push, at exactly the sizes where the buffer doubles.
for 1, 2, 4, 8, 16, 32, 64 -> $n {
    my @a = ^$n;
    @a.push(@a[0]);
    @a.push(@a[*-2]);
    check("self-push at $n", @a.elems, $n + 2);
    check("self-push value at $n", @a[*-1], @a[$n - 1]);
}

# 2. growth over many reallocations, then read every element back.
my @g;
@g.push($_ * 7) for ^5000;
check('grown elems', @g.elems, 5000);
check('grown sum', @g.sum, (^5000).map(* * 7).sum);
check('grown last', @g[*-1], 4999 * 7);

# 3. Str elements — the non-trivially-relocatable member.
my @s;
@s.push("item-$_-" ~ 'x' x ($_ % 90)) for ^2000;   # spans inline, mid-band and promoted
check('str elems', @s.elems, 2000);
check('str first', @s[0], "item-0-");
check('str 1500', @s[1500], "item-1500-" ~ 'x' x 60);
check('str joined chars', @s.join('').chars, (^2000).map({ "item-$_-".chars + $_ % 90 }).sum);

# 4. holes: splice, unshift, shift, pop — before and after a reallocation.
my @h = 1 .. 10;
@h.splice(3, 2, 'a', 'b', 'c');
check('splice', @h.join(','), '1,2,3,a,b,c,6,7,8,9,10');
@h.unshift('z');
check('unshift', @h[0], 'z');
check('shift', @h.shift, 'z');
@h.splice(0, 3);
check('splice head', @h.join(','), 'a,b,c,6,7,8,9,10');
my @big = ^100;
@big.splice(50, 0, |(1000 .. 1099));       # grows across the hole
check('big splice elems', @big.elems, 200);
check('big splice seam', @big[49, 50, 149, 150].join(','), '49,1000,1099,50');

# 5. relocated bytes carrying live refcounts: nested arrays and hashes.
my @n;
for ^500 -> $i { @n.push([$i, $i + 1, [$i * 2]]) }
check('nested elems', @n.elems, 500);
check('nested deep', @n[499][2][0], 998);
my @m;
for ^500 -> $i { @m.push({ k => $i, v => [$i, $i] }) }
check('hash-in-array', @m[400]<v>[1], 400);

# 6. an array aliased into another Value survives its source growing.
my @src = 1, 2, 3;
my $alias = @src;          # binds the same element vector
@src.push(4);
check('alias sees growth', $alias.elems, 4);
my @copy = @src;           # a real copy
@src.push(5);
check('copy does not', @copy.elems, 4);

# 7. sort / reverse / map over a grown array — iterator arithmetic on the
#    raw-pointer iterators the container now hands out.
my @r = (1 .. 3000).pick(*);
check('sorted', @r.sort.List, (1 .. 3000).List);
check('reversed', @r.sort.reverse[0], 3000);
check('mapped', @r.sort.map(* + 1)[0], 2);

if @fail {
    .say for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
