# Regression: inside a CHARACTER CLASS a `[` is a literal member, not a nested
# group. Three separate scanners counted it as an opener — the bare `/…/`
# literal, the `q//`-family reader and the `token`/`rule` body reader — so a
# class holding a bracket never closed, the scan ran past the construct and
# swallowed the rest of the FILE. ECMA262Regex has
#
#     token pattern-character { <-[^$\\.*+?()[\]{}|]> }
#
# and reported its error 400 lines later, on the `=end pod` at the end of the
# file: the give-away shape of a mis-scan is an error nowhere near its cause.
# Fixing it made ECMA262Regex, Path::Finder and Template6 parse.
#
# A class opens `<[`, `<-[` or `<+[` (also `+[`/`-[` in a set expression) and
# cannot nest, so the rule is simply: once inside, brackets are members.
# Contract: exit 0 + last line PASS.
my @fail;

sub ok($desc, $got, $want = True) { @fail.push("$desc (got {$got.raku})") unless $got eqv $want }

# a bare `[` as a class member — the minimal form of the bug
ok('bare [ member',      so 'x' ~~ /<-[[]>/);
ok('[ excluded',         so '[' ~~ /^<-[[]>$/, False);
ok('[ included',         so '[' ~~ /^<[[]>$/);

# …and the whole ECMA262Regex class, which is what found it
ok('the real class',     so 'x' ~~ /<-[^$\\.*+?()[\]{}|]>/);
ok('the real class excludes', so '(' ~~ /^<-[^$\\.*+?()[\]{}|]>$/, False);

# neighbours that must keep working
ok('escaped ]',          so 'x' ~~ /<-[\]]>/);
ok('braces',             so 'x' ~~ /<-[{}]>/);
ok('parens',             so 'x' ~~ /<-[()]>/);
ok('the delimiter',      so 'x' ~~ /<-[\/]>/);
ok('angles',             so 'x' ~~ /<-[<>]>/);
ok('positive class',     so 'b' ~~ /^<[abc]>$/);
ok('range class',        so 'q' ~~ /^<+[a..z]>$/);

# a GROUP still nests: the fix must not make `[ … ]` inert
ok('group',              so 'ab' ~~ /^ [ 'a' 'b' ] $/);
ok('nested groups',      so 'ab' ~~ /^ [ [ 'a' ] [ 'b' ] ] $/);
ok('group with class',   so 'x' ~~ /^ [ <[a]> | <[x]> ] $/);
ok('class inside group', so '[' ~~ /^ [ <[\[]> ] $/);

# the same three scanners, reached through their own syntax
grammar G {
    token TOP  { <chars>+ }
    token chars { <-[^$\\.*+?()[\]{}|]> }
}
ok('token body', so G.parse('hello'));
ok('token body rejects', so G.parse('a(b'), False);

my $rx = rx/<-[[]>/;
ok('rx// form', so 'x' ~~ $rx);

my $s = 'a[b';
$s ~~ s/<-[[]>+//;
ok('substitution', $s, '[b');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
