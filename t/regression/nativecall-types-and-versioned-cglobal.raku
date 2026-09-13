# Regression: two gaps between a dist and a native library it had every right
# to reach. Both were found by installing the library a blocked dist wanted and
# watching what failed NEXT.
#
#  * `use NativeCall::Types` — upstream ships the types as a compunit of their
#    own, separate from the machinery. Here the FFI is built into the compiler,
#    so there is no file to find and the `use` died "Could not find". Thirteen
#    dists sit behind Font::FreeType, whose Raw/Defs opens with exactly that
#    line.
#  * `cglobal(($name, $version), …)` — the (name, version) form `is native`
#    already takes, and what a dist writes when it needs a specific soname
#    (`our $FT-LIB = ('freetype', v6)`). cglobal stringified the list and asked
#    dlopen for the literal "freetype 6", a name no file has ever had.
#
# The cglobal row asserts the RESOLVED NAME rather than a successful load, so it
# proves the fix on a machine that has no such library at all.

use NativeCall :ALL;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- NativeCall::Types loads --------------------------------------------
ck (try EVAL 'use NativeCall::Types; "loaded"'), 'loaded',
   '`use NativeCall::Types` loads';
# …and publishes its types QUALIFIED, which is where upstream puts them. The
# assertion is that the name RESOLVES TO A TYPE, not what `.^name` spells it:
# upstream answers the fully qualified "NativeCall::Types::Pointer" and this
# engine the short "Pointer", and that difference is not what this file is about.
ck (try EVAL 'use NativeCall::Types; NativeCall::Types::Pointer ~~ Any:U'), True,
   '…and NativeCall::Types::Pointer resolves to a type object';

# ---- cglobal takes the (name, version) form ------------------------------
# A name no machine has, so the row is about the RESOLUTION and not the load.
my $lib = ('zzznosuchlib', v7);
my $guessed = guess_library_name($lib);
ck ($guessed.contains('zzznosuchlib') && $guessed.contains('7')), True,
   'guess_library_name resolves (name, version) to a real file name';

my $err = '';
try {
    cglobal($lib, 'nosuchsymbol', Pointer);
    CATCH { default { $err = .message } }
}
ck ($err.contains('zzznosuchlib')), True,
   'cglobal reports the library it was asked for';
# the actual bug: the LIST was stringified, so dlopen saw "zzznosuchlib 7"
ck ($err.contains('zzznosuchlib 7')), False,
   'cglobal does not stringify the (name, version) list';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
