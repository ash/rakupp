#!/usr/bin/env raku
# Every relative link in the documentation, resolved: the file it names, and —
# this is the half nothing checked before — the HEADING it names.
#
#   rakupp tools/check-doc-links.raku [PATH-SUBSTRING]
#
# Anchors are where documentation rots invisibly. A renamed heading leaves every
# link to it silently pointing at the top of the page, and GitHub's slug is not
# the obvious one: it keeps the hyphens of a `--flag`, so
# `## Pinning a run: \`--seed\``  is  #pinning-a-run---seed  with THREE hyphens.
# Three links in CLI.md were written with one and had never resolved; two more
# in GUIDE.md and PARALLEL-SPEEDUP.md pointed at an ASYNC.md heading that the
# concurrency-default flip had reworded.
#
# The slug rule implemented here is GitHub's: lowercase, drop backticks and
# emphasis marks, drop everything that is not a word character, whitespace or a
# hyphen, then turn each whitespace character into a hyphen. `Foo.cpp:724` is a
# line citation, not a filename, so a trailing `:N` (or `:N-M`) is stripped
# before the file is looked for. http(s) and mailto links are not followed.
#
# Exit 0 when everything resolves, 1 otherwise — it is a gate, not a report.

my $ROOT   = $?FILE.IO.parent.parent.absolute.IO;
my $FILTER = @*ARGS[0] // '';

sub slug(Str $h --> Str) {
    my $s = $h.trim.lc;
    $s ~~ s:g/<[`*_]>//;
    $s ~~ s:g/<-[\w\s\-]>//;
    $s ~~ s:g/\s/-/;
    $s
}

sub md-files($dir) {
    my @out;
    for $dir.dir.sort -> $e {
        if $e.d { @out.append(md-files($e)) unless $e.basename eq '.git' }
        elsif $e.extension eq 'md' { @out.push($e) }
    }
    @out
}

my @files = flat($ROOT.add('README.md').e ?? $ROOT.add('README.md') !! Empty,
                 md-files($ROOT.add('docs')));
@files .= grep({ !$FILTER || .Str.contains($FILTER) });

# strip fenced code before looking for headings or links: a `](…)` inside a
# code sample is a sample, not a link, and `# comment` is not a heading
sub uncoded(Str $t --> Str) {
    my ($out, $in) = '', False;
    for $t.lines -> $l {
        if $l.trim.starts-with('```') { $in = !$in; $out ~= "\n" }
        else { $out ~= ($in ?? "\n" !! $l ~ "\n") }
    }
    $out
}

my %anchors;
for @files -> $f {
    %anchors{$f.absolute} = uncoded($f.slurp).lines
        .grep(/^ '#' ** 1..6 \s+ \S /)
        .map({ slug(.subst(/^ '#'+ \s+ /, '')) }).Set;
}

my ($checked, @bad) = 0, ();
for @files -> $f {
    my $t = uncoded($f.slurp).subst(/ '`' <-[`\n]>* '`' /, '', :g);   # and code spans
    for $t.match(/ '](' <( <-[)\s]>+ )> ')' /, :g) -> $m {
        my $tgt = ~$m;
        next if $tgt.starts-with('http://' | 'https://' | 'mailto:');
        $checked++;
        my ($path, $frag) = $tgt.split('#', 2);
        my $lookup = $path.subst(/ ':' \d+ ['-' \d+]? $/, '');
        my $target = $lookup ?? $f.parent.add($lookup).resolve !! $f.absolute.IO;
        if $lookup && !$target.e {
            @bad.push("BROKEN FILE    {$f.relative($ROOT)} -> $tgt");
            next;
        }
        if $frag && %anchors{$target.absolute}:exists && $frag !(elem) %anchors{$target.absolute} {
            @bad.push("BROKEN ANCHOR  {$f.relative($ROOT)} -> $tgt");
        }
    }
}
.say for @bad;
say "$checked links checked (files and anchors), {+@bad} broken";
exit(@bad ?? 1 !! 0);
