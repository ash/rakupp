# Raku++ Internals — the compiler book

A book-length walk through the inside of Raku++: the lexer, the parser, the
AST, `Value`, the interpreter, the regex and grammar engine, Unicode, the five
run modes, the two code generators and the optimizer, module loading,
NativeCall, the extension ABI, and the concurrency runtime.

**[Raku++-Internals.pdf](Raku++-Internals.pdf)** — 398 pages, 43 chapters in
ten parts, plus four appendices under a divider of their own.

## Building it

```sh
rakupp docs/book/build.raku          # -> docs/book/Raku++-Internals.pdf
rakupp docs/book/build.raku --keep   # also leave the merged Markdown behind
```

The builder is written in Raku and run by rakupp itself, like the rest of the
project's tooling. The pipeline is Markdown → pandoc → XeLaTeX → PDF.

Two things must be on the machine:

| | |
|---|---|
| **pandoc** | `brew install pandoc` |
| **tectonic** | `brew install tectonic` — a self-contained TeX that fetches only the packages the preamble asks for, so there is no full TeX installation to maintain |

The fonts (Charter, Helvetica Neue, Menlo, STIX Two Math, Hiragino Sans) all
ship with macOS. On another platform, change `mainfont`/`sansfont`/`monofont`
in [meta.yaml](meta.yaml) to whatever is installed, and the fallback families
at the top of [latex/preamble.tex](latex/preamble.tex) with them.

Two requirements are real, and both are about characters rather than about
looks. The mono face has to carry box drawing, since the diagrams are built
out of it. And the fallback faces have to carry every character the preamble
maps to them — check that rather than assuming, because a face that lacks one
does not fail the build: the character is dropped from the page and the only
trace is a `Missing character` line in a log nobody reads. STIX Two *Text* was
the symbol face until a print pass caught it dropping seven of them.

## Layout

```
ch/            one Markdown file per chapter, ordered by file name
meta.yaml      title, fonts, page size, pandoc variables
latex/         the XeLaTeX preamble: glyph fallbacks, headers, code panels
build.raku     the builder
```

A chapter file starts with `# Title`. A file that opens a new part starts with
a raw `\part{…}` line above it.

## Writing rules

Two that matter, because breaking either shows up in the PDF:

- **Keep lines inside code fences to 80 characters or fewer.** The page is
  narrow and code is set at a fixed width; anything longer runs into the margin.
  This checks it:

  ```sh
  rakupp -e 'my $in = False;
    for dir("docs/book/ch").grep(*.extension eq "md").sort -> $f {
      my $n = 0; $in = False;
      for $f.slurp.lines -> $l { $n++;
        if $l.starts-with("```") { $in = !$in; next }
        say "{$f.basename}:$n ({$l.chars})" if $in && $l.chars > 80 } }'
  ```

- **Avoid long unbreakable `` `identifiers` `` in prose.** A run of several
  code spans joined by slashes will not break across a line. Put a list of
  field names in a fenced block instead.

The build prints any `Overfull \hbox` the typesetter reports; a clean build has
none over a couple of points.

**Do not grep the PDF for the page count**, and do not typeset it a second time
to ask. Grepping fails because the file carries no `/Linearized` dictionary and
its page tree is inside a compressed object stream, so `/N` or `/Count` finds
nothing — or, worse, finds an unrelated `/N` and reports it confidently. Ask a
reader that parses the file, which is the artifact readers will actually hold:

```sh
pdfinfo docs/book/Raku++-Internals.pdf | grep '^Pages'   # poppler
```

Rebuilding the LaTeX by hand and reading tectonic's `Output written on …` line
measures a different book. Measured against an earlier build, the recipe this
file used to give reported **402**, and adding back the `--from …+smart` and
`--highlight-style tango` that `build.raku` passes brought it to **400**,
against a shipped **397** — three counts for one book. The number to trust is
the one `pdfinfo` reads from the shipped file, which is now **398**, and past
which `pdftotext` refuses a page. If you do run the LaTeX by hand anyway, use the merged `.book.md` the
builder leaves behind under `--keep`, not `cat ch/*.md`: `cat` puts no blank
line between files, so each chapter's `#` heading becomes a lazy continuation
of the previous chapter's last paragraph, the chapters run together, and the
count comes out shorter still.

## What it is not

It is not the manual — see [../guide/](../guide/) for using Raku++ — and it is
not a specification of Raku. It documents mechanisms and the reasons behind
them, and it says where Raku++ diverges from Rakudo rather than glossing over
it.

The chapters draw on [../internals/](../internals/), which stays the shorter
per-topic reference, and cover several areas that have no page there: `Value` in
depth, the regex and grammar engine, NativeCall's internals, the extension ABI,
the concurrency runtime, and the tooling built on the AST.
