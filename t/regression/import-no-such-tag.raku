# `use Mod :tag` for a tag the module exports nothing under dies with
# X::Import::NoSuchTag (roast S11-modules/importing.t) — on the first `use`
# and on a later one, when the module is already loaded. A tag that only a
# VARIABLE carries is still a tag, and `:ALL` always is. A `require` list names
# symbols instead, and a missing one is X::Import::MissingSymbols
# (roast S11-modules/require.t).
# Every line answers the same under Rakudo 2026.08.
#
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $dir = $*TMPDIR.add("nosuchtag-{$*PID}");
$dir.mkdir;
END { run 'rm', '-rf', ~$dir if $dir.e }
$dir.add('TagNs.rakumod').spurt: q:to/MOD/;
    unit module TagNs;
    sub f() is export(:subs) { "f" }
    sub g() is export { "g" }
    our $v is export(:vars) = 7;
    MOD

my $inc = ~$dir;
sub try-use(Str $tags) {
    (try { EVAL "use lib '$inc'; use TagNs $tags; 1"; 'lived' })
        // ($! ~~ X::Import::NoSuchTag ?? 'NoSuchTag' !! $!.^name)   # (Rakudo mixes in X::Comp)
}
check(try-use(':nosuch'), 'NoSuchTag', 'an unknown tag, first use');
check(try-use(':subs'),   'lived', 'a sub tag');
check(try-use(':vars'),   'lived', 'a tag only a variable carries');
check(try-use(':nosuch'), 'NoSuchTag', 'an unknown tag, repeat use');
check(try-use(':ALL'),    'lived', ':ALL');

# a `require` list names SYMBOLS: each must be exported, and a bare name is a
# sigilless term, not the sub of that name
sub try-require(Str $syms) {
    (try { EVAL "use lib '$inc'; require TagNs $syms; 1"; 'lived' })
        // ($! ~~ X::Import::MissingSymbols ?? 'MissingSymbols' !! $!.^name)
}
check(try-require('<&g>'),    'lived', 'require of an exported symbol');
check(try-require('<&nope>'), 'MissingSymbols', 'require of a missing symbol');
check(try-require('<g>'),     'MissingSymbols', 'a bare name is a term, not the sub');

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
exit(@fail ?? 1 !! 0);
