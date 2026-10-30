# Regression: a grammar's actions fire DURING the parse, when each subrule
# completes, as Rakudo's do — so what the parse does next can depend on them:
#   - a code assertion right after the call reads its `.made`
#     (`<tag> <?{ $<tag>.made<type> ne 'var' }>`), and
#   - a dynamic variable the action sets is what the next token matches.
#
# The actions used to run once the whole parse was over, bottom-up from the
# recorded tree (and replayed from a completion log on a failed parse). Both
# shapes above then saw nothing: Template::Mustache decides whether a tag
# stands alone on its line from `$<tag>.made<type>`, so `  {{string}}\n` lost
# its indent and newline (t/01-basic, t/12-inheritence), and it switches
# delimiters from the action of the hunk that read `{{=<% %>=}}`, so
# `<%text%>` was never a tag (t/01-basic, t/13-pragmas, the spec suite).
#
# Each node is still built and fired ONCE: the parent's action, a code
# block's `$/` and the finished tree all get the Match built when it completed.
# Every expectation below is Rakudo's.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# --- `.made` in an assertion right after the call ------------------------
grammar Assert {
    token TOP  { [ <line> | <word> ]+ % \s }
    token line { <word> <?{ $<word>.made eq 'LONG' }> }
    token word { \w+ }
}
class AssertA { method word($/) { make $/.chars > 3 ?? 'LONG' !! 'short' } }
my $m = Assert.parse('ab abcd', :actions(AssertA));
ck $m<word>».Str.List, ('ab',),   'a short word is no line';
ck $m<line>».Str.List, ('abcd',), 'a long one is, decided from its .made';

# --- an action's dynamic variable steers the rest of the parse ------------
grammar Delim {
    token TOP   { <chunk>* }
    token chunk { <set> || <tag> || <text> }
    token set   { '=' (\S) ' '? }
    token tag   { $*OPEN (\w+) $*OPEN ' '? }
    token text  { \S+ ' '? }
}
class DelimA {
    has @.tags;
    method set($/) { $*OPEN = ~$0 }
    method tag($/) { @!tags.push: ~$0 }
}
my $*OPEN = '|';
my $da = DelimA.new;
Delim.parse('|a| =% %b% |c|', :actions($da));
ck $da.tags.List, ('a', 'b'), 'the switch to % applies from the next chunk on';
ck $*OPEN, '%', '…and stays switched after the parse';

# --- once each, and the finished tree carries what fired -------------------
grammar Count {
    token TOP  { <item>+ % ',' }
    token item { <num> }
    token num  { \d+ }
}
my %n;
class CountA {
    method num($/)  { %n<num>++;  make +$/ }
    method item($/) { %n<item>++; make $<num>.made * 2 }
    method TOP($/)  { %n<TOP>++;  make $<item>».made.sum }
}
$m = Count.parse('1,2,3', :actions(CountA));
ck %n, {num => 3, item => 3, TOP => 1}, 'each action ran once';
ck $m.made, 12, 'the parent read its children’s .made';
ck $m<item>[1]<num>.made, 2, 'the finished tree carries every .made';

# a non-capturing call fires too (it leaves no node in the tree)
grammar Quiet { token TOP { <.skip> <word> }; token skip { \s* }; token word { \w+ } }
my $skipped = 0;
class QuietA { method skip($/) { $skipped++ } }
Quiet.parse('  hi', :actions(QuietA));
ck $skipped, 1, 'a <.rule> action fires';

# a parse that fails keeps what fired before it failed
grammar Fails { token TOP { <num>+ % ',' ';' }; token num { \d+ } }
my @seen;
class FailsA { method num($/) { @seen.push: +$/ } }
ck Fails.parse('1,2', :actions(FailsA)).defined, False, 'the parse fails';
ck @seen.List, (1, 2), '…after both nums fired';

# an action that dies leaves the parse
grammar Boom { token TOP { <num> 'x' }; token num { \d+ } }
class BoomA { method num($/) { die "boom at $/" } }
my $err = (try Boom.parse('7x', :actions(BoomA))) // $!;
ck ($err ~~ Exception) && $err.message, 'boom at 7', 'a dying action ends the parse';

.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
