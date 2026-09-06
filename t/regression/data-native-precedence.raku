# DATA-PLAN P6: WHICH implementation answers `use Data::Native` and
# `use <X>::Native`, in every case there is.
#
# The compiler answering these names is the whole reason a program pays nothing
# for them, and it is also the thing that can go wrong quietly: an engine that
# answers when it should stand aside makes an installed distribution dead code,
# and one that stands aside when it should answer makes every `use` cost the
# module load again. Neither shows up as a wrong answer — both sides compute the
# same digests — so nothing but this file would notice.
#
# The ladder, in the order the engine walks it:
#
#   1. a versioned `use` belongs to the store; the compiler has no version to
#      offer and answering would be inventing one
#   2. an explicit search path (-I, `use lib`) wins — this is what makes
#      `rakupp test <Dist>` test the distribution rather than the engine
#   3. an INSTALLED distribution newer than the interface this engine
#      implements wins, which is how the distributions get released on their
#      own schedule
#   4. `need` is not ours to answer at all: it asks for the compunit
#   5. otherwise the compiler answers, and nothing loads

use Test;

my $exe = $*EXECUTABLE.absolute;
sub run-with(@args, Str $code) {
    my $p = run($exe, |@args, '-e', $code, :out, :err);
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($p.exitcode, $o.trim, $e.trim)
}

# ---- a scratch store holding a FAKE Digest::Native ------------------------
#
# Five lines per short/ entry — ver, auth, api, source-sha, dist-id — which is
# the CURI layout zef and Rakudo both write. The fake announces itself and
# answers `md5-hex` with a constant, so which side answered is never in doubt.

my $store = $*TMPDIR.add("rakupp-prec-{$*PID}");
LEAVE { $store.&rm-rf }
sub rm-rf(IO::Path $d) {
    return unless $d.e;
    for $d.dir { $_.d ?? rm-rf($_) !! .unlink }
    $d.rmdir;
}

my $sha = &::('rakupp-sha1-hex')('Digest::Native').uc;
$store.add('sources').mkdir;
$store.add('dist').mkdir;
$store.add("short/$sha").mkdir;
$store.add('sources/FAKE001').spurt(q:to/MOD/);
    unit module Digest::Native;
    sub EXPORT(*@names) { Map.new('&md5-hex' => sub ($x) { 'FAKE' }) }
    MOD
$store.add('dist/fakedist001').spurt('{"name":"Digest::Native","ver":"0.2.0"}');
sub install-version(Str $v) {
    $store.add("short/$sha/fakedist001").spurt("$v\nzef:test\n1\nFAKE001\nfakedist001\n");
}

my $ABC = '900150983cd24fb0d6963f7d28e17f72';      # md5("abc"), the engine's answer

# ---- 5. the plain case: the compiler answers -----------------------------

{
    my ($rc, $out) = run-with([], 'use Digest::Native; say md5-hex("abc")');
    is $rc, 0, 'a plain `use Digest::Native` compiles with nothing installed';
    is $out, $ABC, '…and the ENGINE answered it';
}

# ---- 3. an installed distribution NEWER than the interface wins ----------

install-version('0.2.0');
{
    my ($rc, $out) = run-with(["-Iinst#$store"], 'use Digest::Native; say md5-hex("abc")');
    is $rc, 0, 'a newer installed distribution loads';
    is $out, 'FAKE', '…and it ANSWERS — the compiler steps aside, which is how the modules ship on their own schedule';
}

# …and one at or below the interface version does not.
install-version('0.0.1');
{
    my ($rc, $out) = run-with(["-Iinst#$store"], 'use Digest::Native; say md5-hex("abc")');
    is $out, $ABC, 'an installed distribution at the interface version does NOT displace the engine';
}
install-version('0.0.0');
{
    my ($rc, $out) = run-with(["-Iinst#$store"], 'use Digest::Native; say md5-hex("abc")');
    is $out, $ABC, 'nor does an older one';
}
# The comparison is on VERSION, not on string order: 0.10.0 is newer than 0.9.0
# and would sort the other way as text.
install-version('0.0.10');
{
    my ($rc, $out) = run-with(["-Iinst#$store"], 'use Digest::Native; say md5-hex("abc")');
    is $out, 'FAKE', '0.0.10 beats 0.0.1 — the compare is by version, not by string';
}

