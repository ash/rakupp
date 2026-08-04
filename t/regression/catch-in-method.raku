# Regression: a CATCH inside a METHOD, 2026-08-04. `invokeMethod` had no CATCH
# handling at all and ran the block INLINE as an ordinary statement, so it fired
# with no exception in hand and its value became the method's. The same code in a
# `sub` had always worked — two copies of one path, and only callCallable ever
# got the feature. Every expectation checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — " ~ $got.raku ~ ' vs ' ~ $want.raku; $ok = False } }

my class C {
    method topic()     { my $t; CATCH { default { $t = $_.^name; return $t } }; die 'boom' }
    method ret()       { CATCH { default { return 'RET' } }; die 'boom' }
    method whenClause(){ CATCH { when X::AdHoc { return 'WHEN' } }; die 'boom' }
    method unmatched() { CATCH { when X::Numeric::Overflow { return 'NO' } }; die 'boom' }
    method noThrow()   { CATCH { default { return 'CAUGHT' } }; 'normal' }
    method nested()    { CATCH { default { return 'OUTER' } }; self!inner() }
    method !inner()    { die 'from inner' }
    method privCatch() { self!priv() }
    method !priv()     { CATCH { default { return 'PRIV' } }; die 'boom' }
}

# the topic IS the exception, and a `return` from the CATCH leaves the method
ck(C.new.topic,      'X::AdHoc', 'the CATCH topic is the exception');
ck(C.new.ret,        'RET',      'return from default leaves the method');
ck(C.new.whenClause, 'WHEN',     'and from a when clause');

# an unmatched CATCH rethrows rather than swallowing
ck((try C.new.unmatched).defined, False, 'an unmatched CATCH rethrows');

# with nothing thrown the CATCH must not run at all — it used to run inline and
# its value became the method's
ck(C.new.noThrow, 'normal', 'a CATCH does not run when nothing throws');

# it catches from further down the stack, and works in a private method too
ck(C.new.nested,    'OUTER', 'catches an exception from a callee');
ck(C.new.privCatch, 'PRIV',  'a private method gets the same handling');

# a plain sub is unchanged
{
    my sub f() { CATCH { default { return 'SUB' } }; die 'boom' }
    ck(f(), 'SUB', 'a sub still works');
}

say $ok ?? 'PASS' !! 'FAIL';
