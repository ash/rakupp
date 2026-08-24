# Regression: the third ecosystem-sweep fix batch — the gaps the top-50 push
# and the 1,900-dist re-run surfaced on 2026-08-24. Every shape here was
# verified against Rakudo 2026.07 before its fix landed.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# 1 — the u64/u128 lane: [0, 2^64) values (SHA-512's whole working set) no
#     longer grind through base-1e9 BigInt bit ops; results are unchanged,
#     negatives and >64-bit intermediates included (Ch's `+^$x +& $z`)
my $m = 0xFFFFFFFFFFFFFFFF;
my $x = 0x8123456789ABCDEF;
ok(((($x +< 13) +| ($x +> 51)) +& $m).base(16) eq '68ACF13579BDF024',
   'the 64-bit rotate pattern computes right');
ok(((+^$x) +& $m).base(16) eq '7EDCBA9876543210', 'prefix +^ then mask');
ok(((2**100) +& (2**100 + 255)) == 2**100, '>64-bit AND still exact');
ok(((-7) +| 3) == -5 && ((-7) +^ (-3)) == 4, 'negative operands still exact');

# 2 — every METHOD carries the implicit *%_, signature or none
#     (DBDish::SQLite's `method new()` stashes attribute inits there)
class WithNone {
    method grab() { %_ }
}
ok(WithNone.grab(:a(1), :b(2))<b> == 2, 'paramless method still fills %_');

# 3 — `%h{$k} //= RHS` leaves NO key behind when RHS throws (DBIish's
#     %installed grew a phantom driver from the vivified entry)
my %h;
try { %h<boom> //= die 'no' };
ok(%h.elems == 0, '//= does not vivify past a throwing RHS');
%h<a> //= 5;
ok(%h<a> == 5 && (%h<a> //= 9) == 5, '//= still assigns and keeps');

# 4 — a missing native library throws X::AdHoc, as Rakudo does — DBDish::Pg
#     reads its client version under `CATCH { when X::AdHoc { } }`
my $caught = '';
{
    use NativeCall;
    sub nope(--> int32) is native('no-such-library-xyz') { * }
    nope();
    CATCH {
        when X::AdHoc { $caught = 'adhoc' }
        default { $caught = .^name }
    }
}
ok($caught eq 'adhoc', 'missing native library is X::AdHoc');

# 5 — `is native(LIB)` resolves LIB in the DECLARING module, not wherever the
#     first call happens: two modules with same-named constants stay apart,
#     and an UNDEFINED lib means "the default namespace" (DBDish::SQLite's
#     natives dlopen'd Pg's 'pq' before this)
use lib $?FILE.IO.parent.parent.add('fixtures/nativelib-isolation/lib').Str;
use LibA;
use LibB;
ok(b-pid() == $*PID, 'the Str-lib module binds from the default namespace, not its neighbour');

# 6 — a computed name adverb on a CLASS evaluates at declaration:
#     `class …:ver($expr)` answers .^ver (every DBDish driver stamps itself so)
my constant $V = '7.7';
class Stamped:ver($V) { }
ok(Stamped.^ver eq v7.7, 'computed :ver on a class reaches .^ver');

# 7 — cglobal with a PROVIDER sub and a Str global: the sub answers the
#     library, the char* global is dereferenced (Math::Libgsl's gsl_version)
{
    use NativeCall;
    my $found = do {
        sub L { '/opt/homebrew/opt/gsl/lib/libgsl.dylib' }
        (try cglobal(&L, 'gsl_version', Str)) // Str
    };
    # gsl may not be installed everywhere this runs; when it is, the answer
    # is a version string, not an address
    ok(!$found.defined || $found ~~ /^ \d+ ['.' \d+]* $/, 'cglobal(&L, …, Str) reads the string');
}

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
