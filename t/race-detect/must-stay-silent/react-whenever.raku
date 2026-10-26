# race-detect: silent
# edges: react
# expect: 0 1 2 3 4
#
# A react over a Supplier fed by a worker. The whenever body runs on the
# emitting thread; the react ends when the supply is done, and what follows
# the react sees everything its whenevers did.
my $s = Supplier.new;
my @got;
my $ready = Promise.new;
my $emitter = start { await $ready; $s.emit($_) for ^5; $s.done };
react {
    whenever $s.Supply { @got.push($_) }
    $ready.keep;
}
say @got.join(' ');
