# markdown — a Markdown → HTML converter

Turns Markdown into a complete, styled HTML page. The block layer (headings,
lists, fenced code, blockquotes, rules, paragraphs) is recognised line by line;
the inline layer (`**bold**`, `*italic*`, `` `code` ``, `[links](url)`,
`![images](url)`) is a Raku `grammar` with an actions class that emits HTML
directly.

## Run it

```sh
build/rakupp showcase/markdown/md2html.raku showcase/markdown/sample.md > out.html
cat notes.md | build/rakupp showcase/markdown/md2html.raku > notes.html
# or compile a standalone binary:
build/rakupp --exe -o md2html showcase/markdown/md2html.raku
```

It reads a file argument or stdin, and writes a self-contained page (inline CSS,
no external assets) to stdout.

## Example

Input:

```markdown
## Features
A small **Markdown** converter written in *Raku*, parsing with a real `grammar`.

- **bold**, *italic*, and `inline code`
- [links](https://github.com/ash/rakupp)
```

Output (the body, indented for readability):

```html
<h2>Features</h2>
<p>A small <strong>Markdown</strong> converter written in <em>Raku</em>,
   parsing with a real <code>grammar</code>.</p>
<ul><li><strong>bold</strong>, <em>italic</em>, and <code>inline code</code></li>
<li><a href="https://github.com/ash/rakupp">links</a></li></ul>
```

See [`sample.md`](sample.md) for a fuller document exercising every construct.

## Supported

| Blocks | Inline |
|---|---|
| `#`…`######` headings | `**bold**` |
| `-` / `*` / `+` unordered lists | `*italic*` |
| `1.` ordered lists | `` `code` `` |
| ```` ``` ```` fenced code | `[text](url)` links |
| `>` blockquotes | `![alt](url)` images |
| `---` / `***` rules | HTML-escaped text |
| blank-line-separated paragraphs | |

## How it works

- **Blocks, line by line.** Markdown's block structure is inherently line-oriented,
  so a small loop groups lines into headings, lists, code fences, and paragraphs.
- **Inline, by grammar.** Each block's text runs through the `Inline` grammar. It
  uses ordered `||` alternation with a single-character catch-all, so a stray `*`
  degrades to literal text instead of failing the parse — the whole document always
  converts.
