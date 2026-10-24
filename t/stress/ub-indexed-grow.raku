# CONTRACT test: N workers store into one array with NO lock, each store past
# the end when it is made — `@r[$i] = …` auto-extends the array. That is a
# data race (Rakudo loses writes on it); the VALUES are undefined behavior and
# nothing here checks them. What the memory model promises is that the RUNTIME
# survives. Before the auto-extend took the array's stripe, two workers grew
# one buffer at once and every parallel run died (SIGABRT, SIGBUS, SIGSEGV:
# realloc of a buffer another thread had already moved).
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 10_000).Int;
# a block per worker: the last worker's first store grows the array past every
# other worker's block, and from then on they all grow it in step
my @blocks;
await (^$N).map: -> $w { start { @blocks[$w * $M + $_] = $_ for ^$M } };
# interleaved: worker $w stores $w, $w + N, $w + 2N, … so nearly every store
# contends for the end of the array
my @stride;
await (^$N).map: -> $w { start { @stride[$w + $N * $_] = $_ for ^$M } };
# a typed array fills its holes with the element type, a separate fill path
my Int @typed;
await (^$N).map: -> $w { start { @typed[$w + $N * $_] = $_ for ^$M } };
say "survived: blocks={@blocks.elems} stride={@stride.elems} typed={@typed.elems} of {$N * $M} each "
  ~ "(lost writes are allowed; dying is not)";
say 'PASS';
