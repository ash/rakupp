# Documented influence of Perl 6 / Raku on other languages, designers and tooling (2000 – Nov 2026), and audit of influence claims

Method note. Searches used web search with site filters and direct fetches of the primary documents. PDFs (HOPL papers, Lua 5.0 paper, VM-showdown paper) were downloaded and text-grepped for "Perl 6", "Perl", "Parrot", "Pugs". Web search is NOT a full-text index of GitHub repos or mailing lists. "No hit" therefore means "nothing turned up". It does not mean "proved absent", except where a whole document was read (marked "read in full"). Quotes are kept to short fragments. Most design documents are copyrighted, and the PEPs are public domain. Classification tags: **[adopted-from]**, **[considered-and-rejected]**, **[cited-as-prior-art]**, **[cautionary-tale]**, **[joke]**.

## 1. Explicit mentions of Perl 6 / Raku in language-design archives

### Takeaway
Explicit design-archive mentions exist in Python PEPs, PHP RFCs, Swift Evolution pitches, the Ruby tracker, es-discuss (ES4) and pgsql-hackers. Almost all are prior-art citations, considered-and-rejected options, or cautionary tales about the Perl 6 compatibility break and timeline. I found exactly one documented case where a shipped feature says it follows Raku: PHP 8.3's "saner increment/decrement" RFC, which follows Raku's behaviour. One more case comes close: Python 3's exception chaining (PEP 3134) cites a Perl 6 RFC as prior art, though not as its source.

### Cited Findings

