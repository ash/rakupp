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
# The `===` fix reached only the types stored as a kind-tagged HASH. Four more
# were wrong because they are stored as something else, and they fell through to
# the same value-compare fallback (or, for Capture, to reference identity):
#
#   * Buf, Instant and Duration are REFERENCE types in Rakudo but plain tagged
#     scalars here — a Buf is a Str, an Instant/Duration a Num — so there was no
#     address to compare and `Buf.new(1,2) === Buf.new(1,2)` was True. Buf is the
#     one that bites: it is mutable, and retiring one buffer from a list with
#     `.grep({ $_ !=== $buf })` threw away every buffer holding the same bytes,
#     the same failure that left a TLS socket unable to close. Each construction
#     now stamps an identity token; Blob, immutable, keeps value semantics.
#   * a Capture is the one Array-shaped VALUE type — `\(1,2) === \(1,2)` is True
#     — and got reference identity along with every other Array.
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

# ---- the reference types that are NOT hashes underneath --------------------
my $buf = Buf.new(1, 2);
check($buf === $buf,   True,  'a Buf is itself');
sub give-back($x) { $x }
check($buf === give-back($buf), True, 'and still itself after a round trip through a sub');
check(Buf.new(1, 2) === Buf.new(1, 2),  False, 'two Bufs of the same bytes are not identical');
check(Buf.new(1, 2) !=== Buf.new(1, 2), True,  '!=== agrees');
my $alias = $buf;
check($alias === $buf, True, 'a second name for one Buf is that Buf');
check($buf.subbuf(0, 2) === $buf, False, 'a subbuf is a new Buf, even taking all of it');
check(Buf.new(1, 2) === Buf.new(1, 3), False, 'and different bytes are certainly not');

# …while the IMMUTABLE byte types keep value semantics, as they do in Rakudo
check(Blob.new(1, 2) === Blob.new(1, 2), True, 'a Blob is its bytes');
check('ab'.encode === 'ab'.encode,       True, 'and so is a utf8');

my $inst = Instant.from-posix(1000);
check($inst === $inst, True, 'an Instant is itself');
check(Instant.from-posix(1000) === Instant.from-posix(1000), False,
      'two Instants of the same moment are not identical');
check($inst.^name, 'Instant', 'Instant.from-posix answers an Instant, not a Num');
my $dur = Duration.new(5);
check($dur === $dur, True, 'a Duration is itself');
check(Duration.new(5) === Duration.new(5), False, 'two Durations of the same length are not identical');
# $*INIT-INSTANT is ONE Instant however often it is read
check($*INIT-INSTANT === $*INIT-INSTANT, True, '$*INIT-INSTANT keeps one identity');

# ---- …and the one Array-shaped VALUE type ----------------------------------
check(\(1, 2) === \(1, 2),       True,  'a Capture is its arguments');
check(\(1, 2) === \(1, 3),       False, 'a different argument is a different Capture');
check(\(1, 2) === \('1', '2'),   False, 'and the argument TYPES count');
check(\(1, :a<b>) === \(1, :a<b>), True,  'named arguments compare too');
check(\(:a<b>) === \(:a<c>),       False, '…by value');
check(\(1, 2) === (1, 2),          False, 'a Capture is not the list of the same things');

# `===` is `.WHICH` equality — the two must not disagree
check(\(1, 2).WHICH eq \(1, 2).WHICH, True,  'equal Captures share a .WHICH');
check($buf.WHICH eq Buf.new(1, 2).WHICH, False, 'distinct Bufs get distinct .WHICH');
check($buf.WHICH eq $buf.WHICH,          True,  'and one Buf keeps its own');

# the mutable-Buf spelling of the grep the SSL socket runs: two buffers that
# happen to hold the same bytes, only one of them named
my @bufs = (Buf.new(1, 2), Buf.new(1, 2));
my $drop = @bufs[0];
@bufs .= grep({ $_ !=== $drop });
check(@bufs.elems, 1, 'grep drops only the buffer named, not its twin');

if @fail {
    .say for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
