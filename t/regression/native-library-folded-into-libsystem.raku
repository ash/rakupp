# Regression: `is native('uuid')` on macOS. A handful of libraries that stand on
# their own everywhere else are FOLDED INTO libSystem there: no libuuid.dylib
# exists, on disk or in the dyld cache, yet uuid_generate is in every process.
# A dist written on Linux says `is native('uuid')` — LibUUID does, and DB::Pg
# depends on it, and Red depends on DB::Pg — and the only thing wrong with that
# name here is the file it points at.
#
# The fallback is a NAMED LIST, not a general "try the global namespace": that
# would answer a wrong `is native('mylib')` with libc's `open` or `read` instead
# of saying the library is not there. This file checks both halves.
#
# On a non-Darwin host there is a real libuuid, so the first half tests the same
# call through the ordinary path and the rule below is what matters.
#
# This is one of the few regression files that does NOT pass under Rakudo: on
# this machine `raku` fails the first three rows with "Cannot locate native
# library 'libuuid.dylib'", which is the behaviour being improved on. The last
# row — a missing library must still be an error — passes on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

use NativeCall;

# --- the reported shape ---------------------------------------------------
{
    sub uuid_generate(Blob $b) is native('uuid') { * }
    my $b = Buf.allocate(16);
    my $ok = ?(try { uuid_generate($b); True });
    if $*DISTRO.is-win {
        say 'ok - skipped: no libuuid story on Windows';
    }
    else {
        ck($ok, True, "`is native('uuid')` resolves");
        ck($b.elems, 16, '…and the child wrote all 16 bytes');
        ck(?$b.list.grep(* != 0), True, '…which are not all zero');
    }
}

# --- a library that really is not there is still an error -----------------
{
    sub open(int32 $x) returns int32 is native('rakupp-no-such-library-9271') { * }
    my $threw = False;
    try { open(0); CATCH { default { $threw = True } } }
    ck($threw, True, 'a missing library is not silently answered by libc');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
