# Regression: the third batch from 3.log. Each check was run under Rakudo
# 2026.08 as well.
#   - a `whenever` over a MERGE with a signal in it installs the handler
#     before the react body goes on (6.c/MISC/bug-coverage-stress.t: the
#     child prints 'started' and is sent SIGINT that very moment)
#   - each unit sees a package through what IT uses: `Pkg::.keys` in the
#     program lists the types its own imports brought, not every
#     Pkg::* loaded anywhere (S10-packages/precompilation.t, t22)
#   - the program's `MY::` holds its own lexicals: not a type some module
#     `need`ed, nor any `Mod::EXPORT::…` (precompilation.t, t40/t41)
#   - `$cu.handle.globalish-package` is that unit's own GLOBALish
#     (precompilation.t, t37)
#   - `use lib "inst#$path"` evaluates the string and makes that store the
#     head of `$*REPO`, which does Installable and Locally (curli-install.t)
#   - an explicit store installs Distribution::Hash / ::Path, lists and
#     matches candidates, `need`s one without merging its globals (until
#     `GLOBALish.WHO.merge-symbols`), and uninstalls (S11-repository/*)
#   - a FileSystem repository answers candidates from its tree, and a
#     dist's `.content` is a handle on the file it holds
# Contract: exit 0 + last line PASS.
my @fail;

{
    my $child = q:to/CHILD/;
        react {
            whenever signal(SIGTERM).merge(signal SIGINT) { say 'pass'; exit 0 }
            whenever Promise.in(20) { exit 1 }
            whenever Promise.kept { say 'started'; $*OUT.flush }
        }
        CHILD
    my $proc = Proc::Async.new($*EXECUTABLE, '-e', $child);
    my @out; my $res;
    react {
        whenever $proc.stdout.lines {
            @out.push($_);
            $proc.kill(SIGINT) if $_ eq 'started';
        }
        whenever $proc.start { $res = $_; done }
        whenever Promise.in(30) { $proc.kill(SIGKILL); done }
    }
    @fail.push("merged signal handler in place, got {@out.raku} signal {$res.?signal // '?'}")
        unless @out eqv ['started', 'pass'] && $res && $res.signal == 0;
}

my $dir = $*TMPDIR.add("stashes-{$*PID}");
$dir.mkdir;
sub mod($name, $src) {
    my $f = $dir.add($name.subst('::', '/', :g) ~ '.rakumod');
    $f.parent.mkdir;
    $f.spurt: "no precompilation;\n$src\n";
}
mod 'Tq::P', 'role Tq::P { }';
mod 'Tq::G', 'package Tq { }';
mod 'Tq::K', 'use Tq::G; class Tq::K { }';
mod 'Tq::C', 'use Tq::K; class Tq::C { }';
mod 'Tq::F', 'use Tq::P; class Tq::F does Tq::P { }';
mod 'Tq::D', 'role Tq::D { }';
mod 'Tq::R', 'use Tq::D; class Tq::R does Tq::D { }';
mod 'Tq::A', 'use Tq::R;';
mod 'Nd', 'class Nd { method v { 2 } }';
mod 'Tp1', 'need Nd; class Tp1 { method nv { Nd.v } }';
mod 'Tp2', 'need Nd; class Tp2 { }';
mod 'Rt::One', "class Rt::One \{ }\nneed Rt::Two;";
mod 'Rt::Two', "need Rt::Three;\nclass Rt::Two \{ }";
mod 'Rt::Three', 'class Rt::Three { }';
sub run-it(*@args) {
    my $p = run $*EXECUTABLE, '-I', ~$dir, |@args, :out, :err;
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    $o
}

my $o = run-it '-e', 'use Tq::G; use Tq::F; use Tq::A; use Tq::C; print Tq::.keys.sort.join(" ")';
@fail.push("Tq:: as the program's uses bring it, got {$o.raku}") unless $o eq 'C F K P';

$o = run-it '-M', 'Tp1', '-M', 'Tp2', '-e', 'print MY::.keys.grep({ .contains("Nd") || .contains("Tp") }).sort.join(" "), " ", Tp1.nv';
@fail.push("MY:: is the program's own, got {$o.raku}") unless $o eq 'Tp1 Tp2 2';

