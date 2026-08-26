# Issue #34: TAP's parser is `whenever $input.lines(:!chomp) -> $line { … }`.
#
# `.lines` / `.words` on a LIVE Supply (a Supplier's, or a `supply {}` block's)
# had no implementation: they fell through to the generic Any handler, which
# stringified the Supply itself and emitted one bogus value. Only the eager,
# list-backed Supply and a process stream were handled.
#
# They are stream SPLITTERS: the message boundaries are not the piece
# boundaries, so the unfinished tail is held between messages and delivered when
# the source is done.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

sub drain(&build, *@messages) {
    my $s = Supplier.new;
    my @got;
    build($s.Supply).tap(-> $v { @got.push($v) });
    $s.emit($_) for @messages;
    $s.done;
    @got.List;
}

# a line that straddles two messages is ONE line
check drain({ $^s.lines(:!chomp) }, "a\nb\nc\n", "d\ne"),
      ("a\n", "b\n", "c\n", "d\n", "e"),
      '.lines(:!chomp) keeps the newline and joins across messages';
check drain({ $^s.lines }, "x\ny\n"), ("x", "y"), '.lines chomps by default';
check drain({ $^s.lines }, "no-newline-at-all"), ("no-newline-at-all",),
      'the unterminated last line is delivered when the source is done';
check drain({ $^s.words }, "hello wo", "rld  foo\nbar ", "baz"),
      ("hello", "world", "foo", "bar", "baz"),
      '.words joins a word split across messages';

# a supply BLOCK source, which is how TAP feeds its own parser
my @got;
supply { emit "p\nq\n"; emit "r" }.lines.tap(-> $v { @got.push($v) });
check @got.List, ("p", "q", "r"), '.lines on a `supply {}` block';

# chained with the combinators that already worked
check drain({ $^s.lines.grep(*.chars > 1) }, "a\nbb\nccc\n"), ("bb", "ccc"),
      '.lines chains into .grep';
check drain({ $^s.lines.map(*.uc) }, "a\nb\n"), ("A", "B"), '.lines chains into .map';
check drain({ $^s.lines.head(2) }, "a\nb\nc\n"), ("a", "b"), '.lines chains into .head';

# the eager, list-backed Supply must keep its old behaviour
check Supply.from-list("a\nb\n", "c\n").lines.list.List, ("a", "b", "c"),
      '.lines on a list-backed Supply is unchanged';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
