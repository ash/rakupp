# Regression: `$sub does NativeCall::Native[$sub, $soname]` — NativeCall's own
# way of turning a plain sub into a native call, and the only thing LibraryCheck
# uses. It mixes the role into an EMPTY closure, calls it, and reads the
# resulting exception: "Cannot locate native library" means the library is
# absent, anything else means it is present.
#
# The FFI is native to this compiler, so no NativeCall.rakumod declares that
# role and the name resolved nowhere — `$f does NativeCall::Native[…]` died
# "Undeclared name". Nothing threw, so LibraryCheck answered True for EVERY
# library, including a deliberately bogus one, and its own suite failed on
# exactly that assertion.
#
# The bogus name is the load-bearing row: an implementation that always
# succeeds, or always fails, cannot pass both halves.

use NativeCall :ALL;   # guess_library_name is an :ALL-only export, on both engines

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# `library-exists`, written the way LibraryCheck writes it.
sub probe(Str $lib --> Str) {
    my $f = sub {};
    my $soname = guess_library_name($lib, v1);
    $f does NativeCall::Native[$f, $soname];
    # LibraryCheck does exactly this, and it is load-bearing on Rakudo: without
    # it the anonymous closure fails at its (empty) SYMBOL name before the
    # library is ever looked up, and every library reads as present.
    $f.setup-nativecall if $f.^can('setup-nativecall');
    my $verdict = 'present';
    try {
        $f();
        CATCH {
            when /'Cannot locate native library'/ { $verdict = 'absent' }
            default                               { $verdict = 'present' }
        }
    }
    $verdict
}

ck probe('XzippyYayaya'), 'absent', 'a bogus library is reported ABSENT';
ck probe('NoSuchLibraryAnywhere42'), 'absent', '…and so is a second bogus one';

# The POSITIVE case is not portable — the two engines guess different file
# names for the same library (`libc.dylib` here, `libc.1.dylib` upstream) and
# only one of them exists on any given box. LibraryCheck's own suite marks the
# equivalent row `todo("this is not at all cross platform")` for that reason.
# Assert it against a file that demonstrably EXISTS on this machine instead, so
# the row still proves the positive half rather than being skipped.
my $real = first *.IO.e,
    </usr/lib/libSystem.B.dylib /lib/x86_64-linux-gnu/libc.so.6
     /lib/aarch64-linux-gnu/libc.so.6 /usr/lib/libc.so.6 /lib/libc.so.6>;
if $real {
    my $f = sub {};
    $f does NativeCall::Native[$f, $real];
    $f.setup-nativecall if $f.^can('setup-nativecall');
    my $verdict = 'present';
    try { $f(); CATCH { when /'Cannot locate native library'/ { $verdict = 'absent' }
                        default { $verdict = 'present' } } }
    ck $verdict, 'present', 'a library that really exists is reported present';
}
else {
    say 'ok - (no known system library path on this box to test the positive case)';
}

# the mixin leaves the routine a Code, and says which role it now does
my $g = sub {};
$g does NativeCall::Native[$g, guess_library_name('c', v1)];
ck ($g ~~ Callable), True, 'the mixed-in routine is still Callable';

# `does` on a Code mixes into the ROUTINE, not into the container, so a second
# reference to the same sub sees the mixin too. Cloning instead would read more
# safely and match upstream less.
my $plain = sub { 'still plain' };
my $copy  = $plain;
$plain does NativeCall::Native[$plain, guess_library_name('XzippyYayaya', v1)];
my $shared = 'not native';
try { $copy(); CATCH { default { $shared = 'native too' } } }
ck $shared, 'native too', 'another reference to the same sub sees the mixin';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
