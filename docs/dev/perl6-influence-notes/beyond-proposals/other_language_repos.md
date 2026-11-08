# Perl 6 / Raku in the design archives of the remaining languages

Scope and method (7 November 2026). Every repository below was shallow-cloned (`git clone --depth 1`; sparse partial clones for Julia, Zig, Modular and TypeScript, whose full trees are mostly code) into the session scratchpad and every text file was grepped case-insensitively for `perl[ _-]?6|\braku(do)?\b`, plus a multi-line variant for "Perl" and "6" split across a line break (no hits anywhere). Plain `\bperl\b` lines were listed separately and read one by one. The GitHub issue search (`"Perl 6" OR Perl6 OR Raku OR Rakudo`, one query per tracker, 8 s apart, unauthenticated) covered 17 trackers. Every returned thread was then read from its github.com HTML page; threads whose matching comment was hidden behind "Load more" were completed through the public `timeline_more_items` HTML fragments (pull requests), bugs.python.org's read-only HTML (migrated CPython issues), or, for 13 issue threads only, the REST comments endpoint (23 calls in total; see Gaps). Each repository's HEAD date is given in the table so the search can be repeated.

## 1. What was searched, and how many hits each archive had

### Takeaway
Across about 83,500 files in 20 design and documentation repositories plus 329 TC39 proposal repositories, only **three files** name Perl 6 or Raku outside the five already-known PEPs: the Go 2 transition design doc (a cautionary tale), Carbon's `and`/`or`/`not` proposal (prior art), and TC39's Decimal README (prior art for rationals). The issue trackers add about 55 genuine threads, almost all one-line suggestions by non-members; the rest of the search engine's hits are false positives.

### Cited Findings

**Proposal repositories (file grep)**