# ---- 2. an explicit search path beats both -------------------------------

my $lib = $*TMPDIR.add("rakupp-prec-lib-{$*PID}");
LEAVE { $lib.&rm-rf }
$lib.add('Digest').mkdir(:p);
$lib.add('Digest/Native.rakumod').spurt(q:to/MOD/);
    unit module Digest::Native;
    sub EXPORT(*@names) { Map.new('&md5-hex' => sub ($x) { 'ONPATH' }) }
    MOD
{
    install-version('0.0.1');       # the store would lose to the engine…
    my ($rc, $out) = run-with(["-I$lib", "-Iinst#$store"], 'use Digest::Native; say md5-hex("abc")');
    is $out, 'ONPATH', 'a directory on the search path wins over the engine';
}
{
    install-version('0.2.0');       # …and even a newer store copy loses to -I
    my ($rc, $out) = run-with(["-I$lib", "-Iinst#$store"], 'use Digest::Native; say md5-hex("abc")');
    is $out, 'ONPATH', '…and over a newer installed one, so `rakupp test <Dist>` tests the distribution';
}

# ---- 1. a versioned `use` goes to the store ------------------------------

install-version('0.2.0');
{
    my ($rc, $out) = run-with(["-Iinst#$store"], 'use Digest::Native:ver<0.2.0>; say md5-hex("abc")');
    is $out, 'FAKE', 'a versioned `use` is resolved by the store';
}
{
    # …and one the store cannot satisfy FAILS rather than quietly falling back
    # to the compiler's older interface: the caller asked for something
    # specific.
    my ($rc, $out, $err) = run-with(["-Iinst#$store"], 'use Digest::Native:ver<9.9+>; say md5-hex("abc")');
    isnt $rc, 0, 'a version nothing satisfies fails rather than falling back to the compiler';
}

# ---- 4. `need` is not ours to answer -------------------------------------

{
    my ($rc, $out) = run-with([], 'need Digest::Native; say "loaded"');
    isnt $rc, 0, '`need` with nothing installed fails — the compiler has builtins, not a package';
}
{
    install-version('0.0.1');
    my ($rc, $out) = run-with(["-Iinst#$store"], 'need Digest::Native; say "ok"');
    is $rc, 0, '…and with something installed it loads it, even at a version the engine outranks';
}

# ---- the interface version is declared, and matches the distributions ----
#
# Not a behaviour test but a bookkeeping one: the table in Interpreter.cpp says
# what interface this engine implements per module, and the distributions in
# raku-modules carry their own META6 version. If those drift apart, the gate
# above starts answering the wrong question — silently, since both sides
# compute the same digests.

my $sibling = $*PROGRAM.parent.parent.parent.parent.add('raku-modules');
if $sibling.e {
    my %declared = 'Data::Native' => 'Data-Native', 'JSON::Native' => 'JSON-Native',
                   'CSV::Native' => 'CSV-Native', 'Digest::Native' => 'Digest-Native',
                   'Compress::Zlib::Native' => 'Compress-Zlib-Native';
    my $src = $*PROGRAM.parent.parent.parent.add('src/Interpreter.cpp').slurp;
    my $drift = 0;
    for %declared.kv -> $mod, $dir {
        my $meta = $sibling.add("$dir/META6.json");
        next unless $meta.e;
        my $have = ($meta.slurp ~~ /'"version"' \s* ':' \s* '"' (<-["]>+) '"'/) ?? ~$0 !! '';
        my $want = ($src ~~ / '"' $mod '"' \s* ',' \s* '"' (<-["]>+) '"' /) ?? ~$0 !! '';
        unless $have eq $want {
            $drift++;
            diag "$mod: engine declares '$want', the distribution is '$have'";
        }
    }
    is $drift, 0, 'every declared interface version matches its distribution';
}
else {
    skip 'raku-modules checkout not beside this one', 1;
}

done-testing;
