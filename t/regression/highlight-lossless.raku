# Regression: the span scanner is BYTE-LOSSLESS, and a heredoc body is a
# string (FMT-PLAN step 1).
#
# `--highlight` classifies Raku source without executing it, and the formatter
# `--fmt` is designed to stand on that scanner: it rewrites only the plain
# whitespace BETWEEN classified spans and never the bytes inside one. That is
# only safe if concatenating the spans reproduces the input exactly — so this
# case asserts it over the repo's own corpora, which is the property, not a
# sample of it.
#
# The check runs through `--highlight --ansi` and strips the escapes: what is
# left must be the file, byte for byte. (The plan's 2026-08-26 probe did this
# once over 341 files; this is the same sweep made permanent, and the corpus
# has grown since.)
#
# Contract: exit 0 + last line PASS.
my $root = $?FILE.IO.parent.parent.parent;
my @fail;

sub stripped($file) {
    my $p = run($*EXECUTABLE.Str, '--highlight', '--ansi', $file, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $o.subst(/ \e '[' <[0..9;]>* 'm' /, '', :g)
}

sub walk($dir, @out) {
    for $dir.dir -> $e {
        if $e.d { walk($e, @out) }
        elsif $e.Str.ends-with('.raku') || $e.Str.ends-with('.rakumod') { @out.push($e) }
    }
}
my @files;
walk($_, @files) for <t examples rakulib showcase>.map({ $root.add($_) }).grep(*.d);
@fail.push("found no files to scan — the sweep would pass vacuously") if @files < 50;
my $n = 0;
for @files -> $f {
    $n++;
    my $got = stripped($f.Str);
    unless $got eq $f.slurp {
        @fail.push("not lossless: {$f.relative($root)}");
        last if @fail >= 5;
    }
}

# …and the HEREDOC body is one string span, not code. Scanning it as code is
# what `--highlight` used to do — a `#` inside a heredoc came out as a comment
# — and a formatter standing on that would have reindented string contents.
my $hd = $root.add("t/regression/.fmt-heredoc-probe.raku");
$hd.spurt: Q:to/PROBE/;
    my $t = q:to/END/;
    if 1 { say "text" }   # not a comment
    END
    say $t;
    PROBE
my $lit = stripped($hd.Str);
@fail.push("heredoc probe is not lossless") unless $lit eq $hd.slurp;
# the body must carry ONE colour run, so the `#` line is not split off as a
# comment: an un-stripped render has no comment-italic escape inside it.
my $p = run($*EXECUTABLE.Str, '--highlight', '--ansi', $hd.Str, :out, :err);
my $ansi = $p.out.slurp(:close); $p.err.slurp(:close);
@fail.push("a heredoc body is still scanned as code (found the comment colour in it)")
    if $ansi.contains("\e[3;36m");
$hd.unlink;

if @fail { .say for @fail; say "FAIL ({+@fail})"; exit 1 }
note "$n files scanned, all byte-lossless";   # the count is context, not the verdict
say "PASS";
