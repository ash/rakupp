# Regression (#261): `CArray[size_t].new(5, 6)` was Nil in a natively compiled
# binary. The parser read a type parameter as a TYPE only when it was
# capitalized or one of Raku's own natives, so NativeCall's C spellings
# (`size_t`, `ssize_t`, `long`, `ulong`, `bool`, …) made `CArray[size_t]` a
# subscript of the type object — which the interpreter answers as the
# parameterized type, but --exe compiled to rtIndexGet: Nil, and a NULL handed
# to C. Both halves are covered: the literal C spelling, and a type parameter
# that only arrives at run time (`CArray[$T]`, `Array[$T]`, `Buf[$U]`), which
# the native path now parameterizes as the interpreter does.
# Each program runs interpreted and compiled; the binary must print the same.
# Contract: exit 0 + last line PASS.
my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe261-{$*PID}");
$dir.mkdir;

sub both(Str $name, Str $src, Str $want) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my $i = run($rakupp, $f.absolute, :out, :err);
    my $iout = $i.out.slurp(:close).trim; $i.err.slurp(:close);
    @fail.push("$name interpreted: got「$iout」") unless $iout eq $want;
    my $c = run($rakupp, '--exe', $f.absolute, '-o', $dir.add($name).absolute, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    if $c.exitcode != 0 { @fail.push("$name: compile failed: $cout"); return }
    my $r = run($dir.add($name).absolute, :out, :err);
    my $out = $r.out.slurp(:close).trim; $r.err.slurp(:close);
    @fail.push("$name compiled: got「$out」") unless $out eq $want;
}

# 1. the C spellings, written literally: in a `say`, assigned, returned from a
#    sub, and assigned to an attribute in TWEAK, as the issue lists them
both 'c-spellings', q:to/P/, q:to/W/.trim;
    use NativeCall;
    say CArray[size_t].new(5, 6).raku;
    my $a = CArray[size_t].new(5, 6);
    say $a.defined, " ", $a.elems, " ", $a[0] + $a[1];
    my $s = CArray[ssize_t].new(-1, 2); say $s[0], " ", $s.^name;
    sub f { CArray[ulong].new(7) }; say f()[0];
    class C { has $.x; submethod TWEAK { $!x = CArray[longlong].new(9) } }; say C.new.x[0];
    say CArray[long].new(3, 4).elems;
    P
    NativeCall::Types::CArray[size_t].new
    True 2 11
    -1 NativeCall::Types::CArray[ssize_t]
    7
    9
    2
    W

# 2. a type parameter that arrives at run time: the binary compiles natively
#    and must still parameterize, not subscript, the type object
both 'runtime-param', q:to/P/, q:to/W/.trim;
    use NativeCall;
    my $T = size_t; my $c = CArray[$T].new(3, 4); say $c.^name, " ", $c[1];
    my $I = Int; say Array[$I].new(1).raku;
    my $U = int8; say Buf[$U].new(-5).raku;
    constant K = (1, 2, 3); my $i = 1; say K[$i];
    P
    NativeCall::Types::CArray[size_t] 4
    Array[Int].new(1)
    Buf[int8].new(-5)
    2
    W

run('rm', '-rf', $dir.absolute);
if @fail { .say for @fail; exit 1 }
say "PASS";
