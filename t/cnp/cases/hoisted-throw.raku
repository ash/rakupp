# The registers are HOISTED: a slot is unboxed into a register at kernel entry
# and written back at exit. A loop that dies half way therefore has to write
# them back on the way out too, because the CATCH below is about to read them —
# and `div` by zero is a throw from inside a stencil's cold block, which is the
# path that has to carry it out through machine code with no unwind tables.
my $i = 0; my $n = 0; my $d = 5;
{
    while $i < 100 {
        $n = $n + 10 div $d;
        $d = $d - 1;
        $i = $i + 1;
    }
    CATCH { default { say "caught " ~ .^name ~ ": i=$i n=$n d=$d" } }
}
say "after: $i $n $d";
