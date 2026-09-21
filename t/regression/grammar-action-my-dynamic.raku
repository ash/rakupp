# Regression: a `:my %*FOO` declared in a grammar rule must be visible to that
# grammar's ACTION methods — from any rule, not only TOP.
#
# Actions do not fire at the completion site here: they replay bottom-up from
# the recorded parse tree once the whole parse is over. Every subrule exit had
# already rolled its dynamic scope back by then (saveState/restoreState in
# Regex.cpp), so an action saw nothing. A `:my` in TOP was the one exception,
# and the only reason it worked is that TOP is not entered through the subrule
# path, so nothing rolled it back — which is exactly the shape of the bug.
#
# The fix hangs the `*`-twigil dynamics live at a rule's completion on its
# ParseNode and puts them back in `build` for that node's whole subtree. On the
# RATCHET path — every `token` and every `rule` — the snapshot has to be taken
# before the restore, because that path restores before it records or fires.
#
# Rakudo runs the action inside the rule's own frame, where every enclosing
# `:my` is visible; every expectation below is its answer, checked side by side.
# Roast witness: integration/advent2013-day18.t, which went 2 of 10 to 10 of 10.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- visible from every depth, not just TOP --------------------------------

grammar Depth {
    rule TOP   { ^ <deal> $ }
    rule deal  { :my %*SEEN = (); <hand>+ % ';' }
    rule hand  { [ <card> ]**2 }
    token card { \w }
}
class DepthActions {
    has @.saw;
    method card($/) { @.saw.push(%*SEEN.defined ?? 'yes' !! 'no') }
}
my $da = DepthActions.new;
Depth.parse('a b', :actions($da));
ok($da.saw.elems == 2,             'both card actions ran');
ok(!$da.saw.grep('no'),            'a `:my` two rules up is visible to a nested action');

# the declaring rule's OWN action sees it too
grammar Own {
    rule TOP   { ^ <card> $ }
    token card { :my $*MINE = 'here'; \w }
}
class OwnActions {
    has $.saw is rw;
    method card($/) { $!saw = $*MINE // 'missing' }
}
my $oa = OwnActions.new;
Own.parse('a', :actions($oa));
ok($oa.saw eq 'here',              "…and so is the declaring rule's own");

# --- the value that arrives is the one that was live ------------------------

grammar Shadow {
    rule TOP    { ^ <outer> $ }
    rule outer  { :my $*D = 'outer'; <leaf> }
    token leaf  { \w+ }
}
class ShadowActions {
    has $.at-leaf is rw;                      # not `$.leaf`: that accessor would
    method leaf($/) { $!at-leaf = $*D // 'missing' }   # collide with the action method
}
my $sa = ShadowActions.new;
Shadow.parse('x', :actions($sa));
ok($sa.at-leaf eq 'outer',            'the action reads the live value, not a default');

# an inner declaration shadows the outer one for its own subtree
grammar Nest {
    rule TOP   { ^ <mid> $ }
    rule mid   { :my $*D = 'mid'; <inner> }
    rule inner { :my $*D = 'inner'; <leaf> }
    token leaf { \w+ }
}
class NestActions {
    has $.at-leaf is rw; has $.at-mid is rw;
    method leaf($/) { $!at-leaf = $*D // 'missing' }
    method mid($/)  { $!at-mid  = $*D // 'missing' }
}
my $na = NestActions.new;
Nest.parse('x', :actions($na));
ok($na.at-leaf eq 'inner',            'an inner `:my` shadows the outer one for its subtree');
ok($na.at-mid  eq 'mid',              '…and the outer one is back for the rule that declared it');

# --- a mutation an action makes is seen by the next action ------------------
# This is the whole point of the Roast witness: `%*PLAYED{$card}++` in one
# action has to be visible to the next, or duplicate detection never fires.

grammar Dup {
    rule TOP   { :my %*PLAYED; ^ <word>+ % ' ' $ }
    token word { \w+ }
}
class DupActions {
    has @.dups;
    method word($/) { @.dups.push(~$/) if %*PLAYED{~$/}++ }
}
my $ua = DupActions.new;
Dup.parse('a b a b b', :actions($ua));
ok($ua.dups.elems == 3,            'an action sees what an earlier action put in the dynamic');
ok($ua.dups.join(',') eq 'a,b,b',  '…in order, counting each repeat');

# and the same when the declaration is NOT in TOP (the bug's own shape)
grammar Dup2 {
    rule TOP   { ^ <body> $ }
    rule body  { :my %*PLAYED; <word>+ % ' ' }
    token word { \w+ }
}
class Dup2Actions {
    has @.dups;
    method word($/) { @.dups.push(~$/) if %*PLAYED{~$/}++ }
}
my $u2 = Dup2Actions.new;
Dup2.parse('a b a', :actions($u2));
ok($u2.dups.join(',') eq 'a',      '…and when the declaring rule is not TOP');

# --- a deferred `{ make … }` reads it too -----------------------------------
# make-blocks are deferred to the same replay, so they need the same scope.

grammar Made {
    rule TOP   { ^ <leaf> $ }
    rule leaf  { :my $*TAG = 'tagged'; \w+ { make $*TAG // 'missing' } }
}
ok(Made.parse('x')<leaf>.made eq 'tagged',
                                   'a deferred `make` block reads the rule\'s `:my` dynamic');

# --- nothing leaks out of the parse -----------------------------------------

ok(!(try { $*MINE.defined } // False), 'no `:my` dynamic leaks into the caller');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
