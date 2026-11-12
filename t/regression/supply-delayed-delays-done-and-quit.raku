# Regression: `.delayed` delays the source's done and quit by its window, as
# Rakudo's Delayed tappable does (Supply-factories.rakumod). It was a supply
# block with a Promise.in per value and nothing for the ends, so done and
# quit passed on at once: an empty source was done before the delay, and a
# quit cut off the value still waiting — `$s.emit(1); $s.quit(…)` on a
# `.delayed(0.3)` delivered nothing, where Rakudo delivers 1 and then quits.
#
# Timing checks are lower bounds only (a timer never fires early).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

{
    my $t0 = now;
    my @l = supply { }.delayed(0.3).list;
    check (@l, now - $t0 >= 0.25), ([], True), 'an empty source is done after the delay';
}
{
    my $s = Supplier.new;
    my @got;
    my $t0 = now;
    my $p = Promise.new; my $v = $p.vow;
    $s.Supply.delayed(0.3).tap({ @got.push($_) }, quit => { $v.keep((.message, now - $t0)) });
    $s.emit(1); $s.quit(X::AdHoc.new(payload => 'boom'));
    await Promise.anyof($p, Promise.in(5));
    my ($msg, $dt) = $p.status ~~ Kept ?? $p.result !! ('no quit', 0);
    check (@got, $msg, $dt >= 0.25), ([1], 'boom', True),
          'a value emitted before a quit arrives, and the quit after the delay';
}
{
    my @ev;
    react { whenever supply { emit 1; emit 2; emit 3 }.delayed(0.05) { @ev.push($_); LAST { @ev.push('done') } } }
    check @ev, [1, 2, 3, 'done'], 'the values, then the done';
}
check supply { emit 1; sleep 0.3; emit 2; sleep 0.3 }.delayed(0.05).list, (1, 2),
      'a source that blocks in its own tap loses nothing';
{
    my $for = Supply.from-list(1..3);
    check $for.delayed(0) === $for, True, 'a delay of 0 is the supply itself';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
