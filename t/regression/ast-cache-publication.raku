# Regression: the per-node eval caches published a pointer without publishing
# what it pointed at.
#
# Two caches on AST nodes that every thread shares:
#   * Binary::litVal — the literal operand of `$var OP literal`, built on first
#     evaluation and reused. Stored through DecidedOnce, i.e. a RELAXED atomic:
#     a reader could see the pointer without seeing the Value it addressed. On
#     x86 the store buffer usually hides that; arm64 is weakly ordered and does
#     not, and rakupp ships arm64 binaries.
#   * NumLit::ratCache — a Rat literal's BigInt parts. Worse: two plain
#     shared_ptrs, assigned concurrently, which races on the control block
#     itself and can corrupt a refcount rather than merely read a stale pointer.
#
# Both now go through PublishedOnce (Ast.h): release on publish, acquire on
# read, and a compare-exchange so a racing double-build has one winner and the
# loser frees its copy instead of leaking it.
#
# WHAT THIS FILE CAN AND CANNOT DO. A memory-ordering bug does not reliably
# produce a wrong answer — the values here were already correct when it was
# found. ThreadSanitizer is the real detector:
#
#     cmake --build build-tsan -j8
#     TSAN_OPTIONS="halt_on_error=1" build-tsan/rakupp -e \
#       'my @r = await ^8 .map: -> $i { start { $i + 1 } };'
#     TSAN_OPTIONS="halt_on_error=1" build-tsan/rakupp -e \
#       'my @r = await ^8 .map: { start { 3.14 + 0 } };'
#
# So what this guards is the BEHAVIOUR: every expression below is evaluated for
# the FIRST time by several threads at once — the exact window the caches are
# built in — and each must still produce the right value. Each expression is
# spelled differently on purpose, because a repeat of one is a second
# evaluation and no longer races.
#
# Contract: exit 0 + last line PASS.
my @fail;

# --- Binary::litVal: `$var OP literal`, first evaluated concurrently ---------
# Each block is a distinct AST node, so each one races on its own first eval.
{
    my @got = await (
        start { (^8).map({ $_ + 1  }).sum },   start { (^8).map({ $_ + 2  }).sum },
        start { (^8).map({ $_ + 3  }).sum },   start { (^8).map({ $_ + 4  }).sum },
        start { (^8).map({ $_ * 5  }).sum },   start { (^8).map({ $_ * 6  }).sum },
        start { (^8).map({ 7 + $_  }).sum },   start { (^8).map({ 8 * $_  }).sum },
    );
    my @want = 28+8, 28+16, 28+24, 28+32, 28*5, 28*6, 56+28, 8*28;
    @fail.push("litVal: got {@got.raku}, want {@want.raku}") unless @got eqv @want;
}

# --- and the shape where the literal is on the LEFT (fastShape 2) ------------
{
    my @got = await (
        start { 100 - 1 }, start { 200 - 2 }, start { 300 - 3 }, start { 400 - 4 },
    );
    @fail.push("literal-on-left: {@got.raku}") unless @got.List eqv (99, 198, 297, 396);
}

# --- NumLit::ratCache: Rat literals, first evaluated concurrently ------------
# Rats, not Nums: the cache only exists for the exact-rational path.
{
    my @got = await (
        start { 1.1 + 0 }, start { 2.25 + 0 }, start { 3.5 + 0 }, start { 4.75 + 0 },
        start { 0.1 + 0.2 }, start { 1.5 * 2 }, start { 3.14 + 0 }, start { 2.5 - 0.5 },
    );
    my @want = 1.1, 2.25, 3.5, 4.75, 0.3, 3.0, 3.14, 2.0;
    @fail.push("ratCache values: got {@got.raku}, want {@want.raku}") unless @got eqv @want;
    # The whole point of the Rat path is exactness, and the cache hands out the
    # SHARED BigInt parts — so a corrupted one would show up here.
    @fail.push("ratCache exactness: 0.1+0.2 != 0.3") unless (await start { 0.1 + 0.2 }) == 0.3;
    @fail.push("ratCache type: not a Rat") unless (await start { 1.1 + 0 }).WHAT.^name eq 'Rat';
}

# --- the same literal read by many threads AFTER it is cached ----------------
# The published pointer is now shared by everyone; every reader must see the
# same, complete value.
{
    my @got = await ^24 .map: { start { 6.25 + 0 } };
    @fail.push("shared Rat readers disagreed: {@got.unique.raku}")
        unless @got.elems == 24 && @got.unique.elems == 1 && @got[0] == 6.25;
    my @g2 = await ^24 .map: -> $i { start { $i + 9 } };
    @fail.push("shared literal readers: {@g2.raku}") unless @g2.List eqv (9..32).List;
}

# --- and the values are still right single-threaded --------------------------
@fail.push("single-threaded litVal") unless (^8).map({ $_ + 1 }).sum == 36;
@fail.push("single-threaded Rat")    unless 1.1 + 2.2 == 3.3;

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
