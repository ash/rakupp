# The atomic operator family, as in Rakudo (rakudo#6873): `⚛−=` with U+2212
# was a parse error, and `&prefix:<⚛>`, `&prefix:<++⚛>`, `&postfix:<⚛++>` …
# had no callable. `$x ⚛== 1` writes in Rakudo, which the issue calls a bug;
# here it is refused, naming the comparison to write instead.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my atomicint $x = 5;
check ($x ⚛−= 3), 2, '⚛−= with a Unicode minus';
check &prefix:<⚛>($x), 2, '&prefix:<⚛>';
check &prefix:<++⚛>($x), 3, '&prefix:<++⚛>';
check &postfix:<⚛++>($x), 3, '&postfix:<⚛++>';
check &postfix:<⚛-->($x), 4, '&postfix:<⚛-->';
check &prefix:<--⚛>($x), 2, '&prefix:<--⚛>';
check &prefix:<⚛>.name, 'prefix:<⚛>', 'its name';
check (⚛$x == 1), False, 'an atomic read compared (rakudo#6873)';

if $*RAKU.compiler.name eq 'Raku++' {
    my $err = (try EVAL 'my atomicint $y = 0; $y ⚛== 1') // $!;
    check $err.message.contains('⚛$x == 1'), True, '⚛== is refused, naming ⚛$x == 1';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
