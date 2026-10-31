# Regression: two candidates of one role that differ only by a named
# parameter both survive composition (issue #77, FCO's "inherited
# positional constructors are not found").
#
# Red::AST::Infix declares `multi method new($left, $right, :$cast!)` and
# `multi method new($left, $right)`. Composition keys a candidate by its
# positional signature, nameds left out, so the two share a key — and three
# places compared a role's candidates against the ones they had just added
# from the same role:
#   - a class's own multi group, merging its parent role's candidates, kept
#     the `:$cast!` one and dropped the plain one, so
#     `Red::AST::Concat.new($left, $right)` fell through to the default
#     constructor ("only takes named arguments");
#   - a role composed by copy (`class C is Any does I does I2`) did the same;
#   - and a second role with any candidate of that name made the pair a
#     conflict the class "must resolve".

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

role Infix {
    has $.left is required;
    has $.right is required;
    proto method new($left, $right, |) {*}
    multi method new($left, $right, Str() :$cast!) { ::?CLASS.new($left, $right) }
    multi method new($left, $right) { ::?CLASS.Mu::new: :$left, :$right }
}
class Concat { ... }
class Concat does Infix {
    has $.op = '||';
    multi method new($left, $right where * eq '', *%) { $left }
}
ck Concat.new('a', 'b').right, 'b', 'the plain positional candidate beside the class\'s own';
ck Concat.new('a', ''), 'a', 'the class\'s own candidate still wins its case';
ck Concat.new('a', 'b', :cast<int>).left, 'a', 'the required-named candidate too';

role I { multi method m(Int $a) { 'I' } }
role I2 {
    multi method m($a, :$x!) { 'x' }
    multi method m($a) { 'plain' }
}
class ByCopy is Any does I does I2 { }
ck ByCopy.new.m('s'), 'plain', 'a role composed by copy keeps both';
ck ByCopy.new.m('s', :x), 'x', '…the named one included';
ck ByCopy.new.m(1), 'I', '…beside the first role\'s';

class TwoRoles does I does I2 { }
ck TwoRoles.new.m('s'), 'plain', 'no conflict between two roles on different signatures';

# a real conflict is still one
role J { multi method m(Int $a) { 'J' } }
my $died = False;
try { EVAL 'class Clash does I does J { }'; CATCH { default { $died = True } } }
ck $died, True, 'the same signature in two roles is still a conflict';

say 'PASS';
