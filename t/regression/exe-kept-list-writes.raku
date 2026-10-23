# A List KEPT from a literal holds its variables' containers: `f(($a, 2))`
# with `sub f($x) { $x[0] = 5 }` writes $a, as `my $m = ($c, 2); $m[0] = 9`
# writes $c. A native program refuses such a List once it sees a write through
# a subscript anywhere, and bundles — but three writes went where it could not
# see them, and each answered differently compiled than interpreted:
#
#   * bundled, the program's ROUTINES were still compiled natively, and a
#     native `$x[0] = 5` stored into the List's item instead of through it:
#     $a stayed 1, and a second write to the same item (`$x[0]++; $x[0] += 10`)
#     died "Cannot modify an immutable List" (rtIndexRef now writes through
#     the cell an item shares with its variable, as the interpreter's lvalue);
#   * a native program handing the List to a MODULE's routine built it of
#     values, and the module's write died the same way (such a call now counts
#     as a write the program cannot see, and the program is bundled);
#   * a module routine that keeps a List itself is no whole program: its native
#     List held values, and the write died again (it now stays interpreted).
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-kept-list-{$*PID}");
$dir.mkdir;
$dir.add('lib').mkdir;
sub rmtree(IO::Path $p) { if $p.d { rmtree($_) for $p.dir; $p.rmdir } else { $p.unlink } }
LEAVE { try rmtree($dir) }

#| Run $src interpreted and as an --exe binary, with lib/ on the module path.
sub both(Str $src, Str $name) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my @inc = '-I', $dir.add('lib').absolute;
    my $interp = run($bin, |@inc, $f.absolute, :out, :err);
    my $iout = $interp.out.slurp(:close).trim; $interp.err.slurp(:close);
    my $exe = $dir.add("$name.bin");
    my $c = run($bin, '--exe', |@inc, $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); $c.err.slurp(:close);
    return ($iout, 'DID-NOT-COMPILE') unless $exe.e;
    my $r = run($exe.absolute, :out, :err);
    my $cout = $r.out.slurp(:close).trim; $r.err.slurp(:close);
    ($iout, $cout)
}

$dir.add('lib/KeptM.rakumod').spurt(q:to/END/);
    unit module KeptM;
    sub kf($x) is export { $x[0] = 5 }
    sub w($x) { $x[0] = 5 }
    sub kc() is export { my $c = 1; my $m = ($c, 2); $m[0] = 9; $c }
    sub kd() is export { my $c = 1; w(($c, 2)); $c }
    END

# each: the source, and what Rakudo answers
my @cases =
    ('bundled-routines', q:to/END/, "5\n(6 2)\n6\n12\n[8 2]\n[4 2]"),
        sub f($x) { $x[0] = 5; say $x[0]; $x[0]++; say $x }
        my $a = 1; f(($a, 2)); say $a;
        my $b = 1; sub g($x) { $x[0]++; $x[0] += 10 }; g(($b, 2)); say $b;
        my @q = 1, 2; sub h($x) { $x[0] = 8 }; h((@q[0], 3)); say @q; @q[0] = 4; say @q;
        END
    ('to-a-module',      "use KeptM; my \$a = 1; kf((\$a, 2)); say \$a;", "5"),
    ('in-a-module',      "use KeptM; say kc(); say kd();", "9\n5");

for @cases -> ($name, $src, $want) {
    my ($i, $c) = both($src, $name);
    @fail.push("$name: interpreted {$i.raku}, want {$want.raku}") unless $i eq $want;
    @fail.push("$name: compiled {$c.raku}, want {$want.raku}") unless $c eq $want;
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
