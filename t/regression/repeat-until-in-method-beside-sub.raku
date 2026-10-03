# Regression (issue #117): a `repeat … until` inside a method of a class that
# also declares a `sub`. The mentioned-name scan read the RepeatStmt as a
# WhileStmt, whose fields lie in a different order, and followed the `until`
# flag as the body pointer — a segfault at load time under gcc builds
# (`rakupp -MText::Markdown -e 1` on riscv64), silent luck under Clang.
# Both loop forms, both condition placements, and the EVAL path the
# Rakuglaze snippets took.
# Contract: exit 0 + last line PASS.
my @fail;
my class Scanner {
    has @.items;
    sub helper($x) { ($x // 0) * 2 }
    method until-tail() {
        my $i = 0; my @out;
        repeat { @out.push(helper(@!items[$i])); $i++ } until $i >= @!items;
        @out
    }
    method while-tail() {
        my $i = 0; my @out;
        repeat { @out.push(helper(@!items[$i])); $i++ } while $i < @!items;
        @out
    }
    method until-head() {
        my $i = 0; my @out;
        repeat until $i >= @!items { @out.push(helper(@!items[$i])); $i++ }
        @out
    }
}
my $s = Scanner.new(items => (1, 2, 3));
@fail.push("until-tail: {$s.until-tail}") unless $s.until-tail eqv [2, 4, 6];
@fail.push("while-tail: {$s.while-tail}") unless $s.while-tail eqv [2, 4, 6];
@fail.push("until-head: {$s.until-head}") unless $s.until-head eqv [2, 4, 6];
# the loop body runs once even when the condition already holds
my $once = Scanner.new(items => ()).until-tail;
@fail.push("empty until-tail ran {$once.elems} times") unless $once.elems == 1;
my $e = EVAL q:to/END/;
    my class K { sub s($x) { $x + 1 }; method m() { my $n = 0; repeat { $n = s($n) } until $n >= 3; $n } }
    K.m
    END
@fail.push("EVAL: $e") unless $e == 3;
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
