# Regression: two NativeCall fixes from cognates findings 14 and 11.
#
# 14 — the symbol was resolved with dlopen+dlsym on EVERY call, and the failing
#      dlopen candidates ("sqlite3" before "libsqlite3.dylib" hits) made dyld
#      rescan its search path each time: a flat ~67 µs per crossing regardless
#      of signature (250k crossings = 197 s where Rakudo took 5.7 s). Resolution
#      is now cached on the Callable, so 100k crossings must be fast.
# 11 — a `Pointer is rw` out-parameter (sqlite3_open's sqlite3** shape) passed
#      the pointer's VALUE (NULL for a fresh Pointer) instead of the address of
#      a slot; SQLite answered SQLITE_MISUSE (21). It now passes the slot and
#      writes a live Pointer back.
#
# Both need libsqlite3; where it cannot load (MinGW/MSVC runners), skip — the
# guard is the same runtime `try` the File::Find test uses for modules.
# Contract: exit 0 + last line PASS.
use NativeCall;

my @fail;

sub sqlite3_libversion(--> Str) is native('sqlite3') {*}
my $ver = try sqlite3_libversion();
without $ver {
    note '# libsqlite3 not loadable here — skipping NativeCall regression checks';
    say 'PASS';
    exit 0;
}

# 14: 100k crossings. Per-call resolution ≈ 6.7+ s; cached ≈ tens of ms.
my $t0 = now;
sqlite3_libversion() for ^100_000;
my $s = now - $t0;
@fail.push("100k crossings took {$s.round(0.1)} s") if $s > 5;

# 11: the sqlite3** out-param, both spellings
sub open_rw(Str, Pointer is rw --> int32) is native('sqlite3') is symbol('sqlite3_open') {*}
sub sqlite3_close(Pointer --> int32) is native('sqlite3') {*}
my Pointer $h .= new;
my $rc = open_rw(':memory:', $h);
@fail.push("Pointer is rw: rc=$rc (21 = SQLite saw NULL)") unless $rc == 0;
@fail.push("Pointer is rw: handle not written back") unless +$h != 0;
sqlite3_close($h) if $rc == 0;

my $slot = CArray[Pointer].new;
$slot[0] = Pointer;
$rc = do { sub open_ca(Str, CArray[Pointer] --> int32) is native('sqlite3') is symbol('sqlite3_open') {*}; open_ca(':memory:', $slot) };
@fail.push("CArray[Pointer]: rc=$rc") unless $rc == 0;
sqlite3_close($slot[0]) if $rc == 0;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
