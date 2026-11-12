# Regression (rakudo#5835): an index no machine could reach is refused before
# anything grows. `my $b; $b[1e18] = "meow"` grew the array a slot at a time
# until memory ran out (eight gigabytes in six seconds), and so did every other
# way to write past the end: `.ASSIGN-POS`, `.BIND-POS`, a slice, a `:=` bind,
# a nested `[0][i]`. Rakudo refuses each with "Unable to allocate an array of
# N elements" (X::AdHoc). Raku++ now does too, whenever the slots alone would
# outweigh the machine's memory, so an ordinary large array still grows.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub refused(Str $code, Str $want) {
    my $t0 = now;
    my $r = (try { EVAL $code; "no error" }) // "{$!.^name}: {$!.message}";
    @fail.push("{$code.raku}: $r") unless $r eq "X::AdHoc: $want";
    @fail.push("{$code.raku}: took {now - $t0}s") if now - $t0 > 5;
}
my $n = "Unable to allocate an array of 4611686018427387905 elements";
refused q[my @a; @a[2**62] = 1],              $n;
refused q[my $b; $b[2**62] = 1],              $n;
refused q[my @a; @a.ASSIGN-POS(2**62, 1)],    $n;
refused q[my @a; @a.BIND-POS(2**62, 1)],      $n;
refused q[my @a; @a[2**62, 0] = 1, 2],        $n;
refused q[my @a; @a[0][2**62] = 1],           $n;
refused q[my @a; my $x = 1; @a[2**62] := $x], $n;

my @ok; @ok[2**21] = 1;
@fail.push("a 2-million-slot array: {@ok.elems} elements") unless @ok.elems == 2**21 + 1;

# Rakudo panics on 1e18 (MoarVM cannot allocate 8e18 bytes): not catchable there.
if $*RAKU.compiler.name eq 'Raku++' {
    refused q[my $b; $b[1e18] = "meow"], "Unable to allocate an array of 1000000000000000001 elements";
    refused q[my int @a; @a[2**40] = 1], "Unable to allocate an array of 1099511627777 elements";
    refused q[my @a = 1, 2, 3; @a[2**40] = 5], "Unable to allocate an array of 1099511627777 elements";
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
