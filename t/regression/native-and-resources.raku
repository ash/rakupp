# Regression: the NativeCall / distribution-resource machinery that unblocked the
# OpenSSL modules —
#   * Rakudo::Internals::JSON.from-json / .to-json
#   * $*VM.platform-library-name("…/lib/x") → "…/lib/libx.<ext>"
#   * `is native(&sub)` — a code-ref library resolved by calling the sub
#   * a real FFI call returning Str (zlibVersion from the system libz)
#   * fully-qualified `our sub` resolution via EVAL'd `unit module`
# (CStruct/CPointer return-boxing is exercised by the OpenSSL modules, which need
#  an arch-matched libssl, so it isn't portable enough for this test.)
# Uses only system libz (present on every macOS/Linux arch), so it is portable.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;

# --- Rakudo::Internals::JSON round-trip
my %d = Rakudo::Internals::JSON.from-json(q<{"a":1,"b":[2,3],"c":"x","t":true,"n":null}>);
@fail.push('json-int')   unless %d<a> == 1;
@fail.push('json-array') unless %d<b>[1] == 3;
@fail.push('json-str')   unless %d<c> eq 'x';
@fail.push('json-bool')  unless %d<t> === True;
my $back = Rakudo::Internals::JSON.to-json({x => 1});
@fail.push('json-to')    unless $back.contains('"x"') && $back.contains('1');

# --- $*VM.platform-library-name
my $pl = $*VM.platform-library-name("/opt/x/lib/ssl".IO).Str;
@fail.push("platform-lib ($pl)")
    unless $pl.contains('libssl') && ($pl.ends-with('.dylib') || $pl.ends-with('.so') || $pl.ends-with('.dll'));

# --- is native(&sub): code-ref library, Str return (system zlib)
sub zlib-path() { $*KERNEL.name eq 'darwin' ?? '/usr/lib/libz.dylib' !! 'libz.so.1' }
sub zlibVersion() returns Str is native(&zlib-path) {*}
my $ver = zlibVersion();
@fail.push("zlib-version ($ver)") unless $ver ~~ /^ \d+ '.' \d+ /;

# --- fully-qualified `our sub` from a unit module (EVAL'd compunit)
EVAL 'unit module RegT::Mod; our sub answer() returns Int { 42 }';
@fail.push('fq-our-sub') unless RegT::Mod::answer() == 42;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
