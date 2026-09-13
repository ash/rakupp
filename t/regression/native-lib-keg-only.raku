# Regression: a KEG-ONLY Homebrew library is findable.
#
# Homebrew never symlinks a formula whose library would shadow a system one into
# its own lib/. So `/opt/homebrew/lib/libarchive.13.dylib` does not exist while
# `/opt/homebrew/opt/libarchive/lib/libarchive.13.dylib` does, and the same goes
# for libmagic, libidn and a dozen others. Nothing puts that directory on any
# search path, so `is native('archive')` simply failed — on a machine with the
# library installed.
#
# Both spellings are covered: a BARE name (`is native('magic')`) and a full FILE
# name a dist baked in (`libarchive.13.dylib`).
#
# The rows only run where the library is actually installed — this is a search
# PATH fix, and asserting it on a machine without Homebrew would test nothing.
# The skip says so rather than passing quietly.
#
# They also only run on THIS engine, and that is not a dodge: Rakudo does not
# search the keg directory either, so it cannot load libmagic or libarchive on
# this machine at all. This is a place where we are deliberately ahead of
# upstream rather than matching it, and a row asserting otherwise would be
# asserting Rakudo's gap. The negative row at the bottom runs on both.

use NativeCall;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $searches-kegs = $*RAKU.compiler.name eq 'Raku++';

sub keg-only($formula, $file) {
    $searches-kegs
      && ("/opt/homebrew/opt/$formula/lib/$file".IO.e
          || "/usr/local/opt/$formula/lib/$file".IO.e)
}

# ---- a BARE name resolves into the keg ---------------------------------
if keg-only('libmagic', 'libmagic.dylib') {
    sub magic_version(--> int32) is native('magic') { * }
    ck (magic_version() > 0), True, "a bare `is native('magic')` finds the keg-only library";
}
else {
    say 'ok - (no keg-only libmagic here, or an engine that does not search kegs)';
}

# ---- …and so does a baked-in FILE name ---------------------------------
if keg-only('libarchive', 'libarchive.13.dylib') {
    sub archive_version_number(--> int32) is native('libarchive.13.dylib') { * }
    ck (archive_version_number() > 0), True, 'a versioned file name finds the keg-only library';
}
else {
    say 'ok - (no keg-only libarchive here, or an engine that does not search kegs)';
}

# ---- a library that exists NOWHERE still fails --------------------------
# the guard: widening the search must not make every name resolve
{
    sub zzz_no_such_symbol() is native('zzznosuchlibraryanywhere') { * }
    my $threw = 'no';
    try { zzz_no_such_symbol(); CATCH { default { $threw = 'yes' } } }
    ck $threw, 'yes', 'a library that exists nowhere still fails';
}

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
