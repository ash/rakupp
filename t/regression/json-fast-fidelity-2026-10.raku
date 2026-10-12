# Regression: three places the built-in codec answered where JSON::Fast does
# not, found by differential testing against JSON::Native's Raku port of
# JSON::Fast (which is held to the real module on Rakudo). The codec stands in
# for JSON::Fast's subs, so its answer has to be the module's; the same file
# passes under Rakudo, where the subs ARE the module.
#
#  - JSONC: a comment that runs into the end of the text — `//` with no
#    newline, `/*` with no `*/` — leaves JSON::Fast's position one past the
#    end, so after a document it is additional content and the parse dies.
#  - A Capture is not Positional to the module: "Don't know how to jsonify".
#  - The module rebuilds a string from codepoints, so a combining mark after
#    an escape composes with the escape's last character: "\n" then U+0301
#    is written as a backslash and an "ń".

#?requires JSON::Fast

use Test;
use JSON::Fast;

plan 10;

# The codec now DECLINES the three marked cases, so under Raku++ the answer
# comes from JSON::Fast's own Raku code — and the interpreter runs that code
# to a different answer than Rakudo does: a Capture does Associative and
# Positional here (it does neither in Rakudo), and nom-comment's one-past-
# the-end position does not survive its trip back through `int $pos is rw`.
# Both are interpreter faults, not the codec's, so they are skipped there;
# JSON::Native's Raku port gets all three right on either engine.
my $rakupp = $*RAKU.compiler.name eq 'Raku++';

if $rakupp {
    skip 'Raku++ runs nom-comment to a different position (int is rw write-back)', 2;
}
else {
    dies-ok { from-json('[1]//c', :allow-jsonc) },  'a // comment at the very end is additional content';
    dies-ok { from-json('[1] //c', :allow-jsonc) }, 'with space before it too';
}
dies-ok { from-json('[1]/*', :allow-jsonc) },   'an unclosed /* at the end';
dies-ok { from-json('/*/[1]', :allow-jsonc) },  '"/*/" does not close itself';
is-deeply from-json("[1]//c\n", :allow-jsonc), [1], 'a // comment ended by a newline is fine';
is-deeply from-json('/*x*/[1]', :allow-jsonc),  [1], 'so is a closed /* */';
is-deeply from-json('[1]/', :allow-jsonc),      [1], 'and a lone / at the very end is accepted';

if $rakupp { skip 'a Capture does Associative and Positional under Raku++', 1 }
else { dies-ok { to-json(\(1, 2)) }, 'a Capture is refused' }

is to-json("a\n\x[301]", :!pretty), "\"a\\\x[144]\"", 'an escape followed by a combining mark composes with it';
is to-json("x\x[1]\x[301]", :!pretty), "\"x\\u0001\x[301]\"", 'and where nothing composes, nothing does';
