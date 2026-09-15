# A nested `my sub` reached through a closure that OUTLIVES the frame it was
# declared in. The frame-death heuristic cut every non-escaped nested sub's
# closure back-edge to break a reference cycle — but a closure the routine hands
# back still names those subs, and after the cut they ran with no lexicals at
# all: "Variable '$sequence' is not declared", from inside a sub whose own
# declaration was three lines above it. Terminal::ANSIParser builds its whole
# state machine that way, and every dist with a returned dispatch table does.
use Test;
plan 4;

sub make-counter() {
    my $n = 0;
    my sub bump($by) { $n += $by }
    my &via = { bump($_) };
    return sub ($by) { via($by); $n }
}

my &count = make-counter();
is count(1), 1,                 'the nested sub still sees its lexical';
is count(2), 3,                 '…and the state it shares is the same one';

my &other = make-counter();
is other(5), 5,                 'a second instance has state of its own';
is count(1), 4,                 '…and the first one is untouched';
