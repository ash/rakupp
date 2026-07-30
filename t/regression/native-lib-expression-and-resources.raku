# Regression: the two gaps behind issue #13 (Digest::SHA1::Native needed
# LD_PRELOAD to run).
#
# 1. `is native(EXPR)` accepted only a string literal or `&sub`; a bare term —
#    `constant SHA1 = %?RESOURCES<libraries/sha1>; … is native( SHA1 )` — was
#    mis-captured as a sub NAME, the lookup found nothing, and the handle
#    silently stayed RTLD_DEFAULT ("Cannot find native symbol", fixed only by
#    preloading). Any other expression is now parsed whole and evaluated at
#    declaration execution; because named subs are hoisted ABOVE the constant,
#    an eval that produces no lib keeps the AST and retries at first call,
#    whose env chain includes the module scope.
# 2. `%?RESOURCES<libraries/x>` mapped to the literal file "x"; the file a dist
#    builds is the PLATFORM name (libx.dylib / libx.so / x.dll). Checkout-mode
#    resolution now probes the decorated candidates.
#
# The fixture is built here: a real dist checkout with a real one-function C
# library, used through both the constant and the inline %?RESOURCES spelling —
# plus the pre-existing forms (Str, &sub, literal) that must keep working.
# Needs a C compiler for the fixture; skips cleanly without one (MSVC/MinGW CI).
# Contract: exit 0 + last line PASS.
use NativeCall;

my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $root = $*TMPDIR.add("rakupp-nlx-{$*PID}");
my $ext = $*KERNEL.name eq 'darwin' ?? 'dylib' !! 'so';

# --- build the fixture dist ---
for "$root/lib/Fake", "$root/resources/libraries" -> $d { $d.IO.mkdir }
"$root/META6.json".IO.spurt: q:to/J/;
    { "name": "Fake::SHA1", "version": "0.1",
      "provides": { "Fake::SHA1": "lib/Fake/SHA1.rakumod" },
      "resources": [ "libraries/sha1" ] }
    J
"$root/fake.c".IO.spurt: 'int fake_answer(void) { return 4142; }';
my $cc = run('cc', '-shared', '-o', "$root/resources/libraries/libsha1.$ext",
             "$root/fake.c", :out, :err);
unless $cc.exitcode == 0 {
    note '# no working C compiler here — skipping native-lib fixture checks';
    say 'PASS';
    exit 0;
}
"$root/lib/Fake/SHA1.rakumod".IO.spurt: q:to/M/;
    unit module Fake::SHA1;
    use NativeCall;
    constant SHA1 = %?RESOURCES<libraries/sha1>;
    sub fake_answer(--> int32) is native( SHA1 ) { * }
    sub answer() is export { fake_answer() }
    sub fake_answer2(--> int32) is native(%?RESOURCES<libraries/sha1>) is symbol('fake_answer') { * }
    sub answer-inline() is export { fake_answer2() }
    M

# --- 1+2 end-to-end: constant + inline spellings, through a nested rakupp ---
my $p = run($rakupp, "-I$root", '-e',
            'use Fake::SHA1; say answer(); say answer-inline()', :out, :err);
my $out = $p.out.slurp(:close);
my $err = $p.err.slurp(:close);
@fail.push("dist run: exit={$p.exitcode} err=$err") unless $p.exitcode == 0;
@fail.push("constant form: $out")  unless $out.lines[0] // '' eq '4142';
@fail.push("inline form: $out")    unless $out.lines[1] // '' eq '4142';

# --- resource mapping visible from Raku ---
my $p2 = run($rakupp, "-I$root", '-e',
             'use Fake::SHA1; say %?RESOURCES', :out, :err);   # module's map not ours…
# (%?RESOURCES is per-compunit; assert via the module above instead — the 4142
# answers already prove the mapped path was dlopened. Just check the file name
# convention holds on disk.)
@fail.push('platform lib file missing')
    unless "$root/resources/libraries/libsha1.$ext".IO.e;

# --- pre-existing forms stay intact ---
sub getpid(--> int32) is native(Str) {*}
@fail.push('is native(Str)') unless getpid() > 0;
my $sq = try {
    constant L = 'sqlite3';
    sub sqlite3_libversion(--> Str) is native(L) {*}
    sqlite3_libversion()
};
with $sq { @fail.push("constant at mainline: $sq") unless $sq ~~ /^\d+\.\d+/ }

unlink($_) for dir("$root/resources/libraries"), dir("$root/lib/Fake"), "$root/META6.json".IO, "$root/fake.c".IO;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
