# Presentation

A self-contained slide deck introducing Raku++ and its ecosystem —
[`index.html`](index.html). No build step and no dependencies: open the file
in a browser, or serve the directory statically.

**Just want to look?** [`slides.pdf`](slides.pdf) is a 12-page PDF export
(GitHub renders it inline). The interactive `index.html` is the real thing —
keyboard navigation, a light/dark toggle, hover states. Regenerate the PDF from
the deck with a headless browser:

```sh
# forces the dark theme, one slide per landscape page
sed 's/<html lang="en">/<html lang="en" data-theme="dark">/' index.html > /tmp/deck-print.html
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  --headless=new --no-pdf-header-footer --virtual-time-budget=4000 \
  --print-to-pdf="slides.pdf" "file:///tmp/deck-print.html"
```

- **Navigate:** `←` / `→` (also PageUp/PageDown, Space), `Home` / `End`, the dot
  rail, or the on-screen arrows.
- **Theme:** light/dark toggle, top-right (follows the OS setting by default).

Twelve slides: what it is → Roast conformance → language breadth → the five ways
to run → interpreter speed → native (`--exe`) speed → the ecosystem → showcase
programs → dogfooding → roadmap → install. Every figure is drawn from the docs
in [`../docs`](../docs) (README, HIGHLIGHTS, ECOSYSTEM, BENCHMARKS, CHANGELOG).

## Write-ups

Longer-form articles about Raku++:

- [Raku++ — the fastest Raku compiler](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/)
- [Raku++: The Long Read](https://andrewshitov.com/2026/07/15/raku-the-long-read/)
- [Raku: a Language Where 0.1 + 0.2 is 0.3](https://andrewshitov.com/2026/07/18/raku-a-language-where-0-1-0-2-is-0-3/)
- [Raku in a Browser](https://andrewshitov.com/2026/07/20/raku-in-a-browser/)
