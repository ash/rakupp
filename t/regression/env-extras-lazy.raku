# Regression: Env's rarely-used containers moved behind a lazily-allocated
# pointer (EnvExtras), taking sizeof(Env) from 256 to 72 bytes. An Env is built
# for every routine call AND every block, so eight container constructions per
# scope was pure overhead for the ordinary case.
#
# The risk is that a READ path now allocates (defeating the point) or, worse,
# looks at the wrong place. These are the features that live in those
# containers — each must behave exactly as before.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# rwLinks / rwSynced — `is rw` write-through to the caller's container
sub setit($x is rw) { $x = 42 }
my $a = 1; setit($a);
check($a.gist, '42', 'is rw writes through');

# the write must be visible MID-call, not just after it
sub midcall($x is rw, $probe) { $x = 7; $probe() }
my $b = 0; my $seen;
midcall($b, { $seen = $b });
check($seen.gist, '7', 'and is visible mid-call');

# rwDirect — an element bound to an rw param
my @arr = 1, 2, 3;
sub bump($e is rw) { $e = $e + 10 }
bump(@arr[1]);
check(@arr.gist, '[1 12 3]', 'an array element binds rw');

# tempRestores — `temp` restores on scope exit
my $t = 'orig';
sub tmp { temp $t = 'temped'; $t }
check(tmp(), 'temped', 'temp takes effect inside');
check($t, 'orig', 'and is restored on exit');

# varDefault — `is default` and the typed-container reset
my $d is default(7);
check($d.gist, '7', 'is default applies');
$d = 3; check($d.gist, '3', 'and can be assigned');
$d = Nil; check($d.gist, '7', 'Nil resets to the default');
my Int $typed;
check($typed.gist, '(Int)', 'a typed scalar starts as its type object');
$typed = 9; $typed = Nil;
check($typed.gist, '(Int)', 'and Nil resets it');

# varDynamic — `is dynamic` lookup through the caller chain
my $*dyn is dynamic = 5;
sub reader { $*dyn }
check(reader().gist, '5', 'is dynamic resolves dynamically');

# a scope with NONE of the above must still work (the lazy pointer stays null)
sub plain($n) { my $local = $n * 2; $local }
check(plain(21).gist, '42', 'an ordinary scope allocates no extras');
check(plain(1).gist,   '2', 'repeatably');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
