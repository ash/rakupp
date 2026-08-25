# Regression: dynamic-variable resolution order + the `if` dist's trio
# (ecosystem sweep, 2026-08-24). Every expectation here is the byte-for-byte
# Rakudo 2026.07/08 answer (scratch probe dyn-probe.raku, 13 cases).
#
# The two engine bugs this pins down:
#   - reads walked the CALLER chain before the current frame, so a routine's
#     own `my $*X` lost to its caller's;
#   - writes resolved through the LEXICAL chain only, so a callee assigning a
#     caller's dynamic minted a throwaway local instead (`$*PACKAGE_LOADED++`
#     inside a module's EXPORT — the `if` dist's suite — went nowhere).

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- resolution order -------------------------------------------------------

# own-frame declaration beats the caller's
my $*A = 1;
sub f1 { my $*A = 2; $*A }
ok(f1() == 2, 'own-frame my $*X beats the caller chain');

# the dynamic (caller) chain beats scopes the callee merely closes over
my $*B = 1;
sub g2 { $*B }
sub f2 { my $*B = 2; g2() }
ok(f2() == 2, "caller's \$*X beats the lexically-visible outer one");

# innermost caller wins among several
my $*H = 1;
sub a8 { my $*H = 2; b8() }
sub b8 { my $*H = 3; c8() }
sub c8 { $*H }
ok(a8() == 3, 'innermost caller wins through a deep chain');

# a closure does NOT leak its birth frame's dynamic once that frame is gone
my &c7;
sub maker7 { my $*G = 9; &c7 = sub { $*G } }
maker7();
my $*G = 77;
ok(c7() == 77, "a closure resolves \$*G through its CALLERS, not its birth frame");

# --- writes through the caller chain ---------------------------------------

# a callee's ++ lands in the calling sub's frame
sub g5 { $*E++ }
sub f5 { my $*E = 10; g5(); $*E }
ok(f5() == 11, "callee's \$*E++ writes the calling sub's slot");

# ...and in the mainline through two levels
my $*L = 100;
sub a13 { b13() }
sub b13 { $*L = 200 }
a13();
ok($*L == 200, 'two-level write reaches the mainline dynamic');

# EVAL is transparent for both directions
my $*I = 5;
sub f9 { my $*I = 50; EVAL '$*I++'; $*I }
ok(f9() == 51, 'EVAL reads and writes the dynamic of its calling frame');

# a block env inside the routine still counts as the frame's own scope
sub f6 { { my $*F = 6; g6() } }
sub g6 { $*F }
ok(f6() == 6, "a block-scoped my \$*F is visible to the block's callees");

# method frames participate like sub frames
class K12 { method m { $*K } }
sub f12 { my $*K = 12; K12.m }
ok(f12() == 12, 'method calls resolve dynamics through their callers');

# --- the `if` dist trio -----------------------------------------------------

# Raku.legacy: class-method spelling answers True (rakupp speaks the classic
# compiler dialect); the instance spelling dies, as in Rakudo
ok(Raku.legacy === True, 'Raku.legacy is True on the type object');
ok(!(try { $*RAKU.legacy; True } // False), '$*RAKU.legacy (instance) dies');

# use Foo:if(COND) — false skips the load entirely, true loads and runs
# EXPORT, and EXPORT's increment reaches THIS frame's dynamic (the very
# chain the `if` dist's suite exercises)
use lib $?FILE.IO.parent.parent.add('fixtures/modlib');
my $*IF-PROBE-LOADED = 0;
EVAL 'use IfAdverbProbe:if(0)';
ok($*IF-PROBE-LOADED == 0, ':if(0) prevents the load');
EVAL 'use IfAdverbProbe:if($*RAKU.version ~~ v6.*)';
ok($*IF-PROBE-LOADED == 1, ':if(true-expression) loads and runs EXPORT');
EVAL 'use IfAdverbProbe:if($*RAKU.version ~~ v7)';
ok($*IF-PROBE-LOADED == 1, ':if(v7 probe) stays unloaded');
EVAL 'use IfAdverbProbe';
ok($*IF-PROBE-LOADED == 2, 'a bare repeat use replays EXPORT into this frame');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
