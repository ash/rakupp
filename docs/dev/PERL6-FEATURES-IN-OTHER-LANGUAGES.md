# Perl 6's Ideas in Other Languages

Perl 6 specified most of its signature features between 2000 and 2004. That was often years before C#, Swift, Kotlin, Rust, Julia or modern Python shipped something similar. Did those languages take the ideas from Perl 6? This study checks each case against the adopting language's own design records.

### What the records show

| Finding | Details |
|---|---|
| **Five documented borrowings** outside the Perl family | Swift 4's rule for indented multi-line strings shipped exactly as Perl 6 designed it. Ruby took the `->` arrow, PHP 8.3 followed Raku's string increment, Swift's regex design drew on Perl 6, and JavaScript's Joose took Perl 6's object model by way of Perl 5's Moose. |
| **Perl 5 took the most** | `//`, `say`, `state`, given/when, `~~`, `\h`/`\v`, two backtracking verbs, Moose, Regexp::Grammars and the design of the 5.38 `class` feature all trace to Perl 6. Even `use feature` began in 2002 as a `use perl6ish` pragma for them. |
| **Smartmatch took its own path** | Perl 5 copied smartmatch while Perl 6's design of it was still changing. Larry Wall apologised for the mismatch two weeks after 5.10.0 shipped. Perl 5 deprecated it in 2023, and it survives behind feature flags. |
| **Many look-alikes were reached independently** | Indentation-stripping heredocs (outside Swift and Perl 5), half-open ranges, implicit lambda parameters, keyword-only arguments, command-line interfaces generated from signatures, optional typing. |
| **Others share an older ancestor** | The 2003 Traits paper, CLOS and Dylan, APL and NumPy, CLU and Icon generators, Unix pipes, Ada subtypes, Unicode UAX #29, C# async and Rx. |
| **Perl 6 also built on earlier work** | Junctions and switch began as Damian Conway's Perl 5 modules in August 2000. Roles came from the Traits paper, and Supplies from Rx. |
| **Raku's most distinctive ideas are still its own** | Junctions, hyper-operators, Whatever-currying, NFG synthetic graphemes, grammars as classes and slangs have no documented adopter outside the Perl family. Swift, F# and Julia core members discussed junctions or NFG in writing and chose other designs. |

### Why the records rarely name Perl 6

- **Timing.** Perl 6 designed early, and its first stable release came in December 2015. Designers tend to cite languages they can already run: Python, C#, Haskell and Perl 5.
- **Big changes.** In other communities' archives, Perl 6 often comes up in debates about large, compatibility-breaking redesigns, the kind Perl 6 undertook. Those communities' own leaders often raise it.

### How it was checked

- A November 2026 full-text search of the Swift, Rust, Kotlin, C#, Scala and Dart proposal archives and the JEPs found Raku cited as prior art. None of those proposals names it as the source of a shipped feature. The most visible case is C#'s chained-comparison proposal, approved in April 2026.
- A second pass covered design forums, mailing lists, twenty more design repositories and seventeen issue trackers. It found the Swift heredoc credit, in the swift-evolution thread rather than in the proposal.
- The same pass found core-team members in Swift, Rust, Go and D arguing from Perl 6's designs.
- It also found two transmissions below the language level: in a Python regex library and in a Rust grammar tool.

## Perl 6 was years ahead in design: the timeline

Perl 6 has three kinds of date: when an idea was proposed, when it was specified, and when it first ran. They can be years apart. On the design dates, Perl 6 was often years ahead of other languages, so it matters which date a comparison uses.

### Three layers of dates

