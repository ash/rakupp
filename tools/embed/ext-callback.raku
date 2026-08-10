# The A1 gate, Raku side: drives tools/embed/callback-ext.c, which calls back
# into this file. Run by tools/embed-smoke.raku, which compiles the extension
# first and passes its path; not in t/regression/ because that suite is
# deliberately runnable without a C compiler.
#
# Contract: exit 0 + last line PASS.

my $lib = @*ARGS[0] or die "usage: ext-callback.raku <path-to-extension>";
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# The routines the extension reaches back for. Ordinary subs in the scope that
# loads it — that is the whole point: rk_call resolves the way the call site
# would, so a native fast path can defer to the module that installed it.
sub twice($n) { $n * 2 }

class X::Callback::Boom is Exception {
    method message { "boom from Raku" }
}
sub explode() { die X::Callback::Boom.new }

my &ext-load = try &::('rakupp-ext-load');
die "this rakupp has no rakupp-ext-load builtin" unless &ext-load;
ext-load($lib);

# `&::(…)`, not a bare call: the loader installs these at RUN time, and a bare
# name would have to resolve at compile time.
my &call-named    = &::('ext-call-named');
my &call-value    = &::('ext-call-value');
my &map-sum       = &::('ext-map-sum');
my &ext-can       = &::('ext-can');
my &catches       = &::('ext-catches');
my &propagates    = &::('ext-propagates');
my &calls-missing = &::('ext-calls-missing');
my &root-keep     = &::('ext-root-keep');
my &root-read     = &::('ext-root-read');
my &root-release  = &::('ext-root-release');

# --- rk_call by name -------------------------------------------------------
check call-named(21), 43, "rk_call reaches a Raku sub by name (twice(21)+1)";

# --- rk_call_value on a Code argument --------------------------------------
check call-value(-> $x { $x * 10 }, 5), 50, "rk_call_value invokes a passed block";
check call-value(&twice, 8), 16, "rk_call_value invokes a passed &sub";

# --- a callback per element: the shape ABI 1 could not express --------------
check map-sum(-> $x { $x * $x }, [1, 2, 3, 4]), 30, "native loop calling Raku per element";

# --- rk_can ----------------------------------------------------------------
check ext-can("twice"), True, "rk_can sees a routine that exists";
check ext-can("definitely-not-here"), False, "rk_can refuses one that does not";

# --- the error contract: handled -------------------------------------------
check catches().contains("boom from Raku"), True,
      "a Raku exception inside rk_call comes back as a pending error";
check calls-missing().contains("no-such-routine-anywhere"), True,
      "calling a routine that does not exist is an error, not a crash";

# The extension went on to return a value after clearing — so a handled failure
# must leave nothing behind that re-raises later.
check call-named(1), 3, "the context still works after rk_clear_error";

# --- the error contract: propagated, with its type intact ------------------
my $caught = Nil;
try {
    propagates();
    CATCH { default { $caught = $_ } }
}
check ($caught ~~ X::Callback::Boom).Bool, True,
      "an uncleared failure re-raises the ORIGINAL exception type at the call site";
check ($caught.defined ?? $caught.message !! ''), "boom from Raku",
      "…with its message";

# --- rooted handles: a value that outlives its call ------------------------
root-keep("kept across calls");
check root-read(), "kept across calls", "a rooted handle survives into the next call";
root-keep("replaced");
check root-read(), "replaced", "rooting again releases the previous root";
root-release();
check root-read().defined, False, "after rk_unroot there is nothing left to read";
# rk_any() produces the same VT::Any an empty slot holds, and `=== Any` on it
# was False until this gate turned that up — the ABI-visible face of it being
# that every JSON null from a native parser failed the idiom used to test for
# one. Asserted here because this is where it would come back.
check (root-read() === Any), True, "an rk_any() value is identical to Any";

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
