# BROKE (during the Str-Range leg, caught by probes before the gate):
# making "a".."c" a real Range VALUE broke three consumers that had relied
# on the old eager-array representation:
#   1. storing endpoint text in Value.enumName collided with the ENUM
#      machinery — toStr/gist checked enumName first, so ~("a".."c") printed
#      "c" (endpoints now DERIVE from the rFrom/rTo codepoints instead);
#   2. .trans sides: a bare Range side and Range ELEMENTS inside array sides
#      (['a'..'c']) must flatten to chars;
#   3. pair construction stringified Range keys ('A'..'C' => 1 got key
#      "A B C"); Range keys are preserved in pairKey like other non-Str keys.
# FIXED: the Str-Range leg itself (sr1 gate).
my $r = "a".."c";
die 'stringification broken'  unless ~$r eq 'a b c';
die '.raku broken'            unless $r.raku eq '"a".."c"';
die 'membership broken'       unless "b" ~~ $r and not "d" ~~ $r;
die 'trans string-range-key broken'
    unless 'abcde'.trans( ('a..e' => 'A'..'E') ) eq 'ABCDE';
die 'trans range-key + array-of-range value broken'
    unless 'ABCDEF'.trans('A'..'C' => ['a'..'c']) eq 'abcDEF';
my $p = 'A'..'C' => 1;
die 'range pair key broken'   unless $p.key.raku eq '"A".."C"';
say 'PASS';
