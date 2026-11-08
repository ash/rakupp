# Full-text search of language proposal archives for Perl 6 / Raku (7 Nov 2026)

This follow-up closes the gap the report flagged: no one had searched the design-proposal archives of Swift, Rust, Kotlin, C#, Scala, Java and Dart end to end.

## Method

Shallow clones (`git clone --depth 1`) made on 7 Nov 2026:

| Repository | HEAD | HEAD date |
|---|---|---|
| swiftlang/swift-evolution | 11e788d | 2026-11-06 |
| rust-lang/rfcs | 3bdcd22 | 2026-11-01 |
| Kotlin/KEEP | 8d8c7d5 | 2026-11-07 |
| dotnet/csharplang (proposals and LDM notes) | 9b00666 | 2026-11-04 |
| scala/improvement-proposals (current SIPs) | 2cedb2c | 2026-10-12 |
| scala/docs.scala-lang (historical SIPs under `_sips/`) | 449b146 | 2026-10-27 |
| dart-lang/language | bdb42f0 | 2026-11-06 |

JEPs: there is no JEP git repository. All 511 JEPs listed at https://openjdk.org/jeps/0, drafts included, were downloaded from openjdk.org. After 73 pages arrived, the site began answering 403 to every request from this machine, including the bugs.openjdk.org REST API. The block was not circumvented. Missing language, javac and core-library JEPs were then fetched from Wayback Machine snapshots (see "JEP coverage" below).

Pattern, case-insensitive, over every text file: `perl[ _-]?6|\braku(do)?\b`. As a control, every plain `perl` match was also listed and read, to catch Perl 6 features credited to "Perl".

GitHub issue and PR threads are not in the clones, so they were searched with the GitHub search API, `repo:<repo> "Perl 6" OR Perl6 OR Raku OR Rakudo`. Every result's body and comments were downloaded and grepped with the same pattern, because the API returns issue #6 of every repository as a false match on the token `6`. Not covered: csharplang Discussions (GitHub search does not index them), forums.swift.org, contributors.scala-lang.org, internals.rust-lang.org.

## Hits in the archives (files)

Only four files in all seven repositories mention Perl 6 or Raku.

| File | Date | Context | Classification |
|---|---|---|---|
| dotnet/csharplang `proposals/chained-relational-comparison.md` | spec text first committed 19 Apr 2026; champion issue #8861 opened 11 Dec 2024 by Cyrus Najmabadi | Prior-art list: "Raku / Perl 6: native support", linking docs.raku.org, under "Other languages with native chaining" with CoffeeScript, Icon (credited as Python's inspiration) and BCPL/CPL. Python, Julia and Mathematica are listed as math-oriented, and Common Lisp, Clojure, Scheme and Racket as variadic Lisps. | cited-as-prior-art |
| dotnet/csharplang `meetings/2026/LDM-2026-04-27.md` | 27 Apr 2026 | (no Raku mention) The LDM approved the feature "as is", named Python as the model among mathematical languages, and ranked it 4th of 5 for implementation. The proposal sits in `proposals/` root, not `proposals/csharp-15.0/`, so it is not in C# 15. | outcome record |
| rust-lang/rfcs `text/2909-destructuring-assignment.md` | start date 17 Apr 2020; shipped in Rust 1.59 | Prior-art bullet labelled "Perl" links to https://perl6advent.wordpress.com/2017/12/05/day-5-destructure-your-arguments-with-perl-6-signatures/ beside JavaScript and Python | cited-as-prior-art (mislabelled as Perl) |
| Kotlin/KEEP `notes/0002-code-coloring.md` | design note, KEEP issue #240 opened 9 Mar 2021 | "Alternatively, there can be no modifier at all: all functions are suspending. Examples: Go, Raku, Java's Loom Project." | cited-as-prior-art (design note, not a proposal) |
| swiftlang/swift-evolution `proposals/0363-unicode-for-string-processing.md` | Swift 5.7 | Defines a `[raku]: https://docs.raku.org/language/regexes` link reference that the body never uses (confirms the earlier finding) | unused reference |

Zero hits: Scala (both repositories), dart-lang/language files, and every JEP page that downloaded.

Plain-`perl` control: all other Perl mentions are about Perl 5. They cover regex character classes, `\X`, branch reset, `(?x)` and lookbehind (SE-0354/0355/0363/0448, Rust RFCs 0042/1620, JEP 111), `q()` delimiters (SE-0200), `symlink`, `join`, `splitn`, slicing (Rust RFC 0198), hash hardening, shebang handling (cargo-script), CPAN/Carton, and interpolation prior art (Rust RFC 2795). None describes a Perl 6 feature under the name "Perl".

## Hits in GitHub issue/PR threads

