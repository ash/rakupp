# Regression: what an `@` parameter binds, 2026-10-06. Found taking
# Math::SparseMatrix::Native through real use (its own suite stayed green):
# `clone` gave an all-zero matrix and every `dot`/`transpose` printed
# nrow+1 "uninitialized value" warnings. Three causes, all checked against
# Rakudo 2026.09:
#  - a LIVE CArray (here: read back from a CStruct field) was no Positional to
#    the binder, though `~~ Positional` said True: `sub f(@a)` refused it and
#    `sub f(:@a)` wrapped it as one element, whose `.Num` was its address;
#  - Nil, Any and non-Positional type objects bound to `@` as `[Any]`, so
#    `new(values => Nil, …)` picked a `:@values!` candidate Rakudo passes over;
#  - `$obj[1;0]` on a class with its own AT-POS answered Any instead of
#    calling `AT-POS(1, 0)`.

use NativeCall;

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

class S is repr('CStruct') {
    has CArray[num64] $.v;
    submethod BUILD { $!v := CArray[num64].new(7e0, 8e0) }
}
my $s = S.new;           # kept alive: the field is a view of ITS memory
my $live = $s.v;

ck($live.^name.ends-with('CArray[num64]'), True, 'a field-read CArray names its element type');

sub pos-bind(@a) { (@a.^name.ends-with('CArray[num64]'), @a[0], @a[1]) }
sub named-bind(:@a) { (@a.^name.ends-with('CArray[num64]'), @a[0], @a[1]) }
ck(pos-bind($live), (True, 7e0, 8e0), 'a live CArray binds to @a as itself');
ck(named-bind(a => $live), (True, 7e0, 8e0), '…and to :@a as itself');
ck(named-bind(a => $live)[1].Num, 7e0, 'its element is a value, not the address');

# what an @ parameter refuses
sub p(@a) { 'bound' }
sub n(:@a) { 'bound' }
my $X = "X::TypeCheck::Binding::Parameter";
ck((try n(a => Nil)) // $!.^name, $X, ":@a refuses Nil");
ck((try n(a => Any)) // $!.^name, $X, ":@a refuses Any");
ck((try n(a => Int)) // $!.^name, $X, ":@a refuses the Int type object");
ck((try n(a => 42))  // $!.^name, $X, ":@a refuses 42");
ck((try n(a => "s")) // $!.^name, $X, ":@a refuses a Str");
ck((try n(a => (1 => 2))) // $!.^name, $X, ":@a refuses a Pair");
my ($nil, $any, $int) = Nil, Any, Int;
ck((try p($nil)) // $!.^name, $X, "@a refuses Nil");
ck((try p($any)) // $!.^name, $X, "@a refuses Any");
ck((try p($int)) // $!.^name, $X, "@a refuses the Int type object");
ck(p((1, 2)), 'bound', '@a still takes a List');
ck(n(a => [3]), 'bound', ':@a still takes an Array');
ck(p(Array), 'bound', '@a still takes the Array type object');

# …so multi dispatch passes over a :@values! candidate given Nil
multi m(:@values!) { 'values!' }
multi m(*%h) { 'slurpy' }
ck(m(values => Nil), 'slurpy', 'Nil does not select :@values!');
ck(m(values => [1]), 'values!', 'an Array still does');

class Grid {
    multi method new(:@values!) { self.bless }
    submethod BUILD(:$values) { }
}
ck(Grid.new(values => Nil).^name, 'Grid', 'new(values => Nil) falls through to Mu.new');

# a multi-dimensional subscript on an object with its own AT-POS
class C { method AT-POS(*@i) { @i.join(',') } }
ck(C.new[1;0], '1,0', '$obj[1;0] is $obj.AT-POS(1, 0)');
ck(C.new[2;0;5], '2,0,5', '…for any number of dimensions');
ck(C.new[3], '3', 'a single index is unchanged');

say $ok ?? 'PASS' !! 'FAIL';
exit($ok ?? 0 !! 1);
