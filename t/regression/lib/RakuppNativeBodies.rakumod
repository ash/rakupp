# The module half of t/regression/exe-module-native-bodies.raku: routines whose
# bodies `--exe` compiles natively, each leaning on one thing a native body
# has to get from the interpreter's frame or hand back to it.
unit module RakuppNativeBodies;

my $calls = 0;                  # a module-level variable, written from a sub
my %memo;                       # …and a hash, written through a subscript
constant SCALE = 10;            # a constant, read from a sub

sub bump() is export { ++$calls }
sub calls() is export { $calls }

# A sub named like a built-in: calls inside the module must reach this one.
sub sum(*@xs) is export { [+] @xs.map(* * 2) }
sub doubled-sum(*@xs) is export { sum(|@xs) }

sub scaled($x) is export { $x * SCALE }

sub fib-memo(Int $n) is export {
    return %memo{$n} if %memo{$n}:exists;
    %memo{$n} = $n < 2 ?? $n !! fib-memo($n - 1) + fib-memo($n - 2)
}

# No signature: the interpreter binds @_ in the frame.
sub count-args { @_.elems }

sub counted(*@a) is export { count-args(|@a) }

# A nested lexical sub, and a `return` from inside a block.
sub first-even(@xs) is export {
    sub even($n) { $n %% 2 }
    for @xs -> $x { return $x if even($x) }
    Nil
}

# `try` and `$!`.
sub safe-div($a, $b) is export {
    my $r = try { die "zero" if $b == 0; $a / $b };
    $! ?? "error: {$!.message}" !! $r
}

# `+@` binds its arguments one element each: a slipped list of lists stays
# a list of lists.
sub heads(+@rows) is export { @rows.map(*.[0]).join(',') }
sub head-of-tail(@aoa) is export { heads(|@aoa) }

# `%h .= map(…)` stores a Hash; `|$h` slips the pairs of an itemized hash.
sub tag-keys(%h is copy) is export {
    %h .= map({ .key => %( |.value, tagged => True ) });
    %h.keys.sort.map({ $_ ~ '=' ~ %h{$_}<tagged> }).join(' ')
}

class Base is export {
    has $!x = 1;                # shadowed by Derived's own $!x
    has @.log;
    method base-x() { $!x }
    method describe() { "base" }
    method note($m) { @!log.push($m); self }
}

class Derived is Base is export {
    has $!x = 2;
    has $.label is rw = 'd';
    has &.fn;
    has %.cells;
    method own-x() { $!x }
    method describe() { "derived/" ~ callsame() }
    method parent-describe() { self.Base::describe }
    method call-fn($v) { &!fn($v) }
    method adder() { -> $n { $!x + $n } }      # outlives the call
    method relabel($l) { self.label = $l; self.label }
    method set-cell($r, $c, $v) { %!cells{$r}{$c} = $v; %!cells{$r}{$c} }
    method !secret() { 'hidden' }
    method reveal() { self!secret }
    multi method kind(Int $) { 'int' }
    multi method kind(Str $) { 'str' }
    method with-defaults($a, $b = 3, :$c = 4) { "$a/$b/$c" }
    method copy-param($n is copy) { $n += 1; $n }
}

# A phaser keeps the routine interpreted; it must still work.
sub with-leave() is export { my $s = 'body'; LEAVE { $s = 'left' }; $s }

# `return` in a block leaves the routine the block is IN: from a native body
# through a built-in, and from an interpreted routine (the LEAVE keeps it so)
# through a native one.
sub first-big(@xs) is export { @xs.map({ return "big $_" if $_ > 10 }).eager; 'none' }
sub each-call(@xs, &f) { for @xs -> $x { f($x) }; 'each-call finished' }
sub returns-through() is export {
    LEAVE { }
    each-call([1, 2, 3], -> $x { return "returned at $x" if $x == 2 });
    'fell through'
}

# Shapes a native body hands to the interpreter, or keeps interpreted:
sub coerce-int($s) is export { Int($s) + 1 }                    # a coercion call
sub doubled-plus-one($x) is export { my \t = $x * 2; t + 1 }   # a sigilless local
sub bump-rw($n is rw) { $n++ }
sub uses-rw() is export { my $c = 0; bump-rw($c); bump-rw($c); $c }   # a write-back argument
sub dyn-inner() { $*NB-DYN }
sub dyn-outer() is export { my $*NB-DYN = 2; dyn-inner() }      # a dynamic, shadowed
sub scale-all($r is copy, $g is copy) is export { $_ /= 2 for $r, $g; "$r $g" }   # the topic aliases
sub all-but-last(@l) is export { @l[0..*-2].join(',') }                  # a WhateverCode range end

class Holder is export {
    has Int $.n is rw = 3;
    has $.items;
    method clear() { $!n = Nil; $!n }                 # Nil resets to the type
    method keep(@a) { $!items = @a; my $c = 0; for $!items { $c++ }; $c }   # a `$` attribute is one item
}
sub why-suffix($why) is export { ($why andthen ": $_" orelse "") }   # topicalizing short-circuits
