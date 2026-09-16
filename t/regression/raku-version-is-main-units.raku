# Regression: `$*RAKU.version` is the MAIN unit's language revision.
#
# `$*RAKU` is ONE object for the process. Its version reports the revision the
# PROGRAM is written in, not the revision of whatever compilation unit happens
# to be executing — so a module written `use v6.*` and loaded from a 6.d program
# answers 6.d, and a module written `use v6` loaded from a 6.e program answers
# 6.e. This engine answered the CURRENT unit's revision in both directions.
#
# App::ModuleSnap found it: `method get-meta(… Version :$raku-version =
# $*RAKU.version …)` sits in a module headed `use v6.*`, so the META6 it built
# recorded 6.e where its own suite — a 6.d test file — asserts `$*RAKU.version`.
#
# What did NOT change, and must not: langRev_ stays per-unit, because that is
# what gates the features. t/regression/precomp-langrev.raku pins that with a
# 6.e-only surface (`.AST`) reached from inside a `use v6.*` module, across a
# cold and a warm cache.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("rakuver-{$*PID}");
$dir.mkdir;
$dir.add('VerStar.rakumod').spurt: "use v6.*;\nunit module VerStar;\nsub star-ver() is export \{ ~\$*RAKU.version }\n";
$dir.add('VerPlain.rakumod').spurt: "use v6;\nunit module VerPlain;\nsub plain-ver() is export \{ ~\$*RAKU.version }\n";

sub run-it(Str $program) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $program, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

# a 6.* module loaded from a 6.d program answers the PROGRAM's revision
check run-it('use v6; use VerStar; print star-ver()'), '6.d',
      'a `use v6.*` module answers the 6.d main unit';
check run-it('use v6; use VerStar; print ~$*RAKU.version'), '6.d',
      '…and so does the main unit itself';

# …and the other direction: a 6.d module loaded from a 6.e program answers 6.e
check run-it('use v6.e.PREVIEW; use VerPlain; print plain-ver()'), '6.e',
      'a `use v6` module answers the 6.e main unit';
check run-it('use v6.e.PREVIEW; print ~$*RAKU.version'), '6.e',
      '…as the main unit does';

# the pragma still resolves as it always did, in the unit that writes it
check run-it('use v6.c; print ~$*RAKU.version'), '6.c', '`use v6.c`';
check run-it('use v6.*; print ~$*RAKU.version'), '6.e', '`use v6.*` is the newest';
check run-it('print ~$*RAKU.version'),           '6.d', 'no pragma at all';

try { my sub rm($d) { for $d.dir { $_.d ?? rm($_) !! .unlink }; $d.rmdir }; rm($dir) }

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
