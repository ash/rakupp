# Regression: `.raku` is not a module file extension.
#
# A name resolves to `.rakumod`, `.pm6` and — before 6.e — `.pm`, which is what
# Rakudo's CompUnit::Repository::FileSystem looks for. It does NOT resolve to
# `.raku`, even with an explicit -I, because `.raku` is what a PROGRAM wears.
#
# Raku++ used to accept it, and the cost was silent shadowing: a scratch script
# named `paths.raku` in the working directory (which is on this engine's search
# path, where Rakudo needs -I.) became the module `paths`, displacing the
# installed distribution of that name and reporting "Undefined routine 'paths'"
# from inside the scratch file. See docs/guide/faq/differences.md.
#
# The escape hatch is intact and is checked below: a distribution that really
# does keep a module in a `.raku` file names that path in META6 `provides`,
# which is explicit and is tried before any name-derived candidate. Both
# engines honour it.
#
# t/regression/6e-pm-extension.raku covers the 6.e half of the extension list.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $dir = $*TMPDIR.add("modext-{$*PID}");
$dir.mkdir;

sub load(Str $name, *@extra) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, |@extra, '-e', "use $name; say MARKER", :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

# One module name per extension, so a hit names the file it came from.
for <rakumod pm6 pm raku> -> $ext {
    $dir.add("Ext$ext.$ext").spurt:
        "unit module Ext$ext;\nour constant MARKER is export = 'loaded .$ext';\n";
}

check load('Extrakumod'), 'loaded .rakumod',   '.rakumod resolves by name';
check load('Extpm6'),     'loaded .pm6',       '.pm6 resolves by name';
check load('Extpm'),      'loaded .pm',        '.pm resolves by name before 6.e';
# Asserted as "the module did not load", not by the error prose: the two
# engines word an unresolvable `use` differently and neither wording is a
# contract of this test.
check load('Extraku').contains('loaded .raku'), False, '.raku does NOT resolve by name';

# A `provides` path is explicit, so it reaches the file the name would not.
my $dist = $dir.add('dist');
$dist.add('lib').mkdir;
$dist.add('lib/ProvOnly.raku').spurt:
    "unit module ProvOnly;\nour constant MARKER is export = 'loaded via provides';\n";
$dist.add('META6.json').spurt: q:to/JSON/;
    { "name": "ProvOnly", "version": "0.1", "auth": "zef:test", "perl": "6.d",
      "description": "a module kept in a .raku file", "license": "Artistic-2.0",
      "depends": [], "provides": { "ProvOnly": "lib/ProvOnly.raku" } }
    JSON

my $p = run($*EXECUTABLE, '-I', ~$dist, '-e', 'use ProvOnly; say MARKER', :out, :err);
check(($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // '',
      'loaded via provides', 'a META6 provides path still reaches a .raku module');

# The shadowing symptom itself: a PROGRAM whose basename matches a module name
# sits in a searched directory and must not be mistaken for that module.
$dir.add('Shadower.raku').spurt: "say 'the script ran instead of the module';\n";
$dir.add('Shadower.rakumod').spurt:
    "unit module Shadower;\nour constant MARKER is export = 'the module won';\n";
check load('Shadower'), 'the module won', 'a .rakumod is not displaced by a .raku beside it';

# rm -rf rather than unlink/rmdir: loading from a directory can leave a
# .precomp beside the sources, and the tree is ours and under $*TMPDIR.
run 'rm', '-rf', ~$dir;

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