| Layer | What it is | When |
|---|---|---|
| Proposals | 361 RFCs, sent in after Larry Wall announced the redesign at the State of the Onion on 19 July 2000 ([RFC index](https://web.archive.org/web/20191117000414/https://raku.org/archive/rfc/); [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))) | 1 August to late September 2000 |
| Specification | Larry Wall's Apocalypses, Damian Conway's Exegeses and the Synopses. Only A1–A6 and A12 were ever written ([A12](https://www.perl.com/pub/2004/04/16/a12.html/)). The Synopses began with S05 ([S05](https://github.com/Raku/old-design-docs/blob/master/S05-regex.pod)). | A1–A6: April 2001 to March 2003. A12: 16 April 2004. Synopses: June 2002 to about 2015. |
| Implementation | Pugs, Audrey Tang's Haskell interpreter. Its first release ran junctions and chained comparisons. By late May it had multis, signatures, rules via PGE, reduce, hyper-operators and gather/take ([Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)). Then Rakudo. | Pugs: 7 February 2005. Rakudo: see below. |

### Rakudo's milestones

| Date | Milestone |
|---|---|
| January 2008 | Rakudo gets its name ([Michaud](https://raku.github.io/CCR/Remaster/Patrick%20Michaud/The-compiler-formerly-known-as-raku.html)) |
| February 2009 | Released independently of Parrot from now on |
| 29 July 2010 | Rakudo Star ([TPF: Rakudo Star](https://news.perlfoundation.org/post/rakudo_star_-_a_useful_usable)) |
| 25 December 2015 | 6.c, the first stable language version ([Rakudo 2015.12](https://github.com/rakudo/rakudo/blob/main/docs/announce/2015.12.md)) |
| November 2018 | 6.d ([Rakudo 2018.11](https://github.com/rakudo/rakudo/blob/main/docs/announce/2018.11.md)) |
| 28 September 2026 | Rakudo 2026.09: RakuAST is the default frontend, and 6.e is in preview ([Rakudo 2026.09](https://github.com/rakudo/rakudo/blob/main/docs/announce/2026.09.md)) |

### Designs were refined over time

The Apocalypses were working designs, and many decisions were revisited. Smartmatch is a good example:

| Date | Step |
|---|---|
| October 2001 | A3 defines binary `~~` as logical XOR ([A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)) |
| January 2002 | A4 spells smartmatch `=~` ([A4](https://www.perl.com/pub/2002/01/15/apo4.html/)) |
| 24 October 2002 | `~` has become string concatenation. On perl6-language, Larry Wall floats `~~` for smartmatch, among several spellings ([msg11632](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11632.html)) |
| 1 November 2002 | The list's weekly summary counts it as settled ([msg12116](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12116.html)) |
| March 2003 | A6 is the first Apocalypse to use it ([A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)) |

The fair date for when an idea was first specified is its earliest mention in the design record: an Apocalypse, or the perl6-language decision before it. The final meaning usually comes from later Synopsis edits.

### Some well-known features came later in the design

These features were designed later in the process, at about the same time as similar features elsewhere, or after them:

| Feature | First in the spec | First implemented |
|---|---|---|
| `Whatever` star | 21 September 2006 ([b22582e](https://github.com/Raku/old-design-docs/commit/b22582e6bef46cbad0604401f051e74594053c4c)) | Rakudo 2009-04, method calls on `*` build closures ([Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)) |
| `WhateverCode` | 25 August 2009 ([357be11](https://github.com/Raku/old-design-docs/commit/357be1193fb2de11f8a178dcfb7a5cb2e3cdc8d3)) | the 2009-04 star closures above predate the name |
| `:D`/`:U` smileys | in S12 by 26 June 2009 ([04840a3](https://github.com/Raku/old-design-docs/commit/04840a3a6967d9ee641a9a76a1f9ae929b0d0a19)) | Rakudo 2011.09 ([Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)) |
| `sub MAIN`, `USAGE` | S06, 10 August 2006, in one commit by Larry Wall (svn r10804) ([msg25795](https://www.nntp.perl.org/group/perl.perl6.language/2006/08/msg25795.html); [4a841a8](https://github.com/Raku/mu/commit/4a841a8c2d40fdc2dddc25f23a786f2876ee9737)) | Rakudo basic `MAIN` 11 November 2008 ([41d13f8](https://github.com/rakudo/rakudo/commit/41d13f8eac2870031741179106cb35df668936dd)); full `MAIN`/`USAGE` in 2010.06 |
| NFG grapheme strings | name enters S02 30 January 2009 ([dc6f02a](https://github.com/Raku/old-design-docs/commit/dc6f02a49a82becee70b518927fd5a509fc70632)) | MoarVM/Rakudo, 24 April 2015 ([6guts](https://6guts.wordpress.com/2015/04/30/this-week-the-big-nfg-switch-on-and-many-fixes/)) |
| Decimal literals as `Rat` | 22 October 2009 ([f431adc](https://github.com/Raku/old-design-docs/commit/f431adc)) | Rakudo's first `Rat` 30 August 2009, `Int/Int` → `Rat` the next day ([9fff158](https://github.com/rakudo/rakudo/commit/9fff15880d11473fc480b6c167eca0df8bd5b02b); [39c3f4b](https://github.com/rakudo/rakudo/commit/39c3f4b6d5f2a702f336d33e79d23391adf36980)); Pugs had a rational value type by 19 February 2005 ([a8389eb](https://github.com/Raku/mu/commit/a8389eb1d81098de898c82a521d2affbefeb26ba)); literal default not pinned |
| Promise, Channel, Supply | S17 rewrite, 3 November 2013, whose Subscribables were renamed Supply the next day ([0b11765](https://github.com/Raku/old-design-docs/commit/0b11765d37acafd8f074386423c1cae2b3e4e83b)) | Rakudo 2013.07 (JVM) to 2015.09 (`react`/`whenever`) |

### Where the early spec dates come from

Spec dates before April 2006 do not rest on the Apocalypses and Exegeses alone:

- `Raku/old-design-docs` starts in April 2006, with a mirror, and has the canonical import from November 2008.
- The Pugs svn mirror, `Raku/mu`, keeps the complete svn.perl.org Synopsis history: 948 commits, from S05 version 1 (July 2002) to the move into Pugs on 26 November 2008. Revisions before February 2005 are dated by their Last Modified headers ([Raku/mu](https://github.com/Raku/mu); [first commit](https://github.com/Raku/mu/commit/ddda1b02a)).
- From 23 February 2006, every Synopsis commit was also mailed, with its diff, to perl6-language ([perl6-language](https://www.nntp.perl.org/group/perl.perl6.language/)).

That record dates several famous names and operators. Some are later than often assumed:

| Name or operator | Date | What happened |
|---|---|---|
| "junction" | 30–31 October 2002 | Larry Wall asked for a friendlier name than "quantum superposition", and took one from a list of candidates Damian Conway offered ([msg11842](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11842.html); [msg11927](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11927.html); [msg12007](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg12007.html)) |
| `»op«` | 1 November 2002 | Replaced A3's `^op` ([msg12138](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12138.html)) |
| Cross metaoperator `X` | 12 September 2006 | Entered S03 ([088e2dc](https://github.com/Raku/mu/commit/088e2dcaee37c436ff3439c4ce5e01dce6a9df27)) |
| Zip `Z` | 21 February 2007 | Zip got its operator ([c761027](https://github.com/Raku/mu/commit/c761027ac84540afeb103e20cc02e20a9f42e1fa)) |
| Sequence operator `...` | 5 October 2008 | Arrived in one S03 revision, with its arithmetic and geometric deduction ([18d3149](https://github.com/Raku/mu/commit/18d31499442176bc34d608458348399b250a35c8)) |

In the tables, "≤" marks a date by which a feature was already present.

### The verdict scale

Every verdict uses one of these labels:

| Verdict | Meaning |
|---|---|
| **Borrowed (documented)** | The adopter's own record names Perl 6 or Raku as the source: a PEP, RFC, KEEP, Swift Evolution text, commit message, or the designer's own statement. |
| **Borrowed (circumstantial)** | Timing and people make a link likely, but nobody says so. |
| **Considered, rejected** and **cited as prior art** | The record names Perl 6 but takes a different design. |
| **Independent** | No link is documented. The adopter credits something else, or nothing. |
| **Common ancestor** | Both sides descend from an earlier documented source. |
| **Flowed into Perl 6** | Perl 6 was the importer. |

When a verdict says a record does not mention Perl 6, that comes from a full-text search of the documents cited, not of every proposal repository.

## Five documented borrowings outside the Perl family

Outside the Perl family, five features have a record that names Perl 6 as their source. Swift's rule for indented multi-line strings shipped exactly as Perl 6 designed it. The table lists all five; the sections below tell each story.

| Adoption | Perl 6 / Raku dates | Adopter's dates | What the record says | Verdict |
|---|---|---|---|---|
| Swift multi-line string literals (SE-0168) | RFC 162 (27 Aug 2000); A2 (3 May 2001); Pugs 6.2.7 (13 Jun 2005); Rakudo 2012.11 | swift-evolution 25 Apr and 5 May 2016; accepted 19 Apr 2017; Swift 4.0 | Becca Royal-Gordon cites Perl 6's delimiter-indentation rule and lead author John Holdsworth adopts it; both are SE-0168 authors, but the proposal and the acceptance state the rule without credit ([015738](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160425/015738.html); [016646](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160502/016646.html); [SE-0168](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0168-multi-line-string-literals.md); [Accepted](https://forums.swift.org/t/accepted-se-0168-multi-line-string-literals/5715)) | **Borrowed (documented in the design thread)**; shipped intact, credited in the thread rather than the proposal |
| Ruby `->(x) { }` lambda | RFC 173 multi-variable `for` (29 Aug 2000); A4 pointy blocks (15 Jan 2002); Pugs 6.0.7 (18 Feb 2005) | Matz's diary 1 and 22 Jul 2005; commit 27 Jul 2005; ruby-dev:26650, 29 Jul 2005; Ruby 1.9.0, 25 Dec 2007 | Diary says the `->` proposal comes from Perl 6; commit labels the new syntax a Perl6-style lambda; ruby-dev:26650 says the grammar is aligned with Perl 6; Matz confirms the origin in 2008 ([diary 22 Jul](http://www.rubyist.net/~matz/20050722.html); [commit 759a63b6](https://github.com/ruby/ruby/commit/759a63b6a3355b85e1e52b78810d66fd9ee7b838); [ruby-dev:26650](https://blade.ruby-lang.org/ruby-dev/26650); [ruby-core:16734](https://blade.ruby-lang.org/ruby-core/16734); [Ruby 1.9.0](https://www.ruby-lang.org/en/news/2007/12/25/ruby-1-9-0-released/)) | **Borrowed (documented)**, syntax only |
| PHP string `++`/`--` | Raku's current string-increment semantics | RFC 21 Nov 2022; vote 25–0, 28 Jun–12 Jul 2023; implemented in PHP 8.3 | RFC compares PHP with Raku, calls Raku's result consistent, and says its proposal follows Raku's behaviour ([PHP RFC](https://wiki.php.net/rfc/saner-inc-dec-operators)) | **Borrowed (documented)** |
| Swift `Regex` / RegexBuilder | A5 (4 Jun 2002): rules as grammar methods, `/x` by default; LTM ≤ Apr 2006; Rakudo NFA-driven LTM 2011.11 | Ilseman gist 10 Jan 2018; forum posts Sep–Oct 2021; SE-0350/0351/0354/0355 in Swift 5.7 (2022) | Gist lists Perl 6 regex features Swift should take outright; 2021 post says the earlier literal design was heavily Raku-inspired; accepted proposals cite PEGs, PCRE2 and Perl 5 only ([gist](https://gist.github.com/milseman/bb39ef7f170641ae52c13600a512782f); [forum #46](https://forums.swift.org/t/declarative-string-processing-overview/52459/46); [SE-0355](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0355-regex-syntax-run-time-construction.md)) | **Borrowed at design stage (documented)**; shipped syntax is not Raku's |
| Joose (JavaScript) via Moose | A12 (16 Apr 2004); Pugs-era Perl 6 meta-model (2005–06) | Moose 0.01, 15 Mar 2006; Joose manual, © 2008–2009 | Moose says it is built on Stevan Little's Perl 6 meta-model prototypes; Joose says it is based largely on Moose ([Moose 0.18](https://metacpan.org/pod/release/STEVAN/Moose-0.18/lib/Moose.pm); [Joose manual](https://web.archive.org/web/20240208130151/http://www.openjsan.org/doc/s/sa/samuraijack/Joose/3013/lib/Joose/Manual.html)) | **Borrowed via Perl 5 (documented, two hops)** |

### Swift's multi-line strings

Here a Perl 6 design decision shipped whole in a major language outside the Perl family. The credit is in the discussion rather than in the proposal.

| Date | What happened |
|---|---|
| 23 April 2016 | Becca Royal-Gordon, then posting as Brent, mentions her background on swift-evolution: she helped design Perl 6 and was a Parrot pumpking ([forum #21](https://forums.swift.org/t/optional-dictionary-subscripts/719/21)) |
| 25 April 2016 | In the thread on multi-line string literals, Royal-Gordon describes Perl 6's choice: a heredoc's base indentation comes from its delimiter. She asks whether Swift should do the same ([015738](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160425/015738.html)) |
| 5 May 2016 | John Holdsworth replies that the lexer could strip the margin by the whitespace before the closing quote, "as per Perl6" ([016646](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160502/016646.html)) |
| April 2017 | Holdsworth, Royal-Gordon and Tyler Cloutier are SE-0168's authors. The proposal says whitespace in front of the closing delimiter is removed from every line, without naming a source ([SE-0168](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0168-multi-line-string-literals.md)) |
| 8 April 2017 | In the review, Royal-Gordon sums up Perl 6's heredocs as Perl 5's plus de-indenting ([review #17](https://forums.swift.org/t/review-se-0168-multi-line-string-literals/5611/17)) |
| 19 April 2017 | Joe Groff accepts the proposal: the closing delimiter's position sets the indentation of the whole literal. Swift makes under-indentation an error, where Raku warns ([Accepted](https://forums.swift.org/t/accepted-se-0168-multi-line-string-literals/5715)) |

The rule reached Swift 4.0 unchanged. As with Swift's regex work, Perl 6 is credited in the discussion.

### Ruby's `->` arrow

This is the best-documented case. Ruby took the arrow syntax from Perl 6, and gave it its own meaning.

| Date | What happened |
|---|---|
| 1 July 2005 | Matz's diary lists a Perl6-style anonymous function among candidate lambda syntaxes ([diary 1 Jul](http://www.rubyist.net/~matz/20050701.html)) |
| 22 July 2005 | The diary says the `->` proposal came from Perl 6. Matz calls Perl 6 culturally close to Ruby, and wonders when, or whether, it will be widely used ([diary 22 Jul](http://www.rubyist.net/~matz/20050722.html)) |
| 27 July 2005 | Matz commits it as new and very experimental. That is five months after Pugs made pointy blocks runnable, and three and a half years after A4 specified them. |
| 29 July 2005 | Matz tells ruby-dev the grammar is aligned with Perl 6. His comparison table sets Perl6/Ruby2 `-> (x,y) {...}` beside Groovy's and Haskell's forms ([ruby-dev:26650](https://blade.ruby-lang.org/ruby-dev/26650)) |
| 12 May 2008 | On ruby-core, Evan Phoenix quotes Matz's earlier remark that sharing a syntax with Perl 6 would quickly win mind-share, and asks whether Perl 6 has `->` ([ruby-core:16733](https://blade.ruby-lang.org/ruby-core/16733)) |
| 12 May 2008 | Matz answers "Yes. It's the origin." He adds that Perl 6 goes further, with `for obj -> var { }`, and that he once tried a `method(arg) -> x { }` block form himself ([ruby-core:16734](https://blade.ruby-lang.org/ruby-core/16734)) |
| 14 May 2008 | Answering an objection, Matz says Perl 6 has good ideas worth taking ([ruby-core:16766](https://blade.ruby-lang.org/ruby-core/16766)) |

Ruby's `->(x) { }` builds a lambda object. Raku's `->` introduces a Block with a signature, which `for`, `given` and `with` consume.

### PHP's string increment

PHP's increment RFC, by George Peter Banyard, says in its design text that it follows Raku ([PHP RFC](https://wiki.php.net/rfc/saner-inc-dec-operators)). It is the clearest such statement in a shipped mainstream feature.

- The RFC surveys other languages. It describes Raku as a Perl successor whose string increment gives consistent results, and adopts that behaviour.
- Raku had come up before: a 2013 internals thread on letter decrementing cited S03's decrement, which does not fully mirror increment ([68184](https://externals.io/message/68160#68184)).

### Swift's regex

Swift's team studied Perl 6's regexes and grammars for years, and its 2018 plan drew directly on them. The syntax Swift shipped in 2022 is more conventional, but several of the ideas arrived with it.

#### The core team's interest

| Date | Who | What they said |
|---|---|---|
| 9 December 2015 | Chris Lattner | Hoped Swift would get real regex support, with Perl 6-style extensions ([335/4](https://forums.swift.org/t/empower-string-type-with-regular-expression/335/4)) |
| 1 February 2016 | Chris Lattner | Urged forum readers to study Perl 6's regexes ([876/9](https://forums.swift.org/t/empower-string-type-with-regular-expression/876/9)) |
| 23 January 2017 | Joe Groff | Pointed to how Perl 6 grew regexes into full grammars ([4939/68](https://forums.swift.org/t/strings-in-swift-4/4939/68)) |
| 25 January 2017 | Chris Lattner | Wrote that Swift could learn a lot from Perl 6's grammars ([4939/130](https://forums.swift.org/t/strings-in-swift-4/4939/130)) |
| 26 January 2017 | Dave Abrahams | Pointed to the parsers Perl 6's recursive grammars allow ([4939/153](https://forums.swift.org/t/strings-in-swift-4/4939/153)) |
| 15 March 2020 | Chris Lattner | Praised Perl 6's regex ideas again ([34373/10](https://forums.swift.org/t/regular-expressions-in-swift/34373/10)) |

#### The 2018 plan

Michael Ilseman's January 2018 "State of String" gist set Swift the goal of beating Perl at string processing ([gist](https://gist.github.com/milseman/bb39ef7f170641ae52c13600a512782f)). The gist:

- cites Apocalypse 5 as Perl's clean-up of its own regex mess, and notes that Perl 6 has a PEG-like grammar system;
- lists Perl 6 features Swift should take outright: whitespace-insignificant syntax, named captures, multi-line matching and regex interpolation;
- proposes ratcheting, frugal quantifiers and Raku's `%` separator quantifier;
- asks whether alternation should use longest-token matching.

#### Choosing familiar syntax

| Date | What happened |
|---|---|
| 1 October 2021 | Ilseman explains the choice. For a literal, familiar traditional syntax mattered most, so the team would spend its appetite for novelty on a separate `Pattern` type. He personally preferred Raku's syntax ([forum #46](https://forums.swift.org/t/declarative-string-processing-overview/52459/46)) |
| 3 March 2022 | In the first reply to Hamish Knight's regex-syntax pitch, Joe Groff again cites Raku for making `/x` the default ([Swift Forums](https://forums.swift.org/t/pitch-regex-syntax/55711/2)) |
| 2022 | SE-0354 makes extended syntax mandatory in multi-line `#/…/#` literals, and keeps whitespace significant in single-line ones ([SE-0354](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0354-regex-literals.md)) |

#### What shipped in Swift 5.7 (2022)

A5's choices from 2002 arrived two decades later, in adapted form:

- composable, named sub-patterns, in RegexBuilder ([SE-0351](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0351-regex-builder.md));
- PEG-style ordered choice, credited to PEGs ([SE-0350](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0350-regex-type-overview.md));
- mandatory extended syntax in multi-line literals.

Perl 6 is credited in the gist and the forum posts rather than in the proposals. SE-0363 defines a `[raku]` reference link that its text does not use ([SE-0363](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0363-unicode-for-string-processing.md)).

### Joose

Joose is a genuine transmission from Perl 6 to JavaScript. It came by way of Perl 5's Moose, which is built on Stevan Little's prototypes of the Perl 6 meta-model ([Joose manual](https://web.archive.org/web/20240208130151/http://www.openjsan.org/doc/s/sa/samuraijack/Joose/3013/lib/Joose/Manual.html)).

### Two transmissions below the language level

These two are not language features, but they carry Perl 6 designs into other ecosystems:

| Where | What was taken | The record |
|---|---|---|
| Python's third-party `regex` module | Variable-length lookbehind, done by matching backwards | In 2008 its author, Matthew Barnett, said the idea came from a page on how Perl 6 could implement it. He names no page, but S05 had described `after` as reversing the syntax tree since at least April 2006. CPython's own `re` has not taken the module ([cpython#48075](https://github.com/python/cpython/issues/48075#issuecomment-1093436869); [S05](https://github.com/Raku/old-design-docs/blob/master/S05-regex.pod); [PyPI regex](https://pypi.org/project/regex/)) |
| The Rust Grammar Working Group's notation | Raku's `%` and `%%` separator modifiers | In 2018 eddyb, then on Rust's language and compiler teams, attributed them to Perl 6. The wg-grammar files use `%%` throughout; the Rust Reference keeps its own notation ([internals #8](https://internals.rust-lang.org/t/proposal-grammar-working-group/8442/8); [expr.lyg](https://github.com/rust-lang/wg-grammar/blob/master/grammar/expr.lyg)) |

## Designers elsewhere studied Perl 6's ideas in more than thirty records

Designers in many languages studied Perl 6's ideas. In more than thirty records, they raised a Perl 6 idea in their own design discussions, then took a different path. The table lists each one: who raised what, and what shipped.

| Record | Date | Perl 6 / Raku mention | Outcome |
|---|---|---|---|
| Python PEP 344 → PEP 3134, exception chaining | created 12 May 2005; final in Python 3.0 | RFC 88 (Tony Olekshy, 8 Aug 2000) chained exceptions in `@@`, surveyed beside Java, Ruby, C# ([PEP 3134](https://peps.python.org/pep-3134/)); on 20 May 2005 the author, Ka-Ping Yee, told python-dev that Perl 6's proposed `@@` was the only precedent he had found for implicit chaining ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/GXDQLU2RAKBO35P2XOLRBR3YP6CPSGRB/)) | Shipped as `raise … from`, `__cause__`, `__context__`; **cited as prior art** |
| Python PEP 3133, Roles | 1 May 2007 | Takes Squeak traits primarily via Perl 6 roles, including runtime application to instances ([PEP 3133](https://peps.python.org/pep-3133/)) | **Rejected** 24 May 2007, Guido preferring ABCs that can be subclassed and carry concrete methods; ABCs (PEP 3119) instead ([python-3000](https://mail.python.org/pipermail/python-3000/2007-May/007666.html); [acceptance](https://mail.python.org/pipermail/python-3000/2007-May/007913.html)) |
| PHP traits / Horizontal Reuse RFCs | 6 Mar 2008; 12 Oct 2008 | Perl 6 listed among languages that integrated traits ([traits](https://wiki.php.net/rfc/traits); [horizontalreuse](https://wiki.php.net/rfc/horizontalreuse)) | PHP 5.4 traits from the Bern Traits papers; **prior art** |
| Traits authors, TOPLAS | March 2006 | Perl 6 roles listed as an adopter in progress ([TOPLAS 2006](https://scg.unibe.ch/archive/papers/Duca06bTOPLASTraits.pdf)) | Perl 6 as adopter, not source |
| Siek, "What is Gradual Typing" | undated, c. 2007–08 | Perl 6 named with Python 3k and ES4 as languages the theory should underpin ([Siek](https://jsiek.github.io/home/WhatIsGradualTyping.html)) | Theory credits Thatte and Strongtalk, not Perl 6 |
| Partial application: Python PEP 309 naming; Ruby #7939; PHP Partial Function Application v2 | 24 Feb 2004; 24 Feb 2013 – 24 Feb 2014; 2020 | Gisle Aas notes that Perl 6 plans `f.assuming()` ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/SC6A7IZTHCCGOAZ7J2RZBSW3G235PJNA/)); a Ruby request praises Perl 6's `assuming` ([#7939](https://bugs.ruby-lang.org/issues/7939)); PHP's RFC names Raku's `.assuming` as the notable exception ([RFC](https://wiki.php.net/rfc/partial_function_application)) | Python chose `functools.partial` ([PEP 309](https://peps.python.org/pep-0309/)); nobu called it partial application, not currying, and Matz asked for a new name, leaving the issue in Feedback ([note-8](https://bugs.ruby-lang.org/issues/7939#note-8)); PHP **declined** 29–20 |
| PHP Pattern Matching RFC | v0.9; in discussion from 1 Dec 2025 | Future range pattern possibly modelled on Raku `Range` ([RFC](https://wiki.php.net/rfc/pattern-matching)) | Pending |
| C# ranges, csharplang #185 and discussion #198 | 27 Feb 2017 | Commenter proposes Perl 6's `a^..^b` family; HaloFour's companion discussion lists `5 ^.. 10` ([comment](https://github.com/dotnet/csharplang/issues/185#issuecomment-282744126); [#198](https://github.com/dotnet/csharplang/discussions/198)) | C# 8 shipped `..` plus `^n`; a Roslyn team member called Perl's `^` usage irrelevant ([comment](https://github.com/dotnet/csharplang/issues/185#issuecomment-400886623)) |
| Ruby #12133; #21284 | 15 Mar 2016; 27 Apr 2025 | Core committer nobu suggests Perl 6's start-exclusive `^..`, and nine years later still names it as the thing Ruby lacks ([#12133](https://bugs.ruby-lang.org/issues/12133); [#21284 note-3](https://bugs.ruby-lang.org/issues/21284#note-3)) | Not adopted |
| Dart language #2105, ranges in switch cases | comment 21 Apr 2022 | Bob Nystrom surveys Raku's `..`, `^..`, `..^`, `^..^` beside Perl, Ruby, Swift and Rust ([comment](https://github.com/dart-lang/language/issues/2105#issuecomment-1105853005)) | Closed 13 Sep 2022 in favour of relational patterns (`case >= 0x30 & <= 0x39`) and guards ([closing comment](https://github.com/dart-lang/language/issues/2105)) |
| Rust #28237; Zig #359 | 18 Mar 2017; 23 Apr 2020 – 21 Jan 2021 | When `..^` was floated for Rust's *inclusive* range, Scott Olson objected that Perl 6 uses it for the exclusive one; a Zig user proposed Raku's `..^` and `^..^` for switch ranges ([#28237](https://github.com/rust-lang/rust/issues/28237#issuecomment-287577990); [zig#359](https://github.com/ziglang/zig/issues/359#issuecomment-618326211)) | Rust kept `..=`, stable in 1.26.0 ([RELEASES.md](https://github.com/rust-lang/rust/blob/master/RELEASES.md)); Andrew Kelley closed the Zig issue, keeping exclusive `a..b` for slices and inclusive `a...b` for switch cases |
| Ruby #16894; Crystal #2968 | 17 May 2020; 27 Jun 2018 | Commenter praises Raku and Clojure default rationals; a Crystal contributor lists an `Int / Int` that returns a `BigRational`, as Perl 6 does, as one option, then prefers Crystal's existing division ([#16894](https://bugs.ruby-lang.org/issues/16894); [crystal#2968](https://github.com/crystal-lang/crystal/issues/2968#issuecomment-400616952)) | Ruby: no decision; Crystal 0.31.0 (23 Sep 2019) made `/` float division ([changelog](https://github.com/crystal-lang/crystal/blob/master/doc/changelogs/pre-1.0.md)) |
| TC39 Decimal README | 13 Nov 2019; restored 30 Jun 2026 | Champion Daniel Ehrenberg lists Perl 6's `1.5` Rat literals under Rationals, beside Common Lisp, Scheme and Ruby ([README](https://github.com/tc39/proposal-decimal/blob/main/README.md)) | Still Stage 1; the README declines rationals for this proposal, citing denominator growth, GCD normalisation and irrational `exp`/`log` |
| PHP internals, letter decrementing | 18–19 Jul 2013 | Peter Lind cites S03: Perl 6 decrements strings, though not as an exact mirror of increment ([68184](https://externals.io/message/68160#68184)) | No change in 2013; precursor of the 2023 increment RFC |
| Swift character-class pitch | 19 Oct 2021 | Raku row (grapheme dot, normalized strings) in engine survey ([pitch](https://forums.swift.org/t/pitch-character-classes-for-string-processing/52920)) | Dropped from SE-0363's text |
| Julia #14383, string redesign | 17–18 Jan 2016 | A commenter cites NFG's constant-time grapheme indexing; core member Milan Bouchet-Valat answers that it makes sense only when starting from UTF-32 and wastes memory ([comment](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172476026)) | **Considered, rejected**; Julia indexes UTF-8 bytes and iterates graphemes in its `Unicode` stdlib ([manual](https://github.com/JuliaLang/julia/blob/master/doc/src/manual/strings.md)) |
| pgsql-hackers, ANY/ALL | 13 Jun 2011 | Generalised quantifiers compared to Perl 6 junctions ([thread](https://postgrespro.com/list/id/BANLkTinc+D64qc9rdAxMR1K=0v-MpF1Uig@mail.gmail.com)) | Discussion only |
| Swift substring-predicate pitch | 11 Apr 2023 | Language Workgroup member Becca Royal-Gordon likens it to Raku junctions and argues that their unrestricted form hides loops, obscuring time complexity, and that overloading would burden type-checking ([#10](https://forums.swift.org/t/pitch-simplify-substring-searching-with-custom-operators-and-predicates/63915/10)) | **Considered, rejected**; she recommended a package, and the pitch was not pursued |
| F# suggestion #675 | 21 Jun 2018; declined 16 Jun 2022 | A user offers Perl 6's `42 == 30 \| 42 \| 100` ([comment](https://github.com/fsharp/fslang-suggestions/issues/675#issuecomment-399237834)) | **Declined** by Don Syme: not for the language itself, since it can be programmed in other ways ([#675](https://github.com/fsharp/fslang-suggestions/issues/675)) |
| D, "make (a < b < c) illegal?" | 8 Feb 2007 | Andrei Alexandrescu notes that Perl 6 is to implement chained comparisons ([post](https://forum.dlang.org/post/45CAADBF.4000100@erdani.org)) | Walter Bright agreed to make comparisons non-associative; D's grammar rejects `a < b < c`, leaving chaining possible later ([D spec](https://dlang.org/spec/expression.html)) |
| es-discuss; Rust internals pre-RFC | 18 Jul 2013; 12 Oct 2020 | Andy Earnshaw cites Python, Perl 6 and CoffeeScript; kennytm, later on the libs team, describes Python's and Raku's short-circuiting ([msg24857](https://www.mail-archive.com/es-discuss@mozilla.org/msg24857.html); [#20](https://internals.rust-lang.org/t/python-like-chained-comparison-operators/13191/20)) | Neither adopted; Rust comparisons still require parentheses ([Reference](https://doc.rust-lang.org/reference/expressions.html)) |
| C# chained relational comparison | community discussion 20 Jan 2023; champion issue 11 Dec 2024; spec text 19 Apr 2026; approved at the design meeting of 27 Apr 2026 | A 2023 discussion asks for chaining after reading about Raku ([#6899](https://github.com/dotnet/csharplang/discussions/6899)), though the champion issue links a different one; the proposal lists "Raku / Perl 6" among languages with native chaining, beside CPL, Python, Julia, Mathematica, the Lisps, CoffeeScript and Icon ([proposal](https://github.com/dotnet/csharplang/blob/main/proposals/chained-relational-comparison.md); [#8861](https://github.com/dotnet/csharplang/issues/8861)) | **Cited as prior art**; the meeting notes name Python as the model, and the feature is not in C# 15 ([LDM 2026-04-27](https://github.com/dotnet/csharplang/blob/main/meetings/2026/LDM-2026-04-27.md)) |
| Rust RFC 2909, destructuring assignment | start date 17 Apr 2020 | Prior-art list entry labelled "Perl" links a 2017 Perl 6 Advent post on signature destructuring ([RFC 2909](https://github.com/rust-lang/rfcs/blob/master/text/2909-destructuring-assignment.md)) | Shipped (Rust 1.59); **cited as prior art**, beside JavaScript and Python |
| Kotlin KEEP design note "Code Coloring" | note by Ilmir Usmanov, first committed 9 Feb 2021; KEEP #240 opened 9 Mar 2021 | Raku named with Go and Project Loom as languages where any function may suspend, so functions are not coloured ([note](https://github.com/Kotlin/KEEP/blob/master/notes/0002-code-coloring.md); [KEEP #240](https://github.com/Kotlin/KEEP/issues/240)) | **Cited as prior art** in a design note; Kotlin keeps `suspend` |
| Rust RFC PRs #1216 and #1564 | 2015–2016 | Commenters propose Perl 6's `Mu` as the name of `!` and Perl 6's `proto`/`multi` for function clauses ([#1216](https://github.com/rust-lang/rfcs/pull/1216#issuecomment-236557451); [#1564](https://github.com/rust-lang/rfcs/pull/1564#issuecomment-204261408)) | Neither taken: `!` became the never type, and #1564 was closed unmerged |
| Rust #2394, closure syntax | 21–22 May 2012 | Core-team member Patrick Walton proposes Perl 6's `-> args block`, for fewer shifted characters and braces at line end ([comment](https://github.com/rust-lang/rust/issues/2394#issuecomment-5814294)) | Niko Matsakis preferred `\|\| { … }` as more consistent; Walton closed the issue the next day, and Rust closures are written `\|args\| expr` |
| es-discuss, arrow functions | 18 Jan 2012 | Brendan Eich sketches a prefix `->` arrow, suggested to him with reference to Ruby 1.9 and Perl 6 ([msg12716](https://www.mail-archive.com/es-discuss@mozilla.org/msg12716.html)) | ES2015 shipped the infix `=>` |
| Go #35386, `...` statement | 5–26 Nov 2019 | Go team member Brad Fitzpatrick proposes a statement that compiles but panics, linking Raku's `...` stub ([#35386](https://github.com/golang/go/issues/35386)) | Rob Pike objected that it would block `go test`; Fitzpatrick **withdrew** it |
| GHC proposals #754, "Dash sections" | 29 Apr 2026 | Lists Raku's `*` beside Lean, Mathematica, Scala, PureScript and C++ placeholders ([#754](https://github.com/ghc-proposals/ghc-proposals/issues/754)) | Open pre-proposal; **cited as prior art** |
| Carbon p0680, `and`/`or`/`not` | Jul 2021 | Survey notes that Perl, Ruby and Raku have both spellings, the punctuation forms binding tighter, and mentions `andthen`/`orelse` ([p0680](https://github.com/carbon-language/carbon-lang/blob/trunk/proposals/p000680-and-or-not.md)) | Leads chose keyword-only operators on `bool` (31 Jul 2021) and rejected returning the deciding operand ([#684](https://github.com/carbon-language/carbon-lang/issues/684)) |
| TypeScript #5326 | 17 Jan 2019 | A user proposes properties filled from named arguments without a constructor, inspired by Perl 6 ([comment](https://github.com/microsoft/TypeScript/issues/5326#issuecomment-455155622)) | Not taken; issue open |
| Ruby #5011; Go #17178, CLI from `main` parameters | 10 Jul 2011 – 21 Jul 2012; 21 Sep 2016 | Requests to bind command-line arguments to a program's main routine as Perl 6's `MAIN` does ([#5011](https://bugs.ruby-lang.org/issues/5011); [go#17178](https://github.com/golang/go/issues/17178)) | Ruby's developer meeting rejected it, Matz preferring to improve optparse ([note-7](https://bugs.ruby-lang.org/issues/5011#note-7)); Fitzpatrick declined Go's about an hour later |
| Regex reform: CPython bpo-2636; Rust #3591; python-ideas | 23 Mar 2009; 17 Apr 2014; 16 Nov 2017 | Matthew Barnett does not want `re` to go the way of Perl 6 rules ([msg83993](https://bugs.python.org/msg83993)); Andrew Gallant defers grammar-based regexes to libraries ([comment](https://github.com/rust-lang/rust/issues/3591#issuecomment-40775984)); Guido declines to change `re.VERBOSE` whitespace on the strength of Perl 6 alone ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/KW5LHNXQEGHEK5SB3FTK7DKPVQHFJGCO/)) | Neither standard library changed; recursion reached only Barnett's third-party `regex` ([PyPI](https://pypi.org/project/regex/)) |
| Passing mentions | 2000–2026 | Matz on Perl 6's `use` when naming refinements' `using` ([#4085](https://bugs.ruby-lang.org/issues/4085#note-42)); Swift reviews citing Perl 6's `is tighter` precedence and `Stringy` ([SE-0077](https://forums.swift.org/t/review-se-0077-improved-operator-declarations/2661/5); [#14](https://forums.swift.org/t/pitch-string-revision-proposal-1/5546/14)); Clugston calling A5 a must-read for D ([post](https://forum.dlang.org/post/gnjiel$2b7u$1@digitalmars.com)); PHP wishes for junctions, and Andrea Faulds agreeing with Rowan Collins's praise of NFG ([92870](https://externals.io/message/92857#92870); [78213](https://externals.io/message/78174#78213)); Python posts on hyper-operators, feeds and Unicode operators ([2004](https://mail.python.org/archives/list/python-dev@python.org/message/EJ6SPHEID4NDCREDZFEWBZHN6W6V4UEU/); [2006](https://mail.python.org/archives/list/python-dev@python.org/message/ECSDTVX26S5NWMHUWTCOQIO362BP4UM3/); [2016](https://mail.python.org/archives/list/python-ideas@python.org/message/R4T6Y7XKNELHCCLEBEB6VAF2GMRVBZ3Z/)); Rust's `tau` ([#66616](https://github.com/rust-lang/rust/issues/66616#issuecomment-557397246)); Karpinski on multiple dispatch ([Julia](https://discourse.julialang.org/t/julia-motivation-why-werent-numpy-scipy-numba-good-enough/2236/10)); single remarks on the Kotlin, Haskell, OCaml, Crystal, Zig and Nim forums | Nothing taken from Perl 6; Rust's `TAU` shipped in 1.47 on Python's precedent, with Perl 6 and Nim named after it |

### What other languages valued

When another community cites Raku approvingly, it is usually for **a small, cleanly reasoned detail**, often in a survey of how languages handle it. The details cited are string increment, start-exclusive ranges, default rationals, Rat literals, grapheme-level dot, `/x` by default, `assuming`, `Range` matching, chained comparison, dual-precedence `and`/`&&`, Whatever placeholders and uncoloured functions.

### Proposals from core-team members

Core-team members argued from Perl 6 more often than the proposal archives show. Two of their proposals were withdrawn within days, for reasons of consistency and tooling rather than anything about Perl 6:

- Patrick Walton's `->` closures for Rust (2012);
- Brad Fitzpatrick's `...` statement for Go (2019).

### The trade-offs they named

Where designers explained a decision, they named a concrete trade-off. These are useful to an implementer:

| Who | What they weighed | The trade-off they named |
|---|---|---|
| Becca Royal-Gordon (Swift) | Unrestricted junctions | They hide loops and their time complexity |
| Julia | NFG | It pays off only for strings that start as UTF-32 |
| Don Syme (F#) | Junction-style comparison | It can be programmed without language support |

### Python came closest

- PEP 3133 shows that Python's designers read Perl 6 during the Python 3000 period. Guido van Rossum used it mainly to push PEP 3119 toward a more minimal design ([PEP 3133](https://peps.python.org/pep-3133/)).
- PEP 3134 is as close as Python comes to Perl 6 lineage in a shipped feature. Its wording places RFC 88 beside Java, Ruby and C# as prior art ([PEP 344](https://peps.python.org/pep-0344/)).
- Its author's own python-dev message names Perl 6's `@@` as the only precedent he found. That comes very close to a borrowing ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/GXDQLU2RAKBO35P2XOLRBR3YP6CPSGRB/)).

## Perl 5 took the most from Perl 6

Perl 5 is where Perl 6's design travelled furthest. Perl 5.10 brought in a whole set of Perl 6 features, `use feature` was built to import them, and CPAN modules such as Moose carried the object model across. The credits are in perl5-porters threads and module documentation rather than in release notes.

| Feature | Perl 6 date | Perl 5 version (date) | Credit in Perl's own record | Verdict |
|---|---|---|---|---|
| `say` | ≤ A12 (16 Apr 2004); Perl6::Say on CPAN 27 Mar 2004; Pugs 18 Feb 2005 | 5.10.0 (18 Dec 2007) | feature.pm at 5.10.0 calls it the Perl 6 say function; Perl6::Say tells 5.10 users they no longer need it; the 2005 p5p patches were titled as Perl6-style and Perl 6 `say` ([feature.pm @5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/lib/feature.pm); [Perl6::Say](https://metacpan.org/pod/Perl6::Say); [msg98796](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98796.html); [msg106335](https://www.nntp.perl.org/group/perl.perl5.porters/2005/11/msg106335.html)) | **Borrowed (documented)** |
| given/when, `~~` | RFC 22 (4 Aug 2000); A4 (15 Jan 2002); `~~` floated 24 Oct 2002, first in an Apocalypse in A6 (7 Mar 2003); Pugs Mar 2005 | 5.10.0; revised 5.10.1 (22 Aug 2009) | perlsyn at 5.10.0 says the switch feature is closely based on the Perl 6 proposal and lists differences from Perl 6; Robin Houston followed the Perl 6 spec and adapted the smartmatch table from Synopsis 4 ([perlsyn @5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/pod/perlsyn.pod); [msg107265](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107265.html); [msg107579](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107579.html)) | **Borrowed (documented)**; Perl 5 deprecated its smartmatch in 2023 |
| `(*THEN)`, `(*COMMIT)` | A5 `::` and `:::` (4 Jun 2002) | 5.10.0 | perlre identifies them with Perl 6's cut-group and commit constructs ([perlre @5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/pod/perlre.pod)) | **Borrowed (documented)** |
| `no VERSION` | — | 5.10.0 | perlfunc: added during early Raku design so programs could begin `no 6;` ([perlfunc](https://github.com/Perl/perl5/blob/blead/pod/perlfunc.pod)) | Built for Perl 6 coexistence |
| `//`, `//=` (`err` dropped) | A3 (2 Oct 2001) | patch posted 21 Apr 2002, committed 5 Aug 2002; 5.10.0 | Brent Dax's patch was titled as implementing Perl 6's `//`; the perldelta is silent ([msg57437](https://www.nntp.perl.org/group/perl.perl5.porters/2002/04/msg57437.html); [commit c963b15](https://github.com/Perl/perl5/commit/c963b15115); [perl5100delta](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)) | **Borrowed (documented)**: Perl 6's spelling for an older p5p wish |
| `state` | E7 (27 Feb 2004); Pugs 6.2.3 (12 May 2005) | 5.10.0 | Rafael Garcia-Suarez, April 2006: it comes from Perl 6; the perldelta calls them static variables ([msg111439](https://www.nntp.perl.org/group/perl.perl5.porters/2006/04/msg111439.html); [perl5100delta](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)) | **Borrowed (documented)** |
| `\h`, `\H`; new `\v`, `\V` | Perl 6 regex escapes | 5.10.0 | demerphq's 2007 patch says they are from Perl 6 and that Larry requested the `\v` change; the perldelta lists them uncredited ([msg123648](https://www.nntp.perl.org/group/perl.perl5.porters/2007/04/msg123648.html)) | **Borrowed (documented)** |
| Lexical `my $_` | — | 5.10.0; experimental 5.18; removed 5.24.0 | Uncredited in the 2004 patch; Nicholas Clark said in 2012 it had been taken blindly from Perl 6 ([msg86993](https://www.nntp.perl.org/group/perl.perl5.porters/2004/01/msg86993.html); [msg191632](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191632.html); [perl5240delta](https://github.com/Perl/perl5/blob/blead/pod/perl5240delta.pod)) | **Borrowed (documented in retrospect)**; removed |
| `use feature` | — | 5.10.0 | Grew from Larry's 2002 suggestion of a `use perl6ish` pragma for Perl 6 features and Rafael's `perl6ish` patch ([msg64178](https://www.nntp.perl.org/group/perl.perl5.porters/2002/07/msg64178.html); [msg65716](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65716.html)) | Built to import Perl 6 features |
| C3 MRO | canonical MRO of Perl 6 and Parrot | Class::C3 (7 Aug 2005); 5.10.0 `mro` | mro.pm: Dylan, then Python 2.3, then Raku ([mro.pm](https://github.com/Perl/perl5/blob/blead/ext/mro/mro.pm)) | **Common ancestor** (Dylan) |
| Moose / Class::MOP | A12; Pugs meta-model | Class::MOP 2 Feb 2006; Moose 0.01 15 Mar 2006 | Based in large part on the Perl 6 object system ([Moose::Manual](https://metacpan.org/pod/Moose::Manual); [Class::MOP](https://metacpan.org/pod/Class::MOP)) | **Borrowed (documented)**, CPAN |
| Regexp::Grammars | A5 (2002); S05 | 24 Jul 2009 (CPAN) | Angle-bracket subrules as in Perl 6; `<.SUBRULE>` taken directly from Perl 6 ([Regexp::Grammars](https://metacpan.org/pod/Regexp::Grammars)) | **Borrowed (documented)**, CPAN |
| Indented heredoc `<<~` | RFC 162 (27 Aug 2000); A2 (3 May 2001) | 5.26.0 (30 May 2017) | Commit silent; Matthew Horsfall's 2016 thread proposed Perl 6's stripping rules and modelled the spelling on Ruby 2.3 ([commit cdd6375](https://github.com/Perl/perl5/commit/cdd6375d6dbc0eea3676faa4e615af1ae28bb103); [msg237477](https://www.nntp.perl.org/group/perl.perl5.porters/2016/06/msg237477.html); [msg237510](https://www.nntp.perl.org/group/perl.perl5.porters/2016/07/msg237510.html)) | **Borrowed (documented)**: Raku's rule made stricter, Ruby's spelling |
| Chained comparisons | RFC 25 (4 Aug 2000); A3 (2 Oct 2001) | 5.32.0 (20 Jun 2020) | None in the 2017 ticket or the 2020 thread; Dax suggested a Perl 6 backport in July 2002 ([msg63819](https://www.nntp.perl.org/group/perl.perl5.porters/2002/07/msg63819.html); [msg257005](https://www.nntp.perl.org/group/perl.perl5.porters/2020/02/msg257005.html); [perl5320delta](https://github.com/Perl/perl5/blob/blead/pod/perl5320delta.pod)) | **Borrowed (circumstantial)** |
| `defer` | `LEAVE` phaser (A4 era, S04) | 5.36.0 (27 May 2022), experimental | PPC0004 dropped the name `LEAVE` because the semantics differ from Raku's ([PPC0004](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0004-defer-block.md)) | **Raku considered, rejected**; credits Swift, Zig, Jai, Nim, Odin |
| `class`, `field`, `method`, `ADJUST` | A12 (16 Apr 2004) | 5.38.0 (2 Jul 2023), experimental | Core docs silent; Corinna designer Ovid names Raku among the biggest influences ([Ovid](https://curtispoe.org/articles/corinna-in-the-perl-core.html); [perlclass](https://github.com/Perl/perl5/blob/blead/pod/perlclass.pod)) | **Influenced (documented by designer)** |
| `any`/`all` keywords | RFC 225 junctions (14 Sep 2000) | 5.42.0 (3 Jul 2025), experimental | Credited to List::Util; PPC0027 rejects junction semantics ([perl5420delta](https://github.com/Perl/perl5/blob/blead/pod/perl5420delta.pod); [PPC0027](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0027-any-and-all.md)) | **Raku explicitly not followed** |
| Named signature params `:$x` | A6 named-only `+$x` (7 Mar 2003), later `:$x` | 5.44.0 (15 Jul 2026), experimental | PPC0024 cites Function::Parameters, Kavorka, Method::Signatures; the latter two credit Perl 6 ([PPC0024](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0024-signature-named-parameters.md); [Kavorka](https://metacpan.org/pod/Kavorka); [Method::Signatures](https://metacpan.org/pod/Method::Signatures)) | **Borrowed indirectly via CPAN (documented chain)** |

### Perl 5.10.0: the largest import

Perl 5.10.0 (18 December 2007) is the largest documented import of Perl 6 design into another language's core. Its release notes do not spell out the credits:

- The perldelta mentions Perl 6 for the MAD converter and for the `SVt_BIND` placeholder for implementing Perl 6 on Perl 5 ([perl5100delta](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)).
- The Perl Foundation's announcement spoke in general terms of features and inspiration taken from the Perl 6 project ([Linux.com](https://www.linux.com/?p=512004)).
- The specific credits are in feature.pm, perlsyn and perlre ([perlre @5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/pod/perlre.pod)), and above all in the perl5-porters archive, whose threads name Perl 6 openly.

### How `//` and `use feature` arrived

| Date | What happened |
|---|---|
| October 1999 | Wilson Snyder asks for an `ordef` / `\|\|\|` defined-or, with Larry Wall in the thread ([msg1794](https://www.nntp.perl.org/group/perl.perl5.porters/1999/10/msg1794.html)) |
| February 2000 | John Tobey posts a working `\|\|\|` patch ([msg7802](https://www.nntp.perl.org/group/perl.perl5.porters/2000/02/msg7802.html)). The wish is older than Perl 6; Perl 6 supplied the spelling. |
| 21 April 2002 | Brent Dax posts a patch implementing Perl 6's `//` operator, six and a half months after A3 specified it. Dax was a Parrot developer who in 2001 had published Perl6::Parameters, to emulate Perl 6 named parameters in Perl 5 ([msg57437](https://www.nntp.perl.org/group/perl.perl5.porters/2002/04/msg57437.html); [Perl6::Parameters](https://metacpan.org/pod/Perl6::Parameters)) |
| July 2002 | Dax's repost asks whether other unobtrusive Perl 6 features should be backported, naming chained comparisons ([msg63819](https://www.nntp.perl.org/group/perl.perl5.porters/2002/07/msg63819.html)). Hugo van der Sanden, the 5.9 pumpking, replies that Larry had suggested a `use perl6ish` pragma for such features ([msg64178](https://www.nntp.perl.org/group/perl.perl5.porters/2002/07/msg64178.html)) |
| August 2002 | Rafael Garcia-Suarez posts the pragma ([msg65716](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65716.html)) |
| December 2005 | The idea becomes `use feature` and its bundles ([msg107704](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107704.html)). So the mechanism behind every later Perl 5 experiment was designed to import Perl 6. |
| September 2007 | The low-precedence `err` from Dax's patch had been kept because Perl 6 had it ([msg107743](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107743.html)). Larry renames it `orelse` in Perl 6, and Perl 5 drops it before 5.10.0 ([msg128324](https://www.nntp.perl.org/group/perl.perl5.porters/2007/09/msg128324.html)) |

### The 5.9.x port

The 5.9.x work openly ported Perl 6 features:

| Feature | Credit on perl5-porters |
|---|---|
| `say` | Nigel Sandever posted a Perl 6-style `say` patch in March 2005 ([msg98796](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98796.html)). On Steve Peters's later patch, Rafael Garcia-Suarez, the 5.10 pumpking, said he missed `say` partly because he played with Pugs ([msg106378](https://www.nntp.perl.org/group/perl.perl5.porters/2005/11/msg106378.html)) |
| given/when | Damian Conway asked that any core switch follow Synopsis 4 ([msg106978](https://www.nntp.perl.org/group/perl.perl5.porters/2005/11/msg106978.html)). Robin Houston followed the Perl 6 spec as closely as he found sensible ([msg107265](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107265.html)) |
| `~~` | Houston's smartmatch used a table adapted from S04's ([msg107579](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107579.html)) |
| `state` | Rafael said in April 2006 that it came from Perl 6 ([msg111439](https://www.nntp.perl.org/group/perl.perl5.porters/2006/04/msg111439.html)) |
| `\h`, `\v` | demerphq's April 2007 patch said both were from Perl 6 ([msg123648](https://www.nntp.perl.org/group/perl.perl5.porters/2007/04/msg123648.html)) |
| Lexical `my $_` | Also from Perl 6. Nicholas Clark later said it had been taken without the rest of the design it needs ([msg191632](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191632.html)). It became experimental in 5.18 and was removed in 5.24.0 ([perl5240delta](https://github.com/Perl/perl5/blob/blead/pod/perl5240delta.pod)) |

The perldelta's silence about these reflects a writing convention, not an absence of borrowing.

Chained comparisons keep a circumstantial verdict. Dax's 2002 suggestion is on record, but Zefram's 2017 ticket and the 67-message 2020 thread that produced 5.32's chaining name no other language ([msg248224](https://www.nntp.perl.org/group/perl.perl5.porters/2017/12/msg248224.html); [msg257005](https://www.nntp.perl.org/group/perl.perl5.porters/2020/02/msg257005.html)).

### Smartmatch: adopting a design in progress

Perl 5 adopted smartmatch early, while Perl 6's design of it was still changing. Today's perlsyn says Perl's table differs partly because the Raku spec changed after Perl 5 adopted it, and it recommends Raku-style `any` instead ([perlsyn](https://github.com/Perl/perl5/blob/blead/pod/perlsyn.pod)).

| Date | What happened |
|---|---|
| March 2005 | Michael Schwern is uneasy about fixing in Perl 5 a Perl 6 feature that might still change ([msg98850](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98850.html)) |
| December 2005 | Houston's patch adds perlsyn's section on differences from Perl 6, and warns that the S04 table cannot yet be treated as final ([msg108013](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg108013.html)) |
| 3 January 2008 | Two weeks after 5.10.0, Larry Wall tells p5p that Perl 6 had decided a year earlier that `~~` must not be symmetric and that arrays no longer imply `any`. He apologises that these "old misfeatures of the early Perl 6 design" reached Perl 5; he had expected p5p to track the design's principles rather than its features ([msg132467](https://www.nntp.perl.org/group/perl.perl5.porters/2008/01/msg132467.html)) |
| 22 August 2009 | 5.10.1 makes `~~` non-commutative, dispatching mainly on the right operand ([perl5101delta](https://github.com/Perl/perl5/blob/blead/pod/perl5101delta.pod)) |
| 18 May 2013 | 5.18 makes smartmatch experimental ([perl5180delta](https://github.com/Perl/perl5/blob/blead/pod/perl5180delta.pod)) |
| December 2017 | A redesign is reverted because it broke too much of CPAN ([7896dde](https://github.com/Perl/perl5/commit/7896dde7482a2851e73f0ac2c32d1c71f6e97dca)) |
| 2022 | `switch` leaves the 5.36 version bundle |
| 2 July 2023 | 5.38 deprecates switch and smartmatch, and schedules their removal for 5.42 ([perl5380delta](https://github.com/Perl/perl5/blob/blead/pod/perl5380delta.pod)) |
| August 2024 | The removal, written in July, is committed to blead ([cb2167d](https://github.com/Perl/perl5/commit/cb2167d3785e61e23819ca2c58ac1e89d5e4bf3b)) |
| February 2025 | The removal is reverted ([a215a77](https://github.com/Perl/perl5/commit/a215a77dd95c2472a6395310fd80580c750260c2)) |
| 3 July 2025 | 5.42 keeps both behind the `switch` and `smartmatch` feature flags, with removal postponed indefinitely ([perl5420delta](https://github.com/Perl/perl5/blob/blead/pod/perl5420delta.pod)) |

perlexperiment now lists them as experiments kept in place because they are widely used ([perlexperiment](https://github.com/Perl/perl5/blob/blead/pod/perlexperiment.pod)).

### How Perl 5's documentation credits Perl 6 today

- By 5.20, perlsyn described the switch as loosely based on an old version of the Perl 6 proposal ([perlsyn @v5.20.0](https://github.com/Perl/perl5/blob/v5.20.0/pod/perlsyn.pod)).
- A 2020 sweep replaced the name Perl 6 with Raku ([commit 17b3504](https://github.com/Perl/perl5/commit/17b35041bdff13ad8301eae5df5f75578f910ce1)).
- From 5.34, feature.pm describes `say` as Raku-inspired ([feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm)).
- One release made room for Perl 6 itself: 5.24.0 extended hashbang redirection to `perl6`, and 5.24.1 withdrew it because it misfired on interpreter paths such as `/opt/perl64/bin/perl` ([perl5240delta](https://github.com/Perl/perl5/blob/blead/pod/perl5240delta.pod); [perl5241delta](https://github.com/Perl/perl5/blob/blead/pod/perl5241delta.pod)).

### CPAN carried even more

CPAN carried more Perl 6 into Perl 5 than core did:

| Module | Date | What it brought |
|---|---|---|
| Perl6::* modules | 2001–2009 | Dozens, by Damian Conway, Brent Dax, Luke Palmer, chromatic, Moritz Lenz and others. Conway's alone include Placeholders, Currying, Form, Export, Slurp, Gather, Say and Rules |
| Class::Roles | 12 December 2003 | chromatic's Perl 5 implementation of Perl 6 roles, four months before A12 was published ([Class::Roles](https://metacpan.org/pod/Class::Roles)) |
| Perl6::Junction | 2005 | Junctions, built from the Exegesis 6 design ([Perl6::Junction](https://metacpan.org/pod/Perl6::Junction)) |
| Perl6::Str | 2008 | A grapheme-level string API |
| Moose | 2006 | The Perl 6 object model; see below |

### Moose

Moose is the most consequential transfer.

- Its manual says it is based in large part on the Perl 6 object system, plus ideas from CLOS and Smalltalk ([Moose::Manual](https://metacpan.org/pod/Moose::Manual)).
- Class::MOP 0.01 lists the Pugs Perl 6 meta-model work as its prior art ([Class-MOP-0.01](https://fastapi.metacpan.org/source/STEVAN/Class-MOP-0.01/lib/Class/MOP.pm)).
- The `has` keyword is a strong circumstantial link. Moose 0.01's `has 'x' => (is => 'rw')` transliterates A12's `has $.y is rw`, whose declarator Larry Wall chose to signal a has-a rather than an is-a relationship. Stevan Little never said why he picked it ([Moose-0.01](https://fastapi.metacpan.org/source/STEVAN/Moose-0.01/lib/Moose.pm); [A12](https://www.perl.com/pub/2004/04/16/a12.html/)).
- In 2012, Moose core developer Chris Prather said Little had set the Pugs meta-models aside, that Moose's meta-model is very different from Perl 6's, and that what it took from Perl 6 was minor syntax such as `has` ([msg191706](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191706.html)).

### Signatures and classes

Signatures reached Perl 5 through three CPAN modules, each of which credits Perl 6's S06:

| Step | Date |
|---|---|
| Method::Signatures | 26 December 2007 |
| MooseX::Method::Signatures ([MooseX::Method::Signatures](https://metacpan.org/pod/MooseX::Method::Signatures)) | 24 September 2008 |
| Kavorka | 3 October 2013 |
| Perl 5.44's `:$name` parameters, via PPC0024 ([perl5440delta](https://github.com/Perl/perl5/blob/blead/pod/perl5440delta.pod)) | 15 July 2026 |

The PPC itself does not name Raku.

The 5.38 `class` feature grew out of Paul Evans's Object::Pad test bed (2019). Object::Pad began with the `has` keyword of Raku and Moose, and switched to Corinna's `field` in 2022 ([Object::Pad Changes](https://metacpan.org/dist/Object-Pad/changes)). Roles, where the Moose lineage started, are on perlclass's TODO list ([perlclass](https://github.com/Perl/perl5/blob/blead/pod/perlclass.pod)).

### The 2012 debate: "Perl 6 is not a menu"

The 2012 signature debates turned this experience into a rule of thumb: learn from Perl 6's designs, but adapt them to Perl 5.

| Who | What they said |
|---|---|
| Nicholas Clark | Opened a thread titled "Perl 6 is not a menu": parts of a coherent design that has grown apart from Perl 5 cannot be lifted back verbatim ([msg191632](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191632.html)) |
| demerphq | Argued that most Perl 6-inspired features had worked out badly in Perl 5 ([msg191641](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191641.html)) |
| Leon Timmermans | Answered with `say` and `//` as Perl 6-inspired features that worked well ([msg191642](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191642.html)) |
| Ricardo Signes, then pumpking | Said Larry's designs suit Perl 6, and that Perl 5 should learn from them but design its own ([msg191662](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191662.html)) |
| Reini Urban | Urged S06 as the model ([msg194385](https://www.nntp.perl.org/group/perl.perl5.porters/2012/10/msg194385.html)). In 2015 he was still arguing that Perl 5 should follow Perl 6's design choices, as it had not for signatures ([msg230146](https://www.nntp.perl.org/group/perl.perl5.porters/2015/08/msg230146.html)) |

### Perl 5 today

Modern p5p routes features through CPAN prototypes and PPCs. When Raku comes up now, Perl 5 usually takes the idea in its own direction:

| Proposal | The Raku design | Perl 5's choice |
|---|---|---|
| `defer` | `LEAVE` | Another name ([PPC0004](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0004-defer-block.md)) |
| `any` / `all` | Junction values | Plain list operators ([PPC0027](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0027-any-and-all.md)) |
| Everything-slices `@a[*]` | Raku's `*` | Not adopted ([PPC0005](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0005-everything-slices.md)) |

In September 2026, p5p was asked whether the new match/case proposals avoided Raku's keywords on purpose. The answer: given/when are now permanently discouraged, so neither those names nor `~~` can be reused, and any replacement must first solve smartmatch's number-versus-string ambiguity ([msg271262](https://www.nntp.perl.org/group/perl.perl5.porters/2026/09/msg271262.html); [msg271265](https://www.nntp.perl.org/group/perl.perl5.porters/2026/09/msg271265.html); [msg271274](https://www.nntp.perl.org/group/perl.perl5.porters/2026/09/msg271274.html)).

Some look-alikes have no Raku link in Perl's record, so they count as resemblance, not borrowing: `__SUB__` (5.16), lexical subs (5.18), the infix `isa` proposed in 2019 ([#17200](https://github.com/Perl/perl5/issues/17200)), `for my ($k, $v)` (5.36) and infix `^^` (5.40) ([perl5400delta](https://github.com/Perl/perl5/blob/blead/pod/perl5400delta.pod)).

## Other languages reached many of the same ideas independently

Many features in other languages look like Perl 6 designs. For most of them, the adopter's records point to another source, or to none. Perl 6 often chose the answer the industry settled on later, and other languages reached it on their own.

| Feature | Perl 6 date | Adopter and date | What the adopter cites | Verdict |
|---|---|---|---|---|
| Heredoc dedent set by the closing delimiter | RFC 162 (27 Aug 2000); A2 (3 May 2001); Pugs 6.2.7 (13 Jun 2005); Rakudo 2012.11 | PHP 7.3 (RFC 16 Sep 2017); Java 15 text blocks (JEP 378); C# 11 raw strings (Swift 4 is in the borrowings table) | Nothing; Java cites requests for Swift's or Rust's literals and treats Perl 5 heredocs as rejected prior art; C#'s proposal cites neither Swift nor Perl ([PHP RFC](https://wiki.php.net/rfc/flexible_heredoc_nowdoc_syntaxes); [JEP 378](https://openjdk.org/jeps/378); [JEP 326](https://openjdk.org/jeps/326); [C# 11](https://github.com/dotnet/csharplang/blob/main/proposals/csharp-11.0/raw-string-literal.md)) | **Independent (convergent)**; a Perl 6 → Swift → Java path is possible but undocumented |
| Ruby `<<~` | same | Ruby 2.3 (25 Dec 2015); ticket from 10 Nov 2013 | Rails `strip_heredoc`; least-indented-line rule ([#9098](https://bugs.ruby-lang.org/issues/9098)) | **Independent** |
| Half-open ranges | `..^` in A12 (16 Apr 2004) | Groovy `..<` (proposed 10 Jan 2005, adopted Apr 2005); Swift `..<` (23 Jun 2014; Xcode 6 beta 3, Jul 2014); Rust RFC 1192 (Jul 2015); Kotlin `..<` 1.7.20 (29 Sep 2022), stable 1.9.0 (6 Jul 2023) | Groovy: Ruby's confusable `...`; Swift: Groovy, in its own release notes; Kotlin: Swift, Groovy, Ruby, Rust; Rust: Ruby ([groovy-dev](https://marc.info/?l=groovy-dev&m=113684773506831); [Swift notes](https://github.com/swiftlang/swift/blob/swift-2.2-SNAPSHOT-2015-12-01-a/utils/buildbot-release-notes.txt); [beta 3](https://appdevelopermagazine.com/apple-opens-new-swift-blog,-announces-xcode-6-beta-3-for-ios-development/); [KEEP](https://github.com/Kotlin/KEEP/blob/master/proposals/open-ended-ranges.md); [KT-15613](https://youtrack.jetbrains.com/issue/KT-15613); [RFC 1192](https://raw.githubusercontent.com/rust-lang/rfcs/master/text/1192-inclusive-ranges.md)) | **Independent**; Swift borrowed from Groovy, Kotlin from Swift |
| Index from end | `*-1` via Whatever (2006) / WhateverCode (2009) | C# 8 `^1` (2019) | Nothing; Perl 6 raised by a commenter, not taken up ([C# 8 ranges](https://raw.githubusercontent.com/dotnet/csharplang/main/proposals/csharp-8.0/ranges.md)) | **Independent (Perl 6 considered)** |
| Implicit lambda parameters | RFC 23 (4 Aug 2000); `$^a` in A3 (2 Oct 2001); `$_` in bare blocks, A4 (15 Jan 2002); Whatever-currying in Rakudo 2009-04 | Groovy `it` (29 Oct 2003); Scala 2.5 `_ + 1` (2 May 2007); Swift `$0` (18 Sep 2010); Kotlin `it` (by Sep 2011); Ruby 2.7 `_1` (25 Dec 2019); Ruby 3.4 `it` (25 Dec 2024) | Kotlin: Groovy, per Breslav; Groovy, Scala and Swift: nothing; Ruby: Elixir, Clojure, Kotlin, with Perl invoked only as a readability concern ([Groovy c525bfb](https://github.com/apache/groovy/commit/c525bfb600998c4e07805b4647a358153ff94174); [Scala changelog](https://scala-lang.org/files/archive/spec/2.12/15-changelog.html); [Swift 1bef5bd](https://github.com/swiftlang/swift/commit/1bef5bd648352c48baedc77f769c03a3a402fca0); [Breslav](https://downloads.ctfassets.net/oxjq45e8ilak/24vVb20Rpam2MksEU4QCOe/511bcd2575ba6e8af5cf82896db71570/AndreyBreslav_ShouldersOfGiants.pdf); [#4475](https://bugs.ruby-lang.org/issues/4475); [#18980](https://bugs.ruby-lang.org/issues/18980)) | **Independent**; Kotlin borrowed from Groovy |
| Named-argument punning | RFC 84 (10 Aug 2000); `:foo()` in A12 (16 Apr 2004); Pugs 6.0.14 (4 Apr 2005) | Rust RFC 1682 (18 Jul 2016); Ruby 3.1 (25 Dec 2021); PEP 736 (28 Nov 2023, rejected) | Haskell NamedFieldPuns, ES6, MoonScript, OCaml, ReasonML; ES2015's own strawman cites symmetry with destructuring and SpiderMonkey's existing `{x, y}` ([RFC 1682](https://raw.githubusercontent.com/rust-lang/rfcs/master/text/1682-field-init-shorthand.md); [#14579](https://bugs.ruby-lang.org/issues/14579); [PEP 736](https://peps.python.org/pep-0736/); [ES strawman](https://web.archive.org/web/20110702063518/http://wiki.ecmascript.org/doku.php?id=strawman:object_initialiser_shorthand)) | **Independent / common ancestor (ES6)** |
| Keyword-only, positional-only, signature objects | A6 named-only `+$x`, slurpy `*@x` (7 Mar 2003); Pugs 6.0.3 (12 Feb 2005) | PEP 3102 (22 Apr 2006); PEP 362 (21 Aug 2006, Python 3.3); PEP 570 (20 Jan 2018, Python 3.8) | Nothing, except C for `/` ([PEP 3102](https://peps.python.org/pep-3102/); [PEP 570](https://peps.python.org/pep-0570/)) | **Independent** |
| CLI from a signature | `sub MAIN` and `USAGE` in S06 (10 Aug 2006); Rakudo basic `MAIN` 11 Nov 2008, full 2010.06 | plac (2 Jun 2010); argh (12 Nov 2010); clize (4 Dec 2011); Fire (15 Feb 2017); Typer (20 Dec 2019); Nim cligen | plac: its author's own recipe and annotations; cligen: argh ([plac](https://pypi.org/project/plac/); [cligen](https://github.com/c-blake/cligen)) | **Independent** |
| Defined-or vs null-coalescing | A3 (2 Oct 2001) | C# 2.0 `??` (first public in the May 2004 spec; ECMA-334, Jun 2005); Swift `??` (Joe Groff, 26 Jul 2014; Xcode 6 beta 5, Aug 2014); PHP 7 (RFC 6 Sep 2014); Dart 1.12 (31 Aug 2015); JavaScript (TC39); Kotlin `?:` | C#: no origin statement, and ECMA-334's bibliography names no such language; Swift: nil-coalescing operators in other languages; Kotlin: Groovy and C#, per Breslav; PHP: C#; TC39: Wikipedia ([Gunnerson](https://learn.microsoft.com/en-us/archive/blogs/ericgu/updated-c-v2-0-specifications-now-available); [ECMA-334](https://ecma-international.org/wp-content/uploads/ECMA-334_3rd_edition_june_2005.pdf); [Swift 0ea3dbc](https://github.com/swiftlang/swift/commit/0ea3dbc652a94ef2a6c47199ad453655c0be6a34); [Breslav](https://downloads.ctfassets.net/oxjq45e8ilak/24vVb20Rpam2MksEU4QCOe/511bcd2575ba6e8af5cf82896db71570/AndreyBreslav_ShouldersOfGiants.pdf); [PHP RFC](https://wiki.php.net/rfc/isset_ternary); [TC39](https://raw.githubusercontent.com/tc39/proposal-nullish-coalescing/master/README.md)) | **Independent (C# lineage)** |
| Chained comparisons | RFC 25 (4 Aug 2000); A3 (2 Oct 2001); Pugs 6.0.0 (7 Feb 2005) | C# proposal, approved 27 Apr 2026, not yet shipped | CPL as the earliest design, Icon as Python's inspiration, then Python, Julia, Mathematica, the Lisps, CoffeeScript — and Raku; the meeting notes cite Python ([proposal](https://github.com/dotnet/csharplang/blob/main/proposals/chained-relational-comparison.md); [LDM 2026-04-27](https://github.com/dotnet/csharplang/blob/main/meetings/2026/LDM-2026-04-27.md)) | **Common ancestor (CPL, Icon → Python)**; Raku cited as prior art |
| Exact decimal literals | RFC 43 (5 Aug 2000); Rakudo `Rat` 30 Aug 2009, `Int/Int` → Rat 31 Aug 2009; decimal literals as Rat in S02 (22 Oct 2009) | Pyret exact numbers (TM017); Groovy `BigDecimal` default | Nothing ([Rakudo 9fff158](https://github.com/rakudo/rakudo/commit/9fff15880d11473fc480b6c167eca0df8bd5b02b); [TM017](https://github.com/brownplt/pyret-lang/wiki/TM017-Numbers); [Groovy](https://groovy-lang.org/syntax.html)) | **Independent**; Haskell `fromRational` is an ancestor ([Haskell 2010](https://www.haskell.org/onlinereport/haskell2010/haskellch6.html)) |
| Unicode operators | A3 (2 Oct 2001) | Fortress, Julia, Swift; Scala deprecated its Unicode arrows (merged 18 Jan 2019) | Nothing ([scala#7540](https://github.com/scala/scala/pull/7540)) | **Independent** |
| Runtime role mixins | `does`/`but` in A12 (16 Apr 2004); Pugs 6.2.10 (10 Oct 2005) | Groovy 2.3 `withTraits` (2014) | Nothing ([Groovy](https://groovy-lang.org/objectorientation.html)) | **Independent** |
| Optional typing in a dynamic language | RFC 218 (13 Sep 2000); A2 (3 May 2001); A6 (7 Mar 2003) | Siek & Taha (2006); PEP 3107 (2 Dec 2006); Dart (Oct 2011); TypeScript 0.8 (1 Oct 2012); Hack (20 Mar 2014); PEP 484 (29 Sep 2014) | Thatte, Strongtalk, Common Lisp, Dylan, Cecil, mypy ([Siek & Taha](http://schemeworkshop.org/2006/13-siek.pdf); [PEP 484](https://peps.python.org/pep-0484/)) | **Independent / common ancestor** |
| `any`/`all` | RFC 225 (14 Sep 2000); Pugs 6.0.0 (7 Feb 2005) | Python `any()`/`all()` (proposed 10 Mar 2005; Python 2.5) | ABC's quantifiers, which Guido said they mostly restore ([Guido, Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=98196)) | **Independent (ABC ancestor)**; eager reductions, not junctions |
| Expression interpolation | RFCs 222/252 (Sep 2000); `"$(expr)"` in A2 (3 May 2001); `"{ expr }"` proposed 20 Jul 2004, in S02 by 14 Aug 2004 | Swift `\( )` (4 May 2012); Scala SIP-11 (2012); ES2015 template literals (strawman Dec 2009); Python f-strings (PEP 498, 2015) | Swift: reuse of the escape character; ES2015: E quasi-literals, PHP, Scheme quasiquote, and `$ident` for Bash, Perl, PHP and Ruby compatibility; Python's lineage: shells, awk, Perl 5, Tcl; PEP 498 names no language ([Swift 2f44c00](https://github.com/swiftlang/swift/commit/2f44c0038c5a53e6ee4bb8f0a0fec4c3bd5d79a2); [swift-evolution](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160620/021613.html); [harmony:quasis](https://web.archive.org/web/20111223164326/http://wiki.ecmascript.org/doku.php?id=harmony:quasis); [PEP 215](https://peps.python.org/pep-0215/); [PEP 498](https://peps.python.org/pep-0498/)) | **Common ancestor (shell, Perl 5)**; Swift independent |
| Typed three-way compare | `<=>`/`cmp` return the `Order` enum | C++20 `<=>` (P0515R0, 2017) | Perl 5 for the spelling; claims only Haskell returns a type ([P0515R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0515r0.pdf)) | **Perl 5 ancestor; Raku overlooked** |

### Indentation-stripping heredocs

Here Perl 6 chose the industry's eventual answer first, and the choice demonstrably travelled. Apocalypse 2 accepted RFC 162's option in May 2001: remove whitespace equal to the terminator's indentation ([A2](https://www.perl.com/pub/2001/05/03/wall.html/)).

| Language | How it got the rule |
|---|---|
| Swift 4 | From Perl 6, through the SE-0168 authors (see the five borrowings) |
| PHP 7.3, Java 15, C# 11 | The same closing-delimiter rule, more than fifteen years after A2, without citing Perl 6. A 2016 Roslyn comment had shown the C# team a `q:to`-style heredoc ([roslyn #2239](https://github.com/dotnet/roslyn/issues/2239#issuecomment-246411974)) |
| Java, again | Its text-block JEP cites requests for Swift's literals. A path from Perl 6 through Swift to Java is possible, but nothing documents it ([JEP 378](https://openjdk.org/jeps/378)) |
| Ruby 2.3 | A different rule, based on the least-indented line, taken from Rails ([#9098](https://bugs.ruby-lang.org/issues/9098)) |
| Perl 5.26 `<<~` | Perl 6's rule. The commit credits nobody, but Matthew Horsfall opened the June 2016 p5p thread by proposing Perl 6's stripping rules, with a spelling modelled lightly on Ruby 2.3. p5p made mismatched indentation fatal, where Raku warns and continues ([commit cdd6375](https://github.com/Perl/perl5/commit/cdd6375d6dbc0eea3676faa4e615af1ae28bb103); [msg237477](https://www.nntp.perl.org/group/perl.perl5.porters/2016/06/msg237477.html); [msg237510](https://www.nntp.perl.org/group/perl.perl5.porters/2016/07/msg237510.html); [perldiag](https://github.com/Perl/perl5/blob/blead/pod/perldiag.pod)) |

### Half-open ranges

Here the chain runs through Groovy:

- Groovy adopted `..<` in April 2005, from Shijun He's proposal of 10 January 2005, which mentions Ruby ([groovy-dev](https://marc.info/?l=groovy-dev&m=113684773506831); [GroovyJSR notes](https://web.archive.org/web/20051220143509/http://docs.codehaus.org/display/GroovyJSR/2005/04/01/grammar+notes)).
- Swift's release notes record its half-open operator moving from Ruby's `...` in February 2014, through a swap in May, to `..<` on 23 June 2014, which they call precedented in Groovy ([Swift notes](https://github.com/swiftlang/swift/blob/swift-2.2-SNAPSHOT-2015-12-01-a/utils/buildbot-release-notes.txt)).
- Kotlin's YouTrack request named Swift as its model ([KT-15613](https://youtrack.jetbrains.com/issue/KT-15613)).

Raku's `..^` (A12, April 2004) is not in that chain. Raku's ranges did reach other designers, who chose other spellings:

- Designers of C#, Ruby, Dart and Zig saw Raku's start- and end-exclusive range operators, and went another way ([csharplang #185](https://github.com/dotnet/csharplang/issues/185#issuecomment-400886623); [#12133](https://bugs.ruby-lang.org/issues/12133); [dart-lang/language #2105](https://github.com/dart-lang/language/issues/2105#issuecomment-1105853005); [zig#359](https://github.com/ziglang/zig/issues/359#issuecomment-618326211)).
- In Rust, Perl 6's `..^` was an argument against using that spelling for the inclusive range ([rust#28237](https://github.com/rust-lang/rust/issues/28237#issuecomment-287577990)).
- Groovy's `<..<`, the closest match to `^..^`, appears in the Kotlin KEEP as Groovy prior art ([KEEP](https://github.com/Kotlin/KEEP/blob/master/proposals/open-ended-ranges.md)).

### Implicit parameters

Other languages have three families of implicit parameters. Each matches something in Raku, but none cites Raku. The pairing is our own analysis, by meaning; no source makes it.

| Family | Examples | Raku counterpart |
|---|---|---|
| Numbered holes | Swift's `$0`, Ruby's `_1`, Elixir's `&1`, Clojure's `%1` | Positional placeholders, `$^a` |
| One named parameter, usable repeatedly: `{ it * it }` | `it` in Groovy, Kotlin and Ruby 3.4 | `$_` in a bare block, whose ancestor is Perl 5's `$_` in `map` and `grep` |
| A fresh parameter per occurrence: `_ * _` takes two | Scala's `_` | WhateverCode |

The documented origins point elsewhere:

| Feature | Origin in the record |
|---|---|
| Groovy's `it` | James Strachan added it on 29 October 2003, with no stated source ([c525bfb](https://github.com/apache/groovy/commit/c525bfb600998c4e07805b4647a358153ff94174)) |
| Kotlin's `it` | Andrey Breslav's talk on Kotlin's ancestry credits Groovy ([Breslav](https://downloads.ctfassets.net/oxjq45e8ilak/24vVb20Rpam2MksEU4QCOe/511bcd2575ba6e8af5cf82896db71570/AndreyBreslav_ShouldersOfGiants.pdf)) |
| Swift's `$0` | Began as `_0`/`_1` in Chris Lattner's July 2010 commit ([6670786](https://github.com/swiftlang/swift/commit/667078650a35edaaec55c75b8b6a2bdfceb2eedb)). Renamed `$0` on 18 September 2010 because Bertrand, almost certainly Bertrand Serlet, preferred it ([1bef5bd](https://github.com/swiftlang/swift/commit/1bef5bd648352c48baedc77f769c03a3a402fca0); [Lattner 2019](https://oleb.net/2019/chris-lattner-swift-origins/)) |
| Scala's `_ + 1` | Arrived in version 2.5, in May 2007 ([Scala changelog](https://scala-lang.org/files/archive/spec/2.12/15-changelog.html)): more than five years after `$^a`, and eight months after Whatever entered the spec. Neither the changelog nor the commits up to v2.5.0 give a rationale ([scala v2.5.0](https://github.com/scala/scala/tree/v2.5.0)) |
| Ruby's `_1` and `it` | Ruby's tickets mention Perl as a readability concern ([#18980](https://bugs.ruby-lang.org/issues/18980); [#15723](https://bugs.ruby-lang.org/issues/15723)) |

Named-argument punning came closest on the syntax side:

- Rust RFC 1682 considered a prefix-colon `S { :x }` form, nearly Raku's `:$x`, and attributed it to MoonScript ([RFC 1682](https://raw.githubusercontent.com/rust-lang/rfcs/master/text/1682-field-init-shorthand.md)).
- PEP 736's prior-art survey covers Ruby, ReasonML, SystemVerilog, Jakt, OCaml, JavaScript and Rust, but not Raku ([PEP 736](https://peps.python.org/pep-0736/)).

### Keyword-only arguments

PEP 3102 added keyword-only arguments in April 2006. That was three years after A6, and a year before PEP 3133 showed Python 3000 designers reading Perl 6. Yet PEP 3102 names no other language ([PEP 3102](https://peps.python.org/pep-3102/)), and the python-3000 threads show an independent invention:

- Their one Perl 6 remark is Guido's: he set aside a bracketed parameter syntax as emulating Perl 6. A day later he pronounced the bare `*` ([001094](https://mail.python.org/pipermail/python-3000/2006-April/001094.html); [001150](https://mail.python.org/pipermail/python-3000/2006-April/001150.html)).
- Nobody mentioned A6's named-only parameters, and the later PEP 3102 threads name no other language ([python-dev](https://mail.python.org/pipermail/python-dev/2006-April/064598.html)).

### Command-line interfaces from signatures

On paper, Perl 6 was four years ahead here:

| Date | What happened |
|---|---|
| 10 August 2006 | In one S06 commit, Larry Wall specifies `MAIN`, multi-dispatch over `MAIN` candidates, usage text generated from signatures, and a `USAGE` override. He credits Unix switch conventions and Perl 5's `unless caller` idiom ([msg25795](https://www.nntp.perl.org/group/perl.perl6.language/2006/08/msg25795.html)) |
| 11 November 2008 | Rakudo runs a basic `MAIN` ([41d13f8](https://github.com/rakudo/rakudo/commit/41d13f8eac2870031741179106cb35df668936dd)) |
| 2 June 2010 | Michele Simionato's plac, which also builds a command line from a function signature, reaches PyPI ([plac](https://pypi.org/project/plac/)) |
| June 2010 | Rakudo 2010.06 ships full `MAIN` and `USAGE` ([Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)) |

Every later tool in this line, through argh, clize, Fire, Typer and Nim's cligen, descends from the Python side ([cligen](https://github.com/c-blake/cligen)). Two requests to adopt Perl 6's design turned up, and both were declined: Ruby's developer meeting in July 2012, and Go's Brad Fitzpatrick in 2016 ([#5011](https://bugs.ruby-lang.org/issues/5011#note-7); [go#17178](https://github.com/golang/go/issues/17178)).

### Gradual typing

Here Perl 6 came closest to recognition in the research literature:

- Jeremy Siek's explainer names Perl 6, alongside Python 3k and ES4, as a language gradual typing should give a foundation to ([Siek](https://jsiek.github.io/home/WhatIsGradualTyping.html)).
- The 2006 Siek & Taha paper itself mentions Common Lisp, Dylan, Cecil, Bigloo, Strongtalk and Thatte ([Siek & Taha](http://schemeworkshop.org/2006/13-siek.pdf)).
- In Perl 6, annotations add checked constraints and an unannotated variable means `Any`. That is closer to Common Lisp-style optional typing than to Siek's sound-at-the-boundary gradual typing. So Wikipedia's uncited claim that Raku had gradual typing from the start holds in the broader sense ([Wikipedia: Gradual typing](https://en.wikipedia.org/wiki/Gradual_typing)).

### Surveys often leave Raku out

The P0515 survey for C++20's `<=>` says that among major languages only Haskell returns a type from three-way comparison. Raku's `Order` enum does too ([P0515R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0515r0.pdf)). Designers' surveys rarely look at Raku.

## Shared ancestors, and what Perl 6 built on

Some overlaps run deeper than syntax. In these cases Perl 6 and the other language both descend from an older source: a research paper, CLOS, APL, a Unix convention. Perl 6 often combined these sources in its own way.

| Feature | Shared ancestor | Perl 6 date | Other adopters (dates) | Verdict |
|---|---|---|---|---|
| Roles / traits | Schärli, Ducasse, Nierstrasz, Black, ECOOP July 2003 (Squeak) | A12 (16 Apr 2004); stateful roles argued Dec 2003 | Scala traits (JVM Jan 2004); PHP 5.4 (RFCs 2008); Java 8 default methods (strawman May 2010, shipped 2014); Groovy 2.3 (2014) | **Common ancestor; flowed into Perl 6** ([A12](https://www.perl.com/pub/2004/04/16/a12.html/); [horizontalreuse](https://wiki.php.net/rfc/horizontalreuse); [FOOL 2011](https://person.dibris.unige.it/zucca-elena/FOOL2011/goetz-abstract.html)) |
| Multiple dispatch | CLOS, Dylan, Cecil; Conway's Class::Multimethods | RFC 256 (18 Sep 2000); A6 (7 Mar 2003); Pugs 6.0.2 (9 Feb 2005) | Julia (2012 paper cites CLOS, Dylan, Cecil, Diesel); C# 4 `dynamic` (Apr 2010); Clojure | **Common ancestor** ([RFC 256](https://web.archive.org/web/2019/https://raku.org/archive/rfc/256.html); [Julia 2012](https://ar5iv.labs.arxiv.org/html/1209.5145)) |
| Hyper, reduce, cross | APL; PDL and Numerical Python; List::Util `reduce` | RFCs 76, 82 (10 Aug 2000); A3 `^op` (2 Oct 2001); `»op«` (1 Nov 2002); `X` (12 Sep 2006); Pugs hypers 11 Mar 2005, 6.2.4 (24 May 2005) | Julia dot-broadcast 0.5 (2016), fusion 0.6 (2017), citing Matlab, NumPy, R | **Common ancestor** ([RFC 82](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/82.html); [msg12138](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12138.html); [088e2dc](https://github.com/Raku/mu/commit/088e2dcaee37c436ff3439c4ce5e01dce6a9df27); [Pugs r588](https://github.com/Raku/mu/commit/5be91f97ccd2babd6416f2d5f07787fcae286c64); [Julia blog](https://julialang.org/blog/2017/01/moredots/)) |
| Lazy lists, gather/take | Haskell and Python (RFC 81); CLU, Sather, Icon generators | A3 (2001); gather/take in A12 (16 Apr 2004); Pugs 6.2.0–6.2.4 (Apr–May 2005); Rakudo 13 Sep 2008 | PEP 255 (2001); JS 1.7 (2006); Kotlin `sequence {}` (2017); Rust RFC 3513 (10 Oct 2023) | **Common ancestor** ([RFC 81](https://web.archive.org/web/2019/https://raku.org/archive/rfc/81.html); [PEP 255](https://peps.python.org/pep-0255/); [RFC 3513](https://rust-lang.github.io/rfcs/3513-gen-blocks.html); [Rakudo dbadff7](https://github.com/rakudo/rakudo/commit/dbadff78cbc04af1fc41b306df63da7ce0f4259b)) |
| Pipelines | Unix pipes | Feeds in A6 (7 Mar 2003) | Isabelle/ML `\|>` (1994) → F# (2003) → OCaml 4.01 (12 Sep 2013); Elixir; TC39 (2015–) | **Common ancestor** ([A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/); [Syme](https://learn.microsoft.com/en-us/archive/blogs/dsyme/archeological-semiotics-the-birth-of-the-pipeline-symbol-1994)) |
| Predicate subtypes | Ada subtypes, which A12 credits | A12 (16 Apr 2004) as `subtype`, later `subset`; kp6 Aug 2007; Rakudo 27 Mar 2008 | Ada 2012 predicates (AI05-0153; GNAT, Oct 2010) | **Common ancestor (Ada)** ([ARM 3.2.4](https://www.adaic.org/resources/add_content/standards/12rm/html/RM-3-2-4.html); [GNAT patch](https://gcc.gnu.org/ml/gcc-patches/2010-10/msg01914.html); [kp6](https://github.com/Raku/mu/commit/13d8c0de1bf5f060301bc37ae61ddde7baf09af9); [Rakudo aa2db27](https://github.com/rakudo/rakudo/commit/aa2db27a7d0ce7f4061617f4aeda3bcd9a0ebcce)) |
| Success/failure scope blocks | design by contract; C++ ScopeGuard (2000) | KEEP/UNDO/PRE/POST in A4 (15 Jan 2002); Pugs 6.2.13 (17 Oct 2006) | D `on_scope_*` (DMD 0.148, 25 Feb 2006), respelled `scope(exit/success/failure)` in 0.149 (7 Mar 2006); Go `defer` (spec 27 Jan 2009); Swift `defer` (2.0, 2015); Zig `errdefer` (Jan 2018); C N2895 and TS 25755 (draft) | **Common ancestor / independent**; Swift and C borrowed from Go ([D changelog](https://digitalmars.com/d/1.0/changelog2.html); [D forum](https://forum.dlang.org/post/du106f$ptt$1@digitaldaemon.com); [Go 4a903e0](https://github.com/golang/go/commit/4a903e0b32be5a590880ceb7379e68790602c29d); [Swift rationale](https://github.com/swiftlang/swift/blob/main/docs/ErrorHandlingRationale.md); [Zig](https://andrewkelley.me/post/zig-january-2018-in-review.html); [N2895](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2895.htm)) |
| Grapheme strings | Unicode UAX #29, NFC/NFD | A5 level-2 graphemes (4 Jun 2002); NFG named (30 Jan 2009); MoarVM (24 Apr 2015) | Elixir v0.7.1 (18 Nov 2012); Swift `Character` (2014); SE-0363 (2022) | **Common ancestor**; NFG synthetics uncopied ([Elixir changelog](https://github.com/elixir-lang/elixir/blob/v0.7.2/CHANGELOG.md); [String Manifesto](https://github.com/swiftlang/swift/blob/main/docs/StringManifesto.md)) |
| Promise, Supply, react/whenever | Rx; C#/.NET; Haskell, Go, Erlang | S17 (3 Nov 2013); Rakudo 2013.07 (promises, JVM) to 2015.09 (`react`) | PEP 492 (2015), Rust RFC 2394, Kotlin coroutines, all citing C#, JS, Python | **Flowed into Perl 6** ([Advent 2013](https://perl6advent.wordpress.com/2013/12/19/perl-6-supplies-reactive-programming/); [PEP 492](https://peps.python.org/pep-0492/)) |
| Pattern matching | ML, Haskell, Scala, F#, Erlang | given/when in A4 (15 Jan 2002) | C# 7; Python 3.10 (PEP 622/634, 2020); Java 21 (JEP 441); Ruby `case/in`; PHP 8.0 `match`; TC39 | **Common ancestor**; none cites smartmatch ([PEP 635](https://peps.python.org/pep-0635/); [JEP 441](https://openjdk.org/jeps/441)) |
| Definedness in types | ML option types; Spec#; Eiffel | `:D`/`:U` in S12 ≤ 26 Jun 2009; Rakudo 2011.09 | Spec# (OOPSLA 2003); Eiffel attached types (2005); Ceylon (2011); Swift (2014); Kotlin 1.0 (15 Feb 2016); TypeScript 2.0 (2016); C# 8 (prototype Nov 2017); Dart 2.12 (3 Mar 2021) | **Common ancestor; Perl 6 not first** ([Wikipedia: Void safety](https://en.wikipedia.org/wiki/Void_safety); [Dart 2.12](https://dart.cn/blog/announcing-dart-2-12)) |
| Meta-object protocol | CLOS / AMOP, Smalltalk; Forman & Danforth | RFC 92 (11 Aug 2000); A12 (2004) | Python 2.2 metaclasses (Dec 2001) | **Independent; Python earlier** ([descrintro](https://www.python.org/download/releases/2.2/descrintro/)) |
| C3 MRO | Dylan | canonical for Perl 6 and Parrot | Python 2.3; Perl 5.10 `mro` | **Common ancestor** ([Class::C3](https://metacpan.org/pod/Class::C3)) |
| Macros, extensible syntax | Lisp | macros in A6 (2003); slangs in S02 (8 May 2009); Rakudo slangs 2014.06 | Julia (credits Lisp), Elixir | **Common ancestor**; slangs unadopted ([Julia manual](https://docs.julialang.org/en/v1/manual/metaprogramming/); [5cfa62e](https://github.com/Raku/old-design-docs/commit/5cfa62e1c79746f9b0151da987bc862695607e27)) |
| PEG framing of grammars | Ford (POPL 2004), from TDPL/GTDPL | rules in A5 (2002); S05 PEG overview added 5 May 2010 | LPeg (credits SNOBOL4); OMeta (2007); PEP 617 (2020); Pest | **Independent; vocabulary flowed into Perl 6** ([Ford](https://bford.info/pub/lang/peg.pdf); [52d2074](https://github.com/Raku/old-design-docs/commit/52d2074); [LPeg](https://www.inf.puc-rio.br/~roberto/docs/peg.pdf)) |

### Roles

Roles are the best-documented case of a shared ancestor: Perl 6 and the others both took them from the Traits paper.

- A12 accepts the Traits paper's argument that classes should not manage both objects and code reuse, and lists that paper as its only reference ([A12](https://www.perl.com/pub/2004/04/16/a12.html/)).
- Perl 6 extended the idea in three ways. Roles may hold state, which Larry Wall defended on perl6-language in December 2003. Conflicts are not resolved by renaming or excluding methods. And roles can be applied at runtime, as mixins ([perl6-language](https://groups.google.com/g/perl.perl6.language/c/kuxrQlzguIo)).
- The Traits authors then listed Perl 6 roles as an adopter in their 2006 TOPLAS paper, next to Scala ([TOPLAS 2006](https://scg.unibe.ch/archive/papers/Duca06bTOPLASTraits.pdf)).
- PHP, Java 8 and Groovy cite the Traits papers or the mixin tradition. Java's designers described default methods as a limited, stateless form of traits ([FOOL 2011](https://person.dibris.unige.it/zucca-elena/FOOL2011/goetz-abstract.html)).

### Multiple dispatch

Here the shared ancestor is CLOS:

- Julia's 2012 paper places itself with CLOS, Dylan, Cecil and Diesel ([Julia 2012](https://ar5iv.labs.arxiv.org/html/1209.5145)).
- Stefan Karpinski later said that even CLOS was not a motivation, and that Julia's resemblance to Fortress came from shared problems ([julia-dev](https://groups.google.com/g/julia-dev/c/NJ-b_KYZaIA)).

### Element-wise operators

Perl 6's hyper-operators and Julia's dots both mark element-wise application in the syntax, rather than vectorising implicitly. The two languages reached the same principle:

- A3 made that choice in 2001, with a joke about APL ([A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)).
- Julia made it in 2017, contrasting itself with Chapel and citing Matlab and NumPy ([Julia blog](https://julialang.org/blog/2017/01/moredots/)).

### KEEP and UNDO

Perl 6 specified blocks that run only on success or only on failure in A4, in January 2002 ([A4](https://www.perl.com/pub/2002/01/15/apo4.html/)). Perl 6 was first here by four years. D reached the same split on its own:

| Date | What happened |
|---|---|
| 25 February 2006 | DMD 0.148 ships `on_scope_success` and `on_scope_failure` ([D changelog](https://digitalmars.com/d/1.0/changelog2.html)) |
| 7 March 2006 | DMD 0.149 respells them `scope(success)` and `scope(failure)`. Walter Bright credits Alexandrescu's C++ scope-guard macros, and calls it a feature no other language had ([D forum](https://forum.dlang.org/post/du106f$ptt$1@digitaldaemon.com)) |

Bright's claim shows he did not know KEEP/UNDO. It fits his 2001 answer to whether D echoed Perl 6: "any similarities are entirely coincidental" ([post](https://forum.dlang.org/post/9lsnrc$1kdo$1@digitaldaemon.com)). So D's look-alikes are convergent, on its designer's own word.

Other scope-exit features came from elsewhere:

- Go's `defer` entered the spec in Robert Griesemer's commit of 27 January 2009, replacing a to-do item for an `onreturn/undo` statement. The word echoes UNDO, but there is no evidence of a link ([Go 4a903e0](https://github.com/golang/go/commit/4a903e0b32be5a590880ceb7379e68790602c29d); [Go 1593ab6](https://github.com/golang/go/commit/1593ab6bb4d5bf59bed1d66f292992572555b999)).
- Swift's `defer` (2015) is documented as Go's, tied to scope exit ([ErrorHandlingRationale](https://github.com/swiftlang/swift/blob/main/docs/ErrorHandlingRationale.md)).
- Zig's `errdefer` (2018) grew out of Zig's own error sigils ([Zig](https://andrewkelley.me/post/zig-january-2018-in-review.html)).

For an implementer, Perl 5's PPC0004 draws the useful line. Raku phasers are compile-time declarations, active by their presence; `defer` is a statement that takes effect only if execution reaches it ([PPC0004](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0004-defer-block.md)). The ENTER, FIRST, NEXT and LAST phasers have no counterpart in any mainstream language.

### Graphemes

Raku specified grapheme-level characters in 2002, and shipped them in April 2015. Others shipped grapheme-aware strings in between, building on Unicode's UAX #29:

- Elixir made `String.length` grapheme-aware in November 2012 ([Elixir changelog](https://github.com/elixir-lang/elixir/blob/v0.7.2/CHANGELOG.md)).
- Swift built `Character` on grapheme clusters in 2014 ([Swift book](https://docs.swift.org/swift-book/documentation/the-swift-programming-language/stringsandcharacters/)).
- Michael Ilseman later named Perl 6 as a peer in supporting grapheme-cluster regex semantics ([forum #100](https://forums.swift.org/t/declarative-string-processing-overview/52459/100)).

**NFG's distinctive contribution is still Raku's own**: synthetic codepoints that give O(1) grapheme indexing ([MoarVM strings](https://github.com/MoarVM/MoarVM/blob/nqp-mbc/docs/strings.asciidoc)).

- Julia weighed NFG in writing during its 2016 string redesign. It judged NFG sensible only for strings that start as UTF-32, and costly in memory ([#14383](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172476026)).
- In the same thread, a Perl 6 developer pointed out that the Parrot write-up under discussion had always been aspirational. So Julia's judgment rested partly on that older text, not on MoarVM ([comment](https://github.com/JuliaLang/julia/pull/14383#issuecomment-172391604)).

### Definedness smileys

The `:D`/`:U` smileys arrived after Spec# and Eiffel's attached types. They are a binder-checked definedness constraint, not a sum type or a flow analysis, so they are a different design from null safety rather than an early form of it ([04840a3](https://github.com/Raku/old-design-docs/commit/04840a3a6967d9ee641a9a76a1f9ae929b0d0a19)).

### Grammars and PEGs

Here the vocabulary came into Perl 6 later. Ford's 2004 PEG paper does not mention Perl ([Ford](https://bford.info/pub/lang/peg.pdf)), and Larry Wall described Perl 6 patterns as a native PEG implementation in S05 on 5 May 2010 ([52d2074](https://github.com/Raku/old-design-docs/commit/52d2074)).

### What Perl 6 built on

Perl 6 also drew on earlier work, often Perl 5 CPAN modules written by its own designers:

| Perl 6 feature | Source | Source date | Perl 6 date |
|---|---|---|---|
| Junctions | Conway's Quantum::Superpositions, which added `any` and `all` ([Quantum::Superpositions](https://metacpan.org/pod/Quantum::Superpositions)) | CPAN, 9 Aug 2000 | RFC 225, 14 Sep 2000, which points readers to the module ([RFC 225](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/225.html)) |
| given/when | Conway's Switch.pm ([Switch](https://metacpan.org/pod/Switch)) | CPAN, 7 Aug 2000 | Conway's switch RFC 22, 4 Aug 2000; renamed in A4 after Wall worked through options with Conway ([A4](https://www.perl.com/pub/2002/01/15/apo4.html/)) |
| `reduce` | Graham Barr's List::Util ([RFC 76](https://web.archive.org/web/2019/https://raku.org/archive/rfc/76.html)) | before 2000 | RFC 76, 10 Aug 2000 |
| Multimethods | Conway's Class::Multimethods and CLOS ([RFC 256](https://web.archive.org/web/2019/https://raku.org/archive/rfc/256.html)) | before 2000 | RFC 256, 18 Sep 2000 |
| PRE/POST handlers | Conway's Class::Contract ([RFC 271](https://web.archive.org/web/2019/https://raku.org/archive/rfc/271.html)) | before 2000 | RFC 271, 20 Sep 2000 |

The switch module and its RFC appeared within three days of each other, from the same author, so they were parallel prototypes.

The design documents also credit other languages openly:

| Source | What Perl 6 took |
|---|---|
| Haskell | Currying (RFC 23) and laziness (RFC 81) ([RFC 23](https://web.archive.org/web/2019/https://raku.org/archive/rfc/23.html)) |
| APL | Hyper-operators |
| Ada | Subtypes |
| Prolog | The regex cut, and `:=` binding as a kind of unification ([A5](https://www.perl.com/pub/2002/06/04/apo5.html/)) |
| Ruby | Unary `*` flattening, and the July 2004 switch to `"{ expr }"` closure interpolation, with Larry Wall's joke about turning up the Ruby-o-meter ([msg17885](https://www.nntp.perl.org/group/perl.perl6.language/2004/07/msg17885.html)) |
| Java | Interfaces (RFC 265) |
| Unix command lines and Perl 5 I/O layers | Colon-pair adverbs |
| Rx | Supplies ([Advent 2013](https://perl6advent.wordpress.com/2013/12/19/perl-6-supplies-reactive-programming/)) |
| Haskell, Go, Erlang and C# | The concurrency model, according to Larry Wall in 2016 ([Slashdot 2016](https://developers.slashdot.org/story/16/07/14/1349207/the-slashdot-interview-with-larry-wall)) |

Three often-repeated attributions have no primary Perl 6 statement behind them: Icon for gather/take, Eiffel by name for PRE/POST, and Smalltalk. They are third-party glosses.

### Ideas that are still Raku's own

Several of Raku's ideas have no documented counterpart elsewhere. Junctions have been discussed widely, and no language outside Perl 5's CPAN ports has adopted them:

| Language | What happened |
|---|---|
| Python | `any()` and `all()` are eager reductions |
| Perl 5.42 | Chose not to give its `any` and `all` junction semantics ([PPC0027](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0027-any-and-all.md)) |
| Swift | A Language Workgroup member advised against them in 2023 ([Swift #10](https://forums.swift.org/t/pitch-simplify-substring-searching-with-custom-operators-and-predicates/63915/10)) |
| F# | Its designer declined junction-style comparison in 2022 ([F# #675](https://github.com/fsharp/fslang-suggestions/issues/675)) |
| PHP and D | Wishes, and a library experiment ([PHP](https://externals.io/message/92857#92870); [D](https://forum.dlang.org/post/igcoqh$1tir$1@digitalmars.com)) |

The same holds for:

- hyper-operator syntax;
- Whatever-currying, raised for GHC in an open 2026 pre-proposal ([#754](https://github.com/ghc-proposals/ghc-proposals/issues/754));
- NFG synthetic graphemes;
- grammars as subclassable classes, of which the `%` separator modifier reached a Rust grammar tool ([internals #8](https://internals.rust-lang.org/t/proposal-grammar-working-group/8442/8));
- slangs;
- the ENTER, FIRST, NEXT and LAST phasers;
- the `:D`/`:U` smileys;
- dynamically scoped `take`. Kotlin's `sequence {}` forbids suspension from called functions by design, and Python and JavaScript need `yield from` and `yield*` ([KEEP coroutines](https://github.com/Kotlin/KEEP/blob/master/proposals/coroutines.md)).

## Perl 6 also travelled through people, Parrot and debates on language change

Some of Perl 6's influence does not show up as borrowed features. It travelled through the people Pugs brought to Haskell, through Parrot's place in virtual-machine research, and through the part Perl 6 plays when other languages plan big changes.

### Pugs and the Haskell community

- Haskell's HOPL III history (2007) names Darcs and Pugs as the open-source projects that drew people to Haskell. It recounts that Audrey Tang picked Perl 6 as her exercise after reading *Types and Programming Languages*, and records Pugs's growth to about 200 contributors within a year ([HOPL III](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf)).
- Tang adds that Pugs fed back into several GHC extensions, and that people who met Haskell through Pugs, such as Edward Kmett, went on to energise its community ([Tang interview](https://andrewshitov.com/2015/05/05/interview-with-audrey-tang/)).

### Parrot in virtual-machine research

- The Lua 5.0 paper (2005) points to Parrot's planned register design as evidence of growing interest in register VMs, while claiming Lua 5.0 as the first widely used one ([Lua 5.0](https://www.lua.org/doc/jucs05.pdf)).
- The 2008 "Virtual Machine Showdown" paper says the stack-versus-register debate reopened because of Parrot's decision ([VM Showdown](https://mural.maynoothuniversity.ie/10185/1/KC-Virtual-2008.pdf)).
- Rakudo was the one hosted language to make Parrot its primary implementation. Parrot's active development ended in 2021 ([Wikipedia: Parrot](https://en.wikipedia.org/wiki/Parrot_virtual_machine)).

### Perl 6 in debates about language change

Perl 6 was a bold, compatibility-breaking redesign. When other languages weigh big changes, their leaders often bring it up as the reference point:

| Language | Who | When | What they said or did |
|---|---|---|---|
| Python | Barry Warsaw | 24 July 2000 | A language cannot be designed by committee vote, and the Perl 6 and Tcl teams should learn from that ([Warsaw](https://mail.python.org/archives/list/python-dev@python.org/message/RP3CRS7I2GETZQ33XBD7FQ2H5PMNGGQP/)) |
| Python | Guido van Rossum | 31 January 2005 | Python 3000 should be less sweeping than the Perl 6 redesign ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/HA6U5GSTAKNUH2BNZTEDEUCN7ARPDUAX/)) |
| Python | Guido van Rossum | 5 April 2006 | With a smiley: the Python 3000 process aims not to repeat Perl 6 ([Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=155123)) |
| Python | Guido van Rossum | 20 April 2006 | In the thread that led to keyword-only arguments, set aside a bracketed parameter syntax as copying Perl 6 ([001094](https://mail.python.org/pipermail/python-3000/2006-April/001094.html)) |
| Python | Georg Brandl | 1 April 2007 | An April Fools' PEP about matching Perl 6's Unicode operators ([PEP 3117](https://peps.python.org/pep-3117/)) |
| Python | Guido van Rossum | 16 May 2007 | Answered a call for Unicode characters used for fun by pointing to Perl 6 as where that path leads ([007689](https://mail.python.org/pipermail/python-3000/2007-May/007689.html)) |
| Python | Guido van Rossum | 1 October 2008 | Contrasted Python 3 with Perl 6, an entirely new language and implementation ([Linux Journal](https://www.linuxjournal.com/magazine/interview-guido-van-rossum)) |
| Python | Łukasz Langa | 17 August 2014 | A new language called Python 4 should not repeat Perl 6's path ([Langa](https://mail.python.org/archives/list/python-dev@python.org/message/PGJMNZ5GVLAMOW3NFTRTP6OALAQFEHFY/)) |
| JavaScript | Brendan Eich | March 2008 | Nobody on the ES4 effort intends it to resemble Perl 6 ([esdiscuss 2008](https://esdiscuss.org/topic/complexity-tax)) |
| JavaScript | Brendan Eich | 12 August 2014 | Perl 6 is rich in good ideas that could only arrive by breaking compatibility ([esdiscuss 2014](https://esdiscuss.org/topic/thread-about-es6-on-reddit)) |
| Go | Ian Lance Taylor | 2018 | The Go 2 transitions design doc traces Perl 6 from 2000 to its 2015 specification, as a deliberately incompatible and evolving design. It lists avoiding that path among Go's lessons, beside meeting deadlines and not changing everything at once ([28221-go2-transitions.md](https://github.com/golang/proposal/blob/master/design/28221-go2-transitions.md)) |
| Go | Go team | February 2019 | Go 1.12 gates language changes by the `go` directive in `go.mod` ([Go 1.12](https://go.dev/doc/go1.12)) |
| Go | Russ Cox | 14 August 2023 | No Go 2 will break existing Go 1 programs ([Go blog](https://go.dev/blog/compat)) |
| Swift | Douglas Gregor | 18 January 2023 | Set Perl 5 → 6 beside Python 2 → 3 as a transition to learn from in planning Swift 6 ([Gregor](https://forums.swift.org/t/design-priorities-for-the-swift-6-language-mode/62408/57)) |
| Scala | Martin Odersky | 4 March 2019 | Contrasted Perl 6's long time between announcement and release with Scala 3's coming feature freeze ([Odersky](https://contributors.scala-lang.org/t/move-slowly-and-iterate/2888/23)) |
| Rust | Steve Klabnik | 18 December 2015 | Hoped Rust would avoid a transition like Ruby 1.9's, Python 3's or Perl 6's ([Klabnik](https://users.rust-lang.org/t/twir-quote-of-the-week/328/187)) |
| D | Walter Bright | 14 March 2015 | Compared D's own D1-to-D2 change with Perl 6's ([Bright](https://forum.dlang.org/post/me239u$1uj0$1@digitalmars.com)) |
| PHP | Adam Harvey | 21 August 2014 | PHP 7 should not become the Perl 6 of its decade by changing everything at once ([76755](https://externals.io/message/76753#76755)) |
| PHP | Andrea Faulds and Zeev Suraski | July 2014 | The RFC to skip version 6 cited Perl 6 among its reasons ([PHP RFC: php6](https://wiki.php.net/rfc/php6)) |
| PHP | Stanislav Malyshev | 30 April 2016 | Objected that a pipe operator moved PHP's syntax toward Perl 6 ([92968](https://externals.io/message/92960#92968)). The `\|>` operator later shipped in PHP 8.5 ([pipe-operator-v3](https://wiki.php.net/rfc/pipe-operator-v3)) |
| Rust | RFC commenters | 2017 | Mentioned Perl 6 with Python 3 as a transition a Rust 2.0 should avoid ([rfcs #886](https://github.com/rust-lang/rfcs/pull/886#issuecomment-272011711)), and, in the editions debate, as the model for separating a language (Perl 6) from its implementation (Rakudo) ([rfcs #2052](https://github.com/rust-lang/rfcs/pull/2052#issuecomment-312810251)) |
| Perl | Nicholas Clark | 7 June 2021 | Perl's PPC process sets out to avoid proposals nobody knows how to implement, as in the Perl 6 RFC round ([PPC motivation](https://github.com/Perl/PPCs/blob/main/docs/motivation.md)) |

Rust, for its part, modelled its RFC process on Python's PEPs ([Rust RFC 0002](https://rust-lang.github.io/rfcs/0002-rfc-process.html)).

### How other languages tell their own history

- JavaScript's HOPL IV history credits Perl 5 for its regex and array functions, and Python for generators, destructuring and comprehensions ([HOPL JS](https://www.cs.tufts.edu/~nr/cs257/archive/brendan-eich/js-hopl.pdf)).
- Chris Lattner's list of the languages that influenced Swift 1.0 does not include Perl, although he later recommended Perl 6's regexes and grammars to Swift's string work ([nondot.org](https://nondot.org/sabre/)).

### What reference works record

- As of November 2026, Wikidata records no language as influenced by Raku ([Wikidata Q2052676](https://www.wikidata.org/wiki/Q2052676)).
- Raku's Wikipedia infobox lists Perl and Haskell as influenced ([Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))). It cites Audrey Tang's 2010 post on implementing Perl 6, which is her PerlMonks reply of 21 April 2010, reposted on her blog ([Tang](https://web.archive.org/web/20100425002903/http://pugs.blogs.com/pugs/2010/04/how-to-implement-perl-6-in-10.html)).
- That post supports influence on Perl 5, through Moose and other CPAN releases. It makes no claim about Haskell: it says Pugs needed host-language features that later appeared in Haskell 2010 and GHC. One of them, pattern guards, was already documented in GHC 4.08.1 in 2000 ([GHC 4.08.1](https://downloads.haskell.org/~ghc/old_docs/old_4.08.1_head/set/pattern-guards.html)).
- Haskell's own Wikipedia listing of Raku cites a Perl 6 wiki glossary page ([Wikipedia: Haskell](https://en.wikipedia.org/wiki/Haskell)).

### What the Raku community claims

The Raku community has been modest about its influence. Across 1,088 Weekly and Advent posts from 2009 to 2026, 133 6guts posts, raku.org and the docs FAQ, no author claims that a language outside the Perl family borrowed from Perl 6 ([rakuweekly.blog](https://rakuweekly.blog/); [raku-advent.blog](https://raku-advent.blog/); [Raku FAQ](https://docs.raku.org/language/faq)). The Weekly never reported PHP's increment RFC or C#'s chained-comparison proposal. The one outward claim on record is Sidef, a single-author language whose readme names Raku among its inspirations ([Sidef](https://github.com/trizen/sidef)).

### Larry Wall's view

Larry Wall presented Perl 6 as a borrower. In 2016 he said other languages had taken Perl 5's regexes, and that a language borrowing enough of Perl 6 to be consistent would simply have reimplemented it ([Slashdot 2016](https://developers.slashdot.org/story/16/07/14/1349207/the-slashdot-interview-with-larry-wall)). In 2006 he predicted that everyone would move from PCRE to Perl 6 regexes; Swift is the language that went furthest in that direction ([OSDC::Israel 2006](https://perl.org.il/presentations/larry-wall-present-continuous-future-perfect/transcript.html)).

## Conclusion

Perl 6's specification chose several answers the industry later settled on, five to twenty years early:

| Year | Perl 6's choice |
|---|---|
| 2001 | The closing-delimiter heredoc rule, and defined-or |
| 2002 | Success and failure scope blocks, `/x` by default, and grammars as classes |
| 2003 | Named-only parameters |

Those choices rarely turned into citations. Ideas travel with implementations people can ship, and with the languages designers already use.

### What crossed

What crossed, in five documented borrowings and in Perl 5's many, was **small, separable syntax or semantics that a host language could adopt on its own**:

- an arrow token;
- a heredoc margin rule, which shipped intact in Swift and is credited in the swift-evolution thread;
- a string-increment rule;
- a regex whitespace default;
- a CPAN object system;
- below the language level, a lookbehind technique and a separator modifier.

### What is still Raku's own

The features that need a whole-language commitment have not moved: junction autothreading, hyper- and meta-operators, Whatever-currying, NFG, grammar subclassing and slangs. They show how much of Raku is architecture, not syntax.

### Lessons for an implementer

- **Use the right date.** The Apocalypse establishes the idea. Pugs (2005) and Rakudo (2009–2015) decide whether anyone outside could have used it. Later-specified features such as `:D`, NFG and Supplies were contemporaries of their analogues, not ancestors. Even `sub MAIN`, specified four years before plac, became fully usable the month plac appeared.
- **Borrow settled designs.** Perl 5's smartmatch froze a 2002–2007 snapshot of a design that Raku then revised. Larry Wall apologised for the mismatch within weeks of 5.10.0, and Perl 5 has carried the result for eighteen years.
- **Settled, separable pieces travel.** The parts of Raku now cited in PHP, Ruby, Swift, C#, Carbon and TC39 discussions are range endpoints, rational defaults, chained comparison and regex ergonomics. Stable, well-documented semantics for such pieces are what other languages are likely to keep picking up.

## How this was researched

The research ran in two passes on 7 November 2026, plus a check of the Raku side. The proposal archives showed no hidden borrowing; the discussion archives showed one.

### Pass 1: the proposal archives

These repositories, with their GitHub issue and pull-request threads, were searched for Perl 6, Perl6, Raku and Rakudo: [swift-evolution](https://github.com/swiftlang/swift-evolution), [rust-lang/rfcs](https://github.com/rust-lang/rfcs), [Kotlin KEEP](https://github.com/Kotlin/KEEP), [dotnet/csharplang](https://github.com/dotnet/csharplang) (proposals and design-meeting notes), Scala SIP ([current](https://github.com/scala/improvement-proposals) and [historical](https://github.com/scala/docs.scala-lang)), [dart-lang/language](https://github.com/dart-lang/language), and the JEPs.

| Result | Detail |
|---|---|
| Files that matched | Four: the C# chained-comparison proposal, Rust RFC 2909, a Kotlin design note on function colouring, and SE-0363's unused link |
| Issue threads | The Dart range survey and a handful of Rust comments |
| Verdict changes | None. All are prior art or suggestions not taken; other Perl mentions concern Perl 5 regexes, quoting or tooling |
| JEPs | 228 of 511 read, including 157 of the 165 language, javac and core-library ones. openjdk.org stopped answering after 106 pages, and Wayback Machine snapshots supplied 122 more. The rest are HotSpot, GC, security, networking and tooling JEPs ([JEP index](https://openjdk.org/jeps/0)). None of those read mentions Perl 6 or Raku; [JEP 111](https://openjdk.org/jeps/111)'s Perl references are to Perl 5.10-era regex constructs |

### Pass 2: where design is argued

| Venue | What was searched |
|---|---|
| Discourse forums | Seventeen, searched exhaustively, among them forums.swift.org (with the 2015–2017 swift-evolution list), internals.rust-lang.org, contributors.scala-lang.org and discuss.python.org ([forums.swift.org](https://forums.swift.org/search.json?q=raku)) |
| Trackers and forums | csharplang Discussions, Ruby's Redmine ([bugs.ruby-lang.org](https://bugs.ruby-lang.org/search?q=%22perl+6%22&issues=1)), PHP internals through externals.io ([externals.io](https://externals.io/search?q=%22perl+6%22)), and the D and Nim forums ([forum.dlang.org](https://forum.dlang.org/search?q=perl6)) |
| Mailing lists, downloaded and grepped | perl5-porters: the subject lines of all 271,342 articles from August 1999 to November 2026, plus about 2,000 thread bodies chosen by subject ([p5p](https://www.nntp.perl.org/group/perl.perl5.porters/)). The full text of python-3000, python-ideas and python-dev ([python-3000](https://mail.python.org/pipermail/python-3000/)). es-discuss in full from 2006 to 2016, and through its mirror's search to 2021 ([es-discuss](https://www.mail-archive.com/es-discuss@mozilla.org/)). ruby-dev around Matz's 2005 commit |
| Design repositories | Twenty more: Carbon; the Go, GHC, OCaml, F#, PowerShell and Nim proposal archives; TC39's lists, spec and meeting notes; the Julia, Elixir, Zig, Gleam, Crystal, Mojo and TypeScript docs; and the PEPs ([golang/proposal](https://github.com/golang/proposal); [carbon-lang](https://github.com/carbon-language/carbon-lang)). Also 329 TC39 proposal repositories ([tc39/proposals](https://github.com/tc39/proposals)) and 17 issue trackers. Outside the PEPs, three files matched: the Go 2 transitions design doc, Carbon's p0680 and the TC39 Decimal README. The trackers yielded about 55 genuine threads |
| The Raku side | The perl6-language archive (36,873 articles), the Pugs svn mirror, and 1,088 Weekly and Advent posts ([perl6-language](https://www.nntp.perl.org/group/perl.perl6.language/)) |

### What the second pass found

- One borrowing that changed a verdict: Swift's heredoc rule.
- The perl5-porters credits for Perl 5's imports. As the first pass had predicted, the threads behind `//` and `<<~` held the missing credits. The threads behind Perl 5's chained comparisons, and the python-3000 threads behind PEP 3102, did not.
- Two transmissions below the language level, and most of the near-miss rows.
- The origins of look-alikes whose sources are not Perl:

| Look-alike | Documented origin |
|---|---|
| Swift's `$0`, `\( )` and `??` | Dated by Swift's public history from July 2010 ([Swift notes](https://github.com/swiftlang/swift/blob/swift-2.2-SNAPSHOT-2015-12-01-a/utils/buildbot-release-notes.txt)) |
| Swift's `..<` | Groovy, according to the same notes |
| Swift's `defer` | Go ([ErrorHandlingRationale](https://github.com/swiftlang/swift/blob/main/docs/ErrorHandlingRationale.md)) |
| Kotlin's `it` and Elvis operator | Groovy ([Breslav](https://downloads.ctfassets.net/oxjq45e8ilak/24vVb20Rpam2MksEU4QCOe/511bcd2575ba6e8af5cf82896db71570/AndreyBreslav_ShouldersOfGiants.pdf)) |
| ES2015 shorthand properties | SpiderMonkey |
| ES2015 template literals | E, PHP and shell-style interpolation ([harmony:quasis](https://web.archive.org/web/20111223164326/http://wiki.ecmascript.org/doku.php?id=harmony:quasis)) |
| Python's `any` and `all` | ABC, according to Guido van Rossum ([Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=98196)) |
| C#'s `??` | No origin statement yet; dated to the May 2004 C# 2.0 specification ([Gunnerson](https://learn.microsoft.com/en-us/archive/blogs/ericgu/updated-c-v2-0-specifications-now-available)) |

### Still unsearched

- Elixir Forum's own search (blocked), Reddit (HTTP 403) and news.php.net (unreachable)
- IRC logs and conference talks
- golang-nuts and golang-dev, and discuss.python.org beyond a keyword search
- Kotlin's YouTrack and Slack, TC39 Discourse and the Mojo Discord
- Perl's PPC discussions on GitHub, and p5p bodies under unrelated subjects
- es-discuss after October 2016 beyond its mirror's search, ruby-talk and later ruby-dev
- Swift's WWDC 2014 sessions and podcasts

### The research notes

The notes behind every table and verdict are in [perl6-influence-notes/](perl6-influence-notes/README.md), with each hit's URL, date, author and classification, and the gaps each search left:

- [feature-timelines/](perl6-influence-notes/feature-timelines/) holds the first pass: the Perl 6 chronology, the Perl 5 backports, the per-feature timelines, the influence audit and the proposal-archive search.
- [beyond-proposals/](perl6-influence-notes/beyond-proposals/) holds the second: forums, mailing lists, other languages' repositories, Raku-side dates and claims, and the look-alikes' origins.
