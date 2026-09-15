# A token IMPORTED from a module and used as a subrule matched the EMPTY STRING.
# The resolver looks names up in the map of what this compilation unit declared;
# an exported one arrives as a Regex value bound to `&name` instead, missed the
# map, and took the lenient zero-width branch meant for Rakudo built-ins we do
# not have. `$line ~~ / ^ 'prefix ' <stamp> … /` then answered True while
# matching only the prefix — Apache::LogFormat's test asserts its whole log line
# against an imported `<timefmt>` exactly so, and the assertion was worthless.
use Test;
plan 5;

my $dir = $*TMPDIR.add("rakupp-imported-token-{$*PID}");
$dir.add('lib').mkdir;
$dir.add('lib/TokMod.rakumod').spurt: q:to/MOD/;
    unit module TokMod;
    my token inner { \d ** 2 }
    my token stamp is export { '[' <inner> ']' }
    my regex loose is export { 'a' .* 'z' }
    MOD

my $out = run($*EXECUTABLE, '-I' ~ $dir.add('lib').absolute, '-e', q:to/PROG/, :out).out.slurp(:close);
    use TokMod;
    my $s = "x [42] y";
    my $m = $s ~~ / ^ 'x ' <stamp> /;
    say "TEXT:", ~$m;
    say "FULL:", ?($s ~~ / ^ 'x ' <stamp> ' y' $/);
    say "CAP:",  ~($m<stamp>);
    say "NEST:", ~($m<stamp><inner>);
    say "LOOSE:", ?("abcz" ~~ / ^ <loose> $ /);
    PROG

like $out, /'TEXT:x [42]'/,  'the imported token matches its own text';
like $out, /'FULL:True'/,    '…so the pattern around it matches too';
like $out, /'CAP:[42]'/,     '…and captures under its name';
like $out, /'NEST:42'/,      '…with its own inner subrule inside';
like $out, /'LOOSE:True'/,   'an imported regex runs as well';

END { try $dir.add('lib/TokMod.rakumod').unlink; try $dir.add('lib').rmdir; try $dir.rmdir }
