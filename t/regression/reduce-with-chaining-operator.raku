# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#4128): `reduce` with a built-in
# chaining comparison chains, as `[op]` does.
#
# Before the fix `reduce &[>], 6, 5, 4` folded (6 > 5) > 4, True > 4, False,
# where Rakudo answers 6 > 5 > 4, True.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
check 'reduce &[>], 6, 5, 4 (rakudo#4128)', (reduce &[>], 6, 5, 4), True;
check 'reduce &infix:«>»', (reduce &infix:«>», 6, 5, 4), True;
check 'a pair that fails', (reduce &[<], 1, 3, 2), False;
check 'the method form', (1, 2, 3).reduce(&[<]), True;
check 'a Unicode spelling', (3, 2, 1).reduce(&[≥]), True;
check 'string comparisons', ((reduce &[eq], <a a a>), (reduce &[eq], <a a b>)), (True, False);
check 'a non-chaining operator still folds', (1, 2, 3).reduce(&[+]), 6;
# …and the reduce metaop over a built-in's Callable folds it, as Rakudo does:
# `[[&infix:«>»]]` is ((6 > 5) > 4), False, where it died "Unsupported operator"
check '[[&infix:«>»]] folds', ([[&infix:«>»]] 6, 5, 4), False;
check '[[&infix:<+>]]', ([[&infix:<+>]] 1, 2, 3), 6;
check 'its scan form', ([\[&infix:<~>]] <a b c>).List, <a ab abc>;

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