| Thread | Date | Who / what | Outcome |
|---|---|---|---|
| dart-lang/language #2105 "Ranges in switch cases" | 21 Apr 2022 | Bob Nystrom (Dart language team) surveys range syntax: Perl `..`, "Raku uses `..` for inclusive, `^..` for exclusive on the left, `..^` … and `^..^`", Ruby, Swift, Rust | Closed 13 Sep 2022 by Nystrom: relational `&` patterns (`case >= 0x30 & <= 0x39`) and guards cover it. He liked dedicated range syntax, but Dart's grammar is "getting pretty full of punctuation". |
| dotnet/csharplang #185 "slicing / Range" | 27 Feb 2017 | (already in notes) commenter proposes Perl 6 `^..^` family | C# 8 shipped `..` and `^n` |
| rust-lang/rfcs #1216 "Promote `!` to a type" | 1 Aug 2016 | canndrew proposes the name `Mu`, "precedence in Perl6" | RFC merged 29 Jul 2016 (the comment came after the merge); `!` is the never type, `Mu` not used |
| rust-lang/rfcs #1154 "Disjoins (anonymous enums)" | 9 Jun 2015 | flying-sheep proposes `Mu` for `!` "just like Perl6"; gsingh93 says the proposal mentions Perl 6, but the RFC text at the PR head has no such mention | closed unmerged 31 Jul 2015 |
| rust-lang/rfcs #1564 "Function clauses" | 1 Apr 2016 | eternaleye suggests taking Perl 6's `proto` with `{*}` plus `multi` bodies | closed unmerged 8 Apr 2016 |
| rust-lang/rfcs #2052 "Evolving Rust through Epochs" | 4 Jul 2017 | internetionals cites the Perl 6 (language) / Rakudo (implementation) split as a reason to version language and toolchain separately | Editions adopted; separate language versioning not |
| rust-lang/rfcs #886 `#[must_use]` on functions | 11 Jan 2017 | ssokolow: a list of features for "Rust 2.0 without the Perl6/Py3k/etc. drama" | cautionary-tale |

False matches checked and discarded: rust-lang/rfcs #6, #3573, #3267 and #1074; Kotlin/KEEP #6; swift-evolution #6; dart-lang/language #6; scala/improvement-proposals #6. None contains the pattern.

## JEP coverage

- Direct from openjdk.org before the block: 106 of 511 pages. Only JEP 111 mentions Perl; it says Java regexes derive from Perl's and asks for Perl 5.10-era constructs (`\R`, `\h`, `\v`, `\X`, `\N{…}`, `\g{…}`). That is Perl 5, not Perl 6.
- Wayback snapshots of the missing spec/lang, tools/javac, core/lang and core/— JEPs: 122 of the 130 missing pages were recovered. Snapshots were tried at 2026-01, 2025-06, 2024-06 and 2023-06 (`web.archive.org/web/<ts>id_/https://openjdk.org/jeps/<n>`); the 2025–2026 captures often store openjdk.org's error page.
  - Total read: 228 of 511 JEPs, 106 directly and 122 from Wayback.
  - By component: spec/lang 60/61, tools/javac 19/20, core/lang 14/17, core/— 63/66. That is 157 of 165 language-relevant JEPs.
  - Not read: 277, 305, 322, 526, 529, 540, 543, 8303099. The earlier text/control/runtime notes read JEP 305 separately and found no Perl mention.
  - The remaining 283 unread JEPs are HotSpot, GC, security, networking, tooling and similar.
- Word-boundary search (`perl[ _-]?6|\braku(do)?\b|\bperl\b`) over all 228: no Perl 6 / Raku mention. The only Perl mentions are Perl 5:
  - JEP 111 (regex constructs).
  - JEP 326 (raw string literals, withdrawn): Perl is among 16 surveyed languages, its `qq{...}` is in the delimiter table, and heredocs are described as reaching Perl from Unix shells.
- No JEP page that downloaded mentions Perl 6, Perl6, Raku or Rakudo.

## Effect on the report

- No new **borrowed** case. The verdict for every feature is unchanged.
- New **cited-as-prior-art** rows: C# chained comparison (2026), Rust RFC 2909 (2020), Kotlin code-coloring note (2021). New considered-not-adopted rows: Dart range survey (2022), and Rust comments on `Mu` and `proto`/`multi` (2015–2016).
- Chained comparisons get a row in the convergent-reinvention table. The verdict is common ancestor (CPL, Icon → Python), with Raku cited.
- Ranges: Dart joins C# and Ruby as languages whose designers saw Raku's caret ranges and did not adopt them.
- Warning use: two more Rust threads (#886, #2052).

## Remaining gaps

- csharplang Discussions, forums.swift.org, contributors.scala-lang.org, internals.rust-lang.org and the Kotlin YouTrack/Slack were not searched. Most design debate for Swift and Scala happens there.
- JEPs created after mid-2024 that were not among the first 106 downloads rely on whatever snapshot the Wayback Machine holds. Its 2025–2026 captures of openjdk.org often store the error page.
