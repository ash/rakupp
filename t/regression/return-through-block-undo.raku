# Regression: a `return` passing through a block or routine is an
# UNSUCCESSFUL exit for it — UNDO runs, not KEEP (issue #77, FCO's item 6).
#
# `sub wrap(&b) { KEEP …; UNDO …; b() }; wrap { return 42 }` ran wrap's
# KEEP: the phaser was chosen by the returned value being defined, whoever
# the `return` belonged to. Rakudo runs KEEP only in the routine the return
# leaves; every block it unwinds on the way (`if`, `for`, a bare block, a
# routine it passes through) runs UNDO, and undoes its `let`s. Red wraps a
# transaction this way and committed where it should have rolled back.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @log;
sub wrap(&block) { KEEP @log.push('KEEP'); UNDO @log.push('UNDO'); block() }
sub outer { wrap { return 42 }; 0 }
ck outer(), 42, 'the return still leaves outer with its value';
ck @log, ['UNDO'], 'the routine it passed through runs UNDO';

@log = ();
sub through-if { if True { KEEP @log.push('if KEEP'); UNDO @log.push('if UNDO'); return 1 }; 0 }
ck through-if(), 1, 'return from inside an if';
sub through-for { for 1 { KEEP @log.push('for KEEP'); UNDO @log.push('for UNDO'); return 2 }; 0 }
ck through-for(), 2, 'return from inside a for';
sub through-bare { { KEEP @log.push('bare KEEP'); UNDO @log.push('bare UNDO'); return 3 }; 0 }
ck through-bare(), 3, 'return from inside a bare block';
sub through-pointy { my &g = -> { KEEP @log.push('pointy KEEP'); UNDO @log.push('pointy UNDO'); return 4 }; g(); 0 }
ck through-pointy(), 4, 'return from inside a pointy block';
ck @log, ['if UNDO', 'for UNDO', 'bare UNDO', 'pointy UNDO'], 'each block it unwinds runs UNDO';

# …while the routine the return LEAVES keeps the defined-value rule
@log = ();
sub own { KEEP @log.push('KEEP'); UNDO @log.push('UNDO'); return 5 }
own();
sub own-nil { KEEP @log.push('KEEP'); UNDO @log.push('UNDO'); return Nil }
own-nil();
sub plain-wrap { wrap { 6 } }
plain-wrap();
ck @log, ['KEEP', 'UNDO', 'KEEP'], 'its own return: KEEP on a defined value, UNDO on Nil';

# a `let` in a scope a return passes through is undone; a POST still runs
our $x = 1;
my $posts = 0;
sub let-wrap(&b) { let $x = 5; POST { $posts++; True }; b() }
sub let-outer { let-wrap { return 7 }; 0 }
ck let-outer(), 7, 'return through a routine with a let';
ck $x, 1, "…undoes that routine's let";
ck $posts, 1, '…and runs its POST';
sub let-block { { let $x = 9; return 8 } }
ck let-block(), 8, 'return through a block with a let';
ck $x, 1, "…undoes the block's let";

# a METHOD it passes through as well — Red's transaction is one
@log = ();
class DB {
    method transaction(&blk) {
        let $x = 5;
        KEEP @log.push('commit'); UNDO @log.push('rollback'); POST { @log.push('post'); True }
        blk()
    }
}
sub save { DB.transaction({ return 'early' }); 'late' }
ck save(), 'early', 'return through a method';
ck @log, ['rollback', 'post'], '…rolls back, and checks the POST';
ck $x, 1, "…and undoes the method's let";
@log = ();
sub finish { DB.transaction({ 'done' }) }
ck finish(), 'done', 'no return: the method finishes';
ck @log, ['commit', 'post'], '…and commits';

say 'PASS';
