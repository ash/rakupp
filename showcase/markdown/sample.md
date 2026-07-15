# Raku++ Markdown

A small **Markdown** converter written in *Raku*, parsing with a real
`grammar`. It handles the common blocks and inline spans.

## Features

- Headings, `# ` through `###### `
- **bold**, *italic*, and `inline code`
- [links](https://github.com/ash/rakupp) and lists
- Fenced code blocks and blockquotes

## A code block

```
sub greet($name) {
    say "hello, $name";
}
greet('world');
```

## A quote

> Simplicity is prerequisite for reliability.
> — Edsger Dijkstra

## Ordered steps

1. Read the Markdown source
2. Split it into blocks
3. Parse each inline span with the grammar
4. Emit HTML

---

That's it — a single file, no dependencies.
