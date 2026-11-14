# A Parameter knows its twigil, and a sigilless or capture parameter its
# sigil, as in Rakudo (rakudo#2275): `.twigil` was "No such method", and
# `\g` and `|c` answered `g` and `c` for `.sigil`.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

class Foo {
    has $!a; has $.b;
    method f($.b, $*c, @*d, %e, \g, :$!a, |i) { }
}
my @p = Foo.^find_method('f').signature.params;
check @p[0].twigil.raku, '""', 'the invocant has none (rakudo#2275)';
check @p.head(8).map({ .twigil }).join(','), ',.,*,*,,,!,', 'each parameter\'s twigil';
check @p.head(8).map({ .sigil }).join(','), '$,$,$,@,%,\\,$,|', 'each parameter\'s sigil';
check { $^x }.signature.params[0].twigil.raku, '""', 'a placeholder is named without its ^';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
