# Regression: the `$*x` lookup memo (#156). A read of a dynamic variable from
# deep in a call chain used to walk every frame up to the declaring one, on
# every read: 2 us a read at depth 400, 11 us in the issue's build, where
# Rakudo stays flat. The walk now leaves memos on the dynamic chain
# (DynStack) and a later read answers from the nearest one.
#
# A memo must never outlive what it describes. Each case below reads $*X from
# deep code AFTER the chain above it changed in a way a memo of an earlier read
# must not survive: a declaration added later in a frame the walk passed, a
# rebinding, temp, EVAL, CALLER::, a grammar's :my, sibling calls, threads.
# Every expectation is Rakudo 2026.09's answer.
#
# The second half pins three walk rules that differed from Rakudo before the
# memo existed: a later `my $*X` in a caller's frame hides the outer one from
# the start; a called block (pointy, closure) finds dynamics through its
# callers, not the frame it was written in; and `$*X := $y` aliases.
#
# The file runs itself a second time under RAKUPP_DYNCACHE_CHECK=1, which
# checks every memoized answer against the full walk and aborts on the first
# disagreement.

if !%*ENV<RAKUPP_DYNCACHE_CHECK> && $*EXECUTABLE.basename.starts-with('rakupp') {
    my $p = run($*EXECUTABLE, $*PROGRAM-NAME, :out, :err, :env(%*ENV, RAKUPP_DYNCACHE_CHECK => 1));
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    unless $p.exitcode == 0 && $out.lines.tail eq 'PASS' {
        note "under RAKUPP_DYNCACHE_CHECK=1: exit {$p.exitcode}\n$out$err";
        say 'FAIL (memo check)';
        exit 1;
    }
}

my $fails = 0;
sub is($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    note "not ok - $what: got {$got.raku}, want {$want.raku}";
}

