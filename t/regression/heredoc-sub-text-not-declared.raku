# A heredoc's body is text, not code: one that carries a program whose
# `sub q($x) { … }` declares a routine named q no longer makes a later
# `q[…]` in the file a call to it. The textual scan for subs named like a
# quote word (Lexer::scanDeclaredSubNames) skipped comments and quoted
# strings but read a heredoc's lines as code, so `say q[hello]` after one
# died "Undefined routine 'q'".
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $prog = q:to/END/;
    sub q($x) { $x }
    sub qq { }
    END
ck q[hello], 'hello', 'q[…] after a heredoc with a sub q';
ck qq{x}, 'x', 'qq{…} after one with a sub qq';
my $more = qq:to<STOP>;
    sub Q() is pure
    STOP
ck Q[a b], 'a b', 'Q[…] after a qq:to<…> heredoc';
ck $prog.lines.elems, 2, 'the heredoc still has its lines';
ck $more.contains('sub Q'), True, '…and so does the second';

say 'PASS';
