# Presentation

A self-contained slide deck introducing Raku++ and its ecosystem —
[`index.html`](index.html). No build step and no dependencies: open the file
in a browser, or serve the directory statically.

**Just want to look?** [`rakupp-presentation.pdf`](rakupp-presentation.pdf) is a
12-page PDF export — download it and flip through in any PDF viewer (the text
stays selectable). GitHub's inline blob viewer is unreliable with PDFs, so
download it rather than expecting a preview. The interactive `index.html` is the
real thing: keyboard navigation, a light/dark toggle, hover states. Regenerate
the PDF from the deck:

```sh
# 1. force the dark theme, print one slide per 1280x720 landscape page
sed 's/<html lang="en">/<html lang="en" data-theme="dark">/' index.html > /tmp/deck-print.html
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  --headless=new --no-pdf-header-footer --virtual-time-budget=4000 \
  --print-to-pdf="/tmp/deck-chrome.pdf" "file:///tmp/deck-print.html"

# 2. normalise to PDF 1.4 — smaller and widely compatible, text preserved
gs -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dPDFSETTINGS=/prepress \
   -dNOPAUSE -dBATCH -dQUIET -sOutputFile="rakupp-presentation.pdf" /tmp/deck-chrome.pdf
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
