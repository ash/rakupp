# `"a {1 +} z"` — code that does not parse, inside a `{ }` block of an
# interpolating string.
#
#   The block's parse error was caught and dropped: the block produced nothing
#   and the string read on as if it had been empty ("a  z"), without an error
#   or a warning (issue #137 met it through a scanner bug). A block is code as
#   it is anywhere else, so Rakudo refuses to compile it, and so does this now —
#   naming the line the block is on, also inside a heredoc, whose body starts on
#   the line after its opener.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub err($code) { try EVAL $code; $! }

my $e = err(q[say "a {1 +} z"]);
check $e.^name,                 'X::Comp::AdHoc',                     'the type';
check $e.message.lines[0],      'Missing required term after infix', 'the message';
check err("my \$x = 1;\nsay \"a \{1 +} z\";").line, 2,                'the line';
check err("my \$h = qq:to/END/;\n    one\n    a \{1 +} z\n    END\n").line, 3, 'the line in a heredoc';
check err(q[say "a {1 + 2} z"]).defined, False,                       'a block that parses is no error';
check "a {1 + 2} z",            'a 3 z',                              '…and interpolates';
# …and neither does one whose strings the block scanner used to misread —
# these were broken all along, and quietly empty until the error surfaced
my $e2 = True; my \don't = 3;
check "a { $e2 ?? ‘matches’ !! “doesn't match” } z", 'a matches z',  'an apostrophe in “…”';
check "b { “}” } c",            'b } c',                              'a } in “…”';
check "c { 'it' ~ “'s” } d",    "c it's d",                           "a ' in “…”";
check "d {don't} e",            'd 3 e',                              "a name with an apostrophe";
check "e {｢}｣} f",              'e } f',                              'a } in ｢…｣';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
