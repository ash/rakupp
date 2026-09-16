# Regression: `sub term:<name>` — names ending in `?`/`!`, and terms declared in
# a module the file merely `use`s.
#
# A term is SYNTAX, so the file that uses it has to know the name while it is
# still being parsed. The operators a module declares are already harvested from
# its source for exactly that reason (scanOpsIn); terms were not, so a term
# declared in a module and written in a test read as a sub call and whatever
# punctuation followed it.
#
# Lingua::EN::Numbers hits both halves: it declares `sub term:<no-commas?>`
# beside a plain `sub no-commas`, and its suite queries the flag as
# `no-commas?`. The lexer hands back the identifier and the `?` separately, so
# the joined spelling has to be recognised at the use site as well.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# same-file terms, including the punctuated spellings
sub term:<forty-two> { 42 }
sub term:<yes?>      { 'yes' }
sub term:<bang!>     { 'bang' }
check ~forty-two, '42',    'a plain term';
check yes?,       'yes',   'a term whose name ends in ?';
check bang!,      'bang',  '…or in !';

# the ternary must still parse — `?` is not always a term ending
my $t = True;
check ($t ?? 'y' !! 'n'), 'y', 'the ternary is unaffected';

# …and a term declared in a MODULE, used by the file that loads it
my $dir = $*TMPDIR.add("terms-{$*PID}");
$dir.mkdir;
$dir.add('TermMod.rakumod').spurt: q:to/MOD/;
    unit module TermMod;
    our $FLAG = 'off';
    sub flip() is export { $FLAG = 'on' }
    sub term:<flag?> is export { $FLAG }
    MOD

sub run-it(Str $program) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $program, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

check run-it('use TermMod; say flag?'),        'off', 'a term from a used module parses';
check run-it('use TermMod; flip; say flag?'),  'on',  '…and answers the module\'s state';

try { .unlink for $dir.dir.grep(*.f); $dir.rmdir }

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