sub g() { $*X // 'none' }
sub deep($n, &f) { $n == 0 ?? f() !! deep($n - 1, &f) }

my $*X = 'top';
is deep(50, &g), 'top', 'a read 50 frames down';
is (^5).map({ deep(100, &g) }).join(','), 'top,top,top,top,top', 'repeated reads from one depth';

# every third frame declares; reads at each depth
sub thirds($n) { if $n == 0 { g() } else { if $n %% 3 { my $*X = "d$n"; thirds($n - 1) } else { thirds($n - 1) } } }
is (^10).map({ thirds($_) }).join(','), 'top,top,top,d3,d3,d3,d3,d3,d3,d3', 'every third frame declares';

# the same caller reading, then assigning, then binding
sub reb() {
    my @r; my $*X = 'a';
    @r.push: deep(20, &g) for ^3;
    $*X = 'b'; @r.push: deep(20, &g);
    my $y = 'c'; $*X := $y; @r.push: deep(20, &g);
    @r
}
is reb(), ['a', 'a', 'a', 'b', 'c'], 'assignment and binding in a frame a memo passed';

sub tmp() { my @r; @r.push: deep(10, &g); { temp $*X = 'temped'; @r.push: deep(10, &g); }; @r.push: deep(10, &g); @r }
is tmp(), ['top', 'temped', 'top'], 'temp';

sub ev() { my @r; @r.push: deep(10, &g); EVAL 'my $*X = "evaled"; @r.push: deep(10, &g)'; @r.push: deep(10, &g); @r }
is ev(), ['top', 'evaled', 'top'], 'EVAL declaring one';

is deep(5, -> { my $*X = 'inner'; deep(5, &g) }), 'inner', 'a block declaring one';

# gather / take, pulled under another dynamic
sub gat() { my $*X = 'gather-home'; gather { take deep(5, &g) for ^2 } }
my @lz = gat();
sub pull() { my $*X = 'puller'; @lz.join(',') }
is pull(), 'top,top', 'a lazy gather pulled later';

grammar G {
    token TOP { :my $*X = 'rule'; <a>+ }
    token a { 'a' { @*SEEN.push: deep(3, &g) } <b>? }
    token b { :my $*X = 'b-rule'; 'b' { @*SEEN.push: deep(3, &g) } }
}
my @*SEEN;
G.parse('aabab');
is @*SEEN.join(','), 'rule,rule,b-rule,rule,b-rule', 'a grammar rule\'s :my $*X';

# sibling calls: the memo'd frame is popped and another pushed at its index
sub sib($n) { my @r; for ^3 -> $i { if $i == 1 { my $*X = "s$n-$i"; @r.push: deep($n, &g) } else { @r.push: deep($n, &g) } }; @r }
is sib(15), ['top', 's15-1', 'top'], 'sibling calls';

my @p = (^4).map(-> $i { start { my $*X = "t$i"; deep(30, &g) } });
is await(@p).join(','), 't0,t1,t2,t3', 'start blocks';

sub blk() { my @r; @r.push: deep(10, &g); if True { my $*X = 'blk'; @r.push: deep(10, &g) }; @r.push: deep(10, &g); @r }
is blk(), ['top', 'blk', 'top'], 'a dynamic in an inner block of the caller';

sub lp() { my @r; for <p q r> -> $v { if $v ne 'q' { my $*X = $v; @r.push: deep(10, &g) } else { @r.push: deep(10, &g) } }; @r }
is lp(), ['p', 'top', 'r'], 'loop iterations';

sub two() { my $*Y = 'y'; my @r; for ^3 { @r.push: deep(10, &g); @r.push: deep(10, { $*Y }) }; @r }
is two(), [|<top y> xx 3], 'two names alternating';

sub setter() { CALLER::<$*X> = 'via-caller' }
sub cal() { my $*X = 'cal'; my @r; @r.push: deep(5, &g); setter(); @r.push: deep(5, &g); @r }
is cal(), ['cal', 'via-caller'], 'CALLER:: assignment from below';

is deep(40, { $*Q // 'no-Q' }), 'no-Q', 'an undeclared one, deep';
is deep(40, { $*OUT.^name }), 'IO::Handle', 'a PROCESS one, deep';

# a later declaration in a frame the earlier read passed
sub later() { my @r; @r.push: deep(30, &g); { my $*X = 'later'; @r.push: deep(30, &g) }; @r.push: deep(30, &g); @r }
is later(), ['top', 'later', 'top'], 'a declaration after a memoized read';

# --- what a caller's frame holds (Rakudo's rules, once rakupp's own) ------

# a `my $*X` further down a caller's frame exists from the frame's entry,
# undefined and of its type, and hides the outer one from the callee before the
# declaration runs; what the callee writes there, the declaration then sets
sub gx() { $*X.raku }
sub late1() { my @r; @r.push: gx(); my Int $*X = 5; @r.push: gx(); @r }
is late1(), ['Int', '5'], 'a later typed declaration';
sub ga() { @*A.raku }
my @*A = 1, 2;
sub late2() { my @r; @r.push: ga(); my @*A = 3; @r.push: ga(); @r }
is late2(), ['[]', '[3]'], 'a later array declaration';
sub setx() { $*X = 'set-by-callee' }
sub late3() { my @r; setx(); @r.push: g(); my $*X = 'late'; @r.push: g(); @r }
is late3(), ['set-by-callee', 'late'], 'a callee writing a not-yet-declared dynamic';
is $*X, 'top', '...which leaves the outer one alone';
sub late4() { my @r; setx(); @r.push: g(); my $*X; @r.push: g(); @r }
is late4(), ['set-by-callee', 'set-by-callee'], 'a bare declaration keeps what was written';
sub late5() { if True { @*SEEN2.push: g() }; my $*X = 'x' }
my @*SEEN2; late5();
is @*SEEN2, ['none'], 'from inside an inner block of the declaring frame (undefined)';

# a called block finds a dynamic through its callers, not where it was written
my &stored;
sub maker() { my $*X = 'maker'; &stored = -> { $*X } }
maker();
sub caller-of() { my $*X = 'caller'; stored() }
is caller-of(), 'caller', 'a stored pointy block sees its caller';
is stored(), 'top', '...and not the frame that made it';
sub b1() { my $*X = 'b1'; my @r;
    @r.push: (1, 2).map({ $*X }).join(',');
    @r.push: (given 1 { $*X });
    @r.push: (with 1 -> $v { $*X });
    @r.push: (-> { -> { $*X }() })();
    @r.push: await(start { $*X });
    @r.push: await(Promise.kept(1).then({ $*X }));
    my $s = ''; react { whenever Supply.from-list(1) { $s ~= $*X } }; @r.push: $s;
    @r }
is b1(), ['b1,b1', 'b1', 'b1', 'b1', 'b1', 'b1', 'b1'], 'blocks run by builtins reach the routine';

# `:=` to a dynamic aliases the container
my $y = 'y1';
sub d1() { my @r; my $*X := $y; @r.push: g(); $y = 'y2'; @r.push: g(); $*X = 'via-alias'; @r.push: $y; @r }
is d1(), ['y1', 'y2', 'via-alias'], 'my $*X := $y';
sub d2() { my $z = 'z1'; my $*X = 'own'; $*X := $z; $z = 'z2'; g() }
is d2(), 'z2', '$*X := $z rebinds';

# indir in a worker: its block's $*CWD is its own
my $cwd = $*CWD;
is await(start { indir $*TMPDIR, { $*CWD.IO.resolve.Str } }), $*TMPDIR.IO.resolve.Str, 'indir in a start block';
is $*CWD, $cwd, '...and ours is untouched';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
