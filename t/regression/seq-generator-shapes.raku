# BROKE (during the sequence leg, caught by gate sq1): allowing an empty
# seed for closure-only sequences (`{ 3+2 } ... *`) let an EMPTY list
# reach the bounded generator loop, whose back() on empty was UNDEFINED
# BEHAVIOR — context-dependent silent death (S03-sequence/basic.t ended
# mid-file only in file context; limit-arity-2-or-more.t hung).
# FIXED: same leg — lastV guards the empty case; a runaway cap bounds
# generator sequences whose literal endpoint never matches.
die 'alternating endpoint-miss broken'
    unless (1, { -$_ } ... 3).[^5].join(',') eq '1,-1,1,-1,1';
die 'closure-only seed broken'
    unless ({ 3+2 } ... *).[^3].join(',') eq '5,5,5';
die 'gen with reachable endpoint broken'
    unless (1, { $_ + 1 } ... 5).join(',') eq '1,2,3,4,5';
die 'empty-seed with Code endpoint broken (the UB shape)'
    unless do { my $i = -5; ({ ++$i; $i**3-9*$i } ... { ($^a-$^b) > ($^b-$^c) }).elems > 0 };
die 'plain sequences broken'
    unless (1, 2 ... 10).join(',') eq '1,2,3,4,5,6,7,8,9,10';
say 'PASS';
