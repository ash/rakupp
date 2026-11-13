# `return` takes its value as a list operator, below the loose `and`/`or`, as
# in Rakudo (rakudo#2129): `return 0 or 5` returned 5, where it is
# `(return 0) or 5` and answers 0. And a declaration called at once takes a
# postfix chain: `sub foo { … }().say` printed (Any), its `.say` read as a call
# on the topic.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

sub a { return 0 or 5 }
check a(), '0', '`or` after return is not its value (rakudo#2129)';
sub b { return 0.flip or die "bar" }
check b(), '0', 'the issue\'s sub: the die is never reached';
sub c { return 7 and die "x" }
check c(), '7', '`and` too';
sub d { return Nil orelse 5 }
check d().raku, 'Nil', '`orelse` too';
sub e { return 0 || 5 }
check e(), '5', '`||` is tighter: it is the value';
sub f { return (0 or 5) }
check f(), '5', 'parenthesised, `or` is the value';
sub g { return 1, 2 }
check g().raku, '(1, 2)', 'a list is the value';
sub h($c) { return 1 or die "x" if $c; 2 }
check h(False), '2', 'a modifier governs the whole statement';

my @log;
sub k { 'k' }().&{ @log.push($_) };
check @log.join(','), 'k', 'a declaration called at once takes a postfix chain';
check k(), 'k', '…and stays declared';
sub m($n) { $n * 2 }(4).&{ @log.push($_) } if True;
check @log.join(','), 'k,8', '…with arguments and a modifier';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
