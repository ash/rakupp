# Three bugs that between them made TLS unusable, all found by running
# IO::Socket::Async::SSL under rakupp (HTTP::Simple's t/05-tls.t).
#
#   * `sub f() returns T` where T is a `constant` died "Type 'T' is not
#     declared" the moment the routine RETURNED — the return check consulted
#     classes/subsets/native names but never the routine's own closure, where
#     the constant lives. The identical constraint on a PARAMETER bound fine,
#     because that path treats a name it cannot resolve as one it cannot
#     enforce. `IO::Socket::Async::SSL` declares `sub get_dh2048() returns DH`
#     with `my constant DH = Pointer`, so no TLS server could start at all.
#
#   * `orwith` after an `if`/`elsif` was parsed as a SEPARATE statement, so it
#     ran even when an earlier branch had been taken — the chain was not a
#     chain. The SSL client dispatches on
#     `if !$!ssl {} elsif … {} orwith $!connected-promise {}`, so it re-ran its
#     handshake branch after every read and kept an already-kept Promise.
#
#   * `===` compared any KIND-tagged hash by its rendering, which is right for
#     Set/Bag/Mix and wrong for every reference type that is a hash underneath:
#     two distinct Promises both render "Promise" and so compared identical.
#     The SSL socket retires a finished write with
#     `@!outstanding-writes .= grep({ $_ !=== $p })`, which matched every write.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- a constant as a return type -------------------------------------------
my constant T = Int;
sub ret-plain() returns T { 5 }
sub ret-arrow(--> T)      { 7 }
check(ret-plain(), 5, 'returns <constant>');
check(ret-arrow(), 7, '--> <constant>');

class Widget {}
my constant W = Widget;
sub ret-class() returns W { Widget.new }
check(ret-class().^name, 'Widget', 'returns <constant naming a class>');

# the check still ENFORCES, and names the resolved type as Rakudo does
sub ret-wrong() returns T { 'not an int' }
my $msg = 'no throw';
try { ret-wrong(); CATCH { default { $msg = .message } } }
check($msg.contains('expected Int'), True, "return type enforced ($msg)");

# ---- orwith as part of an if-chain -----------------------------------------
sub chain($first, $second, $withval) {
    my @log;
    if $first       { @log.push('if') }
    elsif $second   { @log.push('elsif') }
    orwith $withval { @log.push('orwith') }
    @log.join(',') || '(none)'
}
check(chain(True,  False, 5),   'if',     'if taken, orwith skipped');
check(chain(False, True,  5),   'elsif',  'elsif taken, orwith skipped');
check(chain(False, False, 5),   'orwith', 'orwith taken');
check(chain(False, False, Nil), '(none)', 'nothing taken');

sub two($cond, $withval) {
    my @log;
    if $cond        { @log.push('if') }
    orwith $withval { @log.push('orwith') }
    @log.join(',') || '(none)'
}
check(two(True,  5), 'if',     'if/orwith: if taken');
check(two(False, 5), 'orwith', 'if/orwith: orwith taken');

# `unless` takes none of them, exactly as it refuses else/elsif
my $unless-err = 'no error';
try { '"x".say if False; unless False { } orwith 5 { }'.EVAL;
      CATCH { default { $unless-err = .message } } }
check($unless-err.contains('unless'), True, "unless rejects orwith ($unless-err)");

# ---- === is identity for reference types, value for value types ------------
my $p = Promise.new;
my $q = Promise.new;
check($p === $p,  True,  'a Promise is itself');
check($p === $q,  False, 'two Promises are not identical');
check($p !=== $q, True,  '!=== agrees');
check(Channel.new  === Channel.new,  False, 'two Channels are not identical');
check(Supplier.new === Supplier.new, False, 'two Suppliers are not identical');
check(Lock.new     === Lock.new,     False, 'two Locks are not identical');

# the value types keep value semantics
check(set(1, 2)          === set(1, 2),          True, 'Set is its elements');
check(bag(1, 1, 2)       === bag(1, 1, 2),       True, 'Bag is its elements');
check(mix(1, 2)          === mix(1, 2),          True, 'Mix is its elements');
check(Date.new(2026,1,1) === Date.new(2026,1,1), True, 'Date is its date');
check(Version.new('1.2') === Version.new('1.2'), True, 'Version is its parts');
# …and a mutable one does not
check(SetHash.new(1, 2)  === SetHash.new(1, 2),  False, 'SetHash is not its elements');

# the shape the SSL socket actually uses: a Promise removing ITSELF from a list
my @outstanding = ($p, $q);
@outstanding .= grep({ $_ !=== $p });
check(@outstanding.elems, 1, 'grep drops only the promise named');
check(@outstanding[0] === $q, True, 'and leaves the other one');

if @fail {
    .say for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
