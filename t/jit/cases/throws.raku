# An exception raised INSIDE a kernel has to unwind out of a dlopen'd image and
# back into the interpreter, and be caught by a CATCH the kernel knows nothing
# about. The loop's own variables must read correctly afterwards.
my $bad = "zz"; my $s = 0; my $i = 0;
try {
    while $i < 100000 {
        $s = $s + 1;
        $s = $s + $bad if $i == 77;
        $i = $i + 1;
    }
    CATCH { default { say "caught " ~ .^name ~ " at i=$i" } }
}
say "after: s=$s i=$i";
my $j = 0; my $t = 0;
while $j < 1000 { $t = $t + $j; $j = $j + 1 }
say "and the next loop still tiers up: $t";
