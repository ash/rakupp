# Regression: `.map`, `.grep` and `.do` on an on-demand Supply (a `supply {}`
# block) are on-demand too — Rakudo builds each as a supply block that taps the
# source when IT is tapped. They drained the block eagerly instead: it ran
# before anything tapped, and every value it emitted later was lost. Cro's
# web-socket route returns `$pipeline.transformer(…).map(*.data)`, and the
# upgrade died before the 101 response went out.
# Contract: exit 0 + last line PASS.
my @fail;

# nothing runs until the tap, and later emits arrive
{
    my @log;
    my $src = Supplier.new;
    my $s = supply { @log.push('tapped'); whenever $src.Supply -> $v { emit $v * 10 } };
    my $m = $s.map(* + 1);
    @log.push('mapped');
    my $g = $m.grep(* > 15);
    @log.push('grepped');
    my @got;
    $g.tap(-> $v { @got.push($v) });
    $src.emit($_) for 1..3;
    @fail.push("order: @log[]") unless @log eqv ['mapped', 'grepped', 'tapped'];
    @fail.push("values: @got[]") unless @got eqv [21, 31];
}

# value-context consumers still see every value
{
    my $s = supply { emit 1; emit 2; emit 3; done };
    @fail.push("map.list") unless $s.map(* * 2).list eqv (2, 4, 6);
    @fail.push("grep.list") unless $s.grep(* > 1).list eqv (2, 3);
    @fail.push("grep regex") unless $s.grep(/2/).list eqv (2,);
    my @seen;
    @fail.push("do.list") unless $s.do({ @seen.push($_) }).list eqv (1, 2, 3);
    @fail.push("do side effect") unless @seen eqv [1, 2, 3];
    @fail.push("Promise") unless (await $s.map(* + 100).Promise) == 103;
    my @r;
    react { whenever $s.map(* ~ '!') { @r.push($_) } }
    @fail.push("react: @r[]") unless @r eqv ['1!', '2!', '3!'];
    @fail.push("pairs") unless $s.map({ $_ => $_ ** 2 }).list eqv (1 => 1, 2 => 4, 3 => 9);
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
