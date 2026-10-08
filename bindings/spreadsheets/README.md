# Raku formulas in Excel and Google Sheets

Write a spreadsheet formula in Raku:

```
=RAKU("($^a - $^b) + 1", A1, A2)          Google Sheets
=RAKU.EVAL("($^a - $^b) + 1", A1, A2)     Excel
```

With 43.1 and 43.2 in the two cells, this is exactly `0.9`. Plain Excel works
in doubles, which cannot hold these decimals, and Microsoft's own example,
`=(43.1-43.2)+1`, shows `0.899999999999999`
([Floating-point arithmetic may give inaccurate result in Excel](https://learn.microsoft.com/troubleshoot/microsoft-365-apps/excel/floating-point-arithmetic-inaccurate-result)).
Raku's decimals are exact rationals, its integers have no size limit, and its
regexes and grammars work on the text in your cells. You can also write your
own subs on a sheet named **Raku** and call them from any formula.

The Raku++ engine runs inside the spreadsheet, compiled to WebAssembly
([Raku.js](../../rakujs/README.md)): nothing to install on your computer, no
Python, no macros, and the formulas send nothing anywhere.

## What a formula looks like

The code comes first. `$^a`, `$^b`, … are the values after it, in order, and
`@_` is all of them, with ranges flattened into their cells. These are the
Google Sheets spellings; in Excel, write `RAKU.EVAL` for `RAKU`.

| Formula | Result |
|---|---|
| `=RAKU("($^a - $^b) + 1", 43.1, 43.2)` | `0.9` |
| `=RAKU("$^a - $^b - $^c", 0.5, 0.4, 0.1)` | `0` |
| `=RAKU("[+] @_", A1:A10)` with 0.1 in every cell | `1` |
| `=RAKU("[*] 1..$^n", 30)` | `265252859812191058636308480000000`, as text |
| `=RAKU("(1/3 + 1/7).raku")` | `<10/21>` |
| `=RAKU("(1..4).map(* ** 2)")` | 1, 4, 9 and 16, down a column |
| `=RAKU("(1..2).map({ ($_, $_ * 10) })")` | a table of two rows: 1, 10 and 2, 20 |
| `=RAKU("~($^s ~~ / \d+ ' kg' /)", "Box of 12 kg flour")` | `12 kg` |
| `=RAKU("iban-ok($^s)", "GB82 WEST 1234 5698 7654 32")` | `TRUE`, with `iban-ok` on the Raku sheet |
| `=RAKU("1/0")` | `#DIV/0!` in Excel; an error saying so in Sheets |

## Your own subs: the Raku sheet

Add a sheet named `Raku` and write Raku in column A, one line per row. Every
formula in the file can call what it defines. The **Raku** menu in Sheets and
the **Raku** pane in Excel both have a button that adds the sheet with two
examples on it:

```raku
# =RAKU("iban-ok($^s)", A2) is TRUE for a valid IBAN: 30 digits, modulo 97, exactly.
sub iban-ok(Str $iban) {
    my $t = $iban.uc.comb(/<alnum>/).join;
    my $digits = ($t.substr(4) ~ $t.substr(0, 4)).comb.map({ /\d/ ?? $_ !! .ord - 55 }).join;
    $digits % 97 == 1
}

# =RAKU("split-cents($^a, $^n)", 100, 3) fills three cells: 33.34, 33.33, 33.33.
sub split-cents($amount, $n) {
    my $cents = ($amount * 100).round;
    (^$n).map({ ($cents div $n + ($_ < $cents mod $n ?? 1 !! 0)) / 100 })
}
```

A line that starts with `=`, `+` or `-` would become a formula: type `'`
before it. When something on the sheet does not compile, every formula says
so, with the row: `Raku sheet: … (row 7)`.

The spreadsheet does not know that a formula depends on the Raku sheet. After
you change the sheet, choose **Raku → Recalculate RAKU formulas** in Sheets, or
press **Recalculate** in Excel's Raku pane.

## Google Sheets

### Use a spreadsheet that has it

Open a spreadsheet that has the Raku script and choose **File → Make a copy**.
The copy comes with the script, and its `=RAKU` formulas work in it.

### Put it into a spreadsheet of your own

The script is a folder of files for an Apps Script project. Build it with
Raku++, then upload it with [clasp](https://github.com/google/clasp), Google's
command line for Apps Script. clasp needs Node.js 20 or later, and the Apps
Script API turned on at <https://script.google.com/home/usersettings>.

```bash
cd bindings/spreadsheets && rakupp build.raku
```

```bash
npm install -g @google/clasp
```

```bash
cd bindings/spreadsheets && clasp login && clasp create-script --type sheets --title "Raku formulas" --rootDir dist/google-sheets
```

```bash
cd bindings/spreadsheets && clasp push
```

`create-script` makes a new spreadsheet with the script bound to it; give it
`--parentId` and a spreadsheet's ID instead of `--type` to use one you already
have. Without clasp, open **Extensions → Apps Script** in the spreadsheet and
create one script file for each `.gs` file in `dist/google-sheets`, with the
same name and contents.

### What to expect

- **Every formula is its own Apps Script execution**, and each may load the
  engine again. Loading means unpacking about 12 MB of WebAssembly. In
  `test/sheets.mjs`, which runs the project in a bare V8 the way Apps Script
  does, that takes about half a second, and each formula after it 10 to 25 ms.
  Google's servers may take longer. Give one formula a whole range rather than
  writing one formula per cell: one formula, one load, and a column or a table
  of results.
- A formula has 30 seconds, Google's limit for a custom function.
- What a formula prints with `say` goes to the script's execution log
  (**Extensions → Apps Script → Executions**), not into the cell.
- **Raku → How RAKU formulas work** shows a short help.
- A date cell arrives as text in ISO 8601: `2026-10-08T00:00:00.000Z`.

## Excel

### Install

The add-in is a set of web files and a manifest that tells Excel where they
are. It is not in Microsoft's add-in store yet, so for now you serve the files
yourself and load the manifest by hand.

1. Build it, giving the HTTPS address the files will be served from:

   ```bash
   cd bindings/spreadsheets && rakupp build.raku --base=https://example.org/raku-excel/
   ```

2. Put everything in `dist/excel` on that server. Excel loads add-ins over
   HTTPS only.
3. Load `dist/excel/manifest.xml`, which Microsoft calls sideloading:
   - **Excel on the web:** open the Add-ins dialog and choose
     **Upload My Add-in**
     ([how](https://learn.microsoft.com/office/dev/add-ins/testing/sideload-office-add-ins-for-testing)).
   - **Excel for Mac:** copy `manifest.xml` into
     `~/Library/Containers/com.microsoft.Excel/Data/Documents/wef/` (make the
     folder if it is not there), restart Excel, then choose **Home → Add-ins**
     and pick **Raku formulas** from the menu
     ([how](https://learn.microsoft.com/office/dev/add-ins/testing/sideload-an-office-add-in-on-mac)).
   - **Excel for Windows:** share a folder holding `manifest.xml` and add it as
     a trusted catalog under **File → Options → Trust Center → Trust Center
     Settings → Trusted Add-in Catalogs**
     ([how](https://learn.microsoft.com/office/dev/add-ins/testing/create-a-network-shared-folder-catalog-for-task-pane-and-content-add-ins)).

### Use it

Click the **Raku** button on the Home tab once. That starts the add-in for the
first time, which is when Excel registers `=RAKU.EVAL` — for you, and in every
workbook from then on. Until then a formula shows `#NAME?`, and a cell that
already does keeps it until you enter the formula again.

Then type `=RAKU.EVAL(` in a cell. The **Raku** button on the Home tab opens a pane
with examples you can insert into the selected cell, the buttons for the Raku
sheet and recalculation, the engine's version, and what formulas printed with
`say`.

### What to expect

- The engine loads once per workbook session, in a second or two the first time.
  Excel calls the add-in once per formula; the add-in collects the formulas
  that arrive together and runs them as one Raku program, so 200 formulas cost
  about as much as one.
- A batch that runs longer than 15 seconds is stopped. The formulas in it are
  then run one at a time, so only the slow one shows `#N/A`.
- Only `#VALUE!` and `#N/A` can carry a message in Excel, so most errors are
  `#VALUE!`, with Raku's message under the cell's error flag. Division by zero
  is `#DIV/0!` and an infinite result is `#NUM!`.
- A date cell arrives as Excel's serial number (46303 for 8 October 2026).

## Values in and out

- **Numbers** arrive as the decimal the cell shows: 0.1 is exactly 1/10, and
  30 is the integer 30.
- **A single cell** is its value. **A row or a column** is one list. **A wider
  range** is a list of rows: `$^a.map(*.sum)` sums each row, and `[+] @_` or
  `$^a.flat.sum` sums every cell.
- **An empty cell** is `Any`, the undefined value.
- **A result** that is a rational number becomes the nearest double, once, at
  the end. An integer too big for a double to hold exactly (2⁵³ or more)
  becomes text with all its digits. A list goes down a column, a list of lists
  fills a table, and a hash fills two columns of keys and values.
- **An error** in the code, or a `die`, is an error in the cell with Raku's
  message. A lazy list (`1..*`) is an error too: keep part of it with
  `.head(N)`.

## Limits

- No files, network, `run` or `start`: the engine runs in the spreadsheet's
  own sandbox, on one thread.
- Recursion is limited by the JavaScript stack it runs on: about a hundred
  levels of a plain recursive sub.
- An `exit` in a formula ends that batch with an error; the next formula gets a
  fresh engine.

---

## For experienced users

### How it works

Both hosts build the same program for a batch of formulas: the Raku sheet's
lines first, so that a line number in an error is a row number, then
[`rakusheet.raku`](rakusheet.raku). The formulas and their values go in on
standard input as JSON; the program compiles each formula's code once as the
body of an anonymous sub, calls it, turns the result into rows, and prints one
JSON line after a record separator (`\x1E`), so a formula's own `say` cannot be
mistaken for the answer. [`core/rakusheet-core.js`](core/rakusheet-core.js)
does the JavaScript half for both hosts: values into JSON, `rakupp_run`, and
the answer back into cells.

- **Excel** ([`excel/`](excel)) uses a shared runtime: `taskpane.html` stays
  loaded for the session and holds `functions.js`, which runs batches in a Web
  Worker (`rakusheet-worker.js`) so that a runaway formula can be stopped. A
  batch that overflows the worker's stack is run again on the page's thread,
  which is several times deeper.
- **Google Sheets** ([`google-sheets/`](google-sheets)) has no workers, no
  `fetch` and no binary files, so `build.raku` stores the WebAssembly
  gzip-compressed and base64-encoded in five `RakuWasmNN.gs` files.
  `RakuLoader.gs` decodes and inflates it in plain JavaScript and checks the
  gzip CRC. `RakuSheet.installShims` adds the `TextDecoder`, `performance`,
  `crypto` and `Array.prototype.at` that the Emscripten glue expects. Apps
  Script reads every file with a parser of its own when the project is saved,
  and that parser refuses logical assignment (`a ??= b`), class fields and
  BigInt literals (`0n`), which the glue has; `build.raku` rewrites all three
  into what they mean.
- **The Marketplace add-on** (`dist/google-sheets-addon`) is the same project
  with the add-on's own entry points, [`google-sheets/RakuAddon.js`](google-sheets/RakuAddon.js)
  in place of the marked region of `Raku.js`: its menu is under **Extensions**,
  `onInstall` adds it to the spreadsheet already open, and what formulas print
  is not kept, since an add-on's log is its developer's. Its `appsscript.json`
  asks for one scope, `spreadsheets.currentonly`, and turns exception logging
  off. [`store/README.md`](store/README.md) has the listings in the Google
  Workspace Marketplace and Microsoft AppSource.

### Building

```bash
cd bindings/spreadsheets && rakupp build.raku --help
```

`--base` is where the Excel files will be served from (the manifest's
addresses), `--rakujs` the Raku.js build to ship (`rakujs/playground` by
default; [`rakujs/build.sh`](../../rakujs/build.sh) makes a fresh one), and
`--out` the output directory (`dist`), which gets `excel`, `google-sheets`
and `google-sheets-addon`. Each build stamps every Excel file reference with
`?v=…`, so Office's cache never mixes two builds. `--store` also draws the
listings' icons into `store`, and `--revision` raises the fourth number of
the manifest's version, which AppSource needs to take a changed manifest
between releases.

### Tests

```bash
node bindings/spreadsheets/test/sheets.mjs
```

```bash
node bindings/spreadsheets/test/sheets.mjs --addon
```

run the built Google Sheets project, and the add-on, in a bare V8 context: no
browser or Node globals, no `Array.prototype.at`, files in a random order, a
stand-in `SpreadsheetApp`. They check values, errors, the Raku sheet, the
menu and its help, **Recalculate** (which puts every formula back, even when
a write fails), damaged or missing engine files, and that no file has the
syntax Apps Script's parser refuses.

[`test/excel-harness.html`](test/excel-harness.html) drives the built Excel
add-in in a browser, with stand-ins for `CustomFunctions` and `Excel.run`. It
checks batching, the error codes, deep recursion, `exit`, and the timeout.
Serve the `bindings/spreadsheets` directory over HTTP and open
`/test/excel-harness.html`; it takes about 45 seconds, most of them for the
timeout case.
