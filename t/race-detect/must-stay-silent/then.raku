# race-detect: silent
# edges: then promise
# expect: 43
#
# A `.then` registered before its promise settles: the continuation starts
# with the settler's clock, so it sees what the start block wrote. The start
# block waits on $go so that the `.then` is surely registered first.
my $x;
my $go = Promise.new;
my $p = start { await $go; $x = 42 };
my $q = $p.then({ $x + 1 });
$go.keep;
say await $q;
