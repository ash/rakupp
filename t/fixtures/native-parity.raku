# Native (--exe) parity probes: things that once diverged between the
# interpreter and natively-compiled binaries (t/run.raku "native parity").

# 1. deep recursion: the exe main thread needs a real stack, not the OS
#    default (macOS ld -stack_size / Windows /STACK) — depth 30k overflows
#    a default main stack in native code long before the guard fires
sub deep(Int $n) { $n == 0 ?? 0 !! 1 + deep($n - 1) }
say "deep: ", deep(30_000);

# 2. a caught builtin error answers .message (exceptionFor builds a real
#    exception object; a bare type payload would die inside CATCH and mask
#    the original error)
{
    42.nosuchmethod;
    CATCH { default { say "caught: ", .message } }
}

# 3. a block-final if/elsif/else chain is the pointy block's value
my &pick = -> $x {
    if    $x == 1 { "one" }
    elsif $x == 2 { "two" }
    else          { "many" }
};
say "pick: ", pick(1), " ", pick(2), " ", pick(3);

# 4. sort with a comparator returning hand-built Order values (the enum
#    numerics must survive native name-term resolution)
my @s = (5, 2, 9, 1).sort(-> $x, $y { $x < $y ?? Less !! ($x > $y ?? More !! Same) });
say "sort: ", @s.join(",");

# 5. SHAPED ARRAYS. `my @a[3;2]` was not implemented in the native backend at
#    all: it compiled without a word and then answered `(*)` to .shape and
#    nonsense to `@a[1;1]`. The declaration builds a shaped container now, and
#    a multi-dim subscript goes through the same AT-POS/ASSIGN-POS the
#    interpreter walks.
my @m[3;2] = (1..6).rotor(2);
say "shape: ", @m.shape, " elems: ", @m.elems, " at: ", @m[1;1];
@m[1;0] = 99;
@m[0;1] += 10;
say "raku: ", @m.raku;
my @flat = @m;                        # a shaped source contributes its LEAVES
say "flat: ", @flat.elems, " ", @flat.join(",");
my @g[2;3];
my $c = 0;
for ^2 -> $y { for ^3 -> $x { $c++; @g[$y;$x] = $c } }
say "grid: ", @g.raku, " sum: ", @g.sum;
my $n = 2;
my @dyn[$n;$n] = (1,2),(3,4);         # dimensions may be expressions
say "dyn: ", @dyn.shape, " ", @dyn[1;1];

# 6. `,=` — the assignment metaop over `infix:<,>` (issue #85). The native
#    backend had no case for it: it reached applyArith and died "Unsupported
#    operator ','". Both backends call the same rtCommaAssign now, so `A ,= B`
#    is `A = A, B` here exactly as it is in the interpreter.
my %cm = 1 => 2, 2 => 3;
%cm ,= 5 => 4;
say "comma-hash: ", %cm.elems, " ", %cm{'5'};
my @ca = 1, 2;
@ca ,= 3;
say "comma-array: ", @ca.elems, " ", @ca[1];
my %ce = a => 1;
%ce<b> ,= 9;                          # …and on a subscript, which binds the slot once
say "comma-slot: ", %ce<b>.elems, " ", %ce<b>[1];

# 7. HASH CONSTRUCTION. rtCoerceHash was a second, thinner reading of what a
#    list means as a hash, so three answers were wrong in a compiled binary and
#    nowhere else: `my %c = %b` handed back %b's own map (a write through %c
#    changed %b), a Hash inside the list became a stringified KEY instead of
#    contributing its pairs, and a flat key/value list mixed with pairs went out
#    of step. It delegates to the interpreter's coerceHash now.
my %hb = a => 1, b => 2;
my %hc = %hb;
%hc<z> = 9;
say "hash-copy: ", %hb.elems, " ", %hc.elems;
my %hd = (%hb, c => 3);
say "hash-nest: ", %hd.elems, " ", %hd<a>, %hd<c>;
say "hash-kv: ", (my %he = flat %hb.kv).elems;

# 8. A SELF-REFERENTIAL container renders as a cycle, not as 512 levels of
#    nesting: `.raku` always did, `.gist` did not.
my @cyc = 1, 2;
@cyc[0] = @cyc;
say "cycle: ", @cyc.gist, " ", @cyc.gist.chars < 100;
#    …and .Str, which lives here rather than in t/regression/ because Rakudo
#    hangs on it and that file is run by both engines.
say "cycle-str: ", @cyc.Str.chars < 100;

# 9. A `$` parameter, placeholder or loop variable holds an Array or a Hash as
#    ONE item, and list assignment keeps an itemized Array whole: compiled,
#    `sub f($d) { my @y = $d }` spread [1, 2, 3] into three elements.
sub item-pos($d) { my @y = $d; @y.elems }
sub item-named(:$d) { my @y = $d; @y.elems }
sub item-default($d = [1, 2, 3]) { my @y = $d; @y.elems }
sub item-raku(:$d) { $d.raku }
say "item-param: ", item-pos([1, 2, 3]), item-pos({a => 1, b => 2}), item-named(d => [1, 2, 3]),
    item-default(), { my @y = $^a; @y.elems }([1, 2, 3]), " ", item-raku(d => [1, 2]);
my $ia = [1, 2, 3];
my @ib = $ia;
my @ic = |$ia;
my @id = @($ia);
say "item-assign: ", @ib.elems, @ic.elems, @id.elems;
my @rows = [1, 2], [3, 4];
my $by-var = '';
for @rows -> $r { my @y = $r; $by-var ~= @y.elems }
my $by-topic = '';
for @rows { my @y = $_; $by-topic ~= @y.elems }
say "item-loop: ", $by-var, " ", $by-topic;
#    …and a `%` parameter binds the Hash an item holds, not the item
sub hash-param(%h) { my @y = %h; @y.elems }
my $ih = {a => 1, b => 2};
say "item-hash: ", hash-param($ih);

# 10. A loop parameter with a trait, a type or a default binds through a
#     signature (`-> $x is copy`, `-> Int $a`, `-> $a, $b = 0`): compiled,
#     those names were never declared and the C++ did not build at all.
my $loop-copy = '';
for 1, 2, 3 -> $x is copy { $x *= 10; $loop-copy ~= "$x " }
my $loop-typed = '';
for 1, 2, 3, 4 -> Int $a, $b is copy { $b++; $loop-typed ~= "$a/$b " }
my $loop-default = '';
for 1, 2, 3 -> $a, $b = 0 { $loop-default ~= "$a+$b " }
my @loop-subs;
for 1, 2 -> $x is copy { $x += 5; @loop-subs.push: { $x } }
say "loop-param: ", $loop-copy, "| ", $loop-typed, "| ", $loop-default, "| ", @loop-subs.map({ .() }).join(',');
