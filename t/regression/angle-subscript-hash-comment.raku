# Regression: `#` inside an angle-bracket subscript is a character, not a comment.
#
# `%exts<#csv>` (App::Rak's extension table) lexed `#csv>` as a comment that
# ran to the end of the line and took the closing `>` with it, so the rest of
# the statement — and the next ones, up to a `)` somewhere below — became part
# of the subscript: "Confused (got ')')" three subs further down. A bare
# `< … >` word list already had no comments; the tight subscript form did.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my %exts = '#csv' => [1, 2], 'a' => 3;
ck(%exts<#csv>, [1, 2], 'a key beginning with # reads back');
ck(%exts<a>, 3, 'an ordinary key beside it');
my $after = 'reached';
ck($after, 'reached', 'the statement after the subscript still runs');
my @w = <a #b c>;
ck(@w, ["a", "#b", "c"], 'a bare word list keeps its # word');
my %h;
%h<#x> = 5;                     # assignment through the same subscript
ck(%h<#x>, 5, 'writable too');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
