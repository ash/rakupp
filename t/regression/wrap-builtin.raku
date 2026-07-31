# Regression: `.wrap` on a BUILT-IN routine had no effect. `&dir` minted a fresh
# Callable on every evaluation, so `&dir.wrap(...)` pushed the wrapper onto a
# throwaway object — and even had it survived, a bare `dir(...)` call reaches the
# builtin through a direct table lookup that never consults a Callable at all.
# Now `&builtin` answers one cached Callable per name, and the three call sites
# that dispatch a bare call check it for wrappers first.
#
# File::Find's own test suite mocks `dir` exactly this way to exercise its
# error path, which is what surfaced this.
# Contract: exit 0 + last line PASS.
my @fail;

# the wrapper runs, and `callsame` reaches the real builtin
my $calls = 0;
my $h1 = &lc.wrap(sub ($s) { $calls++; callsame });
@fail.push("wrapper not called")     unless lc("AbC") eq 'abc';
@fail.push("call count: $calls")     unless $calls == 1;

# it can alter the result, and it can skip the builtin entirely
&lc.unwrap($h1);
my $h2 = &lc.wrap(sub ($s) { callsame().uc });
@fail.push('alter result')  unless lc("AbC") eq 'ABC';
&lc.unwrap($h2);
my $h3 = &lc.wrap(sub ($s) { "replaced" });
@fail.push('replace result') unless lc("AbC") eq 'replaced';

# unwrap restores the original
&lc.unwrap($h3);
@fail.push('unwrap restores') unless lc("AbC") eq 'abc';

# an UNWRAPPED builtin is untouched — the check must not leak between names
@fail.push('sibling builtin') unless uc("ab") eq 'AB';

# the same through a listy builtin, called with arguments
my $seen;
my $h4 = &dir.wrap(sub ($_) { $seen = $_; callsame });
my @got = dir(".");
@fail.push('dir wrapper saw the arg') unless $seen eq '.';
@fail.push('dir still returns entries') unless @got.elems > 0;
&dir.unwrap($h4);

# wrapping a USER sub keeps working
sub twice($n) { $n * 2 }
my $h5 = &twice.wrap(sub ($n) { callsame() + 1 });
@fail.push('user sub wrap') unless twice(5) == 11;
&twice.unwrap($h5);
@fail.push('user sub unwrap') unless twice(5) == 10;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
