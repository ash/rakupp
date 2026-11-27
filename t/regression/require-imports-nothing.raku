# Regression: a bare `require Foo` imported Foo's exports (#291).
#
#     # Foo.rakumod                # Foo/Bar.rakumod
#     unit module Foo;             unit module Foo::Bar;
#     sub foo($d) is export { }    require Foo;
#                                  sub foo($d) { }
#
# `rakupp -MFoo::Bar -e1` died with "Redeclaration of symbol '&foo'": the
# statement form of `require` loaded Foo as a `use` would, so its exported
# &foo landed in Foo::Bar's scope beside Foo::Bar's own sub. In Rakudo a bare
# `require` imports nothing, as `need` does; only an import list
# (`require Foo <&foo>`) brings names in, and the EXPORT stash stays reachable.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

my $dir = $*TMPDIR.add("rakupp-require-imports-{$*PID}");
$dir.add('RqFoo').mkdir;
$dir.add('RqFoo.rakumod').spurt: q:to/MOD/;
    unit module RqFoo;
    sub foo($dir) is export { 'exported foo' }
    MOD
$dir.add('RqFoo/Bar.rakumod').spurt: q:to/MOD/;
    unit module RqFoo::Bar;
    require RqFoo;
    sub foo($dir) is export { 'own foo' }
    MOD

LEAVE { run 'rm', '-rf', $dir.Str }

sub probe($code) {
    my $p = run $*EXECUTABLE, '-I', $dir.Str, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close).trim;
    ($p.exitcode, $out, $err)
}

{
    my ($rc, $out, $err) = probe 'use RqFoo::Bar; say foo(1)';
    ok $rc == 0 && $out eq 'own foo', "module requiring a module with a same-named export loads (got rc=$rc, out=$out, err=$err)";
}
{
    my ($rc, $out) = probe 'require RqFoo; say MY::<&foo>:exists';
    ok $rc == 0 && $out eq 'False', "bare require imports nothing (got rc=$rc, out=$out)";
}
{
    my ($rc, $out) = probe 'require RqFoo <&foo>; say foo(1)';
    ok $rc == 0 && $out eq 'exported foo', "require with an import list imports it (got rc=$rc, out=$out)";
}
{
    my ($rc, $out) = probe 'require RqFoo; say ::("RqFoo::EXPORT::DEFAULT::&foo")(1)';
    ok $rc == 0 && $out eq 'exported foo', "EXPORT stash reachable after bare require (got rc=$rc, out=$out)";
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
