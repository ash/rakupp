# Store listings

Raku formulas in the Google Workspace Marketplace (the Sheets add-on) and in
Microsoft AppSource (the Excel add-in): what each store takes, the text of
each listing, and the steps in each console. The accounts and the
submissions are the publisher's; everything they take is built here or
published on raku.online.

## What each store takes

| | Google Workspace Marketplace | Microsoft AppSource |
|---|---|---|
| The add-on | `dist/google-sheets-addon`, pushed to an Apps Script project of its own | `https://raku.online/embed/excel/manifest.xml` |
| Homepage | `https://raku.online/embed/spreadsheets/` | the same |
| Privacy policy | `https://raku.online/embed/spreadsheets/privacy/` | the same |
| Terms of use | `https://raku.online/embed/spreadsheets/terms/` | the same |
| Support | `https://raku.online/embed/spreadsheets/support/` | the same |
| Icons | `dist/store/icon-32.png`, `dist/store/icon-128.png`; `dist/store/icon-120.png` for the consent screen | `dist/store/icon-300.png` |
| Banner | `dist/store/banner-220x140.png` | — |
| Screenshots | 1280 × 800, one to ten, at least one in Sheets | 1366 × 768, one to five |

`--store` draws the icons; the banner is `store/banner.html`, drawn by a
browser for its words. From the repository root, with the site's engine:

```bash
rakupp bindings/spreadsheets/build.raku --base=https://raku.online/embed/excel/ --rakujs=RAKU-ONLINE/www --store
```

```bash
cd bindings/spreadsheets/dist/store && "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --hide-scrollbars --force-device-scale-factor=1 --window-size=220,140 --screenshot=banner-220x140.png banner.html
```

Screenshots are taken in the real applications, with real content in the
cells. Shots that show what the formulas are for:

- `=FACT(30)` beside `=RAKU("[*] 1..$^n", 30)`: `2.65253E+32`, then all 33 digits.
- In Excel, `=(43.1-43.2)+1` beside `=RAKU.EVAL("($^a - $^b) + 1", 43.1, 43.2)`: `0.899999999999999`, then `0.9`.
- The Raku sheet with `iban-ok`, and a column of IBANs with TRUE and FALSE beside them.
- In Excel, the Raku pane open beside a sheet of formulas.

## Google Workspace Marketplace

### Once

