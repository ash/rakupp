# Issue #98: under `||`, a quantified capture from a failed branch leaked
# into the match of the branch that won.
#
# In a token or rule, a quantifier is possessive. It grabs its matches and
# never backtracks into them, so the grabs keep their captures. When the rest
# of the branch then failed (`[ <n> <m>? '=' ]` on 'a <k>'), nothing took
# those captures back. `||` moved on to the next branch with `$<m>` already
# holding one occurrence, so the winner's own `<m>` made it a List of 2. It
# was even set when the winning branch has no `<m>` at all. The possessive
# repetition now marks the capture state and restores it when its
# continuation fails.
#
# Contract: exit 0 + last line PASS. Every expectation here is Rakudo's.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub desc($v) {
    !$v.defined         ?? 'Nil'
    !! $v ~~ Positional ?? "List of {$v.elems}: [{$v.map(~*).join(', ')}]"
    !!                     "Match [{~$v}]"
}

grammar A { rule TOP { [ <n> <m>? '=' ] || [ <n> <m>? ] }
            token n { \w+ }; token m { '<' \w+ '>' } }
check desc(A.parse('a <k>')<m>), 'Match [<k>]', 'rule, <m>? in both branches, the first fails';

grammar B { token TOP { [ <n> \s* <m>? \s* '=' ] || [ <n> \s* <m>? ] }
            token n { \w+ }; token m { '<' \w+ '>' } }
check desc(B.parse('a <k>')<m>), 'Match [<k>]', 'the same as a token';

grammar C { rule TOP { [ <n> <m>? '=' ] || [ <n> <m>? '!' ] || [ <n> <m>? ] }
            token n { \w+ }; token m { '<' \w+ '>' } }
check desc(C.parse('a <k>')<m>), 'Match [<k>]', 'three branches, two fail';

grammar D { rule TOP { [ <n> <m>* '=' ] || [ <n> <m>* ] }
            token n { \w+ }; token m { '<' \w+ '>' } }
check desc(D.parse('a <k>')<m>), 'List of 1: [<k>]', '<m>* is a list, but of one';

grammar G { rule TOP { [ <n> <m>? '=' ] || [ <n> <o> ] }
            token n { \w+ }; token m { '<' \w+ '>' }; token o { '<' \w+ '>' } }
check desc(G.parse('a <k>')<m>), 'Nil', 'the winning branch has no <m> at all';

grammar H { rule TOP { [ <n> [ <m> ]? '=' ] || [ <n> [ <m> ]? ] }
            token n { \w+ }; token m { '<' \w+ '>' } }
check desc(H.parse('a <k>')<m>), 'Match [<k>]', 'a quantified group [ <m> ]?';

grammar I { rule TOP { <n> [ [ <m>? '=' ] || [ <m>? ] ] }
            token n { \w+ }; token m { '<' \w+ '>' } }
check desc(I.parse('a <k>')<m>), 'Match [<k>]', 'the alternation nested in a group';

# positional captures and a named alias go back the same way
check ('ab' ~~ / :r [ (a)? (b)? 'x' ] || [ (a) (b) ] /).list.elems, 2, 'positional captures';
grammar N { token TOP { [ $<q>=[ \d ]? 'x' ] || [ $<q>=[ \d ]? 'y' ] } }
check desc(N.parse('1y')<q>), 'Match [1]', 'a named alias under ?';

# what the parent's action sees is one Match, so .made works
class KA {
    method m($/)   { make ~$/ }
    method TOP($/) { make $<m> ~~ Positional ?? "List({$<m>.elems})" !! ($<m>.made // 'Nil') }
}
check A.parse('a <k>', actions => KA.new).made, '<k>', 'the action sees a single $<m>';

# the declarator shape the issue was found in
grammar Decl {
    rule TOP {
         [ <name> <marks>? ':=' <expr> ]
      || [ <name> <marks>? <typespec> ':=' <expr> ]
      || [ <name> <marks>? <typespec> ]
      || [ <name> <marks>? ]
    }
    token name     { \w+ }
    token marks    { '<' \w+ '>' }
    token typespec { ':' \w+ }
    token expr     { \d+ }
}
check desc(Decl.parse('x <contained>')<marks>), 'Match [<contained>]', 'four branches, three fail';
check desc(Decl.parse('x <c> :int := 5')<marks>), 'Match [<c>]', 'the second branch wins';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
