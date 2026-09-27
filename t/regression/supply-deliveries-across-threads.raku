# Regression: a supply block's deliveries from other threads were lost, torn
# and run side by side (Roast S17-supply/syntax.t test 53, which hung now and
# then with several copies running, 2026-09-28).
#
# - A whenever's value arriving while another body of the same supply was
#   running on another thread joined the activation's queue — but the thread
#   running that body decremented `running` and only then looked at the queue.
#   A value that queued in between was never delivered: syntax.t test 53
#   awaits a promise the second whenever keeps, and waited for ever.
# - The queue was a plain vector that every emitting thread pushed to with no
#   lock: four threads emitting into one supply crashed the process.
# - A `whenever Promise.in(…)` and a `whenever $channel` ran their bodies
#   straight from their workers, alongside whatever else of the supply was
#   running, where a supply block runs one body at a time.
#
# Expectations checked against Rakudo 2026.08 as `rakudo`
# (/opt/homebrew/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    # syntax.t test 53's shape, many times: the second whenever's value comes
    # from another thread while the first whenever is still running
    my ($lost, $wrong) = 0, 0;
    for ^1000 {
        my $trigger1 = Supplier.new;
        my $trigger2 = Supplier.new;
        my $p1 = Promise.new;
        my $p2 = Promise.new;
        my $p3 = Promise.new;
        my $s = supply {
            whenever $trigger1 -> $v { $p1.keep(True); await $p2; emit "a $v" }
            whenever $trigger2 -> $v { emit "the $v"; $p3.keep(True) }
        }
        my @got;
        my $tap = $s.tap({ @got.push($_) });
        start { $trigger1.emit('bear') };
        await $p1;
        start { $trigger2.emit('wolf') };
        $p2.keep(True);
        await Promise.anyof($p3, Promise.in(0.5));
        if !$p3 { $lost++ }
        elsif !(@got eqv ['a bear', 'the wolf']) { $wrong++ }
        $tap.close;
    }
    ck $lost, 0, 'a value queued behind a body on another thread is delivered';
    ck $wrong, 0, '…after that body, one whenever at a time';
}

{
    # four threads emitting into one supply at once: by the time they have all
    # returned, every value has been through its whenever
    my $short = 0;
    for ^20 {
        my @s = (^4).map: { Supplier.new };
        my atomicint $seen = 0;
        my $tap = (supply { for @s -> $s { whenever $s { $seen⚛++ } } }).tap;
        await (^4).map: -> $i { start { @s[$i].emit($_) for ^500 } };
        $short++ unless $seen == 2000;
        $tap.close;
    }
    ck $short, 0, 'concurrent emits into one supply all arrive';
}

{
    # a timer's and a channel's whenever wait their turn like any other
    my atomicint $inside = 0;
    my $overlaps = 0;
    my $s = Supplier.new;
    my $ch = Channel.new;
    my $tap = (supply {
        whenever $s { $overlaps++ if ++⚛$inside > 1; sleep 0.002; --⚛$inside }
        whenever Promise.in(0.05) { $overlaps++ if ++⚛$inside > 1; sleep 0.05; --⚛$inside }
        whenever $ch { $overlaps++ if ++⚛$inside > 1; sleep 0.002; --⚛$inside }
    }).tap;
    await start { for ^40 { $s.emit($_); sleep 0.001 } },
          start { for ^40 { $ch.send($_); sleep 0.001 } };
    sleep 0.15;
    ck $overlaps, 0, 'no two bodies of one supply run at once';
    $ch.close;
    $tap.close;
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
