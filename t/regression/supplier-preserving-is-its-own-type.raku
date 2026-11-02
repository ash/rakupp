# Regression: a Supplier::Preserving IS a Supplier::Preserving.
#
# `Supplier::Preserving.new` made a Supplier with a flag that keeps events for
# a late tap, and that was all the type there was: `.^name` said Supplier, and
# `~~ Supplier::Preserving` was False. Text::CSV's `csv(out => Supply)` makes
# one and hands back its Supply from a `when Supplier::Preserving` arm, which
# never matched, so the call returned True and t/90_csv.t died on `.tap`;
# `rakupp install Text::CSV` refused the dist.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $p = Supplier::Preserving.new;
check '.^name',                      $p.^name, 'Supplier::Preserving';
check '~~ Supplier::Preserving',     $p ~~ Supplier::Preserving, True;
check '~~ Supplier',                 $p ~~ Supplier, True;
check '.isa(Supplier)',              $p.isa(Supplier), True;
check 'a plain one is not preserving', Supplier.new ~~ Supplier::Preserving, False;
check 'a plain one is a Supplier',   Supplier.new.^name, 'Supplier';
sub takes(Supplier::Preserving $s) { 'bound' }
check 'binds a typed parameter',     takes($p), 'bound';
check 'refuses a plain one',         (try takes(Supplier.new)) // $!.^name, 'X::TypeCheck::Binding::Parameter';

# as Text::CSV has it
sub csvish(Any :$out! is copy) {
    given $out { when Supply:U { $out = Supplier::Preserving.new } }
    $out.emit($_) for 1, 2;
    given $out {
        when Supplier::Preserving { $out.done; return $out.Supply }
        when Channel { return 'a channel' }
    }
    'fell through'
}
my $s = csvish(out => Supply);
check 'the Supply comes back', $s.^name, 'Supply';
my @got; $s.tap(-> \r { @got.push: r });
check 'and replays what was kept', @got, [1, 2];

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
