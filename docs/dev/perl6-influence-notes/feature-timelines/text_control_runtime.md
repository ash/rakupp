# Perl 6/Raku text-processing, control-flow and runtime features in other languages (2000 to Nov 2026)

Method note: I downloaded the primary design documents (Swift Evolution SE-0350/0351/0354/0355/0357/0363, the Swift String Manifesto, Python PEPs 237/255/289/310/342/343/380/492/525/617/622/634/635/636/3148/3156, PHP RFCs, Perl PPC 0004, Kotlin KEEP coroutines, Rust RFCs 2394/3513, JEPs 305/406/441, the C# 7 pattern-matching proposal, WG14 N2895, the Ford PEG, LPeg and OMeta papers, the TC39 pattern-matching README, several CLI-library READMEs, and the Raku old-design-docs git history) and searched each for "Perl", "Perl 6" and "Raku". When a section says a document has "no Perl 6/Raku mention", that is the result of this full-text search, not a guess. Dates for Raku-side spec text come from `git log -S` on github.com/Raku/old-design-docs. That repo's history starts with an import in Nov 2008, so "present at import" only means "existed by Oct/Nov 2008".

## 1. Grammars as a language construct, PEG-family tools, and Perl 6-style regex syntax

### Takeaway
The only documented borrowing is Swift's. Michael Ilseman, Apple's String/regex lead, wrote in Jan 2018 that Swift "should outright steal from Perl 6" several regex features. In 2021 he said the earlier Swift regex-literal design "was heavily inspired by Raku". Swift 5.7 (2022) still shipped PCRE-style literal syntax and put the composable, grammar-like ideas into RegexBuilder and a planned "Pattern" type. Ford's PEG paper, LPeg, OMeta, Python's PEG parser (PEP 617), Pest and NimbleParsec do not cite Perl 6. Influence ran the other way for PEG vocabulary: Larry Wall added the "Perl 6 natively implements PEGs" overview to S05 in 2010.

### Cited Findings
**Raku side (dating)**
- Apocalypse 5 (Larry Wall, 4 Jun 2002) makes rules methods: "a rule is simply a method in a grammar class". It also says "`/x` extended syntax is now the default". Its only SNOBOL mention is a contrast: "languages like Prolog or Snobol where the pattern matching is built into the very control structure of the language". In the portion read, no other system is credited as the source of grammars. — [A05 on perl.com](https://www.perl.com/pub/2002/06/04/apo5.html/)
- The S05 sentence "In essence, Perl 6 natively implements Parsing Expression Grammars (PEGs) as an extension of regular expression notation" was added on 2010-05-05 by lwall, in commit 52d2074 ("give overview of Perl patterns in terms of PEGs, and spec tiebreaking explicitly"). That is six years after Ford's paper. — [S05-regex.pod](https://github.com/Raku/old-design-docs/blob/master/S05-regex.pod), [commit 52d2074](https://github.com/Raku/old-design-docs/commit/52d2074)

**PEG theory and PEG tools**
- Bryan Ford, "Parsing Expression Grammars: A Recognition-Based Syntactic Foundation" (POPL 2004). The full text contains no occurrence of "Perl". The lineage it gives is Birman's TS/TDPL and gTS/GTDPL (c. 1970) and Aho & Ullman. — [peg.pdf](https://bford.info/pub/lang/peg.pdf)
- LPeg paper (Ierusalimschy, "A Text Pattern-Matching Tool based on Parsing Expression Grammars"):
  - It mentions Perl only for Perl 5 regexes, PCRE, a Perl 5.8.8 benchmark and *Programming Perl* (3rd ed., 2000). There is no Perl 6 mention.
  - Its stated inspiration is SNOBOL4: "The second way to use LPEG is inspired by SNOBOL4 [8], where patterns are first-class values"; "LPEG users prefer this SNOBOL-like syntax."
  - — [LPeg paper PDF](https://www.inf.puc-rio.br/~roberto/docs/peg.pdf)
- OMeta (Warth & Piumarta, DLS 2007): the extracted text has no mention of Perl or of regular expressions. — [dls07.pdf](https://tinlizzie.org/~awarth/papers/dls07.pdf)
- PEP 617 (new PEG parser for CPython, created 24 Mar 2020, Python 3.9) explains the move from LL(1) to PEG and has no Perl mention. — [PEP 617](https://peps.python.org/pep-0617/)
- Rosie Pattern Language is described as "loosely based on Parsing Expression Grammars (PEG)". Jennings' slides contrast POSIX-style and Perl-style regexes. I found nothing linking it to Perl 6 grammars. — [rpeg README](https://gitlab.com/rosie-pattern-language/rpeg/blob/master/README.md), [Jennings, Lua Workshop 2016](https://lua.org/wshop16/Jennings.pdf)
- Pest (Rust) says it "uses parsing expression grammars (or PEG) as input". NimbleParsec (Elixir) describes itself as "text-based parser combinators". Neither README mentions Perl or Raku. — [pest](https://github.com/pest-parser/pest), [nimble_parsec](https://github.com/dashbitco/nimble_parsec)
- Pomsky, a whitespace-insensitive regex language with quoted literals and variables: a search summary says its comparison appendix lists "Raku Grammars" as a related but Raku-only tool, not as an inspiration. I did not read that page directly (the raw fetch 404'd). — [Pomsky comparison page](https://github.com/pomsky-lang/website/blob/main/content/docs/appendix/comparison.md)

**Swift (documented borrowing in the design process)**
- Ilseman's gist "State of String: ABI, Performance, Ergonomics, and You!" was created 2018-01-10. It is the text the 2021 forum post calls the "prior incarnation":
  - It sets the goal to "be better at string processing than Perl".
  - It says Perl "reflected on the 'mess' [linking A05] and cleaned it up in Perl 6. Perl 6 also has a PEG-like grammar system and we think similar approaches may be a part of Swift's long-term future."
  - It then lists "features Swift should outright steal from Perl 6": free-form (whitespace-insignificant) syntax, named captures, multi-line matching, and "Regex interpolation … This is perhaps the most Swifty aspect of Perl 6 regexes!"
  - It also lists frugal and ratcheting quantifiers, and separator quantification (the Raku `%` modified quantifier, linked to docs.perl6.org).
  - It proposes "ratcheting" concatenation (linking the Perl 6 `:ratchet` docs) and asks "How are alternations matched? LTM?" (linking perlgeek's longest-token-matching article).
  - — [gist milseman/bb39ef7f…](https://gist.github.com/milseman/bb39ef7f170641ae52c13600a512782f)
- Swift Forums, "Declarative String Processing Overview":
  - Post #13 (2021-09-30, John Siracusa): "I find Raku's Grammars to be a more useful higher-level structure for regexes."
  - Post #46 (2021-10-01, Michael Ilseman) replies: "I was very happy to see the (at the time, Perl 6) community embracing PEGs… I also love the terminology, like 'ratchet'."
  - Same post: "The prior incarnation's literal syntax was heavily inspired by Raku. In the end, familiarity (and compatibility…) with traditional regex syntax is the killer feature for the literal… we'd rather spend our design/'weirdness' budget on `Pattern`… even if I personally like Raku's syntax better than the traditional one."
  - Post #46 also says the team "thought about and discussed extending this further to define grammars using indirect enums".
  - — [post #13](https://forums.swift.org/t/declarative-string-processing-overview/52459/13), [post #46](https://forums.swift.org/t/declarative-string-processing-overview/52459/46)
- SE-0350 (Regex type and overview): "The closest formal analogue to what we are proposing are Parsing Expression Grammars ('PEGs')… Our alternation is ordered choice". It lists "Future work: parser combinators" and has no Raku/Perl 6 mention. SE-0351 (RegexBuilder DSL) has no Perl or Raku mention. — [SE-0350](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0350-regex-type-overview.md), [SE-0351](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0351-regex-builder.md)
- SE-0354 (regex literals) cites Perl only as heir of `/…/` and for user-chosen delimiters.
  - Its `#/…/#` multi-line literal makes whitespace non-semantic.
  - It explicitly rejects a Raku-like default: "Non-semantic whitespace by default for single-line literals… would lose out on the familiarity and compatibility of the single-line literal."
  - — [SE-0354](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0354-regex-literals.md)
- SE-0355 (regex syntax) bases the syntax on PCRE 2, Oniguruma, ICU, .NET and Perl 5, with extended syntax available via `(?x)`. It has no Raku mention. — [SE-0355](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0355-regex-syntax-run-time-construction.md)
- The Swift team's internal DeclarativeStringProcessing.md uses Perl as the example of a portable regex flavor ("knowledge… acquired in one tool (e.g. Perl) can often be applied in another"). It cites PEGs and has no Raku mention. — [DeclarativeStringProcessing.md](https://github.com/swiftlang/swift-experimental-string-processing/blob/main/Documentation/DeclarativeStringProcessing.md)

**Has any regex dialect adopted Perl 6 regex syntax?**
- No shipped mainstream dialect has, based on what I examined.
- Swift prototyped a Raku-inspired literal syntax (2018 gist) and abandoned it for PCRE compatibility. — [forum post #46](https://forums.swift.org/t/declarative-string-processing-overview/52459/46)
- Swift's RegexBuilder, an embedded DSL where regexes are composed as named values, realizes the "regex interpolation / refactor into named sub-regexes" goal by other means. — [SE-0351](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0351-regex-builder.md)

### Inferences
- **Swift Regex / RegexBuilder: "borrowed from Perl 6 (documented)" at the design level.** The 2018 and 2021 statements by the feature's lead designer are explicit. Two caveats: the shipped literal syntax is not Raku's, and the final SE proposals never cite Raku. So the evidence trail lives in a gist and forum posts, not in the accepted proposals. SE-0350's ordered-choice/possessive semantics are attributed to PEGs, a shared ancestor.
- **Ford PEG (2004): independent invention / common ancestor (TDPL/GTDPL).** No Perl citation at all, and Perl 6 later described itself in PEG terms.
- **LPeg: common ancestor (SNOBOL4) plus PEG.** The paper explicitly names SNOBOL4.
- **OMeta, PEP 617, Pest, NimbleParsec, Rosie: independent / common ancestor (PEG, Parsec).**
- **Pomsky: plausibly influenced at most (circumstantial).** It shares whitespace-insensitivity and quoted literals but cites no lineage.

### Gaps
- REBOL/Red `parse`, nom, Kotlin/Java parser combinators and the Elixir and Python PEG-tool docs were not text-searched. No Perl 6 citation is expected, but this is unverified.
- The LPeg first-release date (2008) and Swift 5.7 date (Sept 2022) are taken from the brief and were not re-verified.
- No Perl 6 mention was found in the OMeta text extraction. The PDF-to-text conversion may have dropped some pages.

## 2. Grapheme-based strings (NFG vs Swift Character, Elixir)

### Takeaway
Swift (2014) and Elixir (Nov 2012) both count user-perceived characters via Unicode UAX #29 extended grapheme clusters. Neither one's design documents (as far as examined) credits Perl 6. Raku's distinctive part, NFG with synthetic codepoints for O(1) grapheme indexing, has not been copied. The only Swift-side acknowledgement is a 2021 forum aside that Perl 6 regexes support grapheme semantics.

### Cited Findings
- **Raku dating:**
  - "NFG" first appears in S02 in lwall's commit dc6f02a, 2009-01-30 ("random clarifications").
  - S15 (Unicode), which calls NFG "a Perl 6–invented character representation", was added on 2013-12-11 (commit 9c555ea).
  - — [old-design-docs history](https://github.com/Raku/old-design-docs/commit/dc6f02a), [S15-unicode.pod](https://github.com/Raku/old-design-docs/blob/master/S15-unicode.pod)
- **Implementation:** Jonathan Worthington: "Since the 24th April [2015], all strings you work with in Rakudo on MoarVM are at grapheme level". — [6guts, 2015-04-30](https://6guts.wordpress.com/2015/04/30/this-week-the-big-nfg-switch-on-and-many-fixes/); see also [2015-04-12](https://6guts.wordpress.com/2015/04/12/this-week-unicode-normalization-many-rts/), [2015-04-20](https://6guts.wordpress.com/2015/04/20/this-week-digging-into-nfg-fixing-use-fatal-and-more/)
- **MoarVM design:** MoarVM implements strings as NFG, an extension of NFC, with multi-codepoint graphemes stored as "synthetics". — [MoarVM strings.asciidoc](https://github.com/MoarVM/MoarVM/blob/nqp-mbc/docs/strings.asciidoc)
- **Elixir:** the v0.7.1 changelog (2012-11-18) says "[String] Add support for graphemes in `String.length`, `String.at` and others". Later docs say String implements UAX #29 extended grapheme clusters. — [Elixir CHANGELOG at v0.7.2](https://github.com/elixir-lang/elixir/blob/v0.7.2/CHANGELOG.md), [Elixir 1.8 String docs](https://hexdocs.pm/elixir/1.8.0-rc.1/String.html)
- **Swift:** "Every instance of Swift's `Character` type represents a single *extended grapheme cluster*." — [The Swift Programming Language: Strings and Characters](https://docs.swift.org/swift-book/documentation/the-swift-programming-language/stringsandcharacters/)
- **Swift String Manifesto (Swift 4 redesign, 2017):**
  - It grounds the design in UAX #29 grapheme clusters.
  - Its only Perl reference is the Swift 4 goal "Our goal is to be better at string processing than Perl!". No Perl 6, Raku or NFG appears.
  - — [StringManifesto.md](https://github.com/swiftlang/swift/blob/main/docs/StringManifesto.md)
- **SE-0363 (Unicode for String Processing, 2022):**
  - It compares grapheme handling with "NSString, Java, Ruby, Perl" (via `\X`).
  - Its reference list defines `[raku]: https://docs.raku.org/language/regexes`, but no body text uses that link. This is true of both the initial and final revisions and of the source draft in the experimental repo.
  - — [SE-0363](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0363-unicode-for-string-processing.md), [UnicodeForStringProcessing.md](https://github.com/swiftlang/swift-experimental-string-processing/blob/main/Documentation/Evolution/UnicodeForStringProcessing.md)
- **Ilseman, forum post #100 (2021-10-06):** "some engines even support matching under canonical equivalence matching or even grapheme-cluster semantics (IIRC Perl 6 did)." — [post #100](https://forums.swift.org/t/declarative-string-processing-overview/52459/100)

### Inferences
- **Swift Character / String:** "common ancestor (Unicode UAX #29)". Perl 6 is acknowledged as a peer for regex grapheme semantics only (2021), not as a source for `Character`.
- **Elixir:** "common ancestor (UAX #29)". Elixir shipped grapheme `String.length` in Nov 2012, before MoarVM existed. NFG was only a spec idea then (S02, 2009).
- The empty `[raku]` link in SE-0363 hints that Raku was in an earlier comparison draft. It is circumstantial only.

### Gaps
- I did not find José Valim's rationale for grapheme semantics (mailing-list or commit history) and did not check it for a Perl 6 mention.
- I did not read the 2014 Swift 1.0 design discussions (pre-open-source, so not public).

## 3. Scope-exit phasers (LEAVE/KEEP/UNDO/ENTER/FIRST/NEXT/LAST/PRE/POST) vs defer, scope guards and context managers

### Takeaway
Raku specified KEEP/UNDO/PRE/POST in Jan 2002 (Apocalypse 4). D's `scope(exit|success|failure)` (DMD 0.148, Feb 2006) is the closest analogue to LEAVE/KEEP/UNDO, and Zig's `errdefer` (renamed from `%defer` in Jan 2018) is UNDO-like. Both are documented descendants of other lines: Alexandrescu's C++ ScopeGuard and Zig's own error-handling sigils. Perl 5.36's `defer` is the one design that explicitly discusses Raku, and it rejected the `LEAVE` name because the semantics differ. The C `defer` effort cites Go.

### Cited Findings
- **Apocalypse 4 (Larry Wall, 15 Jan 2002):**
  - "`KEEP` blocks would only be executed if the block succeeded. `UNDO` blocks would only be executed if the block failed."
  - "The `POST` block has a corresponding `PRE` block for design-by-contract programmers."
  - "The cleanup mechanism should mesh nicely with the notions of post condition processing under design-by-contract."
  - Eiffel is not named in the portion read.
  - — [A04 on perl.com](https://www.perl.com/pub/2002/01/15/apo4.html/)
- **D:** in a Feb 2006 forum thread about DMD 0.148's scope guard, Walter Bright says the feature is based on "Andrei Alexandrescu's scope guard macros". Today's semantics: `scope(exit)` always runs, while `scope(success)` and `scope(failure)` depend on whether an exception was thrown. — [forum.dlang.org post](https://forum.dlang.org/post/du106f$ptt$1@digitaldaemon.com), [D tour: scope guards](https://tour.dlang.org/tour/en/gems/scope-guards)
- **Go:** the FAQ gives no origin for `defer` and only points to the "Defer, Panic, and Recover" article. — [Go FAQ](https://go.dev/doc/faq)
- **Swift `defer` (Swift 2.0, 2015):**
  - Chris Lattner (swift-users, Nov 2016): "Go has a similar but different defer statement, which runs defer'd actions at the end of the current dynamic *function* scope. The Swift rules are simpler…"
  - Go's Ian Lance Taylor "assume[s]" Swift's was based on Go's.
  - Neither statement is an origin claim by Apple.
  - — [swift-users](https://lists.swift.org/pipermail/swift-users/Week-of-Mon-20161107/003918.html), [golang-nuts](https://groups.google.com/g/golang-nuts/c/4Lzf-uPYjY8/m/09C8YZ6OAwAJ)
- **Zig:** the old `%defer` became `errdefer` (alongside `%return` becoming `try`), per Andrew Kelley's January 2018 review. It shipped in Zig 0.2.0. — [Zig: January 2018 in Review](https://andrewkelley.me/post/zig-january-2018-in-review.html), [0.2.0 release notes](https://ziglang.org/download/0.2.0/release-notes.html)
- **Python `with` (PEP 343, 2005):**
  - The design grew out of PEP 340 and a counter-proposal by Phillip Eby ("Inspired by a counter-proposal to PEP 340 by Phillip Eby").
  - PEP 343 has no Perl mention. Its predecessor PEP 310 mentions only "Smalltalk/Ruby anonymous block style".
  - — [PEP 343](https://peps.python.org/pep-0343/), [PEP 310](https://peps.python.org/pep-0310/)
- **C (WG14 N2895, "A simple defer feature for C", Jens Gustedt & Robert C. Seacord):**
  - "Many modern programming languages (such as C++, Go, D, or Java)… provide proper constructs".
  - It proposes "a similar feature as it is found in Go, coined defer".
  - Prior art it lists: POSIX `pthread_cleanup_push`, MSVC `__try`/`__finally`, and the gcc `cleanup` attribute. There is no Perl mention.
  - — [N2895](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2895.htm)
- **C TS 25755 ("defer"):**
  - A 2026 working draft (N3853) lists JeanHeyd Meneide as project editor. ISO lists it as a Committee Draft.
  - I found no evidence that it has been published as of Nov 2026.
  - — [N3853](https://open-std.org/jtc1/sc22/wg14/www/docs/n3853.pdf), [ISO 91402](https://www.iso.org/standard/91402.html), [thephd.dev](https://thephd.dev/c2y-the-defer-technical-specification-its-time-go-go-go)
- **Perl 5.36 `defer` (Perl PPC 0004, Paul Evans):**
  - "The name 'defer' comes from a collection of other languages. Near-identical syntax is provided by Swift, Zig, Jai, Nim and Odin." He chose lexical (not Go-style function) scope as "more 'Perlish'".
  - Naming history: "this was originally called `LEAVE {}`, which was rejected because its semantics don't match the Raku language feature of the same name. It was then renamed to `FINALLY {}`… All the SHOUTY PHASER BLOCKS are declarations, activated by their mere presence at compiletime, whereas `defer {}` is a statement which only takes effect if dynamic execution actually encounters it."
  - — [PPC 0004](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0004-defer-block.md), [perl5 PR #19019](https://github.com/Perl/perl5/pull/19019)

### Inferences
- **D scope guards:** "common ancestor (Alexandrescu/Marginean C++ ScopeGuard, 2000)". D's success/failure split matches KEEP/UNDO, but no D source cites Perl 6.
- **Go defer (2009):** "independent invention". No origin is documented.
- **Swift defer:** "independent / plausibly Go-influenced (circumstantial)". There is no Perl 6 link.
- **Zig errdefer:** "independent invention". It evolved from Zig's own `%` error sigils, though it is semantically UNDO.
- **Python `with`, C# `using`, Java try-with-resources:** "common ancestor (C++ RAII / try-finally)". No Perl 6 citation was found.
- **C defer (N2895, TS 25755):** "borrowed from Go (documented)", not from Perl 6.
- **Perl 5.36 defer:** Raku was explicitly considered and rejected as the model. Verdict: "independent of Raku (documented); borrowed from Swift/Zig/Jai/Nim/Odin". The PPC's point that Raku phasers are compile-time declarations while defer is a runtime statement is the key semantic split for a Raku implementer.
- **PRE/POST:** "common ancestor: Eiffel design-by-contract". A04 uses Meyer's term without naming Eiffel.

### Gaps
- I did not fetch the Alexandrescu/Marginean CUJ Dec 2000 article or the exact DMD 0.148 changelog.
- Java try-with-resources (JSR 334), the C# `using` design notes, and Swift 2.0 release notes were not text-searched.
- Raku's ENTER/FIRST/NEXT/LAST have no direct counterpart found in any mainstream language. I found no adoption.

## 4. Lazy lists and gather/take vs generators

### Takeaway
Every post-2000 generator design examined cites CLU/Sather/Icon, Python, C#, Scala or Haskell, never Perl 6. Kotlin's `sequence { yield() }` (2017) is the closest look-alike. It is lexically restricted ("restricted suspension"), unlike Raku's dynamically scoped `take`. Rust's 2023 gen-blocks RFC has a 14-language prior-art survey that omits Raku.

### Cited Findings
- **PEP 255 (simple generators, 2001):** "notably iterators in Sather, which were inspired by iterators in CLU; and generators in Icon". There is no Perl mention. — [PEP 255](https://peps.python.org/pep-0255/)
- **PEP 289 (generator expressions, 2004):** no Perl mention. — [PEP 289](https://peps.python.org/pep-0289/)
- **JavaScript 1.7 (2006):** Brendan Eich described infusing JS with "Pythonic generators and iterators", following Python. ES2015 standardized them. — [brendaneich.com "Python and JavaScript"](https://brendaneich.com/?p=49), [es-discuss](https://esdiscuss.org/topic/foreach-on-next)
- **Kotlin coroutines KEEP:**
  - "lazily computed sequences (handled by `yield` in C#, Python…)" and "async/await in C#, JS and other languages".
  - It includes a "Restricted suspension" section for `sequence {}`. There is no Perl mention.
  - — [KEEP coroutines.md](https://github.com/Kotlin/KEEP/blob/master/proposals/coroutines.md)
- **Ruby `Enumerable#lazy` (Feature #4890, shipped in Ruby 2.0):** the discussion cites Scala's `view` and `withFilter` and Haskell list comprehensions. There is no Perl mention. — [Ruby issue #4890](https://bugs.ruby-lang.org/issues/4890)
- **Rust RFC 3513 (gen blocks, 2023-10-10):** the prior-art sections cover CLU/Alphard, Icon, Python, ECMAScript, Ruby, Kotlin, Swift, C#, D, Dart, F#, Racket, Haskell/Idris/Clean, Koka and Rust. There is no Perl or Raku. — [RFC 3513](https://rust-lang.github.io/rfcs/3513-gen-blocks.html)
- **Raku side:** gather/take is present in S04 at the Nov 2008 import (S04 version 70, last modified 14 Oct 2008). — [S04-control.pod history](https://github.com/Raku/old-design-docs/commits/master/S04-control.pod)

### Inferences
- **Python, JS, C# 2.0, Kotlin, Ruby, Rust:** "common ancestor (CLU/Icon generators; Haskell/Scala for laziness)". None is borrowed from Perl 6.
- Dynamically scoped `take` (yielding from a called routine without passing a yielder) remains Raku-specific among mainstream languages. Kotlin forbids it by design (restricted suspension), and Python/JS need `yield from`/`yield*`. Ruby's Fiber-based `Enumerator.new { |y| … }` achieves similar power by passing the yielder object explicitly. This is my analysis, not a cited claim.

### Gaps
- I did not locate a primary C# 2.0 iterator design note naming its ancestry (CLU/Sather is commonly asserted).
- I did not establish when gather/take first entered the Perl 6 design (it predates the Nov 2008 import).

## 5. Junctions (any/all/one/none as values)

### Takeaway
No mainstream language has adopted junction values. Python's `any()`/`all()` (proposed by Guido van Rossum 10 Mar 2005, shipped in Python 2.5) are plain reducing functions, and the proposal does not mention Perl or junctions. The only adopters found are Perl 5 CPAN ports of the Perl 6 design.

### Cited Findings
- Guido van Rossum, "The fate of reduce() in Python 3000" (10 Mar 2005): "Let's add any() and all() to the standard builtins". There is no mention of Perl, Perl 6 or junctions. — [Artima post](https://www.artima.com/weblogs/viewpost.jsp?thread=98196)
- Perl6::Junction (Perl 5 CPAN) is "Perl6 style Junction operators in Perl5", a lightweight subset of Quantum::Superpositions. Syntax::Keyword::Junction is a fork of it, partly to drop the "Perl6" name. — [Fedora package perl-Perl6-Junction](https://packages.fedoraproject.org/pkgs/perl-Perl6-Junction/perl-Perl6-Junction), [Syntax-Keyword-Junction DESCR](https://git.osdyson.ru/mirror/pkgsrc/plain/devel/p5-Syntax-Keyword-Junction/DESCR)
- Origin: Damian Conway's Quantum::Superpositions (2000). — [metacpan Quantum::Superpositions](https://metacpan.org/pod/Quantum::Superpositions) (page not fetched in this session)
- Searches for junction or superposition values in Python and JavaScript languages or libraries returned only the Perl ports. — [search results summarized above](https://packages.fedoraproject.org/pkgs/perl-Perl6-Junction/perl-Perl6-Junction)

### Inferences
- **Python `any()`/`all()`:** "independent invention". These are eager boolean reductions, not autothreading values.
- Junctions remain unadopted. That makes them one of the Raku features most clearly *not* diffused, which is useful as a negative control in the report.

### Gaps
- I did not search Julia, Kotlin or C# library ecosystems exhaustively for junction-like autothreading types.
- I did not fetch the Quantum::Superpositions CPAN date directly.

## 6. given/when + smartmatch and the move to structural pattern matching

### Takeaway
The only documented borrowing is Perl 5.10 (Dec 2007), whose switch feature "is closely based on the Perl 6 proposal". That borrowing then failed:
- experimental in 5.18 (2013);
- "declaring the experiment a failure" in 5.38 (2023);
- removal planned for 5.42, but instead "kept, behind a feature" in 5.42 (2025).

The 2017–2023 wave of pattern-matching designs (C# 7, Python 3.10, Java 17/21, Ruby 2.7/3.0, PHP 8 match) cite ML-family languages, Scala, F#, Rust, Elixir and Haskell, never Perl's smartmatch. The one Raku mention is in PHP's in-discussion Pattern Matching RFC, which suggests Raku's `Range` as a model for a future range pattern.

### Cited Findings
- A04 (Jan 2002): "we've settled on using `given` instead of `switch`, and `when` instead of `case`". Wall credits C ("borrow a bit more from C and use `default`") and "the ever inventive Damian Conway… his famous design" (the Switch.pm design). — [A04](https://www.perl.com/pub/2002/01/15/apo4.html/)
- Perl 5.10.0 perlsyn: "use feature 'switch'… enables a switch feature that is closely based on the Perl 6 proposal". It has a "Differences from Perl 6" section ("The table of smart matches is not identical to that proposed by the Perl 6 specification"). — [perlsyn.pod @ perl-5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/pod/perlsyn.pod)
- perl5180delta: "Smart match, added in v5.10.0 and significantly revised in v5.10.1, has been a regular point of complaint… it is clear that smartmatch is almost certainly either going to change or go away". — [perl5180delta](https://perldoc.perl.org/perl5180delta)
- perl5380delta: "In v5.38.0, we are declaring the experiment a failure… These features will be entirely removed from perl in v5.42.0." — [perl5380delta](https://perldoc.perl.org/perl5380delta)
- perl5420delta, "Switch and Smart Match operator kept, behind a feature": smartmatch is disabled from the 5.41 feature bundle, and switch from the 5.35 bundle. — [perl5420delta](https://perldoc.perl.org/perl5420delta)
- **Python PEP 622 (2020):** "inspired by similar syntax found in Scala, Erlang, and other languages". Its only Perl mention is the general statement "regular expressions from Perl". — [PEP 622](https://peps.python.org/pep-0622/)
- **PEP 635 (rationale):** lists C#, Elixir, Erlang, F#, Grace, Haskell, Mathematica, OCaml, Ruby, Rust, Scala, Swift and Thorn. There is no Perl. PEP 634 and PEP 636 have no Perl mention either. — [PEP 635](https://peps.python.org/pep-0635/), [PEP 634](https://peps.python.org/pep-0634/), [PEP 636](https://peps.python.org/pep-0636/)
- **C# 7.0 pattern-matching proposal:** cites F# active patterns ("Extensible Pattern Matching Via a Lightweight Language", Syme) and Scala ("Matching Objects With Patterns"). There is no Perl. — [csharplang pattern-matching.md](https://github.com/dotnet/csharplang/blob/main/proposals/csharp-7.0/pattern-matching.md)
- **Java:** JEP 305 says "Many languages, from Haskell to C#, have embraced pattern matching". JEP 406 and JEP 441 (pattern matching for switch, final in Java 21) name no other language. — [JEP 305](https://openjdk.org/jeps/305), [JEP 406](https://openjdk.org/jeps/406), [JEP 441](https://openjdk.org/jeps/441)
- **Ruby `case/in` (Feature #14912):** references Elixir's pin operator, Scala extractors, OCaml, Haskell view patterns, Grace/Newspeak and Erlang/Elixir receive. There is no Perl. — [Ruby issue #14912](https://bugs.ruby-lang.org/issues/14912)
- **PHP 8.0 `match`:**
  - v1 RFC (2020-04-12, declined) borrowed block-expression syntax "from Rust".
  - v2 RFC (2020-05-22, implemented in PHP 8.0) has no Perl or smartmatch mention.
  - — [match_expression](https://wiki.php.net/rfc/match_expression), [match_expression_v2](https://wiki.php.net/rfc/match_expression_v2)
- **PHP Pattern Matching RFC:** v0.9, dated 2020-11-11, entered discussion 2025-12-01, status "In Discussion", by Larry Garfield and Ilija Tovilo. It says: "A future range RFC, if any, should include a range pattern, possibly modeling on [Raku](https://docs.raku.org/type/Range) or similar." — [PHP RFC: Pattern Matching](https://wiki.php.net/rfc/pattern-matching)
- **TC39 pattern-matching proposal:** has a "Prior Art" section and no Perl or Raku mention. — [tc39/proposal-pattern-matching](https://github.com/tc39/proposal-pattern-matching)

### Inferences
- **Perl 5.10 given/when/~~:** "borrowed from Perl 6 (documented)". It is also the cautionary tale.
- **C# 7, Python 3.10, Java 17/21, Ruby 2.7/3.0, PHP 8 match, TC39:** "common ancestor (ML/Haskell/Erlang/Scala/F#)".
- **Kotlin `when` (2011), Swift `switch` patterns (2014), Rust `match`:** "common ancestor (ML family)". Their origins were not text-verified.
- **PHP Pattern Matching RFC:** Raku is cited only for `Range` semantics, a narrow "plausibly influenced (documented mention)" for one sub-feature.
- None of the examined proposals cites Perl 5's smartmatch retreat as a warning. If the report wants that "others cite it" claim, it is unsupported by what I examined.

### Gaps
- I did not search C# LDM notes, the Kotlin `when` design history or Swift 1.0 design notes, so Perl mentions there are unknown.
- I did not find any third-party language design document citing Perl 5 smartmatch as a cautionary example.

## 7. Concurrency: Promises/start/await, Supplies/react/whenever, Channels, hyper/race

### Takeaway
Influence flowed into Raku, documented for Rx. Jonathan Worthington tied Supplies to Rx in Dec 2013 and in 2015 cited his experience teaching Rx.NET as motivation for `supply`/`react`/`whenever`. S17 was rewritten in Nov 2013 around his design. No other language's async, coroutine or reactive design examined (PEP 492, Rust RFC 2394, Kotlin KEEP) cites Perl 6. They cite C#, ECMAScript, Hack, Dart, Scala and Python.

### Cited Findings
- **S17 rewrite:** Carl Masak's commit "[S17] jnthn's heresy is the new law" (2013-11-03) introduced Promise. Larry Wall renamed "Subscribables" to "Supply" on 2013-11-04 ("Take a whack at some simpler terminology"). — [old-design-docs S17 history](https://github.com/Raku/old-design-docs/commits/master/S17-concurrency.pod), [S17-concurrency.pod](https://github.com/Raku/old-design-docs/blob/master/S17-concurrency.pod)
- **Worthington, Perl 6 Advent (19 Dec 2013):** "the observer pattern is the mathematical dual of the iterator pattern… Out of this was born reactive programming, and the Rx (Reactive Extensions) library… In [Perl 6], we're providing support for this in core." — [perl6advent 2013-12-19](https://perl6advent.wordpress.com/2013/12/19/perl-6-supplies-reactive-programming/)
- **Worthington (6guts, Jul 2015):** "Having done my share of teaching Rx.Net, I've had plenty of chance to see people grapple with asynchronous data using an API a lot like we have for supplies." This motivated the supply/react/whenever syntax, announced in Advent 2015-12-14. — [6guts 2015-07-23](https://6guts.wordpress.com/2015/07/23/this-week-concurrency-stuff-multi-dimensional-stuff-stuff-stuff/), [perl6advent 2015-12-14](https://perl6advent.wordpress.com/2015/12/14/day-14-a-nice-supplies-syntactic-relief-for-working-with-asynchronous-data/)
- **Worthington on .NET (2014):** "outside of the Perl world, I spend a lot of my time with the .Net platform. C#… has escaped Java-think". His work included teaching "advanced C# programming". — [6guts 2014-04-17](https://6guts.wordpress.com/2014/04/17/racing-to-writeness-to-wrongness-leads/), [6guts 2013-09-17](https://6guts.wordpress.com/2013/09/17/material-from-the-rakudo-and-nqp-internals-course/)
- **PEP 492 (async/await, 2015)** lists prior art: "C# has it since long time ago", the ECMAScript 7 async functions proposal, Facebook's Hack/HHVM, Google's Dart and Scala. There is no Perl. PEP 3156 and PEP 3148 have no Perl mention. — [PEP 492](https://peps.python.org/pep-0492/)
- **Rust RFC 2394 (async/await):** "notable examples include C#, JavaScript, and Python". There is no Perl. — [RFC 2394](https://rust-lang.github.io/rfcs/2394-async_await.html)
- **Kotlin coroutines KEEP:** cites C#, JS, Python and Go-style patterns. There is no Perl. — [KEEP coroutines.md](https://github.com/Kotlin/KEEP/blob/master/proposals/coroutines.md)

### Inferences
- **Raku Supplies:** influence flowed "borrowed from Rx (documented)" into Raku, not out of it.
- **Raku `start`/`await`/Promise:** "plausibly influenced by C#/.NET TPL (circumstantial)". Worthington's day job was C# teaching, but I found no explicit statement that Raku's Promise/await was modeled on .NET Task/await.
- **F# async (2007), C# 5 (2012), Python asyncio (2015), ES2017, Rust async (2019), Kotlin coroutines (2017–18):** "independent of Perl 6". Their documented lineage is C#/F# and each other.
- **Go channels (2009):** "common ancestor (CSP)", also for Raku Channel.
- **Java parallel streams (2014) vs Raku hyper/race:** no documented link either way.

### Gaps
- I did not find a Worthington statement explicitly tying `Promise`/`await` naming or semantics to .NET `Task`/`await`.
- The F# async, Go and Rx origin documents themselves were not fetched. They do not bear on Perl 6 except as ancestors.

## 8. `sub MAIN` vs CLI-from-signature libraries

### Takeaway
Raku's `MAIN` was in S06 by Oct 2008. Python had signature-driven CLI generators independently from 2010: plac (Jun 2010), argh (Nov 2010) and clize (Dec 2011). Fire (2017), Typer (2019) and Nim's cligen trace their lineage to these Python tools or to Click/FastAPI. None of the READMEs examined (Fire, Typer, docopt, cligen, structopt, Comonicon) mentions Perl or Raku.

### Cited Findings
- **Raku:** S06 at the Nov 2008 import (version 96, last modified 14 Oct 2008) already contained the "Declaring a MAIN subroutine" section (20 occurrences of MAIN). — [S06-routines.pod](https://github.com/Raku/old-design-docs/blob/master/S06-routines.pod)
- **plac** (Michele Simionato) "can generate command line parameters from function signatures". The first PyPI upload was 2010-06-02. Its docs say it is inspired by his own Python Cookbook recipe (optionparse) and by Python 3 function annotations. — [PyPI plac](https://pypi.org/project/plac/), [plac docs](https://plac.readthedocs.io/)
- **First PyPI uploads** (PyPI JSON API):
  - argh 0.1.0: 2010-11-12
  - clize 1.0b: 2011-12-04
  - docopt 0.1: 2012-04-10
  - click 0.1: 2014-04-28
  - fire 0.1.0: 2017-02-15
  - typer 0.0.1: 2019-12-20
  - — [pypi.org/project/argh](https://pypi.org/project/argh/), [clize](https://pypi.org/project/clize/), [docopt](https://pypi.org/project/docopt/), [click](https://pypi.org/project/click/), [fire](https://pypi.org/project/fire/), [typer](https://pypi.org/project/typer/)
- **Python Fire** was announced on 2 Mar 2017 by David Bieber (Google Brain) as generating CLIs "from any Python code". The README cites no prior art. — [Google Open Source Blog](https://opensource.googleblog.com/2017/03/python-fire-command-line.html)
- **Nim cligen:** "This approach to CLIs was inspired by Andrey Mikhaylenko's Python argh (Click became more popular)." — [cligen README](https://github.com/c-blake/cligen)
- The structopt, Comonicon.jl, Typer and docopt READMEs contain no Perl or Raku mention. — [structopt](https://github.com/TeXitoi/structopt), [Comonicon.jl](https://github.com/comonicon/Comonicon.jl), [typer](https://github.com/fastapi/typer), [docopt](https://github.com/docopt/docopt)

### Inferences
- **plac, argh, clize, Fire, Typer, cligen, structopt/clap derive, Comonicon:** "independent invention". Reflection- or annotation-driven CLI generation is a convergent idea, and the Python line has its own documented genealogy (plac → argh → … → cligen).
- **docopt:** a different paradigm (parse the usage text), also independent.

### Gaps
- The crates.io API returned no data, so the dates for structopt (2017) and clap 3.0 with derive (2021) were not verified.
- The System.CommandLine DragonFruit design notes were not located.
- I did not determine when `MAIN` first entered the Perl 6 spec (before Oct 2008).

## 9. Exact rationals for decimal literals; arbitrary-precision integers

### Takeaway
Raku's spec made decimal literals Rat on 2009-10-22. Pyret (design note TM017) independently makes decimal literals exact rationals, with `~` marking approximations. Groovy makes decimal literals `BigDecimal`: exact decimal, not rational. Haskell's report already defined a floating literal as `fromRational` of an exact Rational, though type defaulting yields Double. I found no citation of Perl 6 in any of these.

### Cited Findings
- **Raku:** commit f431adc (lwall, 2009-10-22): "[S02] document desire to have decimal literals biased toward Rat storage". S02 now says "Decimal fractions not using 'e' notation are also treated as literal `Rat`… `0.11 == 11/100 # True`". — [commit f431adc](https://github.com/Raku/old-design-docs/commit/f431adc), [S02-bits.pod](https://github.com/Raku/old-design-docs/blob/master/S02-bits.pod)
- **Pyret:**
  - The design note says Pyret "should support two types of numbers, *exact numbers*… and *approximations*". Exact numbers are "positive and negative rational numbers of arbitrary precision", and the note says "No built-in function should automatically cast between the two representations".
  - Current docs: plain decimals such as `0.1` are Exactnums, and `~0.1` is a Roughnum.
  - TM017 has no Perl or Raku mention.
  - — [TM017 Numbers](https://github.com/brownplt/pyret-lang/wiki/TM017-Numbers), [Pyret numbers docs](https://pyret.org/docs/latest/numbers.html)
- **Groovy:** "Conveniently for exact decimal number calculations, Groovy chooses `java.math.BigDecimal` as its decimal number type"; "123.45 == new BigDecimal('123.45') // default BigDecimal type used". — [Groovy syntax docs](https://groovy-lang.org/syntax.html)
- **Haskell 2010 report §6.4.1:** "a floating literal stands for an application of `fromRational` to a value of type `Rational`". — [Haskell 2010 ch. 6](https://www.haskell.org/onlinereport/haskell2010/haskellch6.html)
- **PEP 237 (unifying int/long, 2001; completed in Python 3):** no mention of Lisp, Perl or other languages. — [PEP 237](https://peps.python.org/pep-0237/)

### Inferences
- **Pyret:** "independent invention / common ancestor (Scheme/Racket exact numbers, via Brown PLT)". The Racket lineage is my inference from the authors and the exact/inexact terminology, not a cited claim.
- **Groovy:** "independent". It is decimal-exact via Java's BigDecimal, not rational.
- **Haskell:** an "ancestor" for the idea that a decimal literal denotes an exact rational value.
- **Python 3 int, Ruby Integer:** "common ancestor (Lisp/Smalltalk bignums)".

### Gaps
- R7RS Scheme / Common Lisp literal exactness and Julia `//` docs were not fetched. They are standard, but no URL was verified this session.
- Groovy's first-release date for the BigDecimal default was not verified.

## 10. Macros and extensible syntax (slangs)

### Takeaway
I found no evidence that any post-2000 macro system cites Perl 6 slangs or grammar-mutating parsing. Julia explicitly credits Lisp. Raku's slang concept entered S02 in 2009.

### Cited Findings
- **Raku:** commit 5cfa62e (lwall, 2009-05-08): "[S02] take stab at documenting how interwoven languages work". — [old-design-docs commit 5cfa62e](https://github.com/Raku/old-design-docs/commit/5cfa62e)
- **Julia:** "The strongest legacy of Lisp in the Julia language is its metaprogramming support… true Lisp-style macros operating at the level of abstract syntax trees." — [Julia manual: Metaprogramming](https://docs.julialang.org/en/v1/manual/metaprogramming/)
- **Elixir:** the Elixir macros guide has no Lisp, Perl or Raku mention. — [Elixir macros guide](https://github.com/elixir-lang/elixir/blob/main/lib/elixir/pages/meta-programming/macros.md)

### Inferences
- **Julia, Elixir:** "common ancestor (Lisp)".
- **Rust macro_rules:** "common ancestor (Scheme syntax-rules / macro-by-example)".
- **Scala 3:** "common ancestor (MetaML-style quotes/splices)".
- **Nim:** "common ancestor (Lisp AST macros)".
- The Rust, Scala 3 and Nim origins are not text-verified here.
- No language examined offers Raku-style runtime-composable grammar mixins into its own parser ("slangs"). This is negative evidence only.

### Gaps
- I did not search the Rust RFCs on macros, the Scala 3 macro SIPs or the Nim manual for Perl.

## 11. Cross-feature verdict summary (for the report writer)

### Takeaway
Across ten features and about forty proposals and designs, documented borrowing from Perl 6/Raku appears in only three places:
- Perl 5.10 given/when/smartmatch (2007), later declared a failed experiment.
- The Swift regex design lineage: Ilseman's 2018 gist and 2021 forum posts. The shipped SE proposals do not cite Raku, and the shipped syntax is PCRE-style.
- A narrow Raku `Range` reference in PHP's in-discussion Pattern Matching RFC.

One design explicitly rejected Raku as a model: Perl 5.36 `defer`, which declined the `LEAVE` name. In the one place where documented influence crosses the Raku boundary in the other direction, Supplies, Raku borrowed from Rx.

### Cited Findings
- Perl 5.10 switch is "closely based on the Perl 6 proposal". — [perlsyn 5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/pod/perlsyn.pod)
- Swift: "features Swift should outright steal from Perl 6". — [Ilseman gist, 2018-01-10](https://gist.github.com/milseman/bb39ef7f170641ae52c13600a512782f); "heavily inspired by Raku". — [forum #46](https://forums.swift.org/t/declarative-string-processing-overview/52459/46)
- PHP Pattern Matching RFC: "possibly modeling on Raku". — [wiki.php.net/rfc/pattern-matching](https://wiki.php.net/rfc/pattern-matching)
- Perl PPC 0004 rejected `LEAVE {}` because "its semantics don't match the Raku language feature of the same name". — [PPC 0004](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0004-defer-block.md)
- Raku Supplies: "Out of this was born… the Rx (Reactive Extensions) library… we're providing support for this in core". — [Advent 2013-12-19](https://perl6advent.wordpress.com/2013/12/19/perl-6-supplies-reactive-programming/)

### Inferences
Verdict table:

| Feature / language | Verdict |
|---|---|
| Swift Regex + RegexBuilder (5.7) | Borrowed from Perl 6 (documented, design stage); literal syntax not adopted |
| Ford PEG 2004, OMeta 2007, PEP 617, Pest | Independent / common ancestor (TDPL/GTDPL) |
| LPeg | Common ancestor (SNOBOL4) plus PEG |
| Rosie, NimbleParsec | Common ancestor (PEG, Parsec) |
| Pomsky | Plausibly influenced (circumstantial only) |
| Swift Character, Elixir graphemes | Common ancestor (UAX #29) |
| D scope guards | Common ancestor (Alexandrescu ScopeGuard, 2000) |
| Go defer | Independent |
| Swift defer | Independent (Go-like) |
| Zig errdefer | Independent |
| C defer (N2895 / TS 25755) | From Go (documented) |
| Python `with` | Independent (PEP 340/Eby lineage) |
| Perl 5.36 defer | Raku explicitly rejected; from Swift/Zig/Jai/Nim/Odin |
| Python, JS, C#, Kotlin, Ruby, Rust generators | Common ancestor (CLU/Icon/Sather; Python for JS) |
| Junctions | No adopter; Python any/all independent |
| Perl 5.10 given/when | Borrowed from Perl 6 (documented) |
| C#, Python, Java, Ruby, PHP, TC39 pattern matching | Common ancestor (ML/Haskell/Scala/F#/Erlang) |
| PHP Pattern Matching RFC (range pattern only) | Raku cited as a possible model |
| Raku Supplies | Borrowed from Rx |
| Raku Promise/await | Plausibly influenced by C#/.NET |
| async/await elsewhere | Independent of Perl 6 (C# lineage) |
| sub MAIN analogues | Independent (Python plac/argh line) |
| Rat literals vs Pyret, Groovy | Independent; Haskell `fromRational` as an ancestor |
| Macros | Common ancestor (Lisp/Scheme/MetaML) |

### Gaps
- Absence of a Perl 6 mention was verified by full-text search only for the documents listed in the method note. Language design discussions held on private lists or unindexed forums (C# LDM pre-GitHub, Swift 1.0, Go 2009, Kotlin 2011) were not covered.
