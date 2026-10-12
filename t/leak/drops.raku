# Cycles stranded by a drop no scope exit sees: an outer variable reassigned,
# an element replaced, a key deleted, an array emptied, a sunk result, a
# shift, an attribute overwritten, a map's result list cleared.
use Test;
my atomicint $freed = 0;
class N { has $.other is rw; submethod DESTROY { $freed⚛++ }; method link { $!other = self; self } }
sub check($name, &code, $n = 1000) {
    $freed ⚛= 0;
    code($n);
    $*VM.request-garbage-collection for ^3;
    ok $freed >= $n * 0.95, "$name: freed $freed of $n";
}
check 'outer variable reassigned', -> $n { my $x; for ^$n { $x = N.new; $x.other = $x }; $x = Nil };
check 'element replaced', -> $n { my @a; for ^$n { @a[0] = N.new; @a[0].other = @a[0] }; @a[0] = Nil };
check 'key deleted', -> $n { my %h; for ^$n { %h<k> = N.new; %h<k>.other = %h<k>; %h<k>:delete } };
check 'array emptied', -> $n { my @l; for ^$n { @l.push: N.new; @l[*-1].other = @l[*-1] }; @l = (); 1 };
check 'sunk result', -> $n { N.new.link for ^$n };
check 'shift', -> $n { my @a; for ^$n { @a.push(N.new.link); @a.shift } };
check 'attribute overwritten', -> $n { my $h = N.new; for ^$n { $h.other = N.new.link }; $h.other = Nil };
check 'map result cleared', -> $n { my @x = (^$n).map({ N.new.link }); @x = (); 1 };
done-testing;
