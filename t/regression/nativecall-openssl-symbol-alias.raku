# Regression: two ways `is native` bound the wrong libssl, both reported as
# https failing under HTTP::Simple (2026-09-08).
#
# 1. The peer-certificate call has two names for ONE function —
#    `SSL_get_peer_certificate` up to OpenSSL 1.1, `SSL_get1_peer_certificate`
#    from 3.0 — and a binding picks between them by asking a VERSION, which is
#    answered by whichever libcrypto responded rather than by the libssl it then
#    binds against. The substitution ran one way only (old name → new), so the
#    disagreement in the other direction died with "Cannot locate symbol
#    'SSL_get1_peer_certificate'". Both directions now substitute.
#    `SSL_get0_peer_certificate` is NOT interchangeable with either — it hands
#    back a borrowed reference, so the caller's X509_free would over-free — and
#    the last check here is what keeps it out of the table.
#
# 2. On macOS the versioned-first rule for ssl/crypto covered only the BARE
#    spelling (`is native('ssl')`). A name already carrying `.dylib` skipped it —
#    and `libssl.dylib` is exactly what `$*VM.platform-library-name('ssl'.IO)`
#    answers, so every dist that goes through OpenSSL::NativeLib arrived in the
#    branch that opened macOS's /usr/lib compat stub. That stub does not warn
#    and continue: it aborts the process, which no `try` can catch.
#
# Fixture: three one-function libraries, each exporting a DIFFERENT spelling and
# returning a marker only it could return, so a lookup that lands on the wrong
# library (or on the machine's real OpenSSL) fails rather than passing quietly.
# Needs a C compiler; skips cleanly without one.
# Contract: exit 0 + last line PASS.
use NativeCall;

my @fail;
sub check($got, $want, $what) {
    if $got eqv $want { return }
    @fail.push($what);
    note "# $what: got {$got.raku}, want {$want.raku}";
}

my $dir = $*TMPDIR.add("rakupp-sslalias-{$*PID}");
$dir.mkdir;
my $darwin = $*KERNEL.name eq 'darwin';
my $ext    = $darwin ?? 'dylib' !! 'so';

# Each stub exports ONE spelling, and returns a value nothing else returns.
my %stub =
    'libold'     => 'void* SSL_get_peer_certificate(void* s)  { (void)s; return (void*)0xC0FFEE; }',
    'libnew'     => 'void* SSL_get1_peer_certificate(void* s) { (void)s; return (void*)0xBEEF11; }',
    'libborrow'  => 'void* SSL_get0_peer_certificate(void* s) { (void)s; return (void*)0x0DD0;   }',
    # named for the VERSIONED spelling on purpose: the name asked for below is
    # the unversioned one, which does not exist in this directory.
    "libssl.3"   => 'int rk_fake_marker(void) { return 4242; }';

for %stub.kv -> $name, $src {
    my $c = $dir.add("$name.c");
    $c.spurt($src);
    my $cc = run 'cc', '-shared', '-o', $dir.add("$name.$ext").absolute, $c.absolute, :out, :err;
    $cc.out.slurp(:close); $cc.err.slurp(:close);
    unless $cc.exitcode == 0 {
        note '# no working C compiler here — skipping the libssl binding checks';
        say 'PASS';
        exit 0;
    }
}

# Both engines want the library named at compile time, so it comes from a
# provider sub — the shape OpenSSL::NativeLib itself uses.
sub L-old()    { $dir.add("libold.$ext").absolute }
sub L-new()    { $dir.add("libnew.$ext").absolute }
sub L-borrow() { $dir.add("libborrow.$ext").absolute }
sub L-ssl()    { $dir.add("libssl.$ext").absolute }   # NOT a file: libssl.3.$ext is

# 1a. a 3.x-era binding against a 1.1-era library — the reported failure
my sub new-on-old(Pointer) returns Pointer
    is native(&L-old) is symbol('SSL_get1_peer_certificate') {*}
check((try new-on-old(Pointer).Int) // 'threw', 0xC0FFEE,
      'the 3.x name binds a 1.1 library through the old name');

# 1b. the direction that already worked, which must keep working
my sub old-on-new(Pointer) returns Pointer
    is native(&L-new) is symbol('SSL_get_peer_certificate') {*}
check((try old-on-new(Pointer).Int) // 'threw', 0xBEEF11,
      'the 1.1 name binds a 3.x library through the new name');

# 1c. get0 is a different function and must NEVER stand in for either
my sub new-on-borrow(Pointer) returns Pointer
    is native(&L-borrow) is symbol('SSL_get1_peer_certificate') {*}
check((try new-on-borrow(Pointer).Int) // 'threw', 'threw',
      'SSL_get0_peer_certificate is not substituted for the counted forms');

# 2. macOS only: `libssl.dylib` must reach the versioned file beside it rather
#    than the /usr/lib stub. The marker can only come from the fixture, so the
#    machine's own OpenSSL answering instead is a failure, not a pass.
if $darwin {
    my sub marker() returns int32 is native(&L-ssl) is symbol('rk_fake_marker') {*}
    check((try marker()) // 'threw', 4242,
          'an unversioned libssl.dylib name prefers the versioned file beside it');
}

$dir.add("$_.c").unlink for %stub.keys;
$dir.add("$_.$ext").unlink for %stub.keys;
$dir.rmdir;

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
