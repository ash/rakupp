# Regression: with SEVERAL versions of one dist installed, `use` loaded
# whichever short/<sha1(name)>/ entry readdir() returned first — directory
# hash order, not a choice. The store grows a second version routinely (a
# dependency pinned `:ver<0.1.7>` installs beside an already-present 0.1.8,
# nothing removes the other), and the day that happened to
# Statistics::Distributions, `known-distributions` shrank from 132 keys to 80
# and `.Hash` on a distribution object vanished mid-page on raku.online —
# the loader had silently switched from 0.1.8 to 0.1.7. Rakudo, reading the
# SAME store, kept answering from 0.1.8: its CURI resolves multi-candidate
# names to the newest version satisfying the constraint.
#
# Latent since the short-index resolvers were written (loadModule and the
# parser's findModuleSourceFor both took the first entry); exposed the moment
# a store first held two versions of one name. Both now resolve through
# pickInstalledDist: the NEWEST candidate that satisfies the `use` constraint,
# with versions compared numerically per segment ("0.0.10" > "0.0.9").
#
# The store below is handcrafted in the CURI layout the engine itself writes
# (sources/<key>, short/<sha1(name)>/<dist-id> five-liners, dist/<dist-id>):
# three versions of Regr::DualVer, whose dist ids readdir may hand back in
# any order. Each check runs in a child so every `use` resolves fresh.
# Contract: exit 0 + last line PASS.
my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $tmp    = $*TMPDIR.add("rakupp-newest-{$*PID}");
my $store  = $tmp.add('store');
my $home   = $tmp.add('home');    # fake HOME: the child must see ONLY this store
.mkdir for $tmp, $store, $home, $store.add('sources'), $store.add('dist');

# sha1("Regr::DualVer") — the short-index directory name for the module
my $short = $store.add('short/C8AE22157D0A7FC1D8C9712100B18ED58C241572');
$short.mkdir;

sub install-version(Str $ver, Str $dist-id, Str $extra = '') {
    my $src-key = "SRC-$ver";
    $store.add("sources/$src-key").spurt:
        qq:to/END/;
        unit module Regr::DualVer;
        sub picked is export \{ "$ver" \}
        $extra
        END
    # ver / auth / api / source-key / dist-id — the writer's exact five lines
    $short.add($dist-id).spurt: "$ver\ntest:regr\n1\n$src-key\n$dist-id\n";
    $store.add("dist/$dist-id").spurt:
        '{"name":"Regr::DualVer","ver":"' ~ $ver ~ '","auth":"test:regr",'
        ~ '"provides":{"Regr::DualVer":"lib/Regr/DualVer.rakumod"}}';
}
# dist-id names chosen so no candidate order is "already sorted": the newest
# version sits between the others alphabetically, and 0.0.10 beats 0.0.9 only
# under NUMERIC segment comparison (a string compare says '1' < '9').
install-version('0.0.2',  'REGR-DUAL-AAA');
install-version('0.0.9',  'REGR-DUAL-ZZZ');
install-version('0.0.10', 'REGR-DUAL-MMM',
    'sub infix:<dpick>($a, $b) is export { "$a|$b" }');

sub child(Str $code) {
    my $p = run 'env', "HOME={$home}", 'RAKULIB=',
                $rakupp, '-I', "inst#{$store}", '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    $out eq '' && $err ne '' ?? "ERR: $err".chomp !! $out
}

# the headline: an unconstrained `use` gets the newest version, not readdir's
@fail.push("unconstrained: got {child(Q[use Regr::DualVer; print picked]).raku}, want 0.0.10")
    unless child(Q[use Regr::DualVer; print picked]) eq '0.0.10';

# an exact pin still reaches the older version it names
@fail.push("exact pin: got {child(Q[use Regr::DualVer:ver<0.0.9>; print picked]).raku}, want 0.0.9")
    unless child(Q[use Regr::DualVer:ver<0.0.9>; print picked]) eq '0.0.9';

# a range takes the newest that satisfies, not the first found satisfying
@fail.push("range pin: got {child(Q[use Regr::DualVer:ver<0.0.2+>; print picked]).raku}, want 0.0.10")
    unless child(Q[use Regr::DualVer:ver<0.0.2+>; print picked]) eq '0.0.10';

# the PARSER resolves the same candidate: `dpick` is exported only by 0.0.10,
# and an operator must be seen at parse time or the program does not compile
@fail.push("parser scan: got {child(Q[use Regr::DualVer; print 1 dpick 2]).raku}, want 1|2")
    unless child(Q[use Regr::DualVer; print 1 dpick 2]) eq '1|2';

unlink $_ for $store.add('sources').dir, $short.dir, $store.add('dist').dir;
rmdir $_  for $store.add('sources'), $short, $store.add('short'),
              $store.add('dist'), $store, $home, $tmp;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
