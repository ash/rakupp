# Which declaration a `#|` / `#=` documents (roast S26-documentation/wacky.t).
# A `#|` waits for the NEXT declaration however far below — blank lines and
# code that declares nothing leave it pending, the first declaration or block
# after it takes it — where it used to reach only a declaration directly
# under an unbroken run of `#|` lines, and reached EVERY declaration on that
# line. A `#=` after several declarations on one line is the last one's, and
# an anonymous `sub {` takes the `#=` just inside its brace. Every case here
# answers the same under Rakudo 2026.08 and under RAKUDO_RAKUAST=1.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}
sub why($x) { ($x.WHY // 'Nil').Str }

#| across blank lines


sub a {}
check('a #| reaches the declaration past blank lines', why(&a), 'across blank lines');

#| across code
say "# (a statement that declares nothing)";
my $unrelated = 1;
sub b {}
check('…and past code that declares nothing', why(&b), 'across code');

#| first part

#| second part
sub c {}
check('two #| runs join with a space', why(&c), 'first part second part');

#| taken by the block
if $unrelated { }
sub d {}
check('a block in between takes the doc first', why(&d), 'Nil');

#| past a subscript
my %h = x => 1;
my $v = %h{'x'};
sub e {}
check('a subscript is no block', why(&e), 'past a subscript');

class K {
    method m {}
    #| dangling at the end of a class
}
sub f {}
check('a doc left over at the end of a class goes to the next declaration',
      why(&f), 'dangling at the end of a class');

class A {
    #| lead
    has $.p; has $.q; #= trail
}
check('two attributes on a line: the #| is the first one’s',
      why(A.^attributes.first(*.name eq '$!p')), 'lead');
check('…and the #= the last one’s', why(A.^attributes.first(*.name eq '$!q')), 'trail');

#| the role
role R { method r {} }
check('a one-line role takes its #|', why(R), 'the role');
check('…and its method does not', why(R.^find_method('r')), 'Nil');

sub g(
    #| the x
    Int $x,
    #| the y
    Int $y,
) { }
check('each parameter takes its own #|',
      &g.signature.params.map({ why($_) }).join(' / '), 'the x / the y');

my $s1 = sub {
#= just inside the brace
};
check('an anonymous sub takes the #= just inside its brace', why($s1), 'just inside the brace');
my $s2 = sub ($x) { #= on the brace line
};
check('…or on the brace line', why($s2), 'on the brace line');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
