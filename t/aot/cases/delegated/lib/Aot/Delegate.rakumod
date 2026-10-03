# Statements a native body cannot compile are handed to the interpreter
# (rtAotExec): it runs each in a scope under the routine's frame with the
# native locals it names copied in, and copies them back out afterwards — also
# when the statement dies half-way.
unit module Aot::Delegate;

my $module-count = 0;

class Base is export {
    has $.name = 'base';
    method who() { 'Base' }   #aot: native
    method boom() { die 'boom from Base' }   #aot: native
}
class Kid is Base is export {
    method who() { 'Kid' }   #aot: native
    method parent-who() {   #aot: native
        my $s = 'parent:';
        $s ~= self.Base::who;   #aot: delegated
        $s ~ '.'
    }
}

# A qualified call: the statement writes a native local, read natively after.
sub qualified(Kid $k) is export {   #aot: native
    my $s = 'start';
    $s ~= ' ' ~ $k.Base::who;   #aot: delegated
    $s ~= ' ' ~ $k.who;
    $s
}

# The routine's last statement delegated: its value is the result.
sub tail-qualified(Kid $k) is export {   #aot: native
    my $prefix = '>';
    $prefix ~ $k.Base::who   #aot: delegated
}

# A write made before the statement dies still lands in the local: a closure
# made earlier sees it after the routine has died. (Inside a `try` block the
# statement could not be delegated — a block is a closure — and the whole
# routine would stay interpreted.)
my $peeker;
sub peek() is export {   #aot: native
    $peeker()
}
sub dies-midway(Kid $k) is export {   #aot: native
    my $x = 'before';
    $peeker = -> { $x };
    $x = 'written' and $k.Base::boom;   #aot: delegated
    'not reached'
}

# The topic of a native `for`, and a match's `$/`, reach the statement.
sub with-topic(@kids) is export {   #aot: native
    my @out;
    for @kids {
        @out.push: .Base::who ~ '-' ~ .who;   #aot: delegated
    }
    @out.join(',')
}
sub with-match(Kid $k, $s) is export {   #aot: native
    my $r = 'no match';
    if $s ~~ / (\d+) / {
        $r = $k.Base::who ~ '#' ~ $0;   #aot: delegated
    }
    $r
}

# A module variable (an outer name) written by the statement.
sub counts(Kid $k) is export {   #aot: native
    $module-count += $k.Base::who.chars;   #aot: delegated
    $module-count
}

# An accessor assignment, `.meth = v`.
class Box is export {
    has $.v is rw = 0;
}
sub set-through(Box $b, $v) is export {   #aot: native
    my $before = $b.v;
    $b.v = $v;   #aot: delegated
    "$before -> {$b.v}"
}

# A coercion to a type the module declares is a delegated call — but not in a
# declaration, which the interpreter's scope would take with it: that routine
# stays interpreted.
class Celsius is export {
    has $.deg;
    method COERCE($n) { self.new(deg => $n) }   #aot: native
}
sub to-celsius($n) is export {   #aot: interpreted (type-coercion call)
    my $c = Celsius($n);
    $c.deg ~ 'C'
}
sub to-celsius-assigned($n) is export {   #aot: native
    my $c;
    $c = Celsius($n);   #aot: delegated
    $c.deg ~ 'C'
}
