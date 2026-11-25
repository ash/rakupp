# Regression: a `my` declaration keeps a lowercase type name (#280).
#
# `my foo_t $x` lost its type: a type name was recognised by its capital
# letter, and every lowercase one was taken for a native (int, num, str …)
# that it was not. So `$x` started as Any, took any value, and the
# `my T $x .= new` idiom called .new on Any. C bindings name their structs
# this way: Cairo's `my cairo_matrix_t $matrix .= new` died there, and with
# it `raku install Gnome::Gtk4`. A lowercase name that is no native is a type.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub dies-with(&code, $type, $what) {
    my $got = 'nothing';
    try { code(); CATCH { default { $got = .^name } } }
    @fail.push("$what: died with $got, want $type") unless $got eq $type;
}
class foo_t { has $.x }
subset pos_t of Int where * > 0;

my foo_t $a;
check $a.^name, 'foo_t', 'my foo_t $a starts as (foo_t)';
dies-with { my foo_t $b = 42 }, 'X::TypeCheck::Assignment', 'my foo_t $b = 42';
my foo_t $c .= new(:x(3));
check $c.x, 3, 'my foo_t $c .= new';
my $d of foo_t .= new;
check $d.^name, 'foo_t', 'my $d of foo_t .= new';
my foo_t $e = foo_t.new; $e = Nil;
check $e.^name, 'foo_t', 'Nil resets to (foo_t)';
my foo_t @arr;
check @arr.of.^name, 'foo_t', 'my foo_t @arr is an Array[foo_t]';
@arr.push(foo_t.new);
check @arr.elems, 1, 'a foo_t pushes';
dies-with { @arr.push(1) }, 'X::TypeCheck::Assignment', 'an Int does not push';
dies-with { @arr[5] = 'x' }, 'X::TypeCheck::Assignment', 'nor store';
my foo_t %h;
check %h.of.^name, 'foo_t', 'my foo_t %h';
my pos_t $p = 5;
dies-with { $p = -1 }, 'X::TypeCheck::Assignment', 'a lowercase subset checks';
check $p, 5, '…and keeps its value';
my int32 $n = 7;      check $n, 7, 'my int32 is still native';
my str $s = 'a';      check $s, 'a', 'my str is still native';
my int @ni = 1, 2; @ni.push(3);
check @ni.List, (1, 2, 3), 'my int @ni still a native array';
my buf8 $bb .= new;   check $bb.^name, 'Buf[uint8]', 'my buf8 .= new';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
