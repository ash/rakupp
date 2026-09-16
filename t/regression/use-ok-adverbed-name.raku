# Regression: `use-ok "Mod::Name:auth<…>"` — the `::` in the name is not an
# adverb colon.
#
# use-ok takes a `use` statement's module spec, adverbs and all, and splits the
# bare name from the version requirement before handing it to the loader. The
# scan skipped the SECOND colon of a `::` pair but not the first, so
# `Sway::Config:auth<zef:CIAvash>` read its first adverb as ":Config:auth", fell
# out of the loop, and asked the loader for the whole string — which names no
# module. The test failed for a module that loads perfectly well, and because
# use-ok swallows the exception there was nothing in the log to say why.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("useokadv-{$*PID}");
$dir.mkdir;
$dir.add('Deep').mkdir;
$dir.add('Deep/Name.rakumod').spurt: "unit module Deep::Name:ver<1.0>:auth<zef:probe>;\nsub hi() is export \{ 'hi' \}\n";
$dir.add('Flat.rakumod').spurt: "unit module Flat;\n";

sub tap(Str $spec) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e',
                "use Test; plan 1; use-ok '$spec';", :out, :err);
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    $out.contains('not ok') ?? 'not ok' !! ($out.contains('ok 1') ?? 'ok' !! 'no verdict')
}

check tap('Deep::Name'),                   'ok', 'a :: name with no adverb';
check tap('Deep::Name:auth<zef:probe>'),   'ok', 'a :: name WITH an auth adverb';
# (a :ver requirement against a `-I` filesystem module is a separate question —
#  there is no index to read a version from — so it is not asserted here)
check tap('Flat'),                         'ok', 'a flat name, which always worked';
check tap('Flat:auth<zef:probe>'),         'ok', 'a flat name with an adverb';

# …and use-ok must still be able to say no
check tap('No::Such::Module::Anywhere'),   'not ok', 'a missing module still fails';
check tap('No::Such:auth<zef:probe>'),     'not ok', '…adverb or not';

try { .unlink for $dir.dir(:!d).grep(*.f); $dir.add('Deep').dir.map(*.unlink); $dir.add('Deep').rmdir; $dir.rmdir }

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