**Python (peps.python.org, python-dev, Guido's writing)**
- **Correction to the brief.** PEP 3099, "Things that will Not Change in Python 3000" (Georg Brandl, created 04-Apr-2006), mentions *Perl*, not Perl 6. The context is the LL(1) parser rule: Python avoids "funky grammar rules like some other dynamic languages that will go unnamed, such as Perl." The fetch tool confirmed Perl 6 and Larry Wall do not appear. Classification **[cautionary-tale]** (about Perl in general). — [PEP 3099](https://peps.python.org/pep-3099/)
- **PEP 3133, "Introducing Roles"** (Collin Winter, created 01-May-2007, status **Rejected**). It explicitly proposed importing Perl 6 roles. It says traits were "originally called 'traits' and implemented for Squeak Smalltalk" and "since been adapted for use in Perl 6 where it is called 'roles'". Its examples are "adapted from an article on Perl 6 roles by Curtis Poe", and it describes Perl 6's runtime role application to instances. Rejection note (GvR): the PEP "helped push PEP 3119 towards a saner, more minimalistic approach". Python got ABCs (PEP 3119) instead. **[considered-and-rejected]**. This is the strongest "Python looked at Perl 6" document. — [PEP 3133](https://peps.python.org/pep-3133/)
- **PEP 344, "Exception Chaining and Embedded Tracebacks"** (Ka-Ping Yee, created 12-May-2005, Superseded). It was superseded by **PEP 3134** (same author and text, status **Final**, Python-Version 3.0). Both cite Perl 6 RFC 88 in their Rationale: "For Perl 6, RFC number 88 proposes an exception mechanism that implicitly retains chained exceptions in an array named `@@`". They also note that in that RFC the most recently raised exception is exposed for matching, "as in this PEP". The reference is Tony Olensky's Perl 6 RFC 88 (http://dev.perl.org/perl6/rfc/88.html). The same rationale also surveys Java, Ruby, C#, etc. **[cited-as-prior-art]**. The feature shipped as `__context__`/`__cause__`/`raise ... from` in Python 3.0, but the PEP does not say the idea came *from* Perl 6. — [PEP 344](https://peps.python.org/pep-0344/); [PEP 3134](https://peps.python.org/pep-3134/)
- **PEP 502, "String Interpolation – Extended Discussion"** (Mike G. Miller, created 10-Aug-2015, Rejected). It shows a Perl 6 example `say "Hello { @names.join(', ') } how are you?"` labelled "# Perl 6 version" next to Perl 5, Bash and Ruby. It also notes that an earlier proposal's dollar signs "may have led it to resemble Python's arch-nemesis Perl". **[cited-as-prior-art]**, with Perl-likeness used as a negative. — [PEP 502](https://peps.python.org/pep-0502/)
- **PEP 3117, "Postfix type declarations"** (Georg Brandl, 01-Apr-2007, Rejected, an April-Fools PEP). It says the change "makes Python measure up to Perl 6, which already uses Unicode for its operators". Footnote: "Well, it would, if there was a Perl 6." **[joke]** / **[cautionary-tale]** about the Perl 6 timeline. — [PEP 3117](https://peps.python.org/pep-3117/)
- **Guido van Rossum, Artima weblog, 5 Apr 2006 ("Python 3000 – Adaptation or Generic Functions?").** About the Python 3000 process: "the goal is to avoid a repeat of Perl 6 :-)". **[cautionary-tale]**. — [Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=155123)
- **Guido van Rossum, Linux Journal interview by James Gray, 1 Oct 2008.** The interviewer suggested Python 3 was unusual in breaking compatibility. Guido replied "I think you may have forgotten about Perl 6" and "Perl 6 is a totally new language, with a completely different implementation." **[cautionary-tale/contrast]**. — [Linux Journal](https://www.linuxjournal.com/magazine/interview-guido-van-rossum)
- **LWN (Jonathan Corbet, 4 Sep 2006).** Python 3.0 "should, despite its rather later start, be in use well before Perl 6". This is a journalist's remark, not Guido's. — [LWN](https://lwn.net/Articles/198215/)
- **Secondhand summary of a 2011 Guido talk.** It contrasts perpetual backward compatibility (C++) with a rewrite from scratch (Perl 6). Snippet-level only, not verified verbatim. — [mvdirona.com](https://perspectives.mvdirona.com/2011/05/guido-van-rossum-21-years-of-python/)

**PHP (wiki.php.net RFCs)**
- **"Path to Saner Increment/Decrement operators"** (George Peter Banyard, 2022-11-21, v0.3). Vote 25–0, 28 Jun – 12 Jul 2023, status Implemented (PHP 8.3). It compares PHP's string increment with Raku's. It calls Raku "a PERL successor", shows that "the above snippet in Raku gives a consistent result", and states of its own proposal: "This follows Raku's behaviour." **[adopted-from]**. This is the clearest shipped "taken from Raku" statement found in any mainstream language's design record. — [PHP RFC](https://wiki.php.net/rfc/saner-inc-dec-operators)
- **"Traits for PHP"** (Stefan Marr, 2008-03-06, v1.5; superseded by "Horizontal Reuse", which shipped as traits in PHP 5.4). It lists "Perl 6, Squeak, Scala, Slate and Fortress" as languages that integrated traits, and again "Squeak, Perl6, Scala, Slate, Fortress and even for C#/Rotor". Its references are the Bern SCG traits papers (Schärli, Ducasse). **[cited-as-prior-art]**. The design lineage is the Schärli/Ducasse traits papers, and Perl 6 appears only as another adopter. — [PHP RFC: traits](https://wiki.php.net/rfc/traits)
- **"Partial Function Application" v2** (Paul Crovella, Levi Morrison, Joe Watkins, Larry Garfield; page dated 2020-04-22). Vote 29–20, short of the 2/3 needed, so **Declined**. It notes that "The notable exception is Raku (formerly known as Perl 6), which has an `assuming` method" that prefills arguments left to right. **[cited-as-prior-art]**, and the feature was not adopted. — [PHP RFC](https://wiki.php.net/rfc/partial_function_application)
- **"Pattern Matching"** (Larry Garfield, Ilija Tovilo; v0.9, in discussion per the fetched page). It defers range patterns: a future range RFC "should include a range pattern, possibly modeling on Raku or similar." **[cited-as-prior-art]** as a pointer for future work. — [PHP RFC](https://wiki.php.net/rfc/pattern-matching)
- **"Name of Next Release of PHP"** (Andrea Faulds, Zeev Suraski, July 2014; vote PHP 7: 58, PHP 6: 24). Among its reasons to skip version 6, the fetch tool rendered: "PHP 6 was a failure; Perl 6 was a failure." **[cautionary-tale]**. The wording should be re-checked against the page before verbatim use. — [PHP RFC: php6](https://wiki.php.net/rfc/php6)

**JavaScript (ES4 era / es-discuss / HOPL)**
- **Brendan Eich on es-discuss, "complexity tax" thread (circa 26 Mar 2008, during the ES4 fight):** "No one working on ES4 wants it to be like Perl 6." **[cautionary-tale]**. — [esdiscuss.org](https://esdiscuss.org/topic/complexity-tax)
- **Brendan Eich on es-discuss, 12 Aug 2014 ("Thread about ES6 on reddit"):** "I see Perl 6 (full of great ideas that required a compat break) vs. 5 in the same light." **[cautionary-tale]** about compatibility, which grants the quality of the ideas. — [esdiscuss.org](https://esdiscuss.org/topic/thread-about-es6-on-reddit)
- **Wirfs-Brock & Eich, "JavaScript: The First 20 Years" (HOPL IV, 2020), read in full via text extraction.** No occurrence of "Perl 6" or "Larry". It credits Perl for JS 1.2 array functions (`push`, `pop`, `shift`, `unshift`, `splice`), `String` `match`/`replace`/`substr`, and the fact that regex "syntax and semantics … was borrowed from Perl". All of that is Perl 5 or earlier and predates 2000. It credits Python for the ES4/ES2015 features Eich lobbied for ("iterators, generators, destructuring assignment, and array comprehensions"). The 1999 timeline asks whether regular expressions "should … include Perl 5 features?" — [HOPL JS PDF](https://www.cs.tufts.edu/~nr/cs257/archive/brendan-eich/js-hopl.pdf)
- No TC39 proposal citing Raku or Perl 6 as prior art turned up. Searches covered iterator-helpers, fromEntries, type-annotations and promise-try prior-art sections. — [search result: proposal-iterator-helpers #299](https://github.com/tc39/proposal-iterator-helpers/issues/299)

**Ruby (bugs.ruby-lang.org)**
- **Feature #12133, "Ability to exclude start when defining a range".** Nobuyoshi Nakada (nobu, core committer) wrote on 15 Mar 2016: "Maybe, neko operator in Perl6?" (Raku's `^..`). Status is Feedback/closed and Ruby has no start-exclusive range. Matz did not comment. **[considered-and-rejected / not adopted]**. — [Ruby #12133](https://bugs.ruby-lang.org/issues/12133)
- **Feature #16894, "Integer division for Ruby 3".** Shannon Skipper wrote on 17 May 2020: "Raku (Perl 6) and Clojure's default division to a rational seems really nice". No decision from Matz; the issue has been Assigned since Apr 2024. **[cited-as-prior-art]** by a community member. — [Ruby #16894](https://bugs.ruby-lang.org/issues/16894)
- Other tracker mentions surfaced only at search-snippet level and were not opened individually. Each was a commenter citing Perl 6 behaviour. None is shown as adopted. **[cited-as-prior-art]**.
  - #14777 Range#offset (Perl 6 range arithmetic) — [Ruby #14777](https://bugs.ruby-lang.org/issues/14777)
  - #21284 Array#pad ("^.. is so-called neko-operator in Perl 6") — [Ruby #21284](https://bugs.ruby-lang.org/issues/21284)
  - #7939 curry ("assuming" in Perl 6) — [Ruby #7939](https://bugs.ruby-lang.org/issues/7939)
  - #2034 ICU (Perl 6 optionally links ICU) — [Ruby #2034](https://bugs.ruby-lang.org/issues/2034)
  - #4085 refinements (Perl 6 roles vs `include`) — [Ruby #4085](https://bugs.ruby-lang.org/issues/4085)

**Swift (Swift Evolution, forums.swift.org)**
- **Joe Groff, "[Pitch] Regex Syntax", 3 Mar 2022:** "Raku (the former 'Perl 6') made `/x` mode the standard behavior for its pattern literals". He argued new string-processing designs should follow. **[cited-as-prior-art]**. — [Swift Forums](https://forums.swift.org/t/pitch-regex-syntax/55711)
  - Outcome in **SE-0354 Regex Literals** (Hamish Knight, Michael Ilseman, David Ewing; implemented in Swift 5.7). The final proposal mentions only Perl, never Raku. Multi-line `#/ … /#` literals get extended syntax: "`(?x)` is enabled" and it "may not be disabled". Non-semantic whitespace by default for single-line literals was considered and rejected ("would lose out on the familiarity and compatibility"). So Swift partially did what Raku does, but the final document does not attribute it. — [SE-0354](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0354-regex-literals.md)
- **Nate Cook, "[Pitch] Character Classes for String Processing", 19 Oct 2021.** Its "Broad language/engine survey" table has a Raku row: dot matches "Grapheme cluster", `\X` "n/a", "strings always normalized", `\d` matches FULL WIDTH digit "yes". **[cited-as-prior-art]** (survey). — [Swift Forums](https://forums.swift.org/t/pitch-character-classes-for-string-processing/52920)
  - Outcome in **SE-0363 Unicode for String Processing** (Nate Cook, Alejandro Alonso; Swift 5.7). The final proposal has no Raku or Perl 6 mention. Its comparison table lists "NSString, Java, Ruby, Perl", and its grapheme-cluster default `.` is argued from Swift's own `Character` model. — [SE-0363](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0363-unicode-for-string-processing.md)
- **SE-0355 Regex Syntax** (Knight, Ilseman): no Raku or Perl 6. It adopts a superset of PCRE2, Oniguruma, ICU and .NET syntax, and mentions Perl 5 behaviours six times. — [SE-0355](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0355-regex-syntax-run-time-construction.md)
- **Swift's "Declarative String Processing Overview"** has no Raku, Perl 6 or grammar mention. It mentions PEGs and Perl generically. — [DeclarativeStringProcessing.md](https://github.com/swiftlang/swift-experimental-string-processing/blob/main/Documentation/DeclarativeStringProcessing.md)
- **Swift String Manifesto** (Dave Abrahams, Ben Cohen, 2017) contains no Perl 6, Raku or NFG. Its benchmark is Perl: "Our goal is to be better at string processing than Perl!" — [StringManifesto.md](https://github.com/swiftlang/swift/blob/main/docs/StringManifesto.md)

**Rust**
- No Rust RFC or internals thread citing Perl 6 or Raku turned up in search (not exhaustive). — [search: none found]
- RFC 0002, "RFC process" (2014-03-11), points to Python rather than Perl 6 as the model for a stricter process: "we should likely look to Python's PEP process for inspiration." — [RFC 0002](https://rust-lang.github.io/rfcs/0002-rfc-process.html)

**Kotlin**
- **KEEP "open-ended ranges"** (`..<` / `rangeUntil`; Ilya Gorbunov et al.; implemented in 1.7.20-Beta). Its prior-art table lists Swift (`..<`), Ruby (`...`), Groovy (`..<`, `<..`, `<..<`) and Rust (`..=`). It does **not** mention Raku/Perl 6 `..^`/`^..^`. Relevant to `..<` folklore. — [Kotlin KEEP](https://github.com/Kotlin/KEEP/blob/master/proposals/open-ended-ranges.md)

**Julia**
- No Julia issue or paper citing Perl 6 or Raku turned up. The only hits were a Hacker News comment by Stefan Karpinski comparing Julia threading performance with Raku (a comparison, not influence), and Wikipedia's "Rational data type" contrasting Julia's `//` with Raku's default rationals. — [HN Julia v1.3 thread](https://news.ycombinator.com/item?id=21636661); [Wikipedia: Rational data type](https://en.wikipedia.org/wiki/Rational_data_type)

**Non-language bonus: PostgreSQL**
- On pgsql-hackers, 13 Jun 2011, Stephen J. Butler wrote about generalising `ANY()`/`ALL()` for non-commutative operators: "this is getting close to Perl 6 junctions/superpositions". **[cited-as-prior-art]** in discussion. — [pgsql-hackers via postgrespro](https://postgrespro.com/list/id/BANLkTinc+D64qc9rdAxMR1K=0v-MpF1Uig@mail.gmail.com)

### Inferences
- **Mentions by classification:**
  - **[adopted-from]:** PHP saner inc/dec (2023). This is the only explicit "follows Raku" statement in a shipped mainstream feature.
  - **[considered-and-rejected or not adopted]:** Python PEP 3133 roles (2007), Ruby `^..` (2016), PHP partial application (`assuming` cited, RFC declined 2021), Swift whitespace-insensitive single-line literals (Raku cited in the pitch; rejected for single-line, adopted for multi-line).
  - **[cited-as-prior-art]:** PEP 344/3134, PEP 502, PHP traits RFC, PHP pattern matching, Swift character-class survey, Ruby integer-division comment, pgsql-hackers junctions.
  - **[cautionary-tale]:** Guido (2006, 2008), Eich (2008, 2014), PHP 6 → 7 naming RFC (2014), PEP 3117 joke (2007), and Perl's own perlsyn (section 3).
- **Weight of the record.** The cautionary-tale mentions (the compatibility break and the long timeline) outnumber and outweigh the feature-adoption mentions. In design archives, Perl 6 functioned mostly as a negative reference for "second-system" rewrites.
- **Where positive citations come from.** Raku is cited positively mostly for semantics details where it made a clean, principled choice: string increment, start-exclusive ranges, default rationals, grapheme-level regex dot, `/x` by default, `assuming`. These are survey-table citations, not wholesale borrowings.
- **Python's exception chaining.** PEP 3134 is the closest Python comes to Perl 6 lineage in a shipped feature. Even there the wording is prior-art ("as in this PEP"), not derivation.

### Gaps
- **No exhaustive full-text grep was done** of these repositories: python/peps, rust-lang/rfcs, swiftlang/swift-evolution, Kotlin/KEEP, scala/improvement-proposals, openjdk JEPs, dotnet/csharplang, dart-lang/language, elixir-lang, apache/groovy, jashkenas/coffeescript, nim-lang/RFCs, crystal-lang, ziglang. Web search found **no** Perl 6/Raku mention in Java JEPs, C# design notes/csharplang, Scala SIPs, Dart, Elixir, Groovy, CoffeeScript, Nim, Crystal or Zig. Those are "not found", not "proven absent". A `git clone` plus `grep -ri "perl ?6\|raku\|rakudo\|larry wall\|apocalypse"` would settle it.
- **python-ideas/python-dev archives** were not searched beyond what surfaced; only Guido's blog and interview statements were found.
- **Matz:** no statement about Perl 6 was found, only his statements about Perl (5) as Ruby's model.
- **Eich blog phrase.** A search snippet attributed "Perl 6, which fearlessly revamps Perl's regular expression sub-language" to Brendan Eich's blog, but the post could not be located. Treat as unverified.
- **PHP RFCs not fetched:** the Horizontal Reuse RFC (the version that shipped traits) was not fetched to see whether it keeps the Perl 6 mention. The exact wording of the php6 naming RFC should be re-checked.

## 2. Wikipedia / Wikidata "Influenced" claims: what the citations actually say

### Takeaway
No language's Wikipedia or Wikidata record cites a primary source saying it was influenced by Perl 6/Raku. Raku's own "Influenced: Perl, Haskell, AntLang" rests on one 2010 Audrey Tang talk (Perl, Haskell) and nothing (AntLang). The Haskell→Raku link is cited to a Perl 6 wiki glossary page, which is circular.

### Cited Findings
- **Raku infobox (en.wikipedia), "Influenced by":** Perl, Ruby, Smalltalk, Haskell, JavaScript. Only Haskell has a footnote, [6]: "Glossary of Terms and Jargon", *Perl Foundation Perl 6 Wiki*, 28 Feb 2011 (archived). — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))
- **Raku infobox, "Influenced":** Perl [7], Haskell [7], AntLang (no footnote). Footnote [7] is Audrey Tang (唐鳳), "How to Implement Perl 6 in '10", 21 Apr 2010 (archived talk). The article body contains **no** prose sentence claiming Raku influenced any language. — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))
- **Haskell infobox, "Influenced":** includes Raku, with footnote [21] → the same "Glossary of Terms and Jargon" Perl 6 wiki page. The Haskell article's only Raku/Pugs sentence lists Pugs as a notable Haskell application. — [Wikipedia: Haskell](https://en.wikipedia.org/wiki/Haskell)
- **Perl infobox, "Influenced":** CoffeeScript [citation needed], Groovy [citation needed], JavaScript, Julia, LPC, PHP, Python, Raku, Ruby, PowerShell (no footnotes on these). The article says "Perl 5.10.0 included notable new features, which brought it closer to Perl 6" (switch `given`/`when`, regex updates, smartmatch `~~`), citing footnotes [54][55]. — [Wikipedia: Perl](https://en.wikipedia.org/wiki/Perl)
- **Wikidata, as of Nov 2026.** Querying for items with "influenced by" (P737) = Raku (Q2052676) returns **zero** items; no language on Wikidata records Raku as an influence. The inverse (Raku's own P737) lists Python, Haskell, Perl, Ada, Ruby, Smalltalk, APL, Common Lisp, Self, META II. — [Wikidata Q2052676](https://www.wikidata.org/wiki/Q2052676) (SPARQL run against query.wikidata.org)
- **Wikipedia "Gradual typing"** states "Raku (formerly Perl6) has had gradual typing implemented from the start", with no footnote. It does not claim Raku influenced Siek & Taha (2006) or any other gradual system. — [Wikipedia: Gradual typing](https://en.wikipedia.org/wiki/Gradual_typing)
- **Swift's own credit (Chris Lattner's homepage)** lists "Objective-C, Rust, Haskell, Ruby, Python, C#, CLU, and far too many others to list". Neither Perl nor Perl 6 is named. — [nondot.org/sabre](https://nondot.org/sabre/)

### Inferences
- **"Raku influenced Haskell" (Raku infobox).** At best this rests on Pugs being an early GHC-extension adopter and a feedback source (section 3). That is community and implementation influence, not language-design influence on Haskell. I found no Haskell design document (Haskell 2010 report, GHC proposals) that cites Perl 6.
- **"Raku influenced Perl".** This is real and documented in Perl's own docs: perlsyn on `given`/`when`/smartmatch, Moose, Corinna (section 3). It is the only "Influenced" entry with solid primary evidence.
- **Brief's examples (CoffeeScript, Groovy, Julia, PowerShell).** These list *Perl* (5) on Wikipedia, sometimes with [citation needed]. None lists Perl 6/Raku. Any blog claim that "X was influenced by Perl 6" for these languages is not backed by their Wikipedia infobox.

### Gaps
- The content of footnote [7] (Tang's 2010 talk) was not retrieved, so it is unknown what it says about Haskell.
- The "Glossary of Terms and Jargon" wiki page was not retrieved. It likely defines "lambdacamel" or "Pugs", which would make it weak support for a design-influence claim.
- Infoboxes of smaller languages (AntLang and the like) were not checked individually beyond the Wikidata query.

## 3. Influence through people and infrastructure (Pugs, Parrot, RFC process, Moose/Joose, regex tooling)

### Takeaway
The best-documented Perl 6 influence on the wider world runs through people and infrastructure, not syntax:
- **Pugs** is credited by the Haskell community's own HOPL history as an application that drew people *to* Haskell.
- **Parrot** is cited in the Lua 5.0 and "VM Showdown" papers as the register-VM decision that reopened the stack-vs-register debate.
- **The Perl 6 object model** produced Moose, and through Moose, Joose.
- **Perl 6 rules** produced Damian Conway's Regexp::Grammars.
- **The 2000 RFC process** is remembered mainly as a negative example, including by Perl's own successor process.

### Cited Findings

**Pugs → Haskell**
- **HOPL III, Hudak, Hughes, Peyton Jones & Wadler, "A History of Haskell: Being Lazy with Class" (2007), §12.3 "Open source: Darcs and Pugs".**
  - It says a turning point for a language comes when people learn it for its applications. It names "two open-source projects, Darcs and Pugs" as having "started to have that effect for Haskell".
  - It narrates that Audrey Tang learned Haskell via Darcs, then read Pierce's *TAPL*, "so Tang picked Perl 6". Perl 6 is described as "a ferociously difficult language to implement".
  - Figures given: Pugs started 1 Feb 2005, had 200 contributors a year later, and was about 18,000 lines of Haskell using parser combinators, GADTs and delimited continuations.

  This is a primary, peer-reviewed acknowledgment by Haskell's designers. — [HOPL III Haskell history PDF](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf)
- **Audrey Tang interview (Andrew Shitov, 5 May 2015; updated 2020).**
  - On Haskell: Pugs's main influence was "drawing people to learn Haskell", and "People who encountered Haskell through Pugs — such as Edward Kmett — would go on to vitalize the Haskell community".
  - On GHC: Pugs was an early adopter of several GHC extensions, "we provided feedback on GHC to make these features more solid".
  - "A lambdacamel is someone who identifies with both communities."
  - On Perl 6 design: Pugs clarified multi-method tie-breaking and the interaction of multis with junctions.
  - On Perl 5: Pugs influenced Perl 5's pluggable method-resolution order.

  — [andrewshitov.com](https://andrewshitov.com/2015/05/05/interview-with-audrey-tang/)
- **Wikipedia (Pugs):** Pugs targeted GHC, went on hiatus on 21 Apr 2010, and its contributors moved on to "Perl6-inspired systems as CPAN modules on Perl 5, such as the Moose project". — [Wikipedia: Pugs](https://en.wikipedia.org/wiki/Pugs_(compiler))

**Perl 6 object model → Moose (Perl 5) → Joose (JavaScript)**
- **Moose 0.18 POD (Stevan Little, 2007):** "Moose is based on the prototypes and experiments I did for the Perl 6 meta-model; however Moose is NOT an experiment/prototype, it is for real." **[adopted-from]**. — [Moose 0.18](https://metacpan.org/pod/release/STEVAN/Moose-0.18/lib/Moose.pm)
- **Later Moose docs (0.26 era)** reword this, per the search snippet, to say it is very much inspired by Perl 6 but is an OO system for Perl 5. — [Moose 0.26](https://search.cpan.org/~stevan/Moose-0.26/lib/Moose.pm)
- **Current Moose 2.4001** keeps only an acknowledgement: "I blame Audrey Tang for then encouraging my meta-model habit in #perl6." — [Moose (current)](https://metacpan.org/pod/Moose)
- **Joose manual (JavaScript, Nickolay Platonov; v3.013).** Joose "is based in large part on the Moose system, which in turn borrows a lot of from Perl 6 object system". Joose is a meta-object system with classes, roles (traits) and method modifiers; its 2.x line is listed as stable in 2009. This is a documented two-hop chain: Perl 6 → Moose → Joose. — [Joose Manual](https://www.openjsan.org/doc/s/sa/samuraijack/Joose/3013/lib/Joose/Manual.html); [GitHub Joose](https://github.com/Joose/Joose)

**Perl 6 → Perl 5 core (the main documented downstream language)**
- **perlsyn, "Switch Statements":** the construct "is loosely based on an old version of a Raku proposal, but it no longer resembles the Raku construct."
- **perlsyn, "Differences from Raku":** "The Perl 5 smartmatch and `given`/`when` constructs are not compatible with their Raku analogues". It adds that the smartmatch table differs because "the Raku spec has changed since Perl 5 rushed into early adoption."
- Classification: **[adopted-from]**, then **[cautionary-tale]**, in Perl's own documentation. — [perlsyn](https://perldoc.perl.org/perlsyn)
- **perl5100delta (2007)** does *not* attribute `say`, `state`, `given`/`when`, `//` or `~~` to Perl 6 in its text. Its only Perl 6 mentions are MAD (a planned Perl 5→Perl 6 converter) and `SVt_BIND` "in readiness for the project to implement Perl 6 on 5". In the regex section it compares recursion with PCRE and capture numbering with .NET, not with Perl 6. — [perl5100delta](https://perldoc.perl.org/perl5100delta)
- **perl5420delta (2025):** switch and smartmatch "were deprecated in Perl v5.38.0 and scheduled for removal in Perl v5.42.0", but "their removal has been indefinitely postponed". Switch is disabled in feature bundles from 5.35, and smartmatch is disabled from the 5.41 bundle. — [perl5420delta](https://perldoc.perl.org/perl5420delta)
- **Corinna (Perl 5.38 `class`/`field`/`method`):** search snippets only. Ovid's writing names "Raku, Moo/se, and yes, Java" as the biggest influences. Secondary commentary (heise) says the semantics are influenced more by Raku than Perl 5, and that the attribute syntax is copied from Raku. Not verified verbatim. — [Ovid: Corinna in the Perl core](https://ovid.github.io/articles/corinna-in-the-perl-core.html); [heise](https://heise.de/-9759590); [dev.to/ovid](https://dev.to/ovid/bringing-modern-oo-to-perl-51ak)

**Perl 6 rules → Perl 5 grammar tooling**
- **Regexp::Grammars (Damian Conway, v1.058).** Subrules are invoked by enclosing the name in angle brackets "(like in Perl 6)". The non-capturing subrule-call syntax is "stolen directly from Perl 6". **[adopted-from]**. — [Regexp::Grammars](https://metacpan.org/pod/Regexp::Grammars)
- **PCRE / PCRE2 / Oniguruma:** no evidence found of any feature with Perl 6 origin. In the Perl 5.10 era, regex features were compared with PCRE and .NET (perl5100delta, above). Swift's regex proposals cite PCRE2/Oniguruma/ICU/.NET/Perl, not Raku (section 1). Larry Wall himself says "everyone else did steal (resteal?) Perl 5 regexes" (section 4).

**Parrot (2001–2021) and register VMs**
- **Ierusalimschy, de Figueiredo & Celes, "The Implementation of Lua 5.0" (J.UCS 11(7), 2005).** It notes "a growing interest in register-based virtual machines". Its example is "the planned new virtual machine for Perl 6 (Parrot) will be register based", citing *Perl 6 and Parrot Essentials*. It then claims Lua 5.0 (2003) was the first register-based VM in wide use. **[cited-as-prior-art/context]**. There is no claim that Lua took the idea from Parrot. — [Lua 5.0 paper PDF](https://www.lua.org/doc/jucs05.pdf)
- **Shi, Casey, Ertl & Gregg, "Virtual Machine Showdown: Stack Versus Registers" (ACM TACO 4(4), 2008):** "The controversy between stack and register code has arisen again recently because of the decision to make the Parrot VM, the intermediate representation for the Perl 6 language, a register rather than stack machine". It cites Sugalski's YAPC 2002 "Parrot in detail" talk. The earlier VEE 2005 conference version yielded no Parrot mention in text extraction. **[cited-as-prior-art/motivation]**. — [TACO 2008 PDF](https://mural.maynoothuniversity.ie/10185/1/KC-Virtual-2008.pdf); [VEE 2005 PDF](https://usenix.org/legacy/events/vee05/full_papers/p153-yunhe.pdf)
- **Wikipedia (Parrot).**
  - The most complete implementations were Rakudo, Lua and Winxed. BASIC, PHP, Python and Ruby projects were started, plus esoteric languages and the "squaak" tutorial.
  - "None of these projects became the primary implementation of its language."
  - "In August 2021, it was announced that Parrot virtual machine was no longer being actively developed"; MoarVM had taken over the Perl 6 role.
  - The article claims inspiration flowed *into* Parrot (LLVM, Lua VM, Inferno's Dis) [citation needed], not out of it.

  — [Wikipedia: Parrot](https://en.wikipedia.org/wiki/Parrot_virtual_machine)
- **Parrot's own overview** states the register-architecture decision. Brent Dax's 2003 perl6-internals post gives speed and fewer push/pop opcodes as reasons. — [Parrot overview.pod](https://github.com/parrot/parrot.github.com/blob/56d82ad29c3f252795d8b61604b7e5ecadba5109/overview.pod); [perl6-internals 2003](https://groups.google.com/g/perl.perl6.internals/c/hJmH3ZKASMM/m/bFrgBzCc9S4J)

**The 2000 Perl 6 RFC process as a model?**
- **The process:** announced by Larry Wall at the State of the Onion on 19 Jul 2000, producing 361 RFCs that were answered by Apocalypses and Exegeses. — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))
- **Contemporary criticism:** Mark-Jason Dominus's "Critique of the Perl 6 RFC Process" (perl.com, Nov 2000) centres on the flippant attitude to implementation. Jarkko Hietaniemi's reply "Hold the Sackcloth and Ashes" agrees only partly and calls the process on the whole a success because it got ideas written down. — [MJD critique](https://www.perl.com/pub/2000/11/perl6rfc.html/); [Hietaniemi](https://perldotcom.perl.org/pub/2000/11/jarkko.html)
- **Larry Wall, Apocalypse 1 (Apr 2001):** "there was little coherent design to the suggestions in the RFCs". — [Apocalypse 1](https://www.perl.com/pub/2001/04/02/wall.html/)
- **Perl's own modern process (Perl/PPCs, docs/motivation.md)** lists among things to avoid "The Perl 6 RFC failure of 'no-one knew how to implement it'", linking MJD's critique. **[cautionary-tale]**. — [PPC motivation](https://github.com/Perl/PPCs/blob/main/docs/motivation.md)
- **Rust's RFC process (RFC 0002, 2014)** names Python's PEPs, not Perl 6 RFCs, as the inspiration for a stricter process. — [RFC 0002](https://rust-lang.github.io/rfcs/0002-rfc-process.html)

**Other**
- **Non-Raku adopters of junction-like ideas:** only the 2011 pgsql-hackers remark was found (section 1).

### Inferences
- **Pugs → Haskell** is the single most robust "Perl 6 influenced another language community" claim. It is acknowledged in the Haskell designers' own HOPL history. It was influence on Haskell's adoption and on GHC-extension hardening, not on Haskell's design.
- **Parrot's lasting footprint** is as a cited data point in the 2003–2008 register-VM literature: Lua 5.0 and the VM Showdown paper. No hosted language beyond Rakudo became primary.
- **The Moose → Joose chain** is documented at both hops, Moose 0.18 POD and the Joose manual. It is a genuine Perl 6 → other-language (JavaScript) transmission, though Joose itself was niche.
- **"Perl 6 RFCs → modern RFC processes" is unsupported.** Rust cites PEPs, and Perl's own PPC process cites the Perl 6 RFCs as what to avoid.
- **The regex claim is folklore so far.** "Perl 6 regex design influenced PCRE/Oniguruma" has no supporting evidence found. The documented regex influence is Perl 5 → everyone (JS per HOPL, Swift via PCRE2) and Perl 6 → Regexp::Grammars (Perl 5 CPAN).

### Gaps
- **NQP, MoarVM, 6model:** no evidence found of adoption or citation outside the Raku ecosystem, but no search was specific enough to rule it out. Candidate checks: Google Scholar citations of Worthington's 6model writings, and MoarVM mentions in VM papers.
- **Raku Weekly** was not searched for claims of MoarVM ideas copied elsewhere.
- **"Pugs and the lambdacamels" as a phrase:** not located beyond Tang's interview definition of "lambdacamel".
- **Damian Conway's Perl 6–derived CPAN modules** beyond Regexp::Grammars (Perl6::Rules, Perl6::Export::Attrs, Perl6::Builtins and others) were not individually verified.
- **Siek & Taha 2006** ("Gradual Typing for Functional Languages") could not be fetched (DNS failure on scheme2006.cs.uchicago.edu). Whether it mentions Perl 6 among languages with optional types is unverified, so do not repeat that claim.

## 4. Influence claims made by Raku/Perl 6 people themselves

### Takeaway
Larry Wall's recorded statements present Perl 6 as a borrower, and on other languages borrowing from it he deflects or jokes. I found no primary "X borrowed from Raku" article backed by the other language's own design record, other than the PHP and Swift items in section 1.

### Cited Findings
- **Larry Wall, Slashdot interview, published 14 Jul 2016.**
  - Concurrency: Perl 6 "borrows composable ideas from Haskell, Go, Erlang, and C#".
  - Asked what PHP should borrow: "If you borrowed enough features to make a difference and arrive at a consistent solution, you'd've just reimplemented pretty much all of Perl 6."
  - On regexes: "everyone else did steal (resteal?) Perl 5 regexes for their own language".

  — [Slashdot 2016](https://developers.slashdot.org/story/16/07/14/1349207/the-slashdot-interview-with-larry-wall)
- **A 2017 InfoQ talk summary (secondhand)** lists Perl 6 borrowings: Haskell (laziness), Smalltalk (traits), Go (promises/channels), C# (FRP). — [InfoQ](https://www.infoq.com/presentations/language-design-perl/)
- **A 2006 report on a State of the Onion talk** says Wall wanted Perl to resist "peer pressure" from faddish ideas in other languages (secondhand). — [route-fifty 2006](https://www.route-fifty.com/digital-government/2006/09/raising-kids-and-building-perl/318319/)
- **Searches found no "X borrowed from Raku" article** under the phrases "borrowed from Perl 6", "inspired by Perl 6", "stolen from Perl 6", "Raku had it first" and "Perl 6 had it first". Hits were community commentary (HN, Lobsters, dev.to) describing Raku as a *borrower*. One Lobsters commenter argues Raku's interesting features largely come from Common Lisp/Smalltalk; one blog calls junctions unique to Raku. — [HN](https://news.ycombinator.com/item?id=10637789); [Lobsters](https://lobste.rs/s/wexiaq/raku_is_pretty_damn_cool); [comp.programming.threads post](https://groups.google.com/g/comp.programming.threads/c/9BMbcklSZFs)

### Inferences
- **Claims that hold up:**
  - Perl 6 → Perl 5 (perlsyn; Moose; Regexp::Grammars; Corinna at snippet level)
  - Perl 6 → Haskell community (HOPL III)
  - Raku → PHP 8.3 inc/dec (RFC text)
  - Raku cited in Swift regex design discussion (forum, partial outcome)
- **Folklore unless a primary source turns up:**
  - "Raku influenced Swift's grapheme strings / ranges / regex"
  - "Raku influenced Kotlin `..<`" (the KEEP cites Swift, Ruby, Groovy, Rust)
  - "Perl 6 influenced JavaScript ES6" (HOPL JS credits Python)
  - "Perl 6 regex influenced PCRE"
  - "Raku influenced gradual typing research"

### Gaps
- Raku Weekly / Rakudo Weekly archives and "State of the Onion" transcripts (2000–2019) were not systematically searched for influence claims. A grep of the Weekly archive for "borrowed", "adopted" and "other languages" is the next step.
- The full Slashdot interview's last ~30% of text was not read.

## 5. Reverse direction: what Perl 6 imported after 2000 (to mark "Perl 6 borrowed it")

### Takeaway
Several Raku features that are cited as Raku firsts are documented imports: roles from the Squeak traits papers, Supplies from Rx, the concurrency model from Haskell/Go/Erlang/C#, and functional influences via the Haskell-written Pugs. Other languages adopting the same idea (traits in PHP/Scala, Rx everywhere) is therefore convergent descent from a common source, not Perl 6 influence.

### Cited Findings
- **Roles ← Squeak traits.** PEP 3133: traits were "originally called 'traits' and implemented for Squeak Smalltalk" and "since been adapted for use in Perl 6 where it is called 'roles'". The PHP traits RFC also cites the Bern SCG papers (Schärli PhD, Ducasse et al. TOPLAS), listing Perl 6 only as a fellow adopter. — [PEP 3133](https://peps.python.org/pep-3133/); [PHP RFC: traits](https://wiki.php.net/rfc/traits)
- **Supplies ← Rx.** Jonathan Worthington's 2013 article (CCR remaster) says reactive programming and "the Rx (Reactive Extensions) library" came first, and "In Raku, we're providing support for this in core" (search-snippet level). — [Raku Supplies article](https://raku.github.io/CCR/Remaster/Jonathan%20Worthington/Raku-Supplies-Reactive-Programming.html)
- **Concurrency ← Haskell, Go, Erlang, C#** (Larry Wall, 2016). — [Slashdot 2016](https://developers.slashdot.org/story/16/07/14/1349207/the-slashdot-interview-with-larry-wall)
- **Functional influences via Pugs.** Wikipedia (via HandWiki mirror) says Python and Ruby concepts were early influences, and that because Pugs was written in Haskell "many functional programming influences were absorbed by the Perl 6 design team". Tang says Pugs clarified multi-dispatch tie-breaking and junction interaction. — [HandWiki: Perl 6](https://handwiki.org/wiki/Perl_6); [Tang interview](https://andrewshitov.com/2015/05/05/interview-with-audrey-tang/)
- **Wikidata's "influenced by" for Raku:** Python, Haskell, Perl, Ada, Ruby, Smalltalk, APL, Common Lisp, Self, META II. — [Wikidata Q2052676](https://www.wikidata.org/wiki/Q2052676)
- **Junctions ↔ superpositions.** The pgsql-hackers remark calls them "Perl 6 junctions/superpositions". This reflects the junction design's ancestry in Damian Conway's Quantum::Superpositions module, which predates the Perl 6 design (unverified in this session; see Gaps). — [pgsql-hackers](https://postgrespro.com/list/id/BANLkTinc+D64qc9rdAxMR1K=0v-MpF1Uig@mail.gmail.com)

### Inferences
- **Treat as shared ancestry, not Perl 6 influence:**
  - traits/roles in PHP 5.4, Scala, Rust: all cite or descend from Schärli/Ducasse
  - Rx-style streams
  - async/await (C#)
  - PEG-style grammars (Swift and others cite PEGs generally)
- **Treat as distinctively Raku, and still not adopted elsewhere in documented form:** junctions, hyper-operators, the Whatever-star, NFG-normalised strings as the default, grammars as classes. Swift reaches grapheme-level matching independently and cites its own `Character` model.

### Gaps
- Synopsis S12/S14 text acknowledging the Schärli traits paper was not fetched.
- Quantum::Superpositions (Conway, 2000) as the source of junctions was not re-verified.
- Python/Ruby-sourced Perl 6 features (e.g. `zip`, decorators/traits syntax) were not individually traced.

## 6. Convergent evolution: what language-history sources credit as their sources

### Takeaway
The major language histories and acknowledgements credit Perl (5), Python, Haskell, Ruby, C# and research languages, not Perl 6. JavaScript's HOPL history credits Python for the ES4/ES2015 features that overlap with Perl 6 ideas (generators, destructuring, comprehensions). Swift lists Objective-C, Rust, Haskell, Ruby, Python, C# and CLU. Overlaps with Raku are best explained as convergent evolution or common ancestry.

### Cited Findings
- **HOPL IV, "JavaScript: The First 20 Years" (Wirfs-Brock & Eich, 2020).**
  - Perl (pre-2000) → JS 1.2 array and string functions and regex syntax and semantics.
  - Python → ES4 iterators, generators, destructuring and comprehensions ("directly modeled after equivalent Python features"), and ES2015 "iterators and generators inspired by Python but with significant differences".
  - No Perl 6 mention found in full-text extraction.

  — [HOPL JS PDF](https://www.cs.tufts.edu/~nr/cs257/archive/brendan-eich/js-hopl.pdf)
- **Swift (Chris Lattner):** drew ideas from "Objective-C, Rust, Haskell, Ruby, Python, C#, CLU, and far too many others to list". — [nondot.org/sabre](https://nondot.org/sabre/)
- **Swift String Manifesto:** the explicit benchmark is Perl, not Raku: "Our goal is to be better at string processing than Perl!" — [StringManifesto.md](https://github.com/swiftlang/swift/blob/main/docs/StringManifesto.md)
- **Kotlin `..<` KEEP** cites Swift, Ruby, Groovy and Rust ranges. — [Kotlin KEEP](https://github.com/Kotlin/KEEP/blob/master/proposals/open-ended-ranges.md)
- **Rust RFC process** cites Python's PEPs. — [RFC 0002](https://rust-lang.github.io/rfcs/0002-rfc-process.html)
- **HOPL III Haskell history:** Pugs appears as an application that drew people to Haskell. It does not appear as a design source. — [HOPL III PDF](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf)
- **Matz on Ruby** (Artima 2003 and other interviews, search-snippet level) credits Perl (5) heavily, plus Smalltalk and Lisp. He says he took far less from Python than from Perl. No Perl 6 statements were found. — [Artima: The Philosophy of Ruby](https://www.artima.com/intv/ruby.html)

### Inferences
- **Default reading of an overlap.** When a post-2000 language has a Raku-like feature, the documented source is usually Python, C#, Haskell, the Smalltalk traits papers, Rx or Perl 5. The default classification should be "convergent / common ancestor" unless one of the section 1 primary citations applies.
- **Perl 6's most-cited role in other languages' design records is as a counter-example of a compatibility-breaking rewrite.** Guido in 2006 and 2008, Eich in 2008 and 2014, the PHP 6→7 naming RFC in 2014, and Perl's own PPC process all use it that way.

### Gaps
- python-history.blogspot.com was not searched for Perl 6 mentions.
- HOPL IV papers other than JavaScript's were not grepped for "Perl 6"; the IV volume includes, among others, C++, Clojure, F#, Groovy, MATLAB, Objective-C and S/R.
- C# design notes, Java JEPs and Scala SIPs were not grepped.
