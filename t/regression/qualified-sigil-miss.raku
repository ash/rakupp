# Regression: `%Foo::BAR` / `@Foo::BAR` / `%OUR::BAR` naming no symbol (#274).
#
# A package-qualified `%` or `@` name with nothing behind it read as a DEFINED
# empty Hash or Array — a new one each read — so `%Foo::BAR // 5` answered `{}`
# and `with %Foo::BAR` entered its block. Rakudo answers Any, as it does for
# `$Foo::BAR`. `%OUR::BAR` was worse: X::Undeclared where Rakudo answers Any.
# A write still installs the symbol, and declared names read as they are.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
module Foo { our $BAR = Nil; our @ARR = <a b>; our %H = :a(1); }
package Baz { }
check %Foo::BAR.defined, False, '%Foo::BAR (only $BAR declared)';
check @Foo::BAR.defined, False, '@Foo::BAR';
check %Foo::ARR.defined, False, '%Foo::ARR (only @ARR declared)';
check %NoSuch::Thing.defined, False, '%NoSuch::Thing';
check %Baz::X.defined, False, '%Baz::X';
check (%Foo::BAR // 5), 5, '%Foo::BAR // 5';
check (do with %Foo::BAR { 'entered' } else { 'else' }), 'else', 'with %Foo::BAR';
check %Foo::H.defined, True, 'declared %Foo::H';
check @Foo::ARR.elems, 2, 'declared @Foo::ARR';
%Foo::NEW<k> = 7;
check %Foo::NEW<k>, 7, 'a write installs the symbol';
package P {
    our $BAR = Nil;
    our %H2 = :b(2);
    check %OUR::BAR.defined, False, '%OUR::BAR inside a package';
    check %OUR::H2<b>, 2, 'declared %OUR::H2';
}
check %OUR::NOPE.defined, False, '%OUR::NOPE at file scope';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