| Repository (HEAD date) | What was grepped | Files | Perl 6 / Raku lines (files) | Plain "Perl" lines | Notes |
|---|---|---|---|---|---|
| [carbon-language/carbon-lang](https://github.com/carbon-language/carbon-lang) (2026-11-06) | whole tree incl. `proposals/`, `docs/design/` | 5,869 | 3 (1: [p0680](https://github.com/carbon-language/carbon-lang/blob/trunk/proposals/p000680-and-or-not.md)) | 16 | Perl lines: TIMTOWTDI in the one-way principle (p0829), Perl `eq`/`ne` (p0702), RE2 example comments |
| [golang/proposal](https://github.com/golang/proposal) (2026-10-16) | whole tree (`design/`) | 301 | 7 (1: [28221-go2-transitions.md](https://github.com/golang/proposal/blob/master/design/28221-go2-transitions.md)) | 23 | Perl lines: number-literal survey (Perl 5 history), Perlpod and Markdown's Perl origin in the doc-comment design |
| [ghc-proposals/ghc-proposals](https://github.com/ghc-proposals/ghc-proposals) (2026-10-20) | whole tree | 199 | 0 | 0 | |
| [ocaml/RFCs](https://github.com/ocaml/RFCs) (2026-02-20) | whole tree | 10 | 0 | 0 | |
| [fsharp/fslang-design](https://github.com/fsharp/fslang-design) (2026-11-05) | whole tree | 199 | 0 | 1 | FS-1005 numeric underscores cites perldata (Perl 5) |
| [fsharp/fslang-suggestions](https://github.com/fsharp/fslang-suggestions) (2025-02-27) | whole tree (suggestions live in issues) | 4 | 0 | 0 | |
| [PowerShell/PowerShell-RFC](https://github.com/PowerShell/PowerShell-RFC) (2026-10-16) | whole tree | 285 | 0 | 0 | |
| [nim-lang/RFCs](https://github.com/nim-lang/RFCs) (2023-06-20) | whole tree (RFCs live in issues) | 5 | 0 | 0 | |
| [tc39/proposals](https://github.com/tc39/proposals) (2026-10-14) | README, stage-0/1, finished, inactive lists, `ecma402/` lists | 13 | 0 | 0 | lists link 331 distinct GitHub repos |
| 329 linked TC39 proposal repos (all stages, finished, inactive; 2 of 331 no longer exist: `jasnell/proposal-construct`, `tc39/proposal-array-get-non-index-string-properties`) | every file | 61,897 | 1 ([proposal-decimal README](https://github.com/tc39/proposal-decimal/blob/main/README.md)) | 47 (24 files) | all Perl lines are Perl 5 regex prior art (named groups, `x` mode, lookbehind, `\R`, atomic groups, modifiers), `quotemeta`, numeric separators, `delete $h{k}` |
| [tc39/ecma262](https://github.com/tc39/ecma262) (2026-11-07) | whole tree | 50 | 0 | 2 | "modelled after … Perl 5" |
| [tc39/notes](https://github.com/tc39/notes) (2026-09-02) | all meeting notes 2012–2026 | 413 | 0 | 47 | Perl 5 regex prior art; Perl as a warning about syntax sugar (2018-03 "punctuation budget", 2019-07); "No one uses Perl these days!" (2017-11) |

**Docs and changelogs (file grep)**

| Repository (HEAD date) | What was grepped | Files | Perl 6 / Raku | Plain "Perl" | Notes |
|---|---|---|---|---|---|
| [JuliaLang/julia](https://github.com/JuliaLang/julia) (2026-11-07) | `NEWS.md`, `HISTORY.md`, all root `*.md`, `doc/` | 174 | 0 | 30 | all Perl 5: backtick commands "from the shell, Perl, and Ruby", `$` interpolation "as in Perl", PCRE, perlre modifiers |
| [elixir-lang/elixir](https://github.com/elixir-lang/elixir) (2026-11-07) | whole tree (incl. `lib/elixir/pages`) + `CHANGELOG.md` of all 20 branches v1.0–v1.19 | 779 + 20 | 0 | 2 | PCRE only |
| [ziglang/zig](https://github.com/ziglang/zig) GitHub mirror (frozen 2025-11-26) and [Codeberg](https://codeberg.org/ziglang/zig) (2026-11-07) | root `*.md`, `doc/` (langref) | 291 + 296 | 0 | 0 | |
| [gleam-lang/gleam](https://github.com/gleam-lang/gleam) (2026-11-07) | whole tree incl. `CHANGELOG.md`, `changelog/` | 5,435 | 0 | 1 | bundled highlight.js |
| [crystal-lang/crystal](https://github.com/crystal-lang/crystal) (2026-11-07) + [crystal-book](https://github.com/crystal-lang/crystal-book) (2026-11-05) | whole trees incl. `doc/changelogs/` (pre-1.0 to v1.21) | 2,725 + 196 | 0 | 8 | PCRE, markd fixtures, build scripts |
| [modular/modular](https://github.com/modular/modular) (2026-11-07) | root `*.md`, `mojo/` incl. `mojo/proposals/` (57 entries) and docs/changelog, minus stdlib source | 3,428 | 0 | 8 | Perl one-liners in formatter test data |
| [microsoft/TypeScript](https://github.com/microsoft/TypeScript) (2026-11-06) | root `*.md` (the repo now holds the Go port and has no `doc/`), plus the archived spec at tags [v1.8.10](https://github.com/microsoft/TypeScript/blob/v1.8.10/doc/spec.md) and [v4.9.5](https://github.com/microsoft/TypeScript/blob/v4.9.5/doc/spec-ARCHIVED.md) | 5 + 2 | 0 | 0 | design notes are GitHub issues, covered by the issue search |
| [python/peps](https://github.com/python/peps) (2026-11-06) | all PEPs | 915 | 14 (5 PEPs: 344, 502, 3117, 3133, 3134) | 95 (44 files) | earlier hits confirmed; none missed |

**GitHub issue search** (`repo:<r> "Perl 6" OR Perl6 OR Raku OR Rakudo`; issue #6 discarded in every repo as the known false match)

| Tracker | Returned (−#6) | Genuine Perl 6 / Raku text | False positives (verified by reading the page) | Not fully read |
|---|---|---|---|---|
| golang/go | 10 | 7 (#13229, #17178, #28221, #34313, #35386, #44550, #48061) | 3 (#6943, #10235, #44022) | 0 |
| ziglang/zig | 1 | 1 (#359) | 0 | 0 |
| nim-lang/Nim | 2 | 1 (#1737) | 1 (#10011) | 0 |
| nim-lang/RFCs | 0 | — | — | — |
| JuliaLang/julia | 22 | 8 (#88, #417, #14383, #26211, #30528, #33688, #43176, #57670) | 13 (build logs, `perl` package lists; #11764 checked through the API) | 1 (#36617, Darwin/ARM64 tracking issue, 128 hidden comments) |
| elixir-lang/elixir | 2 | 1 (#2901) | 1 (#6307) | 0 |
| microsoft/TypeScript | 2 | 1 (#5326, via API) | 1 (#62065) | 0 |
| dotnet/roslyn | 3 | 3 (#2239, #6340, #9330 via API) | 0 | 0 |
| fsharp/fslang-suggestions | 3 | 3 (#619 via API, #675, #1079) | 0 | 0 |
| swiftlang/swift | 0 | — | — | — |
| rust-lang/rust | 67 | 8 (#2394, #3591, #28237, #28979, #45510, #66616, #84056; #52753 is a bot list naming the `rakudo-star` package) | 59 (#50547 read in full via the API; #15228, #45317; ~56 PRs whose CI-failure bot comments put an apt `perl` package next to a `6`) | 0 |
| crystal-lang/crystal | 8 | 8 (#323, #501, #758, #2968, #4376, #9198, #10374, #12999) | 0 | 0 |
| python/cpython | 26 | 9 (#38067, #46888 = bpo-2636, #48075, #49787, #51381, #56938, #81552, #83326; PR #14306) | 17 (checked on bugs.python.org; #124130 through the API) | 0 |
| php/php-src | 0 | — | — | — |
| gleam-lang/gleam | 0 | — | — | — |
| carbon-language/carbon-lang | 2 | 2 (#471, #684) | 0 | 0 |
| ghc-proposals/ghc-proposals | 3 | 3 (#125, #570, #754) | 0 | 0 |

- GitHub's phrase search is loose: it matched `"Perl 6"` against text such as `perl/5.16.3`, `perl-HTTP-Message 6.11-1` and "Comment 6 … perl", so a hit count is not a mention count — e.g. [julia#35220](https://github.com/JuliaLang/julia/issues/35220), [bpo-30609](https://bugs.python.org/issue30609), [go#6943](https://github.com/golang/go/issues/6943).

### Inferences
- Outside Python, no design repository other than Go's, Carbon's and TC39's names Perl 6 or Raku at all, and GHC, OCaml, PowerShell, Zig, Gleam, Elixir, Mojo and the TypeScript spec do not even mention Perl 5 in a design sense. The absence is itself the finding: these archives do not treat Perl 6 as a source.
- The issue trackers carry more Raku than the proposal repositories, because suggestions by outsiders land there and are rarely carried into a written proposal.

### Gaps
- Julia #36617 (Darwin/ARM64 tracking, 155 comments) was not read past the visible 27; its subject makes a design mention unlikely.
- Discussion venues outside these repos were not searched: go-nuts / golang-dev, Julia Discourse, Elixir Forum, ziggit, Nim forum, discuss.ocaml.org, Haskell Discourse, es-discuss / esdiscuss.org (including the ES4 era, 2006–08, when Siek grouped Perl 6 with ES4), TC39 Discourse, the TypeScript wiki, and the Mojo Discord.

## 2. The file hits: what each says and what it led to

### Takeaway
The three non-Python file hits are a cautionary tale (Go), a row in a prior-art survey that the proposal did not follow (Carbon), and a one-line catalogue entry for Rat in a proposal that explicitly declined rationals (TC39 Decimal). None is a borrowing.

### Cited Findings
- **Go 2 transitions design doc** — Ian Lance Taylor (Go core team), last updated 15 October 2018, [design/28221-go2-transitions.md](https://github.com/golang/proposal/blob/master/design/28221-go2-transitions.md). Its "Perl" background section dates Perl 6 from 2000 to a 2015 spec, calls it intentionally incompatible, spec-defined and endlessly evolving, and draws the lesson "Don't be Perl 6." beside "Set and meet deadlines" and "Don't change everything at once". It also notes that after "moving to Python 3 or Perl 6" accumulated feature flags can be discarded, which Go cannot do. **Cautionary tale.** Outcome: the companion issue [golang/go#28221](https://github.com/golang/go/issues/28221) ("record language version to support language transitions") was labelled Proposal-Accepted and closed 14 November 2018; from Go 1.12 the `go` directive in `go.mod` "indicates the version of the language used by the files within that module" ([Go 1.12 notes](https://go.dev/doc/go1.12)), which is how Go gates language changes. Russ Cox, 14 August 2023: "There will not be a Go 2 that breaks Go 1 programs." ([Go blog](https://go.dev/blog/compat)). A reviewer on the issue, Andrew Ekstedt (contributor), added that Perl 5's `use VERSION` and Python's `__future__` were better models than the two cautionary cases ([comment, 16 Oct 2018](https://github.com/golang/go/issues/28221#issuecomment-430296936)).
- **Carbon p0680 "And, or, not"** — PR [#680](https://github.com/carbon-language/carbon-lang/pull/680), July 2021, by Richard Smith (Carbon lead), [proposal](https://github.com/carbon-language/carbon-lang/blob/trunk/proposals/p000680-and-or-not.md). The background survey says "Perl, Ruby, and Raku permit both syntaxes, giving the punctuation forms higher precedence and the keyword forms lower precedence", notes Raku's `andthen`/`orelse`, and lists Raku among languages whose `and`/`or` return the deciding operand. **Cited as prior art.** Outcome: the leads decided on 31 July 2021 that all three operators are keywords ([#684](https://github.com/carbon-language/carbon-lang/issues/684)), and the proposal's alternatives section rejects the "produce the decisive value" design that it attributes to Python, Perl and Raku, because chained conditions would need a common type and type deduction would yield surprises such as `var x: auto = p and q` deducing a pointer type ([p0680](https://github.com/carbon-language/carbon-lang/blob/trunk/proposals/p000680-and-or-not.md)). Carbon's operators take and return `bool` and come only in keyword form, not as Raku's dual-precedence pair ([logical_operators.md](https://github.com/carbon-language/carbon-lang/blob/trunk/docs/design/expressions/logical_operators.md)).
- **TC39 Decimal** — [proposal-decimal README](https://github.com/tc39/proposal-decimal/blob/main/README.md), entry added by Daniel Ehrenberg (proposal champion) in the initial README on 13 November 2019 (commit `7c06791`), moved to `RELATED.md` on 3 February 2020 and back into the README by Jesse Alama on 30 June 2026 (commit `1ed01b6`): under "Rationals", "**Perl6**: Literals like `1.5` are Rat instances!" — beside Common Lisp, Scheme and Ruby. **Cited as prior art.** Outcome: Decimal is still at Stage 1 and the README's "So why not rationals?" section rejects rationals for this proposal (denominator growth, GCD normalisation, irrational results of `exp`/`log`), leaving them as "an interesting thing to pursue" separately.
- **PEPs re-check** — the full grep of 915 PEP files matches exactly the five PEPs already known: [PEP 344](https://peps.python.org/pep-0344/) and [PEP 3134](https://peps.python.org/pep-3134/) (Ka-Ping Yee, 12 May 2005; RFC 88 cited for exception chaining; 3134 Final in 3.0), [PEP 3133](https://peps.python.org/pep-3133/) (Collin Winter, 1 May 2007, Rejected; Perl 6 roles via S12), [PEP 3117](https://peps.python.org/pep-3117/) (Georg Brandl, core developer, 1 April 2007, an April Fool: Unicode type markers would "make Python measure up to Perl 6", footnoted "Well, it would, if there was a Perl 6.") and [PEP 502](https://peps.python.org/pep-0502/) (Mike G. Miller, 10 August 2015, Rejected informational survey; under "Perl" it shows `say "Hello { @names.join(', ') } how are you?";  # Perl 6 version`). No PEP was missed.

### Inferences
- The Go document is the one formal design record in these archives that uses Perl 6 purely as the negative example, and it was written by a core team member; this fits the report's existing line that other communities cite Perl 6 more often as a warning about compatibility than as a source.
- The Carbon and Decimal hits follow the report's near-miss pattern exactly: Raku as one row in a survey table, for a small semantic point (dual-precedence logical operators; rational literals), with the design going elsewhere.

### Gaps
- The Carbon proposal file does not name its author; Richard Smith's authorship rests on his comment that he "updated #680" ([#684 comment](https://github.com/carbon-language/carbon-lang/issues/684#issuecomment-888687709)).

## 3. Issue-tracker hits, by repository

### Takeaway
The trackers hold about 55 genuine mentions. The noteworthy ones are two by core members who proposed Perl 6 syntax themselves — Patrick Walton for Rust closures (2012) and Brad Fitzpatrick for a Go `...` statement (2019), both withdrawn — and explicit, reasoned rejections of NFG (Julia), rules-style regex (Rust, and the author of Python's `regex` module), Raku-style exclusive ranges (Zig) and junction-like comparison (F#). Everything else is a passing mention, mostly by non-members.

### Cited Findings

**Go (golang/go)**
- [#35386](https://github.com/golang/go/issues/35386), 5 Nov 2019, Brad Fitzpatrick (Go team), "proposal: spec: support ... TODO statement": a `...` statement that compiles but panics, "Like https://docs.perl6.org/routine/....html", noting that in Raku it must be the only statement in a block. **Cited as prior art (the model), then withdrawn**: Rob Pike objected that it would block `go test`, others preferred `panic("TODO")` or a Kotlin-style `TODO()`, and Fitzpatrick retracted it on 26 Nov 2019 ("It was mostly there for completeness").
- [#17178](https://github.com/golang/go/issues/17178), 21 Sep 2016, non-member: "Parameters in `main()` map to command-line parameters (Perl6)", citing the 2010 Perl 6 Advent post on `MAIN`. **Considered, rejected** within an hour by Fitzpatrick: "We probably won't take every language's every feature in a hypothetical Go2."
- [#44550 comment](https://github.com/golang/go/issues/44550#issuecomment-784498834), 23 Feb 2021, contributor: "Breaking changes are bad. Who is using Perl 6? Python 3 was a pain." **Cautionary tale**; semantic import versioning was kept.
- [#13229 comment](https://github.com/golang/go/issues/13229#issuecomment-156727882), 14 Nov 2015, Dana Jacobsen: as evidence for BPSW primality testing, "I have a BPSW implementation for Perl6 using MoarVM". **Passing mention** (the algorithm is not a Perl 6 design).
- [#34313 comment](https://github.com/golang/go/issues/34313#issuecomment-549169330) (Nov 2019, Perl 6 string review linked in a `runes` package proposal, declined by rsc) and [#48061](https://github.com/golang/go/issues/48061) (Aug 2021, Raku's Slovak collation "does accents right") are **passing mentions**.

**Zig (ziglang/zig)**
- [#359 comment](https://github.com/ziglang/zig/issues/359#issuecomment-618326211), 23 Apr 2020, non-member: "I like how Raku handles this", proposing `0 ..^ 5` and `10 ^..^ 15` in switch ranges. **Considered, rejected**: Andrew Kelley closed the issue on 21 Jan 2021 and Josh Wolfe explained the status quo (exclusive `a..b` for slices, inclusive `a...b` for switch cases); Ginger Bill objected that `^` means "from the end" in Nim.

**Nim (nim-lang/Nim)**
- [#1737](https://github.com/nim-lang/Nim/issues/1737), 14 Dec 2014: an init-action-deinit idiom, "AFAIK Perl6 and J do support such functionality". Andreas Rumpf (creator) closed it in 11 minutes: "So you did re-invent templates." **Passing mention**; Nim already had `withFile`-style templates.

**Julia (JuliaLang/julia)**
- [#14383](https://github.com/JuliaLang/julia/pull/14383) "RFC, WIP: highlander" (Stefan Karpinski's string redesign). On 17 Jan 2016 a commenter cited Perl 6's constant-time grapheme indexing with jnthn's NFG slides ([comment](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172385367)); Simon Byrne (member) summarised NFG's negative synthetic codepoints ([comment](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172387903)); Perl 6 contributor diakopter warned that the Parrot PDD 28 write-up "was always aspirational" ([comment](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172391604)); Milan Bouchet-Valat (member) rejected it for Julia: the approach "only makes sense when starting from UTF-32, and certainly not from UTF-8" and is "terribly wasteful as regards memory use" ([comment, 18 Jan 2016](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172476026)). **Considered, rejected.** The PR closed unmerged on 21 Jun 2016; Julia string indices are byte offsets into UTF-8 ([manual](https://github.com/JuliaLang/julia/blob/master/doc/src/manual/strings.md)) and graphemes are an iterator in the `Unicode` stdlib ([HISTORY.md, 0.7](https://github.com/JuliaLang/julia/blob/master/HISTORY.md)).
- [#88 comment](https://github.com/JuliaLang/julia/issues/88#issuecomment-1445670), 27 Jun 2011, Stefan Karpinski (co-creator), redesigning the regex API: "There's a reason why Larry Wall set out to give the whole business a makeover in Perl 6." **Passing mention**; Julia shipped `r"..."` PCRE literals.
- [#417 comment](https://github.com/JuliaLang/julia/pull/417#issuecomment-9635348), 20 Oct 2012, Mike Nolta (member): "python3 decided to use 0o , while perl6 decided on 0c . I think i'll go ahead and add 0o literals." **Passing mention**, and inaccurate: S02 specifies `0o` for base 8 in every version since the November 2008 import ([S02](https://github.com/Raku/old-design-docs/blob/master/S02-bits.pod)), so Julia's `0o` matches both languages.
- [#33688 comment](https://github.com/JuliaLang/julia/pull/33688#issuecomment-546990082), 28 Oct 2019, Rafael Fourquet (member), on a regex partial-match flag: compares Perl 6's `:p`/`:pos` matching adverb and notes that Perl 6 attaches matching options to the match rather than the regex. **Cited as prior art**; the PR is still open.
- [#26211 comment](https://github.com/JuliaLang/julia/pull/26211#issuecomment-368606162), 26 Feb 2018, Jacob Quinn (member): a cross-language table of "does string match regex" includes "perl 6 str ~~ /string$/". **Passing mention**; the revert was closed unmerged and Julia 0.7 settled on `occursin`.
- [#30528](https://github.com/JuliaLang/julia/issues/30528#issuecomment-450504366) (`/usr/lib/perl6/vendor` as a site-path precedent), [#43176](https://github.com/JuliaLang/julia/issues/43176) (Raku as the one slower language in a `println` benchmark), [#57670](https://github.com/JuliaLang/julia/issues/57670#issuecomment-3451547484) (a raku-community-modules OpenSSL issue cited for a macOS libcrypto problem): **passing mentions**.

**Elixir (elixir-lang/elixir)**
- [#2901 comment](https://github.com/elixir-lang/elixir/pull/2901#issuecomment-64446989), 25 Nov 2014, vendethiel (a Perl 6 contributor): Perl has minmax "(List::Util for perl5 / minmax binary op for perl6)". **Passing mention**; the PR, which added what is now `Enum.min_max`, was merged the same day.

**C# (dotnet/roslyn)**
- [#2239 comment](https://github.com/dotnet/roslyn/issues/2239#issuecomment-246411974), 12 Sep 2016, non-member: "a rather different syntax idea coming from the land of Perl 6", a `^"TERMINATOR"` heredoc modelled on `q:to`. **Suggestion not taken**; the issue was closed as a duplicate in Nov 2021 and C# 11 shipped `"""` raw strings whose closing delimiter sets the indentation, with no Perl 6 citation (the csharplang archive was already searched).
- [#6340 comment](https://github.com/dotnet/roslyn/issues/6340#issuecomment-151314340), 26 Oct 2015, same commenter: Perl 6's `@words».say` "autothreading" as a parallel-by-spec example. **Passing mention**; closed by Neal Gafter in 2017.
- [#9330 comment](https://github.com/dotnet/roslyn/issues/9330#issuecomment-192011753), 3 Mar 2016, non-member Demis Bellot (mythz), in the thread for Mads Torgersen's "C# Design Notes – catch up edition, Feb 29, 2016": Perl "took 16 years to ship its latest Perl 6 … not sure what to take from Perl other than what not do to". **Cautionary tale** (argument against adding features), with no response from the design team on that point.

**F# (fsharp/fslang-suggestions)**
- [#675 comment](https://github.com/fsharp/fslang-suggestions/issues/675#issuecomment-399237834), 21 Jun 2018, non-member: "Perl 6 sort-of has a syntax for this like `if (42 == 30 | 42 | 100 ) { ... }`" — a junction. **Considered, rejected**: labelled declined; Don Syme (F# designer), 16 Jun 2022: "This isn't something we'll do directly in the F# language as there are various ways to program this".
- [#1079 comment](https://github.com/fsharp/fslang-suggestions/issues/1079#issuecomment-1508974106), 14 Apr 2023, non-member: Raku as a language that allows Unicode operators. **Cited as prior art**; suggestion still open.
- [#619 comment](https://github.com/fsharp/fslang-suggestions/issues/619#issuecomment-565733217), 14 Dec 2019, contributor: "Python 2/3, VB6/VB.NET, Perl5/Perl6, ..." as examples of the breaking changes F# fears. **Cautionary tale, passing.**

**TypeScript (microsoft/TypeScript)**
- [#5326 comment](https://github.com/microsoft/TypeScript/issues/5326#issuecomment-455155622), 17 Jan 2019, non-member, on combining destructuring with parameter properties: proposes a `construct` keyword for "**constructed properties**, inspired by languages like Perl6", i.e. fields filled from a named-argument object without writing a constructor, as Raku's `has` attributes are by the default `new`. **Suggestion not taken**; the issue is still open and no TypeScript design note was found that picks it up.

**Rust (rust-lang/rust)**
- [#2394 comment](https://github.com/rust-lang/rust/issues/2394#issuecomment-5814294), 21 May 2012, Patrick Walton (then Rust core team), on closure syntax: "Perl 6's syntax works for us. That's `-> args block`", listing fewer shifted characters and braces at line end as benefits. **Considered, rejected**: Niko Matsakis had just said he preferred `|| { ... }` to `-> { ... }` as "more consistent", Walton closed the issue the next day "because I think we have something better", and Rust 0.3 (12 Jul 2012) listed "More compact closure syntax" and `do` expressions ([RELEASES.md](https://github.com/rust-lang/rust/blob/master/RELEASES.md)); Rust closures have been written `|args| expr` ever since.
- [#3591 comment](https://github.com/rust-lang/rust/issues/3591#issuecomment-40685954), 17 Apr 2014, contributor: a new regex library is incomplete "without a mention of Perl 6", linking S05. Andrew Gallant (BurntSushi, author of the regex work) [replied](https://github.com/rust-lang/rust/issues/3591#issuecomment-40775984): grammar-based regexes "definitely" look cool but are "a separate issue", a "prime case for trying out an EDSL in Rust", and the RE2-style engine has no backreferences or lookaround. **Considered, rejected (deferred to libraries).**
- [#66616 comment](https://github.com/rust-lang/rust/issues/66616#issuecomment-557397246), 22 Nov 2019, contributor: "Python 3.6 added a tau constant … Nim and Perl 6 include one as well." **Cited as prior art**; `f32::TAU`/`f64::TAU` shipped in Rust 1.47.0 (8 Oct 2020).
- [#28979 comment](https://github.com/rust-lang/rust/issues/28979#issuecomment-271120669), 8 Jan 2017, non-member: Cyrillic identifiers in JavaScript, Perl 5 and Perl 6. **Passing mention**; Rust 1.53.0 (17 Jun 2021) allowed UAX #31 identifiers, "the same standard as languages like Python".
- [#84056 comment](https://github.com/rust-lang/rust/issues/84056#issuecomment-1185263120), 15 Jul 2022, non-member: Raku's `uniprop('Numeric_Type')` as a model for `char::is_numeric`. **Cited as prior art**; no API change found.
- [#45510](https://github.com/rust-lang/rust/issues/45510), 24 Oct 2017: an error message is, "to … lovingly borrow a term from the Perl 6 community", "less than awesome". **Passing mention** (LTA vocabulary).
- [#28237 comment](https://github.com/rust-lang/rust/issues/28237#issuecomment-287577990), 18 Mar 2017, Scott Olson (contributor), in the tracking issue for inclusive ranges: when `..^` was floated for the *inclusive* range ([retep998](https://github.com/rust-lang/rust/issues/28237#issuecomment-287563684), and lang-team member Josh Triplett liked it, [comment](https://github.com/rust-lang/rust/issues/28237#issuecomment-287564532)), Olson replied "I've already seen this syntax in Perl 6 where it means exclusive, the opposite of the proposal here." **Cited as a counter-example**; Rust kept `..=`, stabilised in 1.26.0 (10 May 2018) ([RELEASES.md](https://github.com/rust-lang/rust/blob/master/RELEASES.md)).
- #50547 (async/await tracking, 308 comments) was read in full through the API: no Perl 6 or Raku text, a search false positive.

**Crystal (crystal-lang/crystal)**
- [#2968 comment](https://github.com/crystal-lang/crystal/issues/2968#issuecomment-400616952), 27 Jun 2018, contributor: `Int / Int` could return a `BigRational` "like Perl6 does". **Considered, rejected**: Crystal 0.31.0 (23 Sep 2019) made `/` float division, with `//` for floor division ([changelog](https://github.com/crystal-lang/crystal/blob/master/doc/changelogs/pre-1.0.md)).
- [#323 comment](https://github.com/crystal-lang/crystal/issues/323#issuecomment-73782276), 10 Feb 2015, vendethiel: quotes Perl 6's grammar/actions model and `$/` with `~~`/`given`/`when`. **Considered, rejected**: Ary Borenszweig kept Ruby's `$~`, `$1` and `=~`, made thread-safe.
- [#758 comment](https://github.com/crystal-lang/crystal/issues/758#issuecomment-114755901), 24 Jun 2015, vendethiel: "perl6 uses `a ?? b !! c`", in a thread on type-declaration colons. **Passing mention**; Crystal kept the C-style `a ? b : c` ([ternary_if.md](https://github.com/crystal-lang/crystal-book/blob/master/docs/syntax_and_semantics/ternary_if.md)) and adopted spaced `x : Type` declarations ([commit 8ae45d5](https://github.com/crystal-lang/crystal/commit/8ae45d56088b48ff8f5531942ad21258d5ab840d), Jan 2016).
- [#4376](https://github.com/crystal-lang/crystal/issues/4376#issuecomment-299437893) (Perl 5/6 autovivification, closed by asterite 2017), [#501](https://github.com/crystal-lang/crystal/issues/501#issuecomment-86273253) (Perl 6 as gradually typed, 2015), [#10374](https://github.com/crystal-lang/crystal/issues/10374#issuecomment-775261545) (S06 terminology survey by HertzDevil, 2021), [#12999](https://github.com/crystal-lang/crystal/issues/12999) (Raku in a unary-minus/`**` precedence survey, 2023, open) and [#9198](https://github.com/crystal-lang/crystal/issues/9198#issuecomment-621372195) (perl6 benchmark): **passing mentions / prior art**.

**CPython (python/cpython and bugs.python.org)**
- [bpo-2636 msg83993](https://bugs.python.org/msg83993), 23 Mar 2009, Matthew Barnett (author of the `regex` module, not a core developer): "I don't want the re module to go the way of Perl 6's rules; such things belong elsewhere". **Cautionary tale.** Mark Lawrence, a tracker triager, closed the 2003 recursive-regex request with this quote on 18 Aug 2010 ([#38067](https://github.com/python/cpython/issues/38067#issuecomment-1093982169)). The stdlib `re` never added recursion, but Barnett's `regex` module later did ("Recursive patterns", [PyPI](https://pypi.org/project/regex/)).
- [#48075 comment](https://github.com/python/cpython/issues/48075#issuecomment-1093436869), 20 Sep 2008, Matthew Barnett: his rework adds backwards matching operations so look-behind need not be fixed width, "an idea which I got from a page on how it could be implemented in Perl 6!" **Adopted-from (technique, third-party library).** S05 describes `<?after>` as implementing lookbehind "by reversing the syntax tree and looking for things in the opposite order" ([S05](https://github.com/Raku/old-design-docs/blob/master/S05-regex.pod)); the `regex` module ships "Variable-length lookbehind" ([PyPI](https://pypi.org/project/regex/)). Not CPython: the module's stdlib inclusion (bpo-2636) never happened.
- [#83326 comment](https://github.com/python/cpython/issues/83326#issuecomment-1093849526), 28 Dec 2019, Steven D'Aprano (core developer): C3 "has been in use in languages such as Dylan, Python and Perl 6 for at least 16 years". **Cited as prior art** (adopter, not source).
- [#51381](https://github.com/python/cpython/issues/51381#issuecomment-1093483132) (2009, a requester cites Perl 6's arrays of captures from quantified groups; still open), [#56938](https://github.com/python/cpython/issues/56938#issuecomment-1093554293) (Aug 2011, Tom Christiansen: "The Perl 6 people are thinking about clever ways to set up byte offset indices" for O(1) grapheme access, in the run-up to PEP 393), [#49787](https://github.com/python/cpython/issues/49787#issuecomment-1093462057) (2010, Perl 6's 64-bit time), [#81552](https://github.com/python/cpython/issues/81552#issuecomment-1093827612) and PR #14306 (a `PATH` containing `~/.perl6/bin`): **passing mentions / prior art not taken**.

**Carbon (carbon-language/carbon-lang)**
- [#471 comment](https://github.com/carbon-language/carbon-lang/pull/471#issuecomment-823283291), 20 Apr 2021, Matt Fowles Kulukundis (contributor), on a `while … else` proposal: "we are picking one point in the space of phasers", linking Raku's phaser docs, and he would "rather do nothing in the space until we are willing to do a deeper look at all the options". **Cited as prior art against a narrower design**; the PR was closed for inactivity on 20 Aug 2021.
- [#684 comment](https://github.com/carbon-language/carbon-lang/issues/684#issuecomment-888687709), 28 Jul 2021, Richard Smith (lead): the survey behind p0680, "Both: C++, Perl, Ruby, Raku". See §2.

**GHC (ghc-proposals/ghc-proposals)**
- [#754](https://github.com/ghc-proposals/ghc-proposals/issues/754), 29 Apr 2026, Jaro Reinders (contributor), "Dash sections": an explicit herald for tuple sections, listing Lean `·`, Mathematica `#`, Scala and PureScript `_`, C++ `_1` and "[Raku uses `*`](https://docs.raku.org/type/Whatever)". **Cited as prior art**; an open pre-proposal issue, no PR yet.
- [#570 comment](https://github.com/ghc-proposals/ghc-proposals/pull/570#issuecomment-2267438798), 4 Aug 2024, reviewer: asks how the design compares with the Hackage packages string-interpolate and interpolatedstring-perl6. **Passing mention**; the proposal was accepted on 26 Jul 2026 with Scala-style `s"..."` and no Perl reference ([0570](https://github.com/ghc-proposals/ghc-proposals/blob/master/proposals/0570-string-interpolation.rst)). interpolatedstring-perl6 is Audrey Tang's "QuasiQuoter for Perl6-style multi-line interpolated strings with q, qq and qc" ([Hackage](https://hackage.haskell.org/package/interpolatedstring-perl6)).
- [#125 comment](https://github.com/ghc-proposals/ghc-proposals/pull/125#issuecomment-515250063), 25 Jul 2019: the same package cited as evidence that quasiquoters are used for multi-line strings. **Passing mention**; proposal accepted 1 Aug 2019.


### Inferences
- The two core-member proposals that took Perl 6 syntax as the explicit model — Walton's `-> args block` (2012) and Fitzpatrick's `...` (2019) — both died within days, and in both cases the objection was to the semantics or consistency, not to Perl 6 as a source.
- NFG has now been examined and rejected in writing by Julia (2016), adding to the report's case that NFG has no adopter; the rejection rested partly on the outdated Parrot PDD 28 text rather than on MoarVM's implementation, as a Perl 6 developer pointed out in the same thread.
- Junction-like syntax (F# #675) and Whatever-style placeholders (GHC #754) have each been raised once; the first was declined by the language designer, the second is an open idea, so neither changes "no documented adopter".

### Gaps
- Thirteen issue threads (crystal#2968, go#13229, go#44022, julia#11764, roslyn#9330, fslang#619, TypeScript#5326, cpython#124130, rust#3591, rust#28237, rust#28979, rust#50547, rust#84056) hid the matching comment from the HTML page, and the GitHub issue page offers no logged-out HTML pagination; they were read through the REST comments endpoint (23 calls, spaced, within the hourly quota) rather than HTML.
- Comment authors' team status comes from GitHub's author-association label (MEMBER / CONTRIBUTOR / NONE) and public knowledge; it was not verified against each project's governance page.
- Comments on pull-request diffs (review comments) are lazy-loaded and were not fetched; the conversation tabs were read in full.

## 4. Plain "Perl" mentions: is any a Perl 6 feature under the name "Perl"?

### Takeaway
No. Every plain "Perl" line in these archives refers to Perl 5 (regex syntax and flags, POD, CPAN, backticks, `$` interpolation, numeric underscores, `delete`, `quotemeta`, indented heredocs in 5.26, `//`) or to Perl's reputation.

### Cited Findings
- PEPs: [PEP 505](https://peps.python.org/pep-0505/) lists Perl among null-aware languages (Perl 5.10's `//`, which the report already credits to nobody); [PEP 822](https://peps.python.org/pep-0822/) cites Perl 5.26's indented heredocs; [PEP 3136](https://peps.python.org/pep-3136/) cites Perl's `last LABEL`; [PEP 3104](https://peps.python.org/pep-3104/) cites Perl's `my`; [PEP 584](https://peps.python.org/pep-0584/) calls "Only One Way" "a calumny about Python originating long ago from the Perl community". None describes a Perl 6 feature.
- TC39: every "Perl" entry in the regex proposals links perlre or rbuckton's engine comparison pages for Perl 5 ([x-mode](https://github.com/tc39/proposal-regexp-x-mode), [modifiers](https://github.com/tc39/proposal-regexp-modifiers), [named groups](https://github.com/tc39/proposal-regexp-named-groups)); Waldemar Horwat said in May 2026 that ECMAScript regexes "were loosely based on Perl" with his own tweaks for linear-time matching ([notes 2026-05-21](https://github.com/tc39/notes/blob/main/meetings/2026-05/may-21.md)).
- Julia's manual: commands use backticks borrowed "from the shell, Perl, and Ruby", and string interpolation uses `$` "as in Perl" ([running-external-programs.md](https://github.com/JuliaLang/julia/blob/master/doc/src/manual/running-external-programs.md); [strings.md](https://github.com/JuliaLang/julia/blob/master/doc/src/manual/strings.md)) — both Perl 5.
- Go's number-literal design traces `_` separators to "Perl 5.005_55 (1998)" ([19308-number-literals.md](https://github.com/golang/proposal/blob/master/design/19308-number-literals.md)).

### Inferences
- The "Perl" these communities borrow from is Perl 5, and mostly its regex dialect; the archives give no case of a Perl 6 idea credited under the bare name "Perl".

### Gaps
- None for the archives searched.

## 5. Does anything add a fifth documented borrowing or change a verdict?

### Takeaway
No language-level borrowing turns up. The closest candidate is a **third-party library**, Matthew Barnett's Python `regex` module, which documents taking backwards matching for variable-width lookbehind from a Perl 6 description (2008); it is comparable to the Joose case in kind, but it is a matching technique, not a language feature, and CPython never adopted the module. Several findings strengthen existing verdicts: Julia's written rejection of NFG, Rust's and Python's rejections of rules-style regexes, the F# designer's decline of junction-like comparison, and the Go 2 document's "Don't be Perl 6."

### Cited Findings
- Candidate borrowing: Python `regex` module, variable-length lookbehind via reverse matching, credited to Perl 6 by its author ([cpython#48075, 20 Sep 2008](https://github.com/python/cpython/issues/48075#issuecomment-1093436869); [PyPI regex](https://pypi.org/project/regex/); [S05 `after`](https://github.com/Raku/old-design-docs/blob/master/S05-regex.pod)).
- Ecosystem echo, not a language borrowing: Audrey Tang's Haskell package interpolatedstring-perl6 ([Hackage](https://hackage.haskell.org/package/interpolatedstring-perl6)), cited twice in GHC proposal threads, while GHC's accepted interpolation follows Scala ([0570](https://github.com/ghc-proposals/ghc-proposals/blob/master/proposals/0570-string-interpolation.rst)).
- Verdict-strengthening rejections: NFG in Julia ([#14383](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172476026)); rules-style regex in Rust ([#3591](https://github.com/rust-lang/rust/issues/3591#issuecomment-40775984)) and in Barnett's regex work ([msg83993](https://bugs.python.org/msg83993)); junction-style `==` in F# ([#675](https://github.com/fsharp/fslang-suggestions/issues/675)); pointy-block closures in Rust ([#2394](https://github.com/rust-lang/rust/issues/2394#issuecomment-5814294)); start/end-exclusive range operators in Zig ([#359](https://github.com/ziglang/zig/issues/359)), and Perl 6's exclusive `..^` used in Rust as an argument against spelling the inclusive range `..^` ([#28237](https://github.com/rust-lang/rust/issues/28237#issuecomment-287577990)); `MAIN`-generated CLIs in Go ([#17178](https://github.com/golang/go/issues/17178)).
- New cautionary-tale record from a core team: [Go 2 transitions](https://github.com/golang/proposal/blob/master/design/28221-go2-transitions.md) ("Don't be Perl 6.").
- New prior-art rows for the near-miss table: Carbon p0680 (dual-precedence `and`/`or`), TC39 Decimal (Rat literals), Go #35386 (`...` stub), GHC #754 (`*` Whatever), Zig #359 (`..^`), Crystal #2968 (Rat division), Rust #66616 (`tau`).

### Inferences
- If the report counts Joose, it could mention the `regex` module in the same breath as a library-level, technique-level borrowing; it should not be counted as a fifth language borrowing.
- Ruby's `->` remains the only case in which a core designer proposed Perl 6 syntax and shipped it; Walton (Rust) and Fitzpatrick (Go) did the same and withdrew.

### Gaps
- Which page Barnett read in 2008 is not named. S05's sentence "The `after` assertion implements lookbehind by reversing the syntax tree and looking for things in the opposite order going to the left" is already in the April 2006 Synopsis mirror (`Rule.pod`, commit `0879bdc`, 6 Apr 2006, in the [old-design-docs history](https://github.com/Raku/old-design-docs)), so it predates his comment by two years and is the likely source, but he does not cite it.
- Whether the Python `regex` module's current lookbehind code still uses the reverse-matching technique was not checked in its source.
