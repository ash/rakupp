# Regression (issue #109, row A): a subscript adverb ending the operand of an
# operator tighter than item assignment belongs to that OPERATOR, as in Rakudo.
# Short-circuit, chaining and ternary operators and `xx` refuse it at compile
# time (X::Syntax::Adverb); other built-ins have no candidate taking it
# (X::Multi::NoMatch, when called); a user infix receives it as a named
# argument. Parentheses, or a looser operator, keep it on the subscript.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub type-of(Str $code) { try EVAL $code; $! ?? $!.^name !! 'ok' }
my $h = 'my %h = k => 1; my $r = ';
for '0 || %h<k>:exists', '0 && %h<k>:exists', '0 // %h<k>:exists', '1 == %h<k>:exists',
    '1 eqv %h<k>:exists', '1 xx %h<k>:exists', '1 ?? 1 !! %h<k>:exists', '1 == 1 == %h<k>:exists',
    '0 || %h<k>:!exists', '0 || %h{"k"}:delete', '0 || %h<k> :exists', '0 || 2 :foo' -> $c {
    my $t = type-of($h ~ $c);
    @fail.push("$c: $t") unless $t eq 'X::Syntax::Adverb';
}
for '1 + %h<k>:exists', '1 cmp %h<k>:exists', '1 .. %h<k>:exists', '-%h<k>:exists', '1 + 2 :foo' -> $c {
    my $t = type-of($h ~ $c);
    @fail.push("$c: $t") unless $t eq 'X::Multi::NoMatch';
}
for '0 || (%h<k>:exists)', '0 or %h<k>:exists', '(0, %h<k>:exists)', '%h<k>:exists',
    'so %h<k>:exists', '1 => %h<k>:exists', '%h<k>:exists || 0', '0 || foo %h<k>:exists' -> $c {
    my $t = type-of('sub foo($x) { $x }; ' ~ $h ~ $c);
    @fail.push("$c: $t") unless $t eq 'ok';
}
# …at the point it is seen: the operator to its LEFT takes it, mid-expression
# too, through any postfix written after it; the same precedence again on its
# right is ambiguous (with a refusal as well, Rakudo reports the two together)
for '1 || %h<k>:exists.so', '1 || %h<k>:exists && 2', '1 && %h<k>:exists || 2',
    '1 || %h<k>:exists + 2', '1 || @a.elems :foo', '1 || %h<k>:exists, 2' -> $c {
    my $t = type-of('my @a = 1; ' ~ $h ~ $c);
    @fail.push("$c: $t") unless $t eq 'X::Syntax::Adverb';
}
for '1 + %h<k>:exists * 2', '1 * %h<k>:exists + 2', '1 + %h<k>:exists || 2', '1 + %h<k>:exists.so',
    '1 + @a.elems :foo', '1 + @a.elems.Int :foo', '-@a.elems :foo', '1 + -@a.elems :foo' -> $c {
    my $t = type-of('my @a = 1; ' ~ $h ~ $c);
    @fail.push("$c: $t") unless $t eq 'X::Multi::NoMatch';
}
for '1 + %h<k>:exists + 0', '1 + %h<k>:exists - 0', '1 + 2 :foo + 3' -> $c {
    my $t = type-of($h ~ $c);
    @fail.push("$c: $t") unless $t eq 'X::Syntax::AmbiguousAdverb';
}
for '1 || %h<k>:exists || 0', '1 == %h<k>:exists == 2' -> $c {
    try EVAL $h ~ $c;
    @fail.push("$c: {$!.^name}") unless $! ~~ X::Comp::Group &&
        $!.sorrows[0] ~~ X::Syntax::Adverb && $!.panic ~~ X::Syntax::AmbiguousAdverb;
}
for '%h<k>:exists.so', '%h<k>:exists + 2', '%h<k>:exists || 2', 'my @a = 1; @a.elems :foo' -> $c {
    my $t = type-of($h ~ $c);
    @fail.push("$c: $t") unless $t eq 'ok';
}
my %h = k => 1;
sub infix:<foo>($a, $b, :$exists) { $exists }
@fail.push('user infix got no adverb') unless (1 foo %h<k>:exists) === True;
@fail.push('parenthesised value') unless (0 || (%h<k>:exists)) === True;
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
