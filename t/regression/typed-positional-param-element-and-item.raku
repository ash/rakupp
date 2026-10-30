# Regression: two ways a typed `@` parameter lost its array's type:
#
#   * `.signature.params[…].type` of `In:D @f` in a method of class K was
#     Positional[In], the bare name: `.of` displayed K::In, but a smartmatch
#     against it found nothing. Cro::RPC::JSON unmarshals a method's arguments
#     by that type, and `IntraFoo:D @foo` got Bools for objects;
#   * an Array held in a Scalar (`@a[0]`, or slipped from `@args`) bound to an
#     `@` parameter as a COPY: untyped, so `Int @x` refused an Array[Int], and
#     a push through the parameter missed the caller's element.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class K { class In { }; method m(In:D @f, In %h) { } }
my @p = K.^find_method('m').signature.params;
ck @p[1].type.^name, 'Positional[K::In]', "an @ parameter's type names its class's own element type";
ck K::In ~~ @p[1].type.of, True, '…which matches as that type';
ck @p[2].type.^name, 'Associative[K::In]', '…and a % one';

my @ret := Array[Int].new(1, 2);
my @args; @args.push: @ret;
sub f(Int:D @foo) { @foo.raku }
ck f(|@args), 'Array[Int].new(1, 2)', 'a slipped Array[Int] binds to Int @x';
ck f(@args[0]), 'Array[Int].new(1, 2)', '…and one read from an element';
sub g(@x) { @x.push(9) }
my @b = [1, 2],;
g(@b[0]);
ck @b[0].List, (1, 2, 9), "binding an element binds the element itself";
sub c(@x is copy) { @x.push(1); @x.elems }
my @z = [5],;
ck (c(@z[0]), @z[0].elems), (2, 1), '…and `is copy` still copies';
sub l(@x) { @x.^name }
ck l($(1, 2)), 'List', 'an itemized List binds as the List it is';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
