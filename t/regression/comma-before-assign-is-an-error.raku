# A trailing comma before an assignment infix is a syntax error — issue #85.
#
# `,=` is one token (the metaoperator over `infix:<,>`; its semantics live in
# t/regression/comma-assign-metaop.raku), so the SPACED spelling `%h , = 5 => 4`
# leaves the `=` with no term to take. We used to close the one-element list
# `%h ,` and assign to that instead, which silently REPLACED the hash — the same
# wrong answer `,=` itself gave before it had a token, and the reason the
# reporter's follow-up was "rakupp does not throw: it just does something not
# intended". Refused now, exactly as `1, => 2` already was.
#
# Its own file because it must spawn the compiler to see a PARSE error, which
# the --target=js corpus gate cannot judge by comparison; the semantics file
# stays free of that so the JS backend is gated on it.
#
# Passes under both rakupp and Rakudo: the wording is not asserted, only that
# the program is refused before it runs and the diagnostic says a term was
# wanted there.

my $fails = 0;
sub check($got, $want, Str $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "not ok - $desc"; note "GOT [{$got.raku}] WANT [{$want.raku}]" }
}

sub refused(Str $code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($p.exitcode != 0 && $out eq '' && $err.contains('expects a term'))
}

sub runs(Str $code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close).chomp;
    $p.err.slurp(:close);
    $p.exitcode == 0 ?? $out !! "EXIT={$p.exitcode}"
}

check refused('my %h = 1 => 2; %h , = 5 => 4; say %h'), True,
      'a trailing comma before `=` is refused, not quietly assigned to the list';
check refused('my @a; @a, = 1, 2; say @a'), True,
      '…whatever the sigil';
check refused('my $x; $x, := 5; say $x'), True,
      '…and before a bind, too';

# …while every trailing comma that ends a list still does
check runs('my @a = (1, 2,); say @a.elems'), '2', 'a trailing comma inside parens still closes the list';
check runs('sub f(*@a) { @a.elems }; say f(1, 2,)'), '2', '…and in a call\'s arguments';
check runs('my %h = (a => 1,); say %h.elems'), '1', '…and in a hash composer';
check runs('my ($a, $b,) = 1, 2; say "$a$b"'), '12', '…and in a declaration list';
check runs('my %h = 1 => 2; (%h) = 5 => 4; say %h.elems'), '1',
      'and a PARENTHESISED one-element list is still a real assignment target';
check runs('my %h = 1 => 2; %h ,= 5 => 4; say %h.elems'), '2',
      'and the unspaced metaop is untouched';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
