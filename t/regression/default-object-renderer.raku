# Regression: `.gist` and `.raku` of a hookless object are the SAME string, and
# an attribute's SIGIL is a container type.
#
# There were two hand-written default renderers — gistOf's and rakuRepr's — and
# they disagreed four ways at once:
#   * `.raku` iterated only `cls->attrs`, so it silently DROPPED every inherited
#     attribute: `class Q is P` reprd as `Q.new(q => 2)`, which cannot survive a
#     round trip through EVAL.
#   * they ordered attributes differently. Rakudo shows the DERIVED class's own
#     first, then its parents' — `Q.new(q => 2, p => 1)`.
#   * gistOf escaped only 5 characters, so a Str attribute holding a newline
#     printed a RAW newline inside what looked like a string literal.
#   * they disagreed on container form — `(1 2)` / `a => 1` against
#     `[1, 2]` / `{:a(1)}`.
# One renderer now: gistOf delegates to the g_rakuRepr hook that Value.cpp
# already uses, so the two cannot drift again, and it costs one `seen` set per
# top-level call rather than one per attribute.
#
# Fixing the container form exposed a separate bug it had been masking: `has
# @.a = (1,2)` kept the List the initialiser produced and `has %.h = (a=>1)` kept
# a bare Pair, so `.WHAT` answered (List)/(Pair). The sigil coerces now, for both
# a declared default and a value passed to .new.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

class RgP { has $.p = 1 }
class RgQ is RgP { has $.q = 2 }
check(RgQ.new.gist, 'RgQ.new(q => 2, p => 1)', 'inherited attributes appear, derived first');
check(RgQ.new.raku, RgQ.new.gist,              'and .raku is the same string');

class RgS { has $.s = "a\nb\tc" }
check(RgS.new.gist, RgS.new.raku,              'a Str attribute renders identically');
check(RgS.new.gist.contains("\\n"), True,      'and escapes its newline');
check(RgS.new.gist.contains("\n"),  False,     'rather than emitting a raw one');

class RgT { has @.list = (1, 2); has %.h = (a => 1) }
check(RgT.new.gist, 'RgT.new(list => [1, 2], h => {:a(1)})', 'container attributes');
check(RgT.new.raku, RgT.new.gist,                            'and again the same string');

# the sigil is a container type
check(RgT.new.list.WHAT.gist, '(Array)', 'an @. attribute holds an Array');
check(RgT.new.h.WHAT.gist,    '(Hash)',  'a %. attribute holds a Hash');
check(RgT.new.list.elems,     '2',       'with its elements intact');
class RgU { has @.l }
check(RgU.new(l => (5, 6)).l.WHAT.gist, '(Array)', 'a value passed to .new coerces too');
check(RgU.new(l => (5, 6)).l.elems,     '2',       'and keeps its elements');

# unset typed attributes show their declared type
class RgZ { has Int $.i; has $.u }
check(RgZ.new.gist, 'RgZ.new(i => Int, u => Any)', 'an unset typed attribute shows its type');
check(RgZ.new.raku, RgZ.new.gist,                  'in both renderings');

# an attribute-less class, and a user gist hook
class RgE { }
check(RgE.new.gist, 'RgE.new', 'no attributes, no parens');
class RgH { method gist { 'HOOK' } }
check(RgH.new.gist, 'HOOK', 'a user .gist still wins');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
