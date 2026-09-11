# Regression: `proto sub` / `my multi sub` in expression position, and
# `&proto.add_dispatchee`.
#
# CLI::Version is used as `use CLI::Version $?DISTRIBUTION, &MAIN, 'long'`;
# its suite fakes the second argument with `proto sub MAIN(|) {*}` inline,
# and its EXPORT adds the `--version` handler with
# `&proto.add_dispatchee: my multi sub MAIN(:$version!) { … }`. Neither
# declaration was accepted as a term ("expected variable after declarator"),
# and add_dispatchee did not exist.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my &p = proto sub frob(|) {*};
ck(&p.name, 'frob', 'a proto declared as a term');
ck((my multi sub zork(Int $x) { 1 }).name, 'zork', 'a multi declared as a term');
sub add-them(&proto) {   # its own scope, as EXPORT is
    &proto.add_dispatchee: my multi sub frob(Int $x) { "int $x" };
    &proto.add_dispatchee: my multi sub frob(Str $x) { "str $x" };
}
add-them(&p);
ck(&p.candidates.elems, 2, 'two candidates hang on the proto');
ck(p(3), 'int 3', 'dispatch reaches the first');
ck(p('a'), 'str a', 'and the second');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
