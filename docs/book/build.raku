#!/usr/bin/env rakupp
# Build "Raku++ Internals" — the compiler book — as a PDF.
#
#   rakupp docs/book/build.raku            # -> docs/book/Raku++-Internals.pdf
#   rakupp docs/book/build.raku --keep     # also leave the merged Markdown behind
#
# The pipeline is Markdown -> pandoc -> XeLaTeX (tectonic) -> PDF. Tectonic is
# used rather than a full TeX installation because it is self-contained and
# fetches only the packages the preamble asks for. Written in Raku and run by
# rakupp itself, like the rest of the project's tooling.

my $here  = $*PROGRAM-NAME.IO.absolute.IO.parent;
my $ch    = $here.add('ch');
my $out   = $here.add('Raku++-Internals.pdf');
my $merged = $here.add('.book.md');
my $keep  = so @*ARGS.grep('--keep');

# --- collect the chapters, in file-name order -------------------------------
my @parts = $ch.dir.grep(*.extension eq 'md').sort(*.basename);
die "no chapters in $ch" unless @parts;

my $body = '';
for @parts -> $f {
    $body ~= $f.slurp.trim-trailing ~ "\n\n";
}
$merged.spurt($body);

note "book: { @parts.elems } chapter files, { $body.lines.elems } lines";

# --- run pandoc -------------------------------------------------------------
my @cmd =
    'pandoc',
    $here.add('meta.yaml').Str,
    $merged.Str,
    '--from', 'markdown+pipe_tables+backtick_code_blocks+fenced_code_attributes+raw_tex+smart',
    '--to', 'pdf',
    '--pdf-engine', 'tectonic',
    '--include-in-header', $here.add('latex/preamble.tex').Str,
    '--toc',
    '--toc-depth=2',
    '--number-sections',
    '--top-level-division=chapter',
    '--highlight-style', 'tango',
    '-o', $out.Str;

my $proc = run(@cmd, :err);
my $err  = $proc.err.slurp(:close);

# Tectonic is loud about absolute font paths and about glyphs it had to take
# from a fallback family; neither is a failure, so only the rest is shown.
for $err.lines -> $l {
    next if $l.contains('accessing absolute path');
    next if $l ~~ /^ 'warning: ' [ 'Package' | 'Overfull' | 'Underfull' ] /;
    note $l if $l.trim;
}

$merged.unlink unless $keep;

if $proc.exitcode == 0 && $out.e {
    my $kb = ($out.s / 1024).round;
    say "wrote { $out } ({ $kb } KB)";
} else {
    note "pandoc failed (exit { $proc.exitcode })";
    exit 1;
}
