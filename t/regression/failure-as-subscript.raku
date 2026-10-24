# A Failure used as a subscript throws what it carries, as Rakudo's does. It
# is no number to index by and no string to key by, and read as one it was
# the 2 its hash holds: `@a[1 div 0] = 5` stored into @a[2], `@a[$f]` read
# it, `%h{$f} = 5` made a key of the Failure's fields, and `:exists` answered
# True. subscriptKey (the interpreter's subscripts and assignment targets),
# rtIndexGet / rtIndexRef (compiled code and the --cnp kernels) and the
# AT-POS / *-KEY methods of a built-in Array or Hash all check now, a slice's
# and a junction's elements included. Each answer is Rakudo 2026.09's.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub dies(&code, $desc) {
    my $r = try { code(); 'no exception' };
    check $r // $!.^name, 'X::Numeric::DivideByZero', $desc;
}

# a fresh Failure each time: one that has thrown is handled, and answers
# differently the next time it is used
my $one = 1;
sub F() { $one div 0 }
my @p = ^4;
my %h = a => 1;
my int @n = ^4;

dies { @p[F()] }, 'a read';
dies { @p[F()] = 5 }, 'a store';
check @p.List, (0, 1, 2, 3), '…stores nothing';
dies { @p[F()]:exists }, ':exists';
dies { @p[F()]:delete }, ':delete';
dies { @p[F(), 1] }, 'a slice';
dies { @p[F(), 1] = 7, 8 }, 'a slice store';
check @p.List, (0, 1, 2, 3), '…stores nothing';
dies { @p[F() .. 2] }, 'a Range written with it';
dies { @p[any(1, F())] }, 'a junction';
dies { @p[F()]++ }, '`++`';
dies { @p[F()] += 1 }, '`+=`';
dies { my $k = F(); @p[$k] }, 'held in a variable';
dies { my $r = @p; $r[F()] }, 'an array in a scalar';
dies { (1, 2, 3)[F()] }, 'a List';
dies { @n[F()] }, 'a native array';
dies { @n[F()] = 5 }, 'a native array store';
check @n.List, (0, 1, 2, 3), '…stores nothing';
dies { %h{F()} }, 'a hash';
dies { %h{F()} = 5 }, 'a hash store';
check %h.keys.List, ('a',), '…stores nothing';
dies { %h{F()}:exists }, 'a hash :exists';
dies { @p.AT-POS(F()) }, '.AT-POS';
dies { @p.EXISTS-POS(F()) }, '.EXISTS-POS';
dies { %h.AT-KEY(F()) }, '.AT-KEY';
my $s = 0;
my $i = 0;
dies { while $i < 5 { $s = $s + @p[$i == 3 ?? F() !! 0]; $i++ } }, 'in a loop';
check $i, 3, '…stops at the Failure';
check (try { @p[Failure.new("boom")] } // $!.message), 'boom', 'a Failure.new';
check @p[1], 1, 'an ordinary key still reads';
check %h<a>, 1, '…and an ordinary hash key';

if @fail { .say for @fail; say "FAIL"; exit 1 }
say "PASS";
