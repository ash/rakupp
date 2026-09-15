# A `where` constraint on a multi candidate was evaluated TWICE per matching
# call. scoreCandidate ran it to choose the candidate and bindParams ran it
# again to enforce it, so a constraint carrying a side effect fired twice where
# Rakudo fires it once, and every MATCHING call — the hot path — paid for the
# constraint twice. Removing the second evaluation took the perf-guard
# `multiwhere` kernel from 943 ms to 781 ms (-17%).
#
# The two evaluations did not even agree. bindParams wraps a NATIVE parameter
# immediately before checking; scoreCandidate did not. So
# `multi m(uint8 $n where * == 300)` was scored against 300 (passed, and the
# candidate won the dispatch) and then bound 44 and DIED in the bind, instead of
# falling back to the `Int` candidate Rakudo picks. Scoring now wraps too, which
# is also what makes the second evaluation provably redundant.
#
# Guarded here: each shape evaluates its constraint exactly ONCE per call, the
# native candidate falls back instead of dying, and — the reason the fix is not
# simply "delete the bind-time check" — a candidate invoked WITHOUT going through
# dispatch still gets checked, because scoring never ran for it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# --- one evaluation per call, whatever the shape ----------------------------
my @log;
multi g(Int $n where { @log.push($n); $_ == 1|3|5 }) { 'hit' }
multi g(Int $n) { 'miss' }
@log = (); check(g(3), 'hit',  'multi sub: matching call hits');
check(@log, [3],           'multi sub: a MATCHING call evaluates the where once');
@log = (); check(g(2), 'miss', 'multi sub: non-matching call falls back');
check(@log, [2],           'multi sub: a failing call evaluates the where once');

my @m;
class C {
    multi method w(Int $n where { @m.push($n); $_ > 0 }) { 'hit' }
    multi method w(Int $n) { 'miss' }
}
@m = (); check(C.new.w(3), 'hit', 'multi method: matching call hits');
check(@m, [3],                   'multi method: evaluates the where once');

# A SUBSET carries its `where` on the type, not on the parameter, so it is
# enforced by the type checker rather than the bind-time where loop — a second,
# separate path that doubled the same way.
my @s;
subset Pos of Int where { @s.push($_); $_ > 0 }
multi t(Pos $n) { 'hit' }
multi t(Int $n) { 'miss' }
@s = (); check(t(3), 'hit',  'subset param: matching call hits');
check(@s, [3],               'subset param: evaluates the subset where once');

# A single (non-multi) routine is never scored, so its bind-time check is the
# only one it gets and must stay.
my @p;
sub solo(Int $n where { @p.push($n); $_ > 0 }) { 'ok' }
@p = (); check(solo(3), 'ok', 'plain sub: runs');
check(@p, [3],                'plain sub: evaluates the where once');
check((try solo(-1)) // $!.^name, 'X::TypeCheck::Binding::Parameter',
                              'plain sub: still rejects a value the where refuses');

# --- scoring and binding must see the SAME value ----------------------------
# Scored on the unwrapped 300 the native candidate won and then died in the bind.
multi nat(uint8 $n where * == 300) { 'native' }
multi nat(Int $n) { 'fallback' }
check((try nat(300)) // "DIED:{$!.^name}", 'fallback',
    'a native candidate whose where fails on the WRAPPED value falls back, not dies');

# --- a candidate reached without dispatch keeps its check -------------------
# `.candidates[0](-1)` never goes through scoreCandidate, so skipping the
# bind-time check for it would leave the constraint unenforced entirely.
multi d(Int $n where * > 0) { 'ok' }
multi d(Int $n) { 'fallback' }
my @c = &d.candidates;
check(@c.elems, 2, 'both candidates are visible');
check((try @c[0](-1)) // $!.^name, 'X::TypeCheck::Binding::Parameter',
    'a candidate invoked directly still enforces its where');

# --- the no-match error names what was passed -------------------------------
multi only(Int $n where * > 0) { 'ok' }
my $msg = (try only(-1)) // $!.message;
check($msg.contains('only(Int)'), True,
    'the no-match error names the arguments instead of empty parens');

if @fail { note "FAIL:\n" ~ @fail.join("\n"); exit 1 }
say 'PASS';
