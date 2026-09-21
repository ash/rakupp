# Regression: an UNDECLARED dynamic is X::Dynamic::NotFound, not `Any`.
#
# rakupp used to answer `Any` for any `*`-twigil name nothing declared, and to
# MINT A SLOT when one was assigned to — so a misspelt dynamic was silently a
# brand-new variable, and `$*OS` (removed from the language) read as undefined
# instead of saying so. Rakudo draws a three-way distinction this pins down:
#
#   read   — an ARMED FAILURE. Soft on purpose: `.defined`, `if`, `//`, `with`
#            and `so` are how a program probes for an optional dynamic, and all
#            of them stay quiet; only USING the value detonates.
#   assign — throws outright.
#   temp   — throws too, because `temp $*x = 1` assigns through the same lvalue.
#
# Every expectation is the Rakudo 2026.08 answer, probed side by side — which
# is why each group below uses a FRESH name. Rakudo MEMOISES one Failure per
# undeclared name (`$*X.WHICH` is stable across reads), so probing `$*X` once
# marks that one object handled and no later read of `$*X` detonates. rakupp
# mints a fresh armed Failure per read, so it throws where Rakudo has gone
# quiet. Reusing a name here would test that difference by accident instead of
# the behaviour above; the difference itself is left alone deliberately —
# memoising a Failure forever, per name, to make a second read softer is a
# Rakudo implementation artifact, not something the language asks for.
#
# Roast asserts two of these directly: S32-exceptions/misc.t (assign) and
# S04-blocks-and-statements/temp.t (temp).

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }
sub throws-notfound(&c --> Str) {
    my $name = '';
    try { c(); CATCH { default { $name = .^name } } }
    $name;
}

# --- the read is a Failure, and it is SOFT ----------------------------------

ok($*SOFT-A ~~ Failure,            'a read of an undeclared dynamic is a Failure');
ok(!$*SOFT-B.defined,              '…and .defined is False without detonating');
ok(!(?$*SOFT-C),                   '…and it boolifies False');
ok(($*SOFT-D // 'dflt') eq 'dflt', '…and // reaches the default');
ok(!(so $*SOFT-E),                 '…and `so` is quiet');
my $probed = 'no'; with $*SOFT-F { $probed = 'yes' }
ok($probed eq 'no',                '…and `with` takes the else branch');
ok(@*SOFT-ARR ~~ Failure,          'the @ sigil answers a Failure too');
ok(%*SOFT-HASH ~~ Failure,         '…and the % sigil');

# the exception it carries, and the name on it — Rakudo's spelling, sigil and
# twigil included, with the message composed from it
my $ex = $*NAMED-A.exception;
ok($ex ~~ X::Dynamic::NotFound, 'the Failure carries X::Dynamic::NotFound');
ok($ex ~~ Exception,            '…which is an Exception (mro: NotFound, Exception, Any, Mu)');
ok($ex.name eq '$*NAMED-A',     '…and .name is the full spelling, sigil and twigil included');
ok($ex.message eq 'Dynamic variable $*NAMED-A not found',
                                '…and the message reads as Rakudo renders it');

# --- …but USING an untouched one detonates ----------------------------------

ok(throws-notfound({ my $x = $*USE-A + 1 })  eq 'X::Dynamic::NotFound',
   'using the value throws X::Dynamic::NotFound');
ok(throws-notfound({ my $s = "$*USE-B" })    eq 'X::Dynamic::NotFound',
   'interpolating it throws too');

# --- assignment and temp throw ----------------------------------------------

ok(throws-notfound({ $*AN-UNDECLARED-DYNVAR = 42 }) eq 'X::Dynamic::NotFound',
   'assigning to an undeclared dynamic throws');
ok(throws-notfound({ temp $*ALSO-UNDECLARED = 42 }) eq 'X::Dynamic::NotFound',
   '`temp` of an undeclared dynamic throws');
ok(throws-notfound({ %*UNDECLARED-HASH<k> = 1 })    eq 'X::Dynamic::NotFound',
   '…and so does assigning through a subscript on one');

# --- what must NOT throw ----------------------------------------------------
# A declared dynamic, the caller chain, and the engine's own dynamics all
# behave as before. `$*CWD = …` is the case the exemption list exists for: the
# engine synthesizes it on read, so lvalue() finds no container for it and
# would otherwise have started throwing.

my $*DECLARED = 7;
ok($*DECLARED == 7,       'a declared dynamic still reads');
sub sees-callers { $*DECLARED }
ok(sees-callers() == 7,   '…and is still visible down the caller chain');
my $*LATER; $*LATER = 9;
ok($*LATER == 9,          'assigning to a declared-but-unset dynamic still works');
ok($*PROGRAM.defined,     'an engine-provided dynamic still reads');
ok($*KERNEL.defined,      '…and another');
my $cwd = $*CWD;
ok(throws-notfound({ $*CWD = $cwd }) eq '',
   'assigning to an engine-provided dynamic does not throw');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
