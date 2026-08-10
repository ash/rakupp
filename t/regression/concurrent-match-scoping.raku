# Regression: concurrent regex matching corrupted the heap and crashed.
#
# `await ^30 .map: { start { S/.+/{$/.chars.print}/ given "abc" } }` segfaulted
# about one run in four — not a wrong answer, a SIGSEGV, usually inside
# Value::~Value where nothing was wrong. Two independent data races, both found
# with ThreadSanitizer (build-tsan) after the symptom pointed nowhere useful:
#
#   1. A string literal NFC-normalized itself IN PLACE on first eval, mutating
#      a std::string inside an AST node that every thread shares. Now normalized
#      at construction, so eval only reads (StrLit in Ast.h).
#   2. `$/` scoped outwards to the nearest routine frame — and a `start` block's
#      frame is parented to its lexical closure, which every worker shares. So N
#      threads assigned `$/` into ONE Env's std::map at once. A worker's
#      top-level block is now its own routine frame (forceRoutineFrame_), which
#      is also what the semantics want: a thread's match is its own.
#
# Neither needed threads to be asked for: v3.0.0 made parallel the default.
#
# The loop count matters. The original failed ~20-25% per round, so a single
# round proves nothing — 12 rounds would have caught it >90% of the time, and
# the whole file runs in well under a second.
#
# Contract: exit 0 + last line PASS.
my @fail;

# 1. The original: a substitution whose replacement reads $/, on 30 threads.
for ^12 -> $round {
    my @r = await ^30 .map: { start { S/.+/{ $/.chars }/ given "abc"; $/.chars } };
    @fail.push("round $round: substitution saw {@r.raku}, want 30 x 3")
        unless @r.elems == 30 && all(@r.map: * == 3);
}

# 2. The minimal form — a plain match, no substitution. This crashed too, which
#    is what showed the bug was not about S/// at all.
for ^12 -> $round {
    my @r = await ^30 .map: { start { "abc" ~~ /.+/; $/.chars } };
    @fail.push("round $round: plain match saw {@r.raku}, want 30 x 3")
        unless @r.elems == 30 && all(@r.map: * == 3);
}

# 3. Each thread must see ITS OWN match, not another's — the semantic half of
#    the $/ fix. Different subjects per thread, so a shared $/ shows up as a
#    wrong length rather than merely a race.
{
    my @r = await (1..20).map: -> $n {
        start { my $s = 'x' x $n; $s ~~ /x+/; $/.chars }
    };
    @fail.push("per-thread \$/ leaked: {@r.raku}") unless @r.List eqv (1..20).List;  # .List: eqv is type-aware, and @r is an Array
}

# 4. A string literal evaluated concurrently: the first race, on its own.
{
    my @r = await ^30 .map: { start { "a string literal".chars } };
    @fail.push("literal race: {@r.raku}") unless @r.elems == 30 && all(@r.map: * == 16);
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