1. **The domain.** Google checks that the publisher owns the domain of the
   homepage, the privacy policy and the terms. Add `raku.online` as a domain
   property in [Google Search Console](https://search.google.com/search-console)
   (a TXT record at the DNS host), with the account that will publish.
2. **A Cloud project.** In the [Google Cloud console](https://console.cloud.google.com/),
   create a project; Apps Script's default project cannot publish.
3. **The Apps Script project.** After the build, from `bindings/spreadsheets/dist`
   (`.clasp.json` lands there, and git ignores it):

   ```bash
   cd bindings/spreadsheets/dist && clasp create-script --type standalone --title "Raku formulas" --rootDir google-sheets-addon && clasp push --force
   ```

   In the Apps Script editor, **Project Settings → Google Cloud Platform (GCP)
   Project → Change project** takes the Cloud project's number. The script ID
   (**Project Settings → IDs**) goes into the repository variable
   `SHEETS_ADDON_SCRIPT_ID`, with the secret `CLASPRC_JSON`, for the release
   job to push each release.
4. **A test, unpublished.** **Deploy → Test deployments**, type **Editor
   Add-on**, a test with **Latest code** in a spreadsheet, then **Execute**.
   Under **Extensions → Raku formulas** there are three items; `=RAKU("6 * 7")`
   is 42, **Add the Raku sheet** adds it, `=RAKU("iban-ok($^s)", "GB82 WEST 1234 5698 7654 32")`
   is TRUE, and **How RAKU formulas work** opens the help.
5. **A version.** `clasp create-version "rakupp 5.2.1"` prints its number;
   **Deploy → Manage deployments** shows it too.
6. **The OAuth consent screen** (Google Auth Platform):
   - **Branding:** app name `Raku formulas`; user support email
     `mail@deepsoft.online` (the console offers the publishing account's own
     address and Google Groups it manages); logo `dist/store/icon-120.png`
     (optional, and a logo is checked in brand verification); home page,
     privacy policy and terms as in the table; authorized domain
     `raku.online`; developer contact email `mail@deepsoft.online`.
   - **Audience:** External, then **Publish app**.
   - **Data Access:** `https://www.googleapis.com/auth/spreadsheets.currentonly`,
     the add-on's only scope (`appsscript.json`).
   - Submit the verification the console asks for. Should it ask for a demo
     video, the video shows installing the add-on, a RAKU formula, and
     **Add the Raku sheet**.
7. **The Marketplace SDK.** Enable **Google Workspace Marketplace SDK** in the
   Cloud project, then on **App Configuration**:
   - **App visibility:** Public. It cannot be changed afterwards, and a
     @gmail.com account can publish only publicly.
   - **Installation settings:** Individual + Admin Install.
   - **App integration:** Editor add-on, **Sheets add-on**, the script ID and
     the version from step 5.
   - **OAuth scopes:** the one scope above.
   - **Developer information:** name, website, email `mail@deepsoft.online`
     (Google's reviewers write to it, and it is not shown in the listing),
     and trader status (shown to customers in the EEA).
   - **Application website URL:** the homepage.
8. **The store listing**, below; then **Publish**. Google reviews it before it
   is listed.

### Each release

The release job pushes the add-on and makes a version of it, and its notice
names the number; enter it under **App Configuration → Sheets add-on →
Version** and save. By hand: build, `clasp push --force`, `clasp create-version`.

### The listing

**Application name** (as on the consent screen): Raku formulas

**Short description** (182 of 200 characters):

> Write spreadsheet formulas in Raku: decimals that add up exactly, integers of any size, regexes and grammars for the text in your cells, and your own functions on a sheet named Raku.

**Detailed description:**

> Raku formulas adds one function to Google Sheets, =RAKU, which runs code in the Raku programming language and puts the result in the cell.
>
> =RAKU("0.1 + 0.2 == 0.3") is TRUE. Raku keeps decimals as exact fractions, so sums of money and measurements come out as written, and integers have no size limit: =RAKU("[*] 1..$^n", 30) gives all 33 digits of 30 factorial.
>
> What a formula can do:
> • Arithmetic on cells and ranges. $^a, $^b, … are the values after the code, and @_ is all of them, ranges flattened: =RAKU("[+] @_", A1:A10) adds a column.
> • Text. Regexes and grammars work on the text in your cells: =RAKU("~($^s ~~ / \d+ ' kg' /)", A2) finds "12 kg" in "Box of 12 kg flour".
> • Lists. A list fills a column, and a list of lists fills a table: =RAKU("(1..4).map(* ** 2)") gives 1, 4, 9 and 16 down a column.
> • Functions of your own. Write Raku subs in column A of a sheet named Raku, and every formula in the spreadsheet can call them. Extensions → Raku formulas → Add the Raku sheet makes one, with two examples: an IBAN check, and a sum split into cents that add up.
>
> The engine is Raku++, compiled to WebAssembly, and it runs inside the add-on: formulas send nothing anywhere, and the add-on keeps nothing. It asks for one permission, to see and change the spreadsheet it is used in.
>
> Good to know:
> • Every formula is a run of its own, so give one formula a whole range rather than writing one formula per cell.
> • Sheets recalculates a formula when its arguments change. After editing the Raku sheet, choose Extensions → Raku formulas → Recalculate RAKU formulas.
> • Formulas cannot read files or use the network.
>
> Guide and examples: https://raku.online/embed/spreadsheets/
> Raku formulas are free and open source, under the Artistic License 2.0: https://github.com/ash/rakupp

**Category:** Productivity. **Pricing:** Free of charge.

**Support links:** terms, privacy policy and support as in the table;
**Help** `https://raku.online/embed/spreadsheets/`; **Report issue**
`https://github.com/ash/rakupp/issues`.

## Microsoft AppSource

### Once

1. **A Partner Center account** in the Microsoft 365 and Copilot program
   ([open one](https://learn.microsoft.com/partner-center/marketplace-offers/open-a-developer-account)),
   as an individual or a company; Microsoft verifies it.
2. **The offer.** **Marketplace offers → New offer → Office add-in**, named
   `Raku formulas`: the manifest's `DisplayName`, which the listing's name
   must match. The manifest's `ProviderName` is `Raku++`; if the publisher's
   name in Partner Center is a person's or a company's, the two can be made
   the same.
3. **Properties:** a category (Productivity), the privacy policy, and the
   terms of use page as the terms.
4. **Offer listing:** the text below, the support contact
   `mail@deepsoft.online`, the logo `dist/store/icon-300.png`
   (216 to 350 pixels square), the screenshots, and the support and privacy
   links from the table.
5. **Availability:** free, in every market.
6. **Technical configuration:** upload `manifest.xml`, downloaded from
   `https://raku.online/embed/excel/manifest.xml`, so that it points at the
   files raku.online serves.
7. **Notes for certification**, below, then **Review and publish**.

### Each release

The add-in's files are on raku.online, so a new engine reaches everyone who
installed it when the site is republished (`sites/spreadsheets/sync.sh` in
the raku.online repository), without a resubmission. Partner Center needs a
new upload only when the manifest itself must change, and AppSource takes it
only with a higher `<Version>`: a release raises it, and between releases
`--revision=N` raises its fourth number.

### The listing

**Name:** Raku formulas

**Summary** (91 of 100 characters):

> Formulas in Raku: exact decimals, integers of any size, regexes, and functions of your own.

**Description:**

> Raku formulas adds one function to Excel, =RAKU.EVAL, which runs code in the Raku programming language and puts the result in the cell.
>
> =RAKU.EVAL("($^a - $^b) + 1", 43.1, 43.2) is exactly 0.9, where Excel's own =(43.1-43.2)+1 shows 0.899999999999999. Raku keeps decimals as exact fractions, so sums of money and measurements come out as written, and integers have no size limit: =RAKU.EVAL("[*] 1..$^n", 30) gives all 33 digits of 30 factorial.
>
> What a formula can do:
> • Arithmetic on cells and ranges. $^a, $^b, … are the values after the code, and @_ is all of them, ranges flattened: =RAKU.EVAL("[+] @_", A1:A10) adds a column.
> • Text. Regexes and grammars work on the text in your cells: =RAKU.EVAL("~($^s ~~ / \d+ ' kg' /)", A2) finds "12 kg" in "Box of 12 kg flour".
> • Lists. A list spills down a column, and a list of lists fills a table: =RAKU.EVAL("(1..4).map(* ** 2)") gives 1, 4, 9 and 16.
> • Functions of your own. Write Raku subs in column A of a sheet named Raku, and every formula in the workbook can call them. The Raku pane adds the sheet with two examples: an IBAN check, and a sum split into cents that add up.
>
> The Raku button on the Home tab opens the Raku pane: examples to put into the selected cell, the Raku sheet, recalculation, and what formulas print. Formulas that arrive together run as one batch, so a column of them costs about as much as one.
>
> The engine is Raku++, compiled to WebAssembly, and it runs inside Excel, on your computer or in your browser: formulas send nothing anywhere. Formulas cannot read files or use the network.
>
> Works in Excel for Microsoft 365 on Windows and Mac, and in Excel on the web.
>
> Guide and examples: https://raku.online/embed/spreadsheets/
> Raku formulas are free and open source, under the Artistic License 2.0: https://github.com/ash/rakupp

**Search keywords:** Raku, decimal, regex

### Notes for certification

> Raku formulas adds the custom function =RAKU.EVAL(code, values…). There is no sign-in, account or external service: the engine, WebAssembly, is loaded from https://raku.online/embed/excel/ and runs in the add-in's shared runtime.
>
> 1. Install the add-in, and click the Raku button on the Home tab once: Excel registers =RAKU.EVAL on the add-in's first run. The pane shows "Ready: Raku++ …, loaded in … s".
> 2. =RAKU.EVAL("6 * 7") gives 42.
> 3. =RAKU.EVAL("($^a - $^b) + 1", 43.1, 43.2) gives 0.9, where =(43.1-43.2)+1 gives 0.899999999999999.
> 4. With 0.1 in each of A1:A10, =RAKU.EVAL("[+] @_", A1:A10) gives 1.
> 5. =RAKU.EVAL("(1..4).map(* ** 2)") gives 1, 4, 9 and 16, spilling down a column.
> 6. =RAKU.EVAL("1/0") gives #DIV/0!.
> 7. In the pane, Add the Raku sheet adds a sheet named Raku; then =RAKU.EVAL("iban-ok($^s)", "GB82 WEST 1234 5698 7654 32") gives TRUE, and =RAKU.EVAL("split-cents($^a, $^n)", 100, 3) gives 33.34, 33.33 and 33.33.
