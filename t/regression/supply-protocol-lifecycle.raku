# Regression: the Supply protocol — how a Supplier, its taps and their end
# behave. Section A of docs/dev/findings/semantics/Supply.md (items S-01 to
# S-16), extracted from Rakudo 2026.08 and implemented here from the sheet.
#
# What was wrong before: `Supply.new` answered a Method-not-found instead of
# X::Supply::New; `.serial` printed the hash's contents; a Supplier went on
# feeding a tap after `done`; a re-entrant emit nested the emitting tap's own
# handler; `Supplier.quit` with no quit handler vanished instead of surfacing
# in the emitter; Supplier::Preserving never replayed; `Supply.from-list` ran
# the single-argument rule over every argument; the exception thrown by a
# combinator carried no `.combinator`; and an exception in a `.map`/`.do`
# block escaped the tap call and killed the program.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- S-01  a Supply has no public constructor --------------------------------
check ((try Supply.new) // $!.^name), 'X::Supply::New', 'Supply.new throws X::Supply::New';

# --- S-02  tap defaults, the :tap callback, and the rethrowing quit ----------
{
    my @o;
    my $t = Supply.from-list(1, 2).tap(-> $v { @o.push($v) }, tap => -> $tap { @o.push($tap.^name) });
    check @o, ['Tap', 1, 2], ':tap sees the Tap before any value flows';
    check $t.^name, 'Tap', '.tap answers a Tap';
}
{
    my $s = Supplier.new;
    my $x = try { $s.Supply.tap({;}); $s.quit("boom"); "no-throw" };
    check ($! ?? $!.^name ~ ':' ~ $!.message !! $x), 'X::AdHoc:boom',
          'a quit with no quit handler rethrows in the emitter';
}
{   # …and one with a handler does not
    my $s = Supplier.new;
    my @o;
    my $x = try { $s.Supply.tap({;}, quit => { @o.push('q:' ~ .message) }); $s.quit("boom"); "no-throw" };
    check $x, 'no-throw', 'a quit handler consumes the quit';
    check @o, ['q:boom'], 'the handler sees the X::AdHoc message';
}

# --- S-03  Tap.new and its close hook ---------------------------------------
check Tap.new.close, True, 'Tap.new.close is True';
check Tap.new({ 42 }).close, True, 'Tap.new(&hook).close is True';
{
    my $n = 0;
    my $t = Tap.new({ $n++ });
    $t.close; $t.close;
    check $n, 2, 'the close hook runs on every close call';
}

# --- S-04  live and serial --------------------------------------------------
check Supply.from-list(1).live, False, 'from-list is not live';
check Supplier.new.Supply.live, True, 'a Supplier-backed Supply is live';
check Supply.interval(1).live, False, 'interval is not live';
check Supplier.new.Supply.map({ $_ }).live, True, 'map carries liveness';
check Supply.from-list(1).serial, True, 'from-list is serial';
check Supplier.new.Supply.serial, True, 'a live Supply is serial';
check (supply { emit 1 }).serial, True, 'a supply block is serial';

# --- S-05  a Supplier fans out to the taps present at emit time -------------
{
    my $s = Supplier.new;
    $s.emit(1);
    my @o;
    $s.Supply.tap({ @o.push($_) });
    $s.emit(2);
    check @o, [2], 'values emitted before the tap existed are lost';
}
{
    my $s = Supplier.new;
    my @o; my $t;
    $t = $s.Supply.tap({ @o.push($_); $t.close if $_ == 2 });
    $s.emit($_) for 1 .. 4;
    check @o, [1, 2], 'a tap that closes itself stops after that value';
}

# --- S-06  the event grammar is enforced per tap ----------------------------
{
    my $s = Supplier.new;
    my @o;
    $s.Supply.tap({ @o.push($_) }, done => { @o.push('done') });
    $s.emit(1); $s.done; $s.emit(2); $s.done;
    check @o, [1, 'done'], 'emit and done after done are dropped';
}
{
    my $s = Supplier.new;
    my @o;
    $s.Supply.tap({ @o.push($_) }, done => { @o.push('done') }, quit => { @o.push('quit') });
    $s.emit(1); $s.done; $s.quit("x");
    check @o, [1, 'done'], 'a quit after done is dropped';
}
{   # …but the rule is PER TAP: a Supplier that is done still feeds a later tap
    my $s = Supplier.new;
    $s.Supply.tap({;});
    $s.done;
    my @o;
    $s.Supply.tap({ @o.push($_) }, done => { @o.push('done') });
    $s.emit(7);
    check @o, [7], 'a tap taken after done is a fresh tap';
}

# --- S-07  a re-entrant emit is deferred for the emitting tap only ----------
{
    my $s = Supplier.new;
    my @o;
    $s.Supply.tap({ @o.push("a$_"); $s.emit(9) if $_ == 1 });
    $s.Supply.tap({ @o.push("b$_") });
    $s.emit(1);
    check @o, ['a1', 'b9', 'a9', 'b1'], 'one tap never nests inside itself';
}

# --- S-08  quit(Str) is an X::AdHoc, quit(Exception) passes through ---------
{
    my $s = Supplier.new;
    my @o;
    $s.Supply.tap({;}, quit => { @o.push(.^name ~ ':' ~ .payload) });
    $s.quit("q");
    check @o, ['X::AdHoc:q'], 'a string quit is an X::AdHoc with that payload';
}
{
    my $s = Supplier.new;
    my $x = try { $s.Supply.tap({;}); $s.quit(X::AdHoc.new(payload => "q")); "no-throw" };
    check ($x // $!.message), 'q', 'an exception quit travels as it is';
}

# --- S-09  Supplier::Preserving replays, and resumes preserving -------------
{
    my $s = Supplier::Preserving.new;
    $s.emit(1); $s.emit(2);
    my @o;
    $s.Supply.tap({ @o.push($_) });
    $s.emit(3);
    check @o, [1, 2, 3], 'the kept events reach the first tap, then it is live';
}
{
    my $s = Supplier::Preserving.new;
    $s.emit(1);
    my $t = $s.Supply.tap({;});
    $t.close;
    $s.emit(2);
    my @o;
    $s.Supply.tap({ @o.push($_) });
    check @o, [2], 'preserving resumes when the last tap closes';
}
{
    my $s = Supplier::Preserving.new;
    $s.emit(1); $s.done;
    my @o;
    $s.Supply.tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 'done'], 'done is preserved too';
}
{   # only the FIRST tap gets the replay
    my $s = Supplier::Preserving.new;
    $s.emit(1);
    my @a; my @b;
    $s.Supply.tap({ @a.push($_) });
    $s.Supply.tap({ @b.push($_) });
    $s.emit(2);
    check @a, [1, 2], 'the first tap replays and then runs live';
    check @b, [2],    'a second tap sees only what follows';
}

# --- S-10  sanitize, serialize --------------------------------------------
check Supply.from-list(1, 2, 3).serialize.list, (1, 2, 3), 'serialize keeps the values';
check Supply.from-list(1, 2, 3).sanitize.list,  (1, 2, 3), 'sanitize keeps the values';

# --- S-11  act = sanitize then tap ------------------------------------------
{
    my @o;
    (supply { whenever Supply.from-list(1, 2) { emit $_ } }).act({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 2, 'done'], 'act delivers values then done';
}

# --- S-12  on-close runs on every close call --------------------------------
{
    my $s = Supplier.new;
    my $closed = 0;
    my $t = $s.Supply.on-close({ $closed++ }).tap({;});
    $t.close; $t.close;
    check $closed, 2, 'the on-close hook runs per close call';
}
{   # …and not on a natural done
    my $s = Supplier.new;
    my $closed = 0;
    $s.Supply.on-close({ $closed++ }).tap({;});
    $s.emit(1); $s.done;
    check $closed, 0, 'on-close does not fire on done';
}

# --- S-13  X::Supply::Combinator names the combinator -----------------------
check (do { my $e = try { Supply.merge(Supply, Supply) }; $! ?? $!.combinator !! 'no-error' }),
      'merge', 'merge names itself in the exception';
check (do { my $e = try { Supply.zip(Supply) }; $! ?? $!.combinator !! 'no-error' }),
      'zip', 'zip names itself in the exception';

# --- S-14  nothing follows a quit (Rakudo leaks the later values; we do not) -
{
    my @o;
    Supply.from-list(1, 2, 3).map({ die "bad" if $_ == 2; $_ })
          .act({ @o.push($_) }, quit => { @o.push('quit:' ~ .message) });
    check @o, [1, 'quit:bad'], 'a map that dies quits, and the stream stops';
}
{
    my @o;
    Supply.from-list(1, 2, 3).do({ die "d" if $_ == 2 })
          .tap({ @o.push($_) }, quit => { @o.push('quit:' ~ .message) });
    check @o, [1, 'quit:d'], 'a do that dies quits, and the stream stops';
}

# --- S-16  from-list follows the single-argument rule -----------------------
check Supply.from-list([1, 2, 3]).list.elems, 3, 'one Iterable argument flattens';
check Supply.from-list((1, 2), (3, 4)).list.elems, 2, 'several arguments each stay whole';
check Supply.from-list().list.elems, 0, 'no arguments is an empty supply';
check Supply.from-list(1 .. 3).list.elems, 3, 'one Range flattens';
check Supply.from-list((1, 2).Slip, (3, 4).Slip).list.elems, 4, 'a Slip always splices';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
