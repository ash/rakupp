# The `done` control exception + whenever-source coercions (found fixing
# Supply.interval: the real ticker un-masked all of these). Passes on both
# engines.
#
# - `done` EXITS the whenever block it is in (code after it must not run),
# - `done` in a plain supply body ends the body right there,
# - `whenever $supplier` (a raw Supplier) taps its .Supply — it must NOT run
#   the block eagerly with the Supplier as topic,
# - `whenever $promise` on an UNKEPT promise REGISTERS: the block fires once
#   with the KEPT VALUE when it settles (the `whenever $kill { done }`
#   shutdown idiom), not immediately with the promise object.

# done exits the whenever block
my $x = 0;
react {
    whenever Supply.from-list(1, 2, 3) {
        done if $_ == 2;
        $x++;
    }
}
die "done did not exit the whenever block: x=$x" unless $x == 1;

# done ends a plain supply body
my @c;
my $s = supply { emit 1; done; emit 2; @c.push('body continued') };
$s.tap({ @c.push($_) });
die "done did not end the supply body: @c[]" unless @c == 1 && @c[0] == 1;

# whenever over a raw Supplier taps its .Supply
my $trigger = Supplier.new;
my $seen;
my $sup = supply {
    whenever $trigger -> $v { $seen = $v; done; }
}
my $done-p = Promise.new;
$sup.tap({ ; }, done => { $done-p.keep(True) });
$trigger.emit('payload');
await $done-p;
die "supplier whenever saw '{$seen // '(nothing)'}'" unless $seen eq 'payload';

# whenever over an unkept Promise fires ONCE with the kept value
my $p = Promise.new;
my $got;
my $keeper = start { sleep 0.1; $p.keep(42) };
react {
    whenever $p -> $v { $got = $v; done; }
}
die "promise whenever got {$got.raku}" unless $got == 42;

# the standard shutdown idiom: the react stays alive until the kill promise
my $kill = Promise.new;
my $ticks = 0;
my $killer = start { sleep 0.35; $kill.keep };
react {
    whenever Supply.interval(0.05) { $ticks++ }
    whenever $kill { done }
}
die "react died early: $ticks ticks" if $ticks < 2;

say "PASS";
