# Regression: two fixes found taking HTTP::Tiny from 7 to 8 of its 10 files,
# 2026-08-04. Both are general; the module only showed where to look. Every
# expectation checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — " ~ $got.raku ~ ' vs ' ~ $want.raku; $ok = False } }

# 1. `$_` is an ALIAS to the topic, not a copy, so a mutation writes back to the
#    variable. Array topics already did this (their elements are containers);
#    a SCALAR topic did not, so `s/x/y/ given $str` quietly changed nothing.
{
    my $a = 'foo';  s/f/F/ given $a;            ck($a, 'Foo', 'given as a statement modifier');
    my $b = 'foo';  given $b { s/f/F/ }         ; ck($b, 'Foo', 'given as a block');
    my $c = 'foo';  given $c { $_ = .uc }       ; ck($c, 'FOO', 'assigning to $_ writes back');
    my $d = 'foo';  for $d { s/f/F/ }           ; ck($d, 'Foo', 'a scalar `for` topic too');

    my @e = <foo bar>; for @e { s/o/0/ };         ck(@e, ['f0o', 'bar'], 'an array topic is unchanged');

    # a topic with nowhere to write back is left alone rather than erroring
    my $lit = do given 'literal' { $_.uc };
    ck($lit, 'LITERAL', 'a literal topic still works');
    my $f = 'keep';
    my $viaCall = do given $f.uc { $_ };
    ck($viaCall, 'KEEP', 'a call result is not written back');
    ck($f, 'keep', 'and the source is untouched');
}

# 2. `$0.uc()` in a substitution replacement is a METHOD CALL, as in any other
#    interpolating string — and, as there, only WITH parentheses.
{
    my $a = 'content-type';
    $a ~~ s:g/ <|w> (\w) /$0.uc()/;
    ck($a, 'Content-Type', 'a method call on a capture');

    my $b = 'hello world';
    $b ~~ s:g/(\w+)/$0.tc()/;
    ck($b, 'Hello World', 'and on each match of a global substitution');

    my $c = 'xy';
    $c ~~ s/(x)/[$0]/;
    ck($c, '[x]y', 'a plain capture is unaffected');

    my $d = 'content-type';
    $d ~~ s:g/ <|w> (\w) /{$0.uc}/;
    ck($d, 'Content-Type', 'the block form still works');
}

say $ok ?? 'PASS' !! 'FAIL';
