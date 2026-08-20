# Regression: a Junction ARGUMENT autothreads wherever the method is called from,
# not only from a literal method call in the source.
#
# Issue #22: `'00011', { .subst(/01/, '10', :g) } ... *.contains: none '01'` never
# terminated. The endpoint is a curried `*.contains(none '01')`, and a
# WhateverCode calls methodCall() directly rather than going through the eval arm
# where junction autothreading used to live — so it answered a plain False where
# Rakudo answers none(False), which is TRUE, and the sequence ran to the cap.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# the reported case
my @seq = '00011', { .subst(/01/, '10', :g) } ... *.contains: none '01';
check @seq.join(' '), '00011 00101 01010 10100 11000', 'the sequence terminates';

# the mechanism, directly
my &p = *.contains(none "01");
check p("11000").^name, 'Junction', 'a curried method call keeps a Junction result';
check so p("11000"),    True,       '…and it is true, which is what ended the sequence';

my &anyp = *.contains(any "0", "9");
check anyp("11000").^name, 'Junction', 'any() threads through a curried call too';
check so anyp("11000"),    True,       '…and collapses the way Rakudo does';

# the literal call was always right, and stays right
check "11000".contains(none "01").^name, 'Junction', 'a literal call still autothreads';

# MATCHER positions must NOT autothread: a junction there is a smartmatch target
check (1,2,3).grep(any(2,3)).join(','), '2,3', 'grep treats a junction as a matcher';
check ("abc" ~~ any(/b/, /z/)).so,      True,  'smartmatch against a junction of regexes';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
