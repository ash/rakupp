# BROKE: leg 17 — X::Phaser::Multiple ("only one CATCH per block") counted
# CONTROL blocks too, because CONTROL shares Block.isCatch internally;
# S04-phasers/control.t and S32-exceptions/misc.t died at parse (caught by
# gate dx6). FIXED: same leg — Block.phaser tags CATCH vs CONTROL and only
# CATCH counts toward the limit. This locks that the combination PARSES.
my $reached = False;
{
    CONTROL { default { } }
    CATCH   { default { } }
    $reached = True;
}
die 'CONTROL + CATCH in one block broken' unless $reached;
say 'PASS';
