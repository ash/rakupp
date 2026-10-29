# Regression: an imported operator keeps its precedence and associativity
# traits (issue #60).
#
# The importing file learns a module's operators from a scan of its source
# (Parser::scanOpsIn), which took the name only: every imported infix bound
# at the default additive precedence, left-associative, whatever it said.
# `1 ⊗⊗ 2 ⊗⊗ 3` with `is assoc<right>` nested left, `is equiv(&infix:<&&>)`
# under `is equiv(&infix:<||>)` came out as one long left-nested chain, and
# a chain or a `tighter`/`looser` operator read wrong. The scan reads the
# traits now, through the reader the file's own declarations use. Two faults
# of that reader went with it: the short reference `is equiv(&[**])` was not
# understood (the operator stayed additive), and a `sub` that gives a
# BUILT-IN spelling traits — FunctionalParsers' `(&)` and `(|)` — still bound
# as the built-in set operator, even declared in the file itself. Every
# answer below is Rakudo 2026.09's.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppImportedOpTraits;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (1 ⊗⊗ 2 ⊗⊗ 3), 'A(1,A(2,3))', '`is assoc<right>`';
ck (1 ⊗⊗ 2 ⊕⊕ 3 ⊗⊗ 4 ⊗⊗ 5 ⊕⊕ 6), 'O(A(1,2),A(3,A(4,5)),6)',
    '`is equiv(&infix:<&&>)` binds tighter than `is equiv(&infix:<||>)`, which is `is assoc<list>`';
ck (1 «&» 2 «&» 3), 'D(1,D(2,3))', 'a guillemet spelling';
ck (1 «&» 2 «|» 3 «&» 4 «|» 5), 'E(D(1,2),D(3,4),5)', '…under its list-associative partner';
ck (1 (&) 2 (&) 3), 'S(1,S(2,3))', 'a built-in spelling a `sub` gave traits';
ck (1 (&) 2 (|) 3 (&) 4 (|) 5), 'U(S(1,2),S(3,4),5)', '…under its partner';
ck (2 * 5 ⊲ 3), 4, '`is equiv(&[**])`: tighter than `*`';
ck (1 + 2 ⊳ 3), 24, '`is tighter(&infix:<+>)`';
ck (1 + 2 ⊴ 3 + 4), 'W(3,7)', '`is looser(&infix:<+>)`';
ck (2 * 3 ⋄ 4), 68, '`is tighter<*>`';
ck (1 ⊶ 2 ⊶ 3), True, '`is assoc<chain>`, rising';
ck (3 ⊶ 2 ⊶ 1), False, '…and falling: a chain, not `(3 ⊶ 2) ⊶ 1`';

# the file's own declarations read their traits the same way
sub infix:<(-)>(*@a) is equiv(&infix:<||>) is assoc<list> { "M(" ~ @a.join(",") ~ ")" }
sub infix:<⊲⊲>($a, $b) is equiv(&[**]) { $a - $b }
ck (1 (-) 2 (-) 3 ⊗⊗ 4), 'M(1,2,A(3,4))', 'a built-in spelling declared here';
ck (2 * 5 ⊲⊲ 3), 4, '`is equiv(&[**])` declared here';

# the built-in operators keep their own precedence
ck (1 + 2 * 3), 7, '`+` and `*`';
ck (2 ** 3 ** 2), 512, '`**` nests to the right';

say 'PASS';
