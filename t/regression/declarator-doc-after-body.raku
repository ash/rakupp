# A `#=` after the closing brace of a routine's or package's body documents
# that declaration, as Rakudo's leave-block-body makes it: on the brace's
# line, or alone on the lines below it before the next code. Such a doc was
# lost — `~&f.WHY` of `#| a` / `sub f {` / `}` / `#= b` was "a", where Rakudo
# answers "a\nb". (A routine's run goes on past blank lines and plain
# comments; a package's stops at the first line without a doc.) A
# declarator block's `.raku` is the constructor naming its declarand and its
# two parts (it was the text), `.leading` and `.trailing` are Nil when a part
# is missing (they were Any), and a bracketed doc folds its lines into one,
# each trimmed and the blank ones dropped.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

#|{ a    b
    c

  d }
class P { } #= t

ck $=pod[0].leading, 'a    b c d', 'a $=pod entry keeps its leading part, folded';
ck $=pod[0].trailing, 't', '…and its trailing one';
ck ~P.WHY, "a    b c d\nt", 'a bracketed doc folds its lines';
ck P.WHY.raku, 'Pod::Block::Declarator.new(WHEREFORE => P, leading => ["a    b c d"], trailing => [["t"],], config => {}, contents => [])',
    'a declarator block\'s .raku is its constructor';

#| a
sub f {
}
#= b
ck ~&f.WHY, "a\nb", 'a #= on the line below the closing brace';
ck &f.WHY.leading, 'a', 'the leading part';
ck &f.WHY.trailing, 'b', 'the trailing part';

sub h {
    1
} #= d
ck ~&h.WHY, 'd', 'a #= on the closing brace\'s line';
ck &h.WHY.leading.raku, 'Nil', 'no leading part is Nil';

class C {
    has $.a; #= x
    method m {
    } #= mm
    method n {
    }
    #= nn
}
#= cc
ck ~C.WHY, 'cc', 'a class\'s, below its brace';
ck ~C.^lookup('m').WHY, 'mm', 'a method\'s, on its brace\'s line';
ck ~C.^lookup('n').WHY, 'nn', 'a method\'s, below it';
ck C.^attributes[0].WHY.raku,
    'Pod::Block::Declarator.new(WHEREFORE => Attribute.new, leading => [], trailing => [["x"],], config => {}, contents => [])',
    'an attribute\'s block';

sub k {
} #= k1
#= k2

# a plain comment
#= k3
ck ~&k.WHY, 'k1 k2 k3', 'a routine\'s run goes on past blank lines and comments';

sub s1 { }; sub s2 { } #= z
ck &s1.WHY.raku, 'Nil', 'a declaration later on the line takes the doc';
ck ~&s2.WHY, 'z', '…which is that declaration\'s';

sub p(
    $a, #= pa
) {
}
#= pb
ck ~&p.WHY, 'pb', 'the routine\'s, after a parameter\'s';
ck &p.signature.params[0].WHY.raku,
    'Pod::Block::Declarator.new(WHEREFORE => $a, leading => [], trailing => [["pa"],], config => {}, contents => [])',
    'a parameter\'s block';

say 'PASS';
