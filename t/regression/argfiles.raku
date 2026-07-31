# Regression: $*ARGFILES (issue #14) was undefined — `say $*ARGFILES` printed
# (Any) and `.lines` yielded nothing, so the awk/perl-style one-liner
# `$*ARGFILES.lines>>.words.classify(*[1])` produced an empty Bag.
#
# The reading machinery already existed (a bare `lines()` spans @*ARGS); what
# was missing was the HANDLE. It is built on ACCESS, never at startup — a
# program that doesn't mention it must not read files or block on stdin — and
# holds the concatenated content of every file in @*ARGS, or reads $*IN when
# there are none.
#
# Fixing it also fixed in-memory handles generally: the lines/get/words path
# had no branch for a "captured" handle, so `run(…, :out).out.lines` — the
# same shape — silently returned nothing.
# Contract: exit 0 + last line PASS.
my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-argf-{$*PID}");
$dir.mkdir;
my $a = $dir.add('a.txt'); $a.spurt("x\ny\n");
my $b = $dir.add('b.txt'); $b.spurt("z\n");

sub rp(*@args, :$in, :$err) {
    my $p = $in
        ?? do { my $q = run($rakupp, |@args, :in, :out, :err); $q.in.print($in); $q.in.close; $q }
        !! run($rakupp, |@args, :out, :err);
    my $o = $p.out.slurp(:close); my $e = $p.err.slurp(:close);
    ($err ?? $e !! $o).chomp
}

# one file, two files — .lines spans them in order
@fail.push('one file')  unless rp('-e', 'say $*ARGFILES.lines.join("|")', $a.absolute) eq 'x|y';
@fail.push('two files') unless rp('-e', 'say $*ARGFILES.lines.join("|")', $a.absolute, $b.absolute) eq 'x|y|z';

# the issue's own shape: classify over words of every line
@fail.push('classify') unless rp('-e', 'say $*ARGFILES.lines>>.words.classify(*[0]).keys.sort.join(",")', $a.absolute) eq 'x,y';

# .slurp / .get / .words / gist
@fail.push('slurp') unless rp('-e', 'say $*ARGFILES.slurp.chars', $a.absolute) eq '4';
@fail.push('get')   unless rp('-e', 'say $*ARGFILES.get', $a.absolute) eq 'x';
@fail.push('words') unless rp('-e', 'say $*ARGFILES.words.join(",")', $a.absolute) eq 'x,y';
@fail.push('gist')  unless rp('-e', 'say $*ARGFILES', $a.absolute)
                            eq "IO::ArgFiles(opened on \"{$a.absolute}\".IO)";

# no arguments: reads standard input
@fail.push('stdin') unless rp('-e', 'say $*ARGFILES.lines.join("|")', :in("p\nq\n")) eq 'p|q';

# it must not be built unless used — a program with a file argument that never
# mentions $*ARGFILES still runs (and @*ARGS keeps the names)
@fail.push('unused') unless rp('-e', 'say @*ARGS.elems', $a.absolute) eq '1';

# a file that cannot be opened is FATAL, as in Rakudo — silently skipping it
# turned a mistyped path into an empty result (this is how the issue's reporter
# and I both chased a phantom for a while)
@fail.push('missing file must fail')
    unless rp('-e', 'say $*ARGFILES.lines.elems', '/nonexistent-rakupp-xyz', :err)
             .contains('Failed to open file');

# an UNDEFINED classify key keeps its gist (Nil, (Any), (Int)) instead of
# collapsing to the empty string — the `*[1]` classifier on a line with fewer
# than two words is exactly that case
@fail.push('classify Nil key')
    unless rp('-e', 'say (1,2).classify({ Nil }).Bag') eq 'Bag(Nil(2))';
@fail.push('classify Any key')
    unless rp('-e', 'say (1,2).classify({ Any }).Bag') eq 'Bag((Any)(2))';
@fail.push('classify empty-string key stays empty')
    unless rp('-e', 'say (1,2).classify({ "" }).Bag') eq 'Bag((2))';

# the same in-memory-handle path, through a Proc
@fail.push('proc.out.lines') unless rp('-e', 'my $p = run("printf", "a\nb\n", :out); say $p.out.lines.join("|")') eq 'a|b';

unlink($a, $b);
rmdir($dir);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
