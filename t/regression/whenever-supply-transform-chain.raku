# Regression: `whenever $supplier.Supply.head(n)` (or .grep/.map/…) must carry the
# Supply's TRANSFORM CHAIN onto the tap it registers. The whenever path built a tap
# record with only the emit block — no chain — so:
#   a) the transform was ignored (head(2) delivered every value, not 2), and
#   b) worse, the chain never reported completion, so the tap's react source was
#      never released and the enclosing `react` blocked forever.
# (b) was masked whenever no worker thread existed: runReactLoop early-breaks on
# `!gilHeld_`, so the hang only appeared once a `start {…}` had engaged the GIL —
# which is exactly what S17-supply/head.t does before its final react test.
# Contract: exit 0 + last line PASS. A hang (no output) is a failure.
my @fail;

# a worker must exist first — that engages the GIL and disables runReactLoop's
# "no async emitter can exist" early break, which is what exposed the deadlock.
start { 1 };
sleep 0.2;

# head(n) limits AND completes (this react used to hang forever)
{
    my $s = Supplier.new;
    my @got;
    react {
        whenever $s.Supply.head(2) -> $v { @got.push($v) }
        for 1..5 { $s.emit($_) }
        done;
    }
    @fail.push("head-2 ({@got.join(',')})") unless @got eqv [1, 2];
}

# head(1) — the exact shape from S17-supply/head.t (rakudo issue 3877)
{
    my $s = Supplier.new;
    my int $seen;
    react {
        whenever $s.Supply.head(1) { ++$seen }
        $s.emit(42);
    }
    @fail.push("head-1 seen=$seen") unless $seen == 1;
}

# other chain ops travel too, and each tap gets its OWN state
{
    my $t = Supplier.new;
    my @evens;
    react {
        whenever $t.Supply.grep(* %% 2) -> $v { @evens.push($v) }
        for 1..6 { $t.emit($_) }
        done;
    }
    @fail.push("grep ({@evens.join(',')})") unless @evens eqv [2, 4, 6];
}
{
    my $m = Supplier.new;
    my @doubled;
    react {
        whenever $m.Supply.map(* * 2) -> $v { @doubled.push($v) }
        for 1..3 { $m.emit($_) }
        done;
    }
    @fail.push("map ({@doubled.join(',')})") unless @doubled eqv [2, 4, 6];
}

# an unchained whenever still works (no regression to the plain path)
{
    my $p = Supplier.new;
    my @all;
    react {
        whenever $p.Supply -> $v { @all.push($v) }
        for 1..3 { $p.emit($_) }
        done;
    }
    @fail.push("plain ({@all.join(',')})") unless @all eqv [1, 2, 3];
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
