# Raku++ Internals — the compiler book

A book-length walk through the inside of Raku++: the lexer, the parser, the
AST, `Value`, the interpreter, the regex and grammar engine, Unicode, the four
run modes, the native code generator and its optimizer, module loading,
NativeCall, the extension ABI, and the concurrency runtime.

**[Raku++-Internals.pdf](Raku++-Internals.pdf)** — 267 pages, 36 chapters in
nine parts, plus three appendices.

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

The fonts (Charter, Helvetica Neue, Menlo, STIX Two Text) all ship with macOS.
On another platform, change `mainfont`/`sansfont`/`monofont` in
[meta.yaml](meta.yaml) to whatever is installed; the only real requirement is
that the mono face carries box-drawing characters, since the diagrams use them.

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

**The page count is not derivable from the PDF** — it carries no `/Linearized`
dictionary, and its page tree is inside a compressed object stream, so grepping
for `/N` or `/Count` finds nothing (or, worse, finds an unrelated `/N` and
reports it confidently). Get it from the typesetter instead:

```sh
pandoc docs/book/meta.yaml <(cat docs/book/ch/*.md) --to latex --standalone \
  --include-in-header docs/book/latex/preamble.tex --toc --number-sections \
  --top-level-division=chapter -o /tmp/bk.tex
cd /tmp && tectonic bk.tex --keep-logs && grep "Output written" bk.log
```

## What it is not

It is not the manual — see [../guide/](../guide/) for using Raku++ — and it is
not a specification of Raku. It documents mechanisms and the reasons behind
them, and it says where Raku++ diverges from Rakudo rather than glossing over
it.

The chapters draw on [../internals/](../internals/), which stays the shorter
per-topic reference, and cover several areas that have no page there: `Value` in
depth, the regex and grammar engine, NativeCall's internals, the extension ABI,
the concurrency runtime, and the tooling built on the AST.
