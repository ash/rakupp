# Regression: an `our` declaration run on worker threads does not crash, and
# an `our` exists from the start (review ledger O29).
#
# The first run of an `our` on a worker inserted the package's variable into
# GLOBAL's name table while the other workers read it, and a top-level `our`,
# predeclared as a plain slot, could not be turned into the shared container
# while workers were live, so every worker fell back to publishing a view of
# its own and they raced on the table: `await (^8).map: { start { for ^200
# { our $x //= 0 } } }` died with SIGBUS or SIGSEGV in about half its runs,
# and in all of them before 432a3398. Now every `our` a unit declares gets
# its container before any of the unit runs — as Rakudo installs the stash
# entry at compile time, which is also why `GLOBAL::<$x>:exists` is True
# before a nested `our $x` has run — and a worker's `our` only reads.
# Each program below runs several times: one crashing run fails the check.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my @programs =
    Q[await (^8).map: { start { for ^200 { our $p1 //= 0 } } }; say (our $p1).defined]            => 'True',
    Q[our $p2; await (^8).map: { start { for ^200 { our $p2 //= 0 } } }; say (our $p2).defined]   => 'True',
    Q[await (^8).map: { start { for ^200 { our Int $p3 = 3 } } }; say our $p3]                    => '3',
    Q[sub w { our $p4 //= 0; $p4 } ; await (^8).map: { start { w() for ^200 } }; say (our $p4)]   => '0',
    Q[class TC { method m { our $p5 //= 5 } }; await (^8).map: { start { TC.m for ^200 } }; say $TC::p5] => '5';
for @programs -> (:key($code), :value($want)) {
    my @got;
    for ^6 {
        my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
        my $out = $p.out.slurp(:close).trim;
        $p.err.slurp(:close);
        @got.push: $p.exitcode == 0 && !$p.signal ?? $out !! "died ({$p.signal || $p.exitcode})";
    }
    check @got.unique.join(' | '), $want, $code.substr(0, 60);
}

# installed before it runs, as in Rakudo
my $never = False;
check GLOBAL::<$q1>:exists, True, 'a nested our exists before it runs';
if $never { our $q1 = 2 }
check (our $q1), Any, '…holding Any';
sub sq { our $q2 = 1 }
check GLOBAL::<$q2>:exists, True, "a sub's our too";
package PQ { sub t { our $q3 } }
check $PQ::q3, Any, "…and a package sub's, under the package's name";

# a unit module's our, from its subs
{
    my $dir = $*TMPDIR.add("our-threads-$*PID");
    $dir.mkdir;
    $dir.add('OurUM.rakumod').spurt: q:to/END/;
        unit module OurUM;
        our $v = 1;
        sub bump is export { our $v; $v++; $v }
        our sub get { our $v }
        END
    my $p = run $*EXECUTABLE, "-I$dir", '-e', 'use OurUM; say bump(); say bump(); say $OurUM::v; say OurUM::get()', :out;
    check $p.out.slurp(:close).lines.List, ('2', '3', '3', '3'), "a unit module's sub bumps the module's variable";
    sub rmtree(IO::Path $d) { for $d.dir { .d ?? rmtree($_) !! .unlink }; $d.rmdir }
    rmtree($dir);   # (with the .precomp a compiler may have left)
}

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
