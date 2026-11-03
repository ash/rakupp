# Regression: a supply block's `whenever $promise` bodies run in the order
# their promises are kept.
#
# Each promise kept after its whenever was registered fires the body on a
# pooled worker of its own, and the workers raced to the supply: kept 0 to 9
# in a row, the values came out in another order in 19 of 20 runs, and
# supply-tapper-death-is-not-a-quit.raku's two (a TLS listener's shape) in
# about 1 run in 30 with eight copies running at once. Rakudo cues the
# `.then`s on its pool's queue in the order of the keeps, and a run of ten
# comes out in that order there (20 of 20 on Rakudo 2026.09). The keep now
# hands each delivery its turn, and a body's worker waits until the ones
# before it have their place in the supply's queue.
#
# Contract: exit 0 + last line PASS.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

for 1..3 -> $round {
    my @p = Promise.new xx 10;
    my @got;
    my $all = Promise.new;
    my $s = supply { for ^10 -> $i { whenever @p[$i] -> $v { emit $v } } }
    $s.tap: -> $v { @got.push($v); $all.keep if @got == 10 };
    @p[$_].keep($_) for ^10;
    await Promise.anyof($all, Promise.in(10));
    ck @got, [^10], "round $round: ten promises kept in a row deliver in that order";
}

# …and the same through a whenever registered from another whenever's body,
# as a listener registers each connection's
{
    my @got;
    my $conns = Supplier.new;
    my @accepted = Promise.new xx 5;
    my $all = Promise.new;
    my $listener = supply {
        whenever $conns.Supply -> $i { handle($i) }
        sub handle($i) { whenever @accepted[$i] -> $conn { emit $conn } }
    }
    $listener.tap: -> $conn { @got.push($conn); $all.keep if @got == 5 };
    $conns.emit($_) for ^5;
    @accepted[$_].keep("conn $_") for ^5;
    await Promise.anyof($all, Promise.in(10));
    ck @got, ["conn $_" for ^5], 'promises a whenever body registered deliver in keep order too';
}

say 'PASS';
