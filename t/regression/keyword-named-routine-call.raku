# Regression: a routine the program names after a block prefix (`start`, `do`,
# `try`, `gather`, `quietly`, `once`, `react`, `supply`) is CALLED, with its
# arguments, when its name is glued to a paren; written with a space or before
# a block, the word is still the prefix.
#
# The parser read `start(5)` as the prefix with the thunk `(5)`, and handed the
# user's `start` that Block: `sub start($x) { $x + 1 }; say start(5)` said 1,
# and a two-parameter `start` died "Calling start(Block) will never work".
# `do(5)`, `try(5)`, `quietly(5)` and `once(5)` were the prefix and never
# called the routine; `gather(5)` was an empty list. A routine declared further
# down was not seen at all, which `sub if` / `sub repeat` shared. Found
# 2026-10-24 by a script that named its block `&start`; fixed in the parser
# (kNeedsEndKeyword, the token pre-scan in parseProgram).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- glued to the paren: the routine, with its arguments ----
{
    sub start($x) { $x + 1 }
    ck start(5), 6, '`start(5)` calls the program\'s `sub start`';
    my $p = start { 7 };
    ck $p.^name, 'Promise', '…and `start { … }` is still the prefix';
    ck await($p), 7, '…which runs its block';
    my $q = (start 8);
    ck await($q), 8, '…and so is `start EXPR`, with a space';
}
{
    my &start = -> $a, $b { $a * $b };
    ck start(3, 4), 12, 'a `my &start` is called with both arguments';
}
{
    sub start(*@a, :$n = 0) { @a.elems + $n }
    ck start(1, 2, 3), 3, 'a slurpy `start` gets every argument';
    ck start(n => 4), 4, '…and a named one';
}
{
    sub do($x) { $x + 1 }
    ck do(5), 6, '`do(5)` calls `sub do`';
    ck (do { 5 }), 5, '…and `do { … }` is still the prefix';
}
{
    sub try($x) { $x + 1 }
    ck try(5), 6, '`try(5)` calls `sub try`';
    ck (try { die "x"; 1 }) // 'caught', 'caught', '…and `try { … }` still catches';
}
{
    sub gather($x) { $x + 1 }
    ck gather(5), 6, '`gather(5)` calls `sub gather`';
    ck (gather { take 1; take 2 }).List, (1, 2), '…and `gather { … }` still gathers';
}
{
    sub quietly($x) { $x + 1 }
    sub once($x) { $x + 2 }
    ck quietly(5), 6, '`quietly(5)` calls `sub quietly`';
    ck once(5), 7, '`once(5)` calls `sub once`';
}
{
    sub supply($x) { $x + 1 }
    ck supply(5), 6, '`supply(5)` calls `sub supply`';
    my $s = supply { emit 1; emit 2 };
    ck $s.list, (1, 2), '…and `supply { … }` is still a Supply';
}
{
    sub react($x) { $x + 1 }
    ck react(5), 6, '`react(5)` calls `sub react`';
    my @seen;
    react { whenever Supply.from-list(3, 4) { @seen.push($_) } }
    ck @seen.List, (3, 4), '…and `react { … }` still reacts';
}

# ---- declared further down ----
{
    ck later(), 11, 'a `start` declared below its call is called';
    sub later() { start(10) }
    sub start($x) { $x + 1 }
}
{
    ck repeat(20), 21, '…and so is a `repeat`, a statement keyword';
    multi sub repeat($x) { $x + 1 }
}

# ---- a method of the name is a method ----
{
    class C { method start($x) { $x + 100 } }
    ck C.new.start(5), 105, '`.start(5)` is the method';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
