# Regression: YAMLish (github.com/ash/rakupp/issues/100). Sparrow6 reported
# "Couldn't parse YAML" on a plain config file that v3.24 parsed. Three
# separate engine regressions sat behind YAMLish, and each broke a different
# part of it:
#
#   * `' ' ** { $minimum-indent..* }` in `token block(Str $indent, Int $minimum-indent)`:
#     a grammar parameter is bound as a Str, so the bound is `"1"..*`, and
#     after 93b60d8b ("A Range's endpoints are objects") a Str-endpoint range
#     no longer fills the integer fields the quantifier read. Every NESTED
#     block (a map under a key, a list under a key) failed to parse.
#   * `<tags=tag-directive>` under `[ … ]+` records under BOTH names, but only
#     the alias was marked list-valued, so `@<tag-directive>` was a lone Match.
#     Once `@` of a Match answered its positional captures (as in Rakudo), the
#     directives action read none and `%TAG` handles went unknown.
#   * "Use of Nil in string context", hundreds of times. A successful parse
#     replays the actions of rejected branches (Rakudo fires them during the
#     match); YAMLish's `<key>` tries `<single-key>` on `'a b'` before the list
#     entry wins with `<single-quoted>`. The replayed node's kids had no
#     `.made`: one shared with the final tree was a memo hit logged once, and
#     `space` is inlined and never logged. The gap was older; 80b16d18 ("Nil
#     is a Cool") is where joining those Nils started to warn.
#
# The vendored copy lives in t/fixtures/yamlish; its own test suite runs from
# t/run.raku as well. This file pins the issue's input and the minimal shapes.
use lib $?FILE.IO.parent.add('../fixtures/yamlish/lib');
use YAMLish;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- the engine shapes, without YAMLish --------------------------------------
grammar Indent {
    token TOP { <block(1)> }
    token block(Int $min) { ' ' ** { $min..* } 'x' }
}
ck so(Indent.parse('  x')), True, '`** { $param..* }` with a token parameter';
grammar IndentBounded {
    token TOP { <block(1)> }
    token block(Int $min) { ' ' ** { $min..3 } 'x' }
}
ck so(IndentBounded.parse('  x')), True, '`** { $param..3 }` with a token parameter';

grammar Directives {
    token TOP { [ '%' [ <version=yd> | <tags=td> ] \n ]+ }
    token yd { 'Y' }
    token td { 'T' }
}
{
    my $/ = Directives.parse("%T\n");
    ck +@<td>,   1, '`<tags=td>` under `+`: @<td> is the list';
    ck +@<tags>, 1, '`<tags=td>` under `+`: @<tags> is the list';
}

# ---- YAMLish itself -----------------------------------------------------------
# the file from the issue, byte for byte (note the trailing space after `item:`)
my $config = q:to/END/;
main:
  foo: "1"
  bar: "2"


item:
  - 1
  - 2
  - 3
  - 4
END
ck load-yaml($config), ${ main => { foo => '1', bar => '2' }, item => [1, 2, 3, 4] },
   'the issue #100 config';
ck load-yaml("a:\n  b: 1\n"), ${ a => { b => 1 } }, 'a map nested under a key';
ck load-yaml("a:\n  - 1\n"), ${ a => [1] }, 'a list nested under a key';
ck load-yaml("a:\n  b: 1\n\nc: 2\n"), ${ a => { b => 1 }, c => 2 }, 'a nested map, then a blank line';
ck load-yaml("%TAG !yaml! tag:yaml.org,2002:\n---\n!yaml!str 1\n...\n"), '1',
   'a %TAG directive handle resolves';

# no warnings: a single-quoted scalar used to warn once per piece of it
my @warned;
{
    CONTROL { when CX::Warn { @warned.push(.message); .resume } }
    ck load-yaml("- 'a b'\n"), $['a b'], 'a single-quoted scalar';
}
ck @warned.elems, 0, 'no warnings while loading';

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
