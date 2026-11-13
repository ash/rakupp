# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#2485): a Callable count to
# `.head`, `.tail` and `.skip` is an offset from the end: called with 0, and
# added to the element count, as Rakudo reads it.
#
# Before the fix the Callable was called with the element count, which agrees
# for `*-1` but not otherwise: `<a b c>.head(*.elems - 1)` was (), where
# Rakudo calls it with 0, gets 0.elems - 1 = 0, and takes all three.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, Str $want) {
    @fail.push("$what: got {$got.gist}, want $want") unless $got.gist eq $want;
}
check '.head(*.elems - 1) (rakudo#2485)', <a b c>.head(*.elems - 1), '(a b c)';
check '.head({$_.elems - 1})', <a b c>.head({$_.elems - 1}), '(a b c)';
my @l = <a b c d e>;
check '*-N as before', (@l.head(*-1), @l.head(*-2), @l.tail(*-1), @l.tail(*-2), @l.skip(*-1)),
      '((a b c d) (a b c) (b c d e) (c d e) (e))';
check 'a block gets 0', do { my @seen; @l.head({ @seen.push($_); 2 }); @seen }, '[0]';
check '…and its answer is added to the count', (@l.head({2}), @l.skip({2})), '((a b c d e) ())';
check 'a Range', ((1..5).head({-1}), (1..5).tail({-1})), '((1 2 3 4) (2 3 4 5))';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
