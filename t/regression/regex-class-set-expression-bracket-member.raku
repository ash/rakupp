# `<:L + ["]>` — a character class whose first member is NAMED and a later one
# bracketed.
#
#   The `[` after `<:L + ` was read as a GROUP, not a class: in the `/…/`,
#   `m!…!` and `rx{…}` scanners a class opened only right after `<`, `+` or
#   `-`, so the blank in ` + [` hid it, and the compile-time regex check knew
#   no class after a named member at all. A `"` in it then opened a string to
#   the end of the pattern and a `#` a comment ("couldn't find final ')'",
#   "Couldn't find terminator /"); a `(` counted as an unclosed group.
#   `\w+ [ ']' ]` is still a quantifier and a group.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub ev($code) { my $r is default(Nil) = try EVAL $code; $! ?? $!.^name !! $r }

check ev(q{so 'a"' ~~ / a <:L + ["]>+ /}),           True,  'a quote';
check ev(q{so 'a(' ~~ / a <:L + [(]>+ /}),           True,  'a paren';
check ev(q{so 'a#' ~~ / a <:L + [#]> /}),            True,  'a hash';
check ev(q{so 'a"' ~~ / a <:L+["]>+ /}),             True,  'no blanks';
check ev(q{so 'a"' ~~ rx{ a <:L + ["]>+ }}),         True,  'rx{}';
check ev(q{so 'a"' ~~ m! a <:L + ["]>+ !}),          True,  'm!!';
check ev(q{so 'a"' ~~ / a <:L + :N - [x] + ["]> /}), True,  'after two named members';
check ev(q{so 'a"' ~~ / a <+alpha + ["]>+ /}),       True,  '<+alpha + […]>';
check ev(q{so 'ab' ~~ / a <[b] + ["]> /}),           True,  'two bracketed members';
check ev(q{so 'a"' ~~ / a <:L + [b]> /}),            False, 'a member that does not match';
check ev(q{grammar G { token TOP { a <:L + ["]>+ } }; so G.parse('a"')}), True, 'in a token';
# …and a quantifier then a group is no class
check ev(q{so 'aa]' ~~ / \w+ [ ']' ] /}),            True,  "\\w+ [ ']' ]";
check ev(q{so 'a b' ~~ / a \s+ [ ' ' | b ] /}),      True,  "\\s+ [ ' ' | b ]";

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