$o = run-it '-e', q:to/CODE/;
    my @*MODULES;
    my $cu = $*REPO.need(CompUnit::DependencySpecification.new(:short-name<Rt::One>));
    my $g = $cu.handle.globalish-package;
    print $g.keys.join(","), " ", so $g<Rt>.WHO<One Two Three>:exists.all;
    CODE
@fail.push("globalish-package, got {$o.raku}") unless $o eq 'Rt True';

# an explicit installation store, end to end (in a child: `use lib` is
# compile-time, and the store must never be the user's own)
mod 'Cf', 'unit class Cf; method foo { "foo" }';
$dir.add('store').mkdir;
my $store-run = run :out, :err, :env(%*ENV, CF_STORE => ~$dir.add('store')), $*EXECUTABLE, '-I', ~$dir, '-e', q:to/CODE/, ~$dir.add('store'), ~$dir;
    use lib "inst#{%*ENV<CF_STORE>}";
    my @r;
    @r.push: ($*REPO ~~ CompUnit::Repository::Installable) && ($*REPO ~~ CompUnit::Repository::Locally);
    @r.push: $*REPO.short-id eq 'inst' && $*REPO.id.chars == 40 && $*REPO.prefix eq @*ARGS[0];
    my $prefix = @*ARGS[1].IO;
    my %provides = Cf => 'Cf.rakumod';
    my $d1 = Distribution::Hash.new({ :name<Cf>, :api<1>, :ver(v1.2.3), :%provides }, :$prefix);
    my $d2 = Distribution::Hash.new({ :name<Cf>, :api<2>, :ver(v2.3.4), :%provides }, :$prefix);
    $*REPO.install($_) for $d1, $d2;
    @r.push: (try { $*REPO.install($d1); False } // $!.message.contains('already installed'));
    my $spec = CompUnit::DependencySpecification.new(:short-name<Cf>, :api-matcher<1>);
    @r.push: $*REPO.candidates(CompUnit::DependencySpecification.new(:short-name<Cf>)).map(*.meta<ver>).join(',') eq '2.3.4,1.2.3';
    my $cu = $*REPO.need($spec);
    @r.push: $cu.version eq '1.2.3' && ::('Cf') ~~ Failure;
    GLOBALish.WHO.merge-symbols($cu.handle.globalish-package);
    @r.push: ::('Cf').foo eq 'foo' && $*REPO.loaded.elems == 1;
    @r.push: so $*REPO.uninstall($*REPO.candidates($spec).head);
    @r.push: $*REPO.candidates(CompUnit::DependencySpecification.new(:short-name<Cf>)).elems == 1;
    print @r.map({ $_ ?? 1 !! 0 }).join;
    CODE
$o = $store-run.out.slurp(:close); $store-run.err.slurp(:close);
@fail.push("an explicit installation store, got {$o.raku}") unless $o eq '11111111';

# a FileSystem repository's tree is its distribution
{
    $dir.add('fst/lib/Fz').mkdir;
    $dir.add('fst/lib/Fz/Q.rakumod').spurt: 'unit module Fz::Q;';
    $dir.add('fst/resources').mkdir;
    $dir.add('fst/resources/c.txt').spurt: 'cfg';
    my $r = CompUnit::Repository::FileSystem.new(:prefix(~$dir.add('fst/lib')));
    my @cand = $r.candidates('Fz::Q');
    my $d = @cand.head;
    @fail.push("CURFS candidates, got {@cand.elems}") unless @cand.elems == 1 && $r.candidates('Nope').elems == 0;
    @fail.push("CURFS dist files, got {$d.meta<files>.raku}")
        unless $d && $d.meta<files><resources/c.txt> eq 'resources/c.txt';
    @fail.push("Distribution.content") unless $d && $d.content('resources/c.txt').open(:bin).slurp.decode eq 'cfg';
}

sub nuke(IO::Path $p) {
    if $p.d { nuke($_) for $p.dir; try $p.rmdir }
    else { try $p.unlink }
}
nuke($dir);

if @fail { say "FAILED:"; .say for @fail; say "FAIL"; exit 1 }
say "PASS";
