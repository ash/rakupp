# `--lint`'s new-arg-matches-no-attribute reads the class a `.new` builds
# (2026-11-06). It registered the FIRST declaration of each name, so a
# forward declaration (`class T { ... }`, which a class two others refer to
# needs) stood in for the class and had no attributes: `T.new(line => 1)` was
# flagged though T declares `$.line`. showcase/ledger's Transaction is
# declared that way. And an `augment class A { has $.y }` added an attribute
# the check never saw, so `A.new(y => 2)` was flagged as well. A stub no
# longer claims the name, and an augment's attributes count as its class's —
# while a real typo is still caught, now naming the attribute that exists.
# Contract: exit 0 + last line PASS.

if $*RAKU.compiler.name ne 'Raku++' {
    note 'lint-new-args-stub-and-augment: --lint is rakupp\'s';
    say 'PASS';
    exit 0;
}

my $src = $*TMPDIR.add("lint-new-args-stub-$*PID.raku");
$src.spurt: q:to/END/;
    use MONKEY-TYPING;
    class T { ... }
    class P { has T $.t }
    class T { has $.line }
    say T.new(line => 1).line;
    class A { has $.x }
    augment class A { has $.y }
    say A.new(x => 1, y => 2).y;
    say T.new(lien => 1).line;
    END

my $p = run($*EXECUTABLE, '--lint', $src.Str, :out, :err);
my @found = ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.grep(*.contains('[new-arg-matches-no-attribute]'));
$src.unlink;

my @fail;
@fail.push("expected one finding, got {+@found}: @found[]") unless @found == 1;
@fail.push("the finding is not the typo on line 9: @found[]")
    unless @found && @found[0].contains(':9:') && @found[0].contains("'lien'") && @found[0].contains('(has: line)');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
