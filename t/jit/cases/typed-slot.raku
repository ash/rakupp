# JIT: refused
# A native container must NOT be written by a kernel: `my int8` wraps at eight
# bits on assignment, and a kernel assigns the container directly, which would
# not wrap. The JIT has to refuse the loop and leave it interpreted. The
# wrapped answer is the proof the guard fired — 300 unwrapped, 44 wrapped.
my int8 $n = 0;
my $i = 0;
while $i < 300 { $n = $n + 1; $i = $i + 1 }
say "$n $i";
