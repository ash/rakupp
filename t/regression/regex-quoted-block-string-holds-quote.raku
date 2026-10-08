# `/"{'a"b'}"/` — a `"` in a string inside the `{ }` block of a double-quoted
# regex atom.
#
#   The lexer's regex scanners — `/…/`, `m!…!` and `s/…/…/`, and the body of
#   a `token`/`regex` declaration — took the first `"` after the atom's opening
#   one for its closer, so the `"` in `'a"b'` ended the atom and the next `'`
#   opened a string that ran to the end of the file ("Couldn't find terminator
#   /", "Missing block"; a `my regex` swallowed the statements after it
#   without a word). Inside a double-quoted atom a `{ … }` is code now, read
#   through its own strings. (issue #138 is the same atom's VALUE.)
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub ev($code) { my $r is default(Nil) = try EVAL $code; $! ?? $!.^name !! $r }

check ev(q[so 'a"b' ~~ /"{'a"b'}"/]),                  True, '/…/';
check ev(q[so 'a"b' ~~ m!"{'a"b'}"!]),                 True, 'm!…!';
check ev(q[so 'a"b' ~~ rx{"{'a"b'}"}]),                True, 'rx{…}';
check ev(q[my $s = 'a"b'; $s ~~ s/"{'a"b'}"/X/; $s]),  'X',  's/…/…/';
check ev(q[my $s = 'a"b'; $s ~~ s{"{'a"b'}"} = 'X'; $s]), 'X', 's{…} = …';
check ev(q[grammar GQ { token TOP { "{'a"b'}" } }; so GQ.parse('a"b')]), True, 'a token';
check ev(q[my regex rq { "{'a"b'}" }; so 'a"b' ~~ /<rq>/]), True, 'my regex';
check ev(q[so 'a/b' ~~ /"{'a/b'}"/]),                  True, 'a / in the block';
check ev(q[so 'x}y' ~~ /"x{'}'}y"/]),                  True, 'a } in the block';
# …and the statement after a `my regex` is still there
check ev(q[my regex rr { "{'a"b'}" }; 42]),            42,   'the code after it runs';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
