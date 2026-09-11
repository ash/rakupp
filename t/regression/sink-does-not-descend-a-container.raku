# Regression: sinking a statement's value must NOT reach through a CONTAINER.
# Rakudo's sink does not descend a Scalar, so `$p;`, `$q = failing-proc();` and
# `@a[0];` are quiet where `failing-proc();` and `$p<>;` throw. rakupp sank the
# value in every shape, so a helper that ends in `$proc` — or in an assignment —
# detonated the moment a caller discarded its result.
#
# App::RaCoCo (Red's test dependency) is the reported case:
#     silently({ $actual = RunProc.new.run('not-exists', :!err) })
# `silently` calls the block as a statement, the block ends in an assignment,
# and three of its tests died on the Proc instead of reading its exitcode.
#
# The line between the two halves is where the value came from, not what it is:
#   - a bare variable / assignment / subscript      → contained, quiet
#   - a routine's IMPLICIT tail value               → decontainerized, sinks
#   - an explicit `return $p`, an `is raw` tail,
#     a BLOCK's tail value                          → contained, quiet
# Every row was checked against Rakudo. (Rakudo also prints an "unhandled
# Failure detected in DESTROY" warning for the two soft-Failure rows, at
# whatever moment it collects them — noise from the GC, not a verdict.)

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# A Proc that failed, with its exitcode already read (so nothing else is owed).
sub mkfail() { my $p = shell('exit 7', :!err, :!out); my $c = $p.exitcode; $p }
# A soft Failure, the other value sinkValue() acts on.
sub mkf()    { my $f = Failure.new(X::AdHoc.new(payload => 'BOOM')); $f }

# `b()` is a STATEMENT here, so whatever the block hands back is sunk.
sub sinker(&b) { b(); 'quiet' }
sub verdict(&b) { (try sinker(&b)) // 'threw' }

# --- contained: the sink stops at the Scalar ------------------------------
ck(verdict({ my $p = mkfail(); $p }),          'quiet', 'a bare variable is not sunk');
ck(verdict({ my $q; $q = mkfail(); }),         'quiet', 'nor is an assignment');
ck(verdict({ my $r = mkfail(); }),             'quiet', 'nor a declaration with an initializer');
ck(verdict({ my @a = mkfail(),; @a[0] }),      'quiet', 'nor a subscript');
ck(verdict({ my $p = mkfail(); ($p) }),        'quiet', 'parens keep the container');
ck(verdict({ my $p = mkfail(); do { $p } }),   'quiet', '…so does a do-block');
ck(verdict({ my $p = mkfail(); if 1 { $p } }), 'quiet', '…and an if-statement value');
ck(verdict({ my $p = mkfail(); 1 ?? $p !! 0 }),'quiet', '…and a ternary branch');
ck(verdict({ my $p = mkfail(); $p // 1 }),     'quiet', '…and a short-circuit operand');
ck(verdict({ my $f = mkf(); $f }),             'quiet', 'a Failure in a variable stays soft');
ck(verdict({ my $f = mkf(); my @a = $f,; @a[0] }), 'quiet', '…and one in a subscript');

# --- not contained: the value itself is what the statement produced -------
ck(verdict({ mkfail() }),                      'threw', 'a call result IS sunk');
ck(verdict({ my $p = mkfail(); $p.self }),     'threw', '…and so is a method call result');
ck(verdict({ my $p = mkfail(); $p<> }),        'threw', 'an explicit decont sinks what is inside');
ck(verdict({ (mkfail()) }),                    'threw', 'parens around a call do not help');
ck(verdict({ do { mkfail() } }),               'threw', '…nor a do-block');
ck(verdict({ if 1 { mkfail() } }),             'threw', '…nor an if-statement');
ck(verdict({ 1 ?? mkfail() !! 0 }),            'threw', '…nor a ternary');
ck(verdict({ Nil // mkfail() }),               'threw', '…nor a short-circuit');
ck(verdict({ 1 div 0 }),                       'threw', 'a soft-failing infix is a call, and sinks');
ck(verdict({ mkf() }),                         'threw', 'a returned Failure detonates');

# --- where the value came OUT of a routine --------------------------------
{
    sub tailVar()  { my $p = mkfail(); $p }
    sub tailAsgn() { my $q; $q = mkfail(); }
    sub retVar()   { my $p = mkfail(); return $p }
    sub retCall()  { return mkfail() }
    sub retRaw() is raw { my $p = mkfail(); $p }
    ck(verdict({ tailVar()  }), 'threw', "a sub's implicit tail value is decontainerized");
    ck(verdict({ tailAsgn() }), 'threw', '…even when the tail statement is an assignment');
    ck(verdict({ retVar()   }), 'quiet', 'an explicit `return $p` hands the container on');
    ck(verdict({ retCall()  }), 'threw', '…but `return CALL` has no container to hand on');
    ck(verdict({ retRaw()   }), 'quiet', 'an `is raw` routine returns the container');
    ck(verdict({ my $p = mkfail(); { $p } }), 'quiet', 'a bare block does not decontainerize');
}

# --- and a SUCCESSFUL Proc is quiet however it is sunk --------------------
ck(verdict({ shell('exit 0', :!err, :!out) }), 'quiet', 'exit 0 sinks quietly');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
