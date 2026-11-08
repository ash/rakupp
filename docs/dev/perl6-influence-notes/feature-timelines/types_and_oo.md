# Perl 6 / Raku type-system and object-model features in other languages after 2000

Scope note: the dates for Perl 6 come from its own design documents (Apocalypses and Synopses), checked in this pass. "Documented" means a primary design document of the other language names Perl 6 or Raku. Some secondary sources (search-result excerpts and Wikipedia) are marked as such. Some items are background knowledge that this pass did not re-verify; they are listed only under Gaps or marked "unverified".

## Q0. Scorecard: where does another language's design record actually cite Perl 6 for a type or OO feature?

### Takeaway
Only two non-Perl design documents were found that cite Perl 6 for a type or OO feature. Python PEP 3133 (2007) took roles directly from Perl 6, but it was rejected. PHP's traits RFC (2008) names Perl 6 only as an earlier adopter of the Traits paper. Perl 6 is also cited by the Traits authors (as an adopter) and on Jeremy Siek's gradual-typing explainer (as an upcoming language with optional annotations). The one clear, documented case of borrowing is Moose, which is in Perl 5. None of the shipped mainstream features checked (Python type hints, keyword-only and positional-only parameters, signature objects, PHP 7 types, Julia dispatch, Rust traits, Swift, the TypeScript null checks) cites Perl 6.

### Cited Findings
- **Python PEP 3133 "Introducing Roles"** (Collin Winter, created 1 May 2007, targeting Python 3.0, status Rejected). It says traits were "originally called “traits” and implemented for Squeak Smalltalk". It says the idea "has since been adapted for use in Perl 6, where it is called “roles”, and it is primarily from there that the concept is now being interpreted for Python 3." Its references include S12 and a Curtis Poe article on Perl 6 roles. Guido van Rossum's rejection says the PEP "helped push PEP 3119 towards a saner, more minimalistic approach" and that he prefers PEP 3119 (ABCs) — [PEP 3133](https://peps.python.org/pep-3133/)
- **PEP 3119 (ABCs)** was the alternative that won (GvR and Talin, 18 Apr 2007). It does not mention Perl, roles, PEP 3133 or traits — [PEP 3119](https://peps.python.org/pep-3119/)
- **PHP "Horizontal Reuse for PHP" RFC** (Stefan Marr, 2008-10-12, last updated 2011-01-09, "Implemented in PHP 5.4 (traits only)"). It names Perl 6 as a prior adopter: "integration in languages such as Perl 6, Squeak, Scala, Self, Slate and Fortress". It also says implementations for "Squeak, Perl6, Scala, Slate, Fortress and even for C#/Rotor ... are available". Its bibliography is the Bern SCG Traits material (SCG traits page, TOPLAS 2006 paper, Schärli PhD thesis) — [PHP RFC: horizontalreuse](https://wiki.php.net/rfc/horizontalreuse)
- **Moose (Perl 5; 0.01 released 2006-03-15)**. Its manual says: "Moose is based in large part on the Perl 6 object system, as well as drawing on the best ideas from CLOS, Smalltalk, and many other languages". It also says "Moose is based on the prototypes and experiments Stevan did for the Perl 6 meta-model", and that "we have "borrowed" features from Perl 6, CLOS (LISP), Smalltalk, Java, BETA, OCaml, Ruby and more" — [Moose::Manual](https://metacpan.org/pod/Moose::Manual); release date from [Moose on MetaCPAN](https://metacpan.org/pod/Moose)
- **The Traits authors cite Perl 6 as an adopter.** The TOPLAS 2006 paper (Vol. 28, No. 2, March 2006) says traits are "a standard feature of the statically typed language Scala ... and there is an implementation of Traits for Perl 5. Furthermore, there is ongoing work ... making traits a standard feature of the upcoming Perl 6 (under the name "roles")". This was read from the PDF text extracted locally — [Ducasse, Nierstrasz, Schärli, Wuyts, Black, TOPLAS 2006](https://scg.unibe.ch/archive/papers/Duca06bTOPLASTraits.pdf). The SCG project page likewise mentions "Roles, the variation of Traits that is planned as a fundamental language element of Perl 6" and "Stevan Little implemented Traits in Perl 5" — [SCG Traits page](https://scg.unibe.ch/research/traits)
- **Siek's "What is Gradual Typing"** lists "Common LISP, Dylan, Cecil, Visual Basic.NET, Bigloo Scheme, Strongtalk" as languages with optional annotations. It then names new languages in development, "such as Python 3k, the next version of Javascript (ECMAScript 4), and Perl 6", and says "Gradual typing is meant to provide a foundation for what these languages do". The page is undated; the Python 3k and ES4 wording suggests about 2007–08 — [Siek, What is Gradual Typing](https://jsiek.github.io/home/WhatIsGradualTyping.html)
- **Other PEPs that mention Perl 6, on non-type topics** (search index): PEP 3134 (exception chaining) cites Perl 6 RFC 88; PEP 3117 (an April-fool proposal for postfix type declarations) cites Perl 6's Unicode operators; PEP 502 shows a Perl 6 interpolation example — [search of peps.python.org](https://peps.python.org/pep-3134/), [PEP 3117](https://peps.python.org/pep-3117/), [PEP 502](https://peps.python.org/pep-0502/)
- **Checked and found NOT to mention Perl, Perl 6 or Raku:**
  - PEP 3102 — [link](https://peps.python.org/pep-3102/)
  - PEP 3107 — [link](https://peps.python.org/pep-3107/)
  - PEP 362 — [link](https://peps.python.org/pep-0362/)
  - PEP 570 — [link](https://peps.python.org/pep-0570/)
  - PEP 482 — [link](https://peps.python.org/pep-0482/)
  - PEP 483 — [link](https://peps.python.org/pep-0483/)
  - PEP 484 — [link](https://peps.python.org/pep-0484/)
  - PEP 3124 — [link](https://peps.python.org/pep-3124/)
  - Guido van Rossum's "Adding Optional Static Typing to Python", 23 Dec 2004 — [Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=85551)
  - Siek & Taha 2006 — [PDF](http://schemeworkshop.org/2006/13-siek.pdf)
  - Julia 2012 arXiv paper — [ar5iv](https://ar5iv.labs.arxiv.org/html/1209.5145)
  - Rust Reference "Influences" — [link](https://doc.rust-lang.org/reference/influences.html)
  - Chris Lattner's Swift influences — [nondot.org](https://nondot.org/sabre/)
  - TypeScript 2.0 release notes — [link](https://www.typescriptlang.org/docs/handbook/release-notes/typescript-2-0.html)
  - PHP 7 scalar types RFC — [link](https://wiki.php.net/rfc/scalar_type_hints_v5)
  - Clojure multimethods reference — [link](https://clojure.org/reference/multimethods)
- **Wikipedia (secondary).** "Gradual typing" lists "Raku (formerly Perl6) has had gradual typing implemented from the start", with no citation — [Wikipedia: Gradual typing](https://en.wikipedia.org/wiki/Gradual_typing). "Multiple dispatch" lists Raku with a citation to the "Perl 6 FAQ" (retrieved 2008) — [Wikipedia: Multiple dispatch](https://en.wikipedia.org/wiki/Multiple_dispatch)

### Inferences
- Where Perl 6 appears in other communities' records, it is cited as a peer or a data point: the Traits paper's adopter list, PHP's list of prior art, and Siek's list of languages that gradual typing should give a foundation to. It is not cited as the origin of a feature. The single explicit "taken from Perl 6" outside Perl, PEP 3133, was rejected.
- Perl 6's direct, documented influence on types and OO is mostly inside the Perl ecosystem: Moose and Class::MOP (2006), and through them Moo, Role::Tiny and Mouse. Those last three are inferred from the "Moose-family" lineage and were not checked separately.
- A theme across the features: the published Perl 6 designs (2001–2004) were early, but the ideas each have an older ancestor that the other languages cite instead. Those ancestors are the Traits paper, CLOS, Dylan and Cecil, Strongtalk and Common Lisp, ML option types and Spec#/Eiffel, and Ada and refinement types.

### Gaps
- No full-text search was done of the Swift Evolution, Kotlin KEEP, Rust RFCs, dotnet/csharplang or dart-lang/language repositories for "Perl 6" or "Raku", because cloning repositories was avoided. Only the specific documents listed above were checked.
- Some PEP results come from a site-restricted web search, which may not be exhaustive. A grep of the python/peps repository would settle it.

---

## Q1. Gradual / optional typing in a dynamic language

### Takeaway
Perl 6 promised optional types early (A6 in 2003 and the S02 drafts). Siek names it as one of the languages gradual typing was meant to provide a foundation for. But no gradually typed language checked here cites Perl 6 as a source. The documented lineages run through Common Lisp, Dylan, Cecil and Strongtalk (via Bracha), Thatte's quasi-static typing, Siek & Taha 2006, and then mypy.

### Cited Findings
**Perl 6 side**
- An early S02 draft says Perl 6 "will have an optional type system ... The compiler is free to infer what type information it can from the types you supply, but will not complain about missing type information unless you ask it to" — [S02, Perl6-Bible 0.22 on MetaCPAN](https://metacpan.org/pod/release/INGY/Perl6-Bible-0.22/lib/Perl6/Bible/S02.pod) (quote from search-result excerpt)
- A6 (Larry Wall, 7 Mar 2003) says formal parameters "may have any type that any other variable may have", and that a type system is "fine for Perl to have … as long as it’s optional" — [Apocalypse 6, perl.com](http://www.perl.com/pub/2003/03/07/apocalypse6.html/)

**The academic line**
- Siek & Taha, "Gradual Typing for Functional Languages", Scheme Workshop 2006. Local text extraction gives these passages (spacing restored):
  - "Common LISP [23] and Dylan [12, 37] are examples of such languages" (optional annotations used for performance)
  - "Languages that support gradual typing to a large degree include Cecil [8], Boo [10], extensions to Visual Basic.NET and C# proposed by Meijer and Drayton [26], and extensions to Java proposed by Gray et al. [17], and the Bigloo [6, 36] dialect of Scheme"
  - "Bracha cites Strongtalk [5] as an example of an optional type system"
  - The abstract credits Thatte's Quasi-Static Typing and Anderson & Drossopoulou.
  - There is no mention of Perl — [Siek & Taha 2006](http://schemeworkshop.org/2006/13-siek.pdf)
- Siek's explainer names Perl 6 alongside Python 3k and ES4 as upcoming languages with optional annotations (see Q0) — [Siek](https://jsiek.github.io/home/WhatIsGradualTyping.html)
- Wikipedia: "The term was coined by Jeremy Siek, who developed gradual typing in 2006 with Walid Taha." It cites earlier attempts by Thatte (1990) and Oliart (1994), and Bracha's 2004 "Pluggable Type Systems" — [Wikipedia: Gradual typing](https://en.wikipedia.org/wiki/Gradual_typing)

**Language-by-language**

| Language | Date | Documented inspiration | Perl 6 cited? | Source |
|---|---|---|---|---|
| Python (PEP 3107 annotations) | created 2 Dec 2006, Python 3.0 | References are only python-3000 threads, typecheck libraries and PyProtocols. "Python's 2.x series" is the only precedent. | No | [PEP 3107](https://peps.python.org/pep-3107/) |
| Python (Guido's blog) | 23 Dec 2004 | Java mentioned only for compiler and overloading contrasts | No | [Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=85551) |
| Python (PEP 484) | created 29 Sep 2014, Python 3.5 | "strongly inspired by mypy"; "taken from the idea of gradual typing" | No | [PEP 484](https://peps.python.org/pep-0484/) |
| Python (PEP 483) | created 19 Dec 2014 | Points readers to Siek's "What is Gradual Typing" | No (although that page itself names Perl 6) | [PEP 483](https://peps.python.org/pep-0483/) |
| Python (PEP 482 literature survey) | 8 Jan 2015 | Surveys mypy, Reticulated Python, PyCharm, ActionScript, Dart, Hack, TypeScript | No | [PEP 482](https://peps.python.org/pep-0482/) |
| Dart | announced Oct 2011 | Bracha: optional types are "a type assertion mechanism"; Strongtalk's optional type system was the precursor; 2004 "Pluggable Type Systems" | No | [InfoQ](https://www.infoq.com/news/2011/10/google-dart-language), [QCon slides](https://qconsf.com/sf2011/dl/qcon-sanfran-2011/slides/GiladBracha_DartInDepth.pdf), [Seth Ladd transcript](https://blog.sethladd.com/2011/11/transcription-of-quick-tour-of-dart-by.html?m=1), [Bracha 2004](https://www.cs.tufts.edu/~nr/cs257/archive/gilad-bracha/pluggableTypesPosition.pdf) (search summaries) |
| TypeScript | 0.8, 1 Oct 2012 | No primary design document found | Not found | Wikipedia mirror [TypeScript](https://en.wikipedia.com/wiki/TypeScript) (secondary) |
| Hack | 20 Mar 2014 | Co-designer Menghrajani says the team built "a gradual type system for PHP" | No | [FB Engineering](https://engineering.fb.com/2014/03/20/developer-tools/hack-a-new-programming-language-for-hhvm/), [Menghrajani](https://www.quaxio.com/on_the_design_of_hack) (search summaries) |
| PHP 7 scalar types | RFC v0.5.3, 2015-02-18, PHP 7.0 | Cites F#, Go, Haskell, Rust and Hack (strict typing); Java, D, Pascal (widening); C, C#, C++, VB (weak typing) | No | [PHP RFC scalar_type_hints_v5](https://wiki.php.net/rfc/scalar_type_hints_v5) |
| Ruby (Sorbet) | open-sourced 20 Jun 2019 | Inspiration not checked | Not checked | [Sorbet blog](https://sorbet.org/blog/2019/06/20/open-sourcing-sorbet) |
| Ruby (RBS / TypeProf) | Ruby 3.0.0, 25 Dec 2020 | Inspiration not checked | Not checked | [ruby-lang.org](https://www.ruby-lang.org/en/news/2020/12/25/ruby-3-0-0-released/) |
| Typed Racket, ActionScript, C# 4 `dynamic` | — | Listed by Wikipedia with citations to Tobin-Hochstadt & Felleisen, Rastogi et al. 2012, and MSDN | Not cited | [Wikipedia: Gradual typing](https://en.wikipedia.org/wiki/Gradual_typing) |

### Inferences
- **Verdicts**
  - Siek & Taha: independent formalization; Perl 6 is named on Siek's page as a target the theory serves.
  - Python (3107 / 484 / mypy): common ancestor (Siek & Taha via mypy). Perl 6 and Python 3k ran in parallel and were listed side by side by Siek, but no borrowing is documented.
  - Dart: common ancestor (Strongtalk, Bracha's pluggable types).
  - TypeScript: independent; no lineage documented in what was checked.
  - Hack: common ancestor (gradual typing literature).
  - PHP 7: independent; its stated precedents are Hack and statically typed languages.
  - Ruby RBS / Sorbet: independent, unverified.
  - Typed Racket: common ancestor (academic: soft typing and contracts).
  - C# 4 `dynamic`: common ancestor (Meijer & Drayton's "static typing where possible", cited by Siek & Taha).
- Perl 6's model (annotations add runtime-checked constraints, an unannotated variable means `Any`, and compile-time errors only when provable) is close to what Siek calls optional typing in the Common Lisp and Dylan sense. Siek's soundness-at-boundaries is a different property. It is fair to say Raku is "gradual" in the loose Wikipedia sense, not in the Siek–Vitousek sense of sound blame. This is an inference; the Wikipedia claim has no citation.

### Gaps
- Apocalypse 2 (2001) was not fetched in this pass; the S02-draft quote is from a search excerpt.
- The ES4 / ActionScript 3 (2006–08) design documents were not checked for Perl 6 mentions. Siek lists ES4 and Perl 6 as contemporaries.
- The Typed Racket (POPL 2008) and Strongtalk (Bracha & Griswold, OOPSLA 1993) papers were not checked for Perl mentions; a Perl mention in either is unlikely.
- Groovy's optional typing lineage was not checked.

---

## Q2. Roles / traits as composable units with conflict detection

### Takeaway
Here the direction of borrowing is documented, and it runs into Perl 6, not out of it. Apocalypse 12 (April 2004) says "we buy the argument of the Traits paper", and Perl 6 had been discussing "the smalltalk paper" since at least December 2003. The Traits authors (TOPLAS 2006) then listed Perl 6 roles as an adopter. Later trait systems (PHP, Java default methods, Scala and others) cite the Traits paper or mixins, not Perl 6. The exceptions are PEP 3133 (rejected) and Moose.

### Cited Findings
**Perl 6 dates and its own citations**
- A12 (Larry Wall; created 13 Apr 2004, last modified 4 Dec 2004; perl.com 16 Apr 2004):
  - "we buy the argument of the Traits paper", namely that "classes should not be used both to manage objects and to manage code reuse"
  - Conflict detection: "If two methods of the same name are composed into the same class, the ambiguity will be caught."
  - "The Traits paper recommends providing ways of renaming or excluding one or the other of the conflicting methods."
  - Naming joke: "role" is short for "role model", "trait" for "traitor"
  - Sources: [A12 POD (Perl6-Bible)](https://web.do.metacpan.org/release/INGY/Perl6-Bible-0.20/source/lib/Perl6/Bible/A12.pod), [perl.com A12](https://www.perl.com/pub/2004/04/16/a12.html)
- The perl6-language "Vocabulary" thread (12–13 Dec 2003; Luke Palmer, Larry Wall, Stéphane Payrard and others):
  - Payrard: "the smalltalk paper ... said that roles were stateless"
  - Wall: "Though they did point out that state was one of the thing they wanted to look into", and "A role can also supply one or more attributes"
  - This shows Perl 6 roles were stateful from the start, departing from the paper — [Google Groups archive](https://groups.google.com/g/perl.perl6.language/c/kuxrQlzguIo)
- chromatic (2009): "When we released Apocalypse 12 in 2004, we included a new feature called roles. The design borrowed from a Smalltalk idea called Traits" — [The Why of Perl Roles](https://modernperlbooks.com/mt/2009/04/the-why-of-perl-roles) (search-result excerpt)

**Traits origin**
- Schärli, Ducasse, Nierstrasz, Black, "Traits: Composable Units of Behaviour", ECOOP 2003. Wikipedia instead says "Traits originate in the programming language Self", citing Curry et al. 1982 — [Wikipedia: Trait](https://en.wikipedia.org/wiki/Trait_(computer_programming))
- The PHP RFC says the semantics "has been fully defined at first in 2003" — [PHP RFC](https://wiki.php.net/rfc/horizontalreuse)
- TOPLAS 2006 lists Scala (citing Odersky et al. 2004), Perl 5 and Perl 6 as adopters. It also cites Mohnen 2002 as "an extension of Java that allows interfaces to have default implementations" — [TOPLAS 2006](https://scg.unibe.ch/archive/papers/Duca06bTOPLASTraits.pdf)

**Other languages**

| Language / feature | Date | Documented inspiration | Perl 6 cited? | Source |
|---|---|---|---|---|
| Squeak traits | 2002–03 | Origin | n/a | [SCG](https://scg.unibe.ch/research/traits) |
| Scala traits | public on JVM Jan 2004 (developed from 2001); v2 Mar 2006 | "modular mixin composition combines the advantages of mixins and traits" (overview, LAMP-REPORT-2006-001) | Not found (PDF only partly readable) | [Scala Overview](https://www.scala-lang.org/docu/files/ScalaOverview.pdf) |
| Python PEP 3133 roles | 1 May 2007, rejected | Squeak traits via Perl 6 roles | **Yes, as the direct source** | [PEP 3133](https://peps.python.org/pep-3133/) |
| PHP traits | RFC 2008; shipped in PHP 5.4 | Bern SCG Traits papers | Yes, as a prior adopter | [PHP RFC](https://wiki.php.net/rfc/horizontalreuse) |
| Java 8 default methods | strawman May 2010; FOOL 2011; Java 8 (2014) | Goetz/Buckley: a "limited form of stateless traits"; full traits were "too big a change"; conflicting defaults must be resolved explicitly | No | [FOOL 2011 abstract](https://person.dibris.unige.it/zucca-elena/FOOL2011/goetz-abstract.html), [slides](https://person.dibris.unige.it/zucca-elena/FOOL2011/goetz-slides.pdf) (search summaries; direct fetch failed), [lambda-dev strawman](https://mail.openjdk.org/pipermail/lambda-dev/2010-May/001308.html) |
| Groovy traits | 2.3 (2014, per Wikipedia) | Conflict rule: "the method from the last declared trait in the implements clause wins"; explicit `Trait.super.foo` is optional; no inspiration stated | No | [Groovy docs](https://groovy-lang.org/objectorientation.html), [Wikipedia: Trait](https://en.wikipedia.org/wiki/Trait_(computer_programming)) |
| Kotlin interfaces with bodies | the `trait` keyword was renamed `interface` in M12 (2015) | Not stated | No | [Wikipedia: Trait](https://en.wikipedia.org/wiki/Trait_(computer_programming)) |
| Rust traits | 2010s | "Haskell (GHC): typeclasses, type families" | No | [Rust Reference: Influences](https://doc.rust-lang.org/reference/influences.html) |
| Swift protocol extensions | 2015 | Lattner: ideas from "Objective-C, Rust, Haskell, Ruby, Python, C#, CLU" | No | [nondot.org](https://nondot.org/sabre/), [Wikipedia: Trait](https://en.wikipedia.org/wiki/Trait_(computer_programming)) |
| C# default interface methods | C# 8.0 | Not checked | Not checked | [Wikipedia: Trait](https://en.wikipedia.org/wiki/Trait_(computer_programming)) |
| Fortress traits | ~2006 | Not checked | Not checked | Listed by the [PHP RFC](https://wiki.php.net/rfc/horizontalreuse) and [Wikipedia](https://en.wikipedia.org/wiki/Trait_(computer_programming)) |
| Moose roles (Perl 5) | 2006 | "based in large part on the Perl 6 object system" | **Yes** | [Moose::Manual](https://metacpan.org/pod/Moose::Manual) |

### Inferences
- **Who cites whom.** Perl 6 cites the Traits paper (A12, and the 2003 thread). The Traits authors cite Perl 6 as an adopter (TOPLAS 2006, SCG page). So each side knew of the other, and the ideas flowed Traits → Perl 6.
  - Perl 6's own contributions to the concept are stateful roles (Dec 2003), runtime mixing-in of roles (Q8), and roles as types and parametric roles. The 2007 "Stateful Traits" work by Bergel et al., cited in the PHP RFC, may parallel the first; whether it cites Perl 6 was not checked.
- **Verdicts**
  - Scala: common ancestor (mixins, as in Bracha & Cook, plus Schärli traits). Linearization is not Schärli-style conflict detection.
  - PHP: common ancestor (Traits paper). Perl 6 is mentioned only as a precedent.
  - Java 8: common ancestor (the Traits paper's vocabulary, plus Mohnen 2002). Explicit resolution of conflicting defaults matches the "conflicts must be resolved" principle in both Raku and the paper.
  - Groovy: independent or Scala-like. Last-declared-wins is mixin linearization, not trait conflict detection.
  - Kotlin, Swift, C# 8: independent (interface or protocol lineage).
  - Rust: common ancestor (Haskell type classes).
  - Python: documented borrowing from Perl 6, but rejected.
  - Moose: documented borrowing from Perl 6.
- **Possible error in Wikipedia.** Its "originate in Self" claim cites a 1982 paper, which predates Self (1987). The 1982 Curry et al. "Traits" is the Xerox Star / Mesa work. This is an inference; treat Wikipedia's origin sentence as unreliable.

### Gaps
- Schärli's 2005 PhD thesis and the ECOOP 2003 paper itself were not checked for Perl mentions; ECOOP 2003 predates A12, so a mention is unlikely.
- Fortress's own statement of where its traits come from was not checked.
- The Scala overview's bibliography could not be extracted, so whether it cites Schärli et al. directly is unverified.
- PHP's actual conflict semantics (fatal error, `insteadof` / `as`) were not verified from the RFC text in this pass.

---

## Q3. Multiple dispatch / multimethods

### Takeaway
Perl 6 multis (A6, March 2003; A12, 2004) name no inspiration in the text checked. The languages that adopted multiple dispatch after 2000 cite CLOS, Dylan, Cecil and Diesel (Julia), or nothing at all (Clojure's reference page, C# `dynamic`, Groovy). Julia's co-creator explicitly denies even CLOS as a motivation. No document checked names Perl 6.

### Cited Findings
- A6 (7 Mar 2003) defines multimethods as "dispatched based on the types of one or more of its arguments" and names no inspiration — [A6](http://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- A12 says: "With traditional multimethods, all methods live in the same global namespace". No CLOS or Dylan by name — [A12 POD](https://web.do.metacpan.org/release/INGY/Perl6-Bible-0.20/source/lib/Perl6/Bible/A12.pod)
- **Julia 2012 paper** (Bezanson, Karpinski, Shah, Edelman):
  - "Generic functions have appeared in several object systems in the past, notably CLOS", with Dylan cited in the same sentence
  - Julia sits with Cecil and Diesel but "uses generic functions as its primary abstraction mechanism"
  - It avoids the "overly permissive" features of CLOS
  - `Type{T}` is likened to CLOS eql specializers
  - No mention of Perl — [ar5iv 1209.5145](https://ar5iv.labs.arxiv.org/html/1209.5145)
- Stefan Karpinski on julia-dev (29 Jun 2013): "being like CLOS was not a motivation". He adds that Julia's similarities with Fortress "stem from the two languages trying to solve the same problems, not from direct inspiration either way" — [julia-dev thread](https://groups.google.com/g/julia-dev/c/NJ-b_KYZaIA)
- The Julia manual says Julia "borrows much from popular dynamic languages, including Lisp, Perl, Python, Lua, and Ruby". This is generic; it is not about dispatch and does not specify Perl 6 — [Julia docs](https://docs.julialang.org/en/v1/)
- "Fast Flexible Function Dispatch in Julia" (2018) classifies Cecil and Fortress as symmetric dispatch, and CLOS and Dylan as asymmetric (left-to-right). It calls Fortress the closest relative — [arXiv 1808.03370](https://arxiv.org/pdf/1808.03370) (search summary)
- **Clojure.** The reference page describes "a multimethod system that supports dispatching on types, values, attributes and metadata of, and relationships between, one or more arguments". It does not mention CLOS — [clojure.org multimethods](https://clojure.org/reference/multimethods)
- **C#.** "introduced support for dynamic multimethods in version 4" (April 2010) via `dynamic` — [Wikipedia: Multiple dispatch](https://en.wikipedia.org/wiki/Multiple_dispatch)
- **Groovy.** "the actual method invoked is determined dynamically based on the run-time type of methods arguments" — [Groovy docs](https://groovy-lang.org/objectorientation.html)
- **Nim.** Multi-methods are built in up to v0.19; from v0.20 they need a compiler flag — [Wikipedia: Multiple dispatch](https://en.wikipedia.org/wiki/Multiple_dispatch)
- **Theory and usage data.**
  - "The theory of multiple dispatching languages was first developed by Castagna et al."
  - Muschevici et al. 2008 studied CLOS, Dylan, Cecil, MultiJava, Diesel and Nice. They found 2.7–6.5% of generic functions dispatch on more than one argument.
  - Julia's standard library averages 5.86 methods per generic function, against 2.33 for Cecil — [Wikipedia: Multiple dispatch](https://en.wikipedia.org/wiki/Multiple_dispatch)
- **Python PEP 3124** (Phillip Eby, 28 Apr 2007, Deferred) cites "method combining (ala CLOS and AspectJ)" and Haskell type classes; no Perl — [PEP 3124](https://peps.python.org/pep-3124/)

### Inferences
- **Verdicts**
  - Julia: common ancestor (CLOS, Dylan, Cecil, Diesel), with parallel evolution alongside Fortress. Its generic mention of Perl in the manual is not evidence of a Perl 6 influence on dispatch.
  - Clojure: common ancestor (Lisp generic functions), though the reference page does not say so.
  - C# 4: independent (an emergent property of runtime overload resolution in the DLR).
  - Groovy: independent (Java overload rules applied at run time).
  - Nim: unknown.
  - Python PEP 3124: common ancestor (CLOS).
- Perl 6 itself descends from the CLOS, Dylan and Cecil line. Damian Conway's Perl 5 Class::Multimethods (about 1999–2000) is a plausible intra-Perl precursor, but A6's text as fetched does not credit it (see Gaps).

### Gaps
- Rich Hickey's HOPL IV "A History of Clojure" (2020) could not be fetched (ACM returned 403), so Clojure's own statement of where multimethods came from is unverified — [DOI](https://doi.org/10.1145/3386321)
- A6 and A12 were read only in part, because of truncation; a credit to Class::Multimethods or CLOS elsewhere in them cannot be ruled out.
- The Nim and Fortress inspiration statements were not checked.

---

## Q4. Definedness / nullability in the type (Raku `:D` / `:U` vs. option and nullable types)

### Takeaway
Raku's type smileys (`Int:D`, `Int:U`, `Int:_`) do not belong to the 2001–2004 design period as far as could be established. They are in S12 v134 (2014) and in RT tickets from 2012; their exact introduction date was not found. The mainstream null-safety wave descends from ML option types, Hoare's "billion-dollar mistake" framing, Fähndrich–Leino and Spec# (2003–04), Eiffel attached types (2005) and Ceylon (2011). None of these cites Perl 6.

### Cited Findings
- S12 (created 27 Oct 2004; v134, last modified 26 Oct 2014) contains:
  - "Int:D       Allow only defined (concrete) Int values"
  - "Int:U       Allow only undefined (abstract) Int values"
  - "Int:_       Allow either defined or undefined Int values"
  - There is no `:T` and no older `:defined` spelling in that version — [S12-objects.pod](https://raw.githubusercontent.com/Raku/old-design-docs/master/S12-objects.pod)
- RT #114442 (2012) quotes the spec as saying the smileys check `.DEFINITE`, not `.defined`, and says `:T` was removed from the spec — [RT 114442](https://rt.perl.org/rt3/Public/Bug/Display.html?id=114442) (search summary)
- Hoare (2009 talk) traces the null reference to ALGOL W (1965), "my billion-dollar mistake". Wikipedia also says:
  - Meyer coined "void safety", with attached types presented at ECOOP 2005
  - Eiffel void safety arrived in EiffelStudio 6.1, with modern syntax in 6.4
  - Ceylon had type-based null safety via union types "since 2011", and TypeScript followed — [Wikipedia: Void safety](https://en.wikipedia.org/wiki/Void_safety)
- ECMA-367 (Eiffel) was first approved June 2005, second edition June 2006 — [ECMA-367 2nd ed.](https://www.ecma-international.org/wp-content/uploads/ECMA-367_2nd_edition_june_2006.pdf) (search summary)
- Fähndrich & Leino (OOPSLA 2003) proposed non-null types for C#/Java-like languages. Spec# made references non-null with a `!` marker — [Fähndrich & Leino PDF](https://web.eecs.umich.edu/~bchandra/courses/papers/Fahndrich_NonNull.pdf), [Spec# retrospective](https://alastairreid.github.io/RelatedWork/papers/barnett:cacm:2011) (search summaries)
- **Swift (2014).** Lattner's stated influences are Objective-C, Rust, Haskell, Ruby, Python, C# and CLU; no Perl — [nondot.org](https://nondot.org/sabre/). The Rust reference credits Swift for "optional bindings" (`if let`) — [Rust influences](https://doc.rust-lang.org/reference/influences.html)
- **Kotlin.** Project Kotlin was unveiled July 2011; 1.0 shipped 15 Feb 2016 — [JetBrains blog](https://blog.jetbrains.com/kotlin/2016/02/kotlin-1-0-released-pragmatic-language-for-jvm-and-android/), [InfoQ](https://www.infoq.com/news/2016/02/kotlin-10-released) (search summaries)
- **TypeScript 2.0 (2016).** `--strictNullChecks` "switches to a new strict null checking mode"; no inspiration is named — [TS 2.0 release notes](https://www.typescriptlang.org/docs/handbook/release-notes/typescript-2-0.html)
- **C# 8 nullable reference types.**
  - Prototype announced Nov 2017 — [Microsoft DevBlogs](https://devblogs.microsoft.com/dotnet/nullable-reference-types-in-csharp/)
  - A User Voice request had been filed in 2011 — [InfoQ](https://www.infoq.com/news/2017/11/c-sharp-nullable-references/)
  - No primary source checked mentions Spec#.
- **Dart sound null safety.** Shipped in Dart 2.12 on 3 March 2021 — [Announcing Dart 2.12](https://dart.cn/blog/announcing-dart-2-12) (dart.cn mirror of the official blog). This contradicts Wikipedia's "Dart 2.0, August 2018" — [Wikipedia: Void safety](https://en.wikipedia.org/wiki/Void_safety)

### Inferences
- **Verdicts.** None of Swift, Kotlin, TypeScript, C# 8 or Dart cites Perl 6.
  - Swift: common ancestor (ML / Haskell option types).
  - Kotlin: common ancestor (nullable-type line: Eiffel, Spec#); Kotlin's own sources were not checked.
  - TypeScript 2.0: common ancestor (union types, the Ceylon/Flow era).
  - C# 8: common ancestor (Spec# / Fähndrich–Leino is the natural in-house precursor, but no checked source states it).
  - Dart: independent / common ancestor.
- The semantic analogy is weak anyway. Raku's `:D` / `:U` divides type objects from instances; it is a definedness constraint checked by the binder, not a sum type and not a flow-sensitive nullability analysis. "Perl 6 had it first" is not a defensible claim. The general Perl 6 idea of undefined, typed "protoobjects" may be early (A12), but the smiley syntax appears no earlier than about 2010–2012 on the evidence found.

### Gaps
- When `:D` / `:U` first appeared in the specs or in Rakudo (likely around the 2010–2011 "nom" era) is unconfirmed. A `git log -S':D'` on Raku/old-design-docs would settle it.
- Kotlin's own design statement on null safety was not fetched.
- Whether the C# design notes or the csharplang proposal cite Spec# was not checked.

---

## Q5. Subset / refinement types (`subset Even of Int where * %% 2`)

### Takeaway
Perl 6 had runtime-checked predicate subtypes in A12 (April 2004), originally spelled `subtype`. They became `subset` by S12. The closest mainstream analog is Ada 2012's Dynamic_Predicate / Static_Predicate (AI05-0153; implemented in GNAT around 2010). It comes from Ada's own constrained-subtype and contract tradition, with no evidence of contact with Perl. Static refinement types (Liquid Haskell, F*) are a separate academic line.

### Cited Findings
- **A12 (2004)** says "a subtype is something that takes away capabilities". Example: `type Str_not2b ::= Str where /^[isnt|arent|amnot|aint]$/;`, later updated to `my subtype Str_not2b of Str where ...` — [A12 POD](https://web.do.metacpan.org/release/INGY/Perl6-Bible-0.20/source/lib/Perl6/Bible/A12.pod)
- **S12 (v134, 2014)** uses `subset` throughout, for example `my Str subset Str_not2b where ...`, "which is why we use the C<subset> keyword for it" — [S12](https://raw.githubusercontent.com/Raku/old-design-docs/master/S12-objects.pod)
- **Ada 2012 subtype predicates.**
  - Normative text: ARM 3.2.4 — [ARM 3.2.4](https://www.adaic.org/resources/add_content/standards/12rm/html/RM-3-2-4.html)
  - Source AI: AI05-0153, according to the annotated ARM — [AARM 3.2.4](https://ada-rapporteur-group.github.io/ARM/Ada_202Y/AA-3-2-4.html)
  - GNAT patch "completing predicates as described in AI05-0153-1/06", Oct 2010 — [gcc-patches](https://gcc.gnu.org/ml/gcc-patches/2010-10/msg01914.html)
  - Static predicates exist so the subtype can be used in case choices and for-loops; dynamic predicates may call user functions — [AdaCore Gem 147](https://www.adacore.com/gems/gem-147-superbtypes-in-ada-2012-part-2)
  - Bob Duff (author of the AI) "preferred to have a single aspect Predicate" — [comp.lang.ada](https://usenet.ada-lang.io/comp.lang.ada/lqk4jq$pea$1@loke.gir.dk/t) (search summaries)
- **Moose (2006).** Its manual describes it as "based in large part on the Perl 6 object system" — [Moose::Manual](https://metacpan.org/pod/Moose::Manual). Its `subtype ... as ... where` type-constraint DSL is the obvious descendant of A12's `subtype ... of ... where`; this is an inference from syntax, not a specific citation.

### Inferences
- **Verdicts**
  - Ada 2012: independent / common ancestor (Ada 83 range subtypes and Ada 2012's contract aspects). The design discussions that were checked mention no outside language.
  - Liquid Haskell and F*: independent; academic refinement-type lineage (Freeman & Pfenning 1991, unverified here).
  - TypeScript: no refinement types; branded types and type guards are a different mechanism. Not sourced in this pass.
  - Moose: documented general borrowing from Perl 6, and very probably this feature specifically.
- Raku's `subset` is a nominal predicate subtype checked at bind and assign time. It is closest to Ada's Dynamic_Predicate and to PVS-style predicate subtypes (an older ancestor, unverified here), not to SMT-checked refinement types.

### Gaps
- The Liquid Haskell, F* and PVS predicate-subtyping papers were not fetched.
- The Ada 2012 Rationale's own statement of inspiration (chapter 3, "Subtype predicates") was not fetched.

---

## Q6. Coercion types (`Int(Str)`), type captures, parametric roles vs. generics

### Takeaway
None of these features was shown to be both early (2001–2004) and borrowed by another language. Coercion types do not appear in S12 even at v134 (2014), so they are a later Raku feature. Mainstream generics descend from CLU/ML/Ada and GJ/Pizza, not from Perl 6.

### Cited Findings
- S12 v134 contains no `Int(Str)` coercion type. It does have enum coercion ("The enumeration typename itself may be used as a coercion operator") and an `as` "coercion type of the method" in the method descriptor — [S12](https://raw.githubusercontent.com/Raku/old-design-docs/master/S12-objects.pod)
- PEP 484 says its generic syntax is "used in the same fashion in other languages from Pascal to Swift" and is "common practice in languages with generics (e.g. Java, TypeScript)", with Scala cited for square brackets — [PEP 484](https://peps.python.org/pep-0484/)

### Inferences
- **Verdict.** Generics everywhere are independent of Perl 6, with ancestors in CLU, ML and Ada and, for Java and C#, GJ/Pizza and Kennedy & Syme (background knowledge, not verified here). Coercion types have no clear mainstream equivalent; implicit conversions in Scala and C# are a different mechanism. No influence claim is supportable.

### Gaps
- The date Rakudo or the spec gained `Int(Str)` coercion types was not found (probably the 2010s).
- Parametric roles (`role Foo[::T]`) and type captures were not traced in the Apocalypses or Synopses in this pass.

---

## Q7. Rich signatures: named-only and positional-only parameters, slurpies, destructuring, introspection objects

### Takeaway
Perl 6 had named-only (`+$x`) and slurpy (`*@x`) parameters in A6 (March 2003). The corresponding Python proposals came later: PEP 3102 for keyword-only (April 2006), PEP 362 for Signature objects (2006, shipped in 3.3), and PEP 570 for positional-only (2018, shipped in 3.8). None of them mentions Perl or any other language as precedent, except PEP 570, which mentions C. Coincidence of timing is not evidence.

### Cited Findings
- A6 (7 Mar 2003): "Parameters marked with a `+` zone marker are named-only parameters", and "“Slurpy” parameters are denoted by a unary `*` before the variable name" — [A6](http://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- **PEP 3102 "Keyword-Only Arguments"** (Talin, created 22 Apr 2006, Final): no other language mentioned — [PEP 3102](https://peps.python.org/pep-3102/)
- **PEP 362 "Function Signature Object"** (Brett Cannon, Jiwon Seo, Yury Selivanov, Larry Hastings; created 21 Aug 2006; Python 3.3; Final): no other language as precedent, only C built-ins in passing — [PEP 362](https://peps.python.org/pep-0362/)
- **PEP 570 "Positional-Only Parameters"** (Larry Hastings, Pablo Galindo, Mario Corchero, Eric Vander Weele; created 20 Jan 2018; Python 3.8): the only other language named is C ("The `/` syntax has already been introduced for C functions") — [PEP 570](https://peps.python.org/pep-0570/)
- **PEP 3107** (function annotations, 2006): no other language mentioned — [PEP 3107](https://peps.python.org/pep-3107/)

### Inferences
- **Verdict for Python.** Keyword-only, positional-only and Signature objects are independent inventions. Python evolved them from its own `*args` / `**kwargs`, from C-level `PyArg_ParseTuple` conventions (the `/` came from Argument Clinic), and from `inspect.getargspec`.
  - PEP 3102 appeared three years after A6, in the same Python 3000 period when PEP 3133 (2007) did cite Perl 6. So Python's designers were reading Perl 6 documents then. This is circumstantial at most for keyword-only arguments: "plausibly influenced (circumstantial only)" is the strongest possible label, and "independent" is the better-supported one.
- Ruby 2.0 keyword arguments (2013), ES2015 rest and destructuring parameters, and Swift and Kotlin named arguments were not checked. Smalltalk and Objective-C keyword messages and Ada and Common Lisp named and `&key` parameters are pre-2000 ancestors (background knowledge).

### Gaps
- The python-3000 mailing-list threads behind PEP 3102 (2006) were not searched for "Perl 6". That is the one place a documented link could still turn up.
- No check was made of PEP 3132 (extended iterable unpacking, 2007), Ruby keyword arguments, or the destructuring and rest-parameter proposals for ES2015 / JS 1.7.

---

## Q8. Runtime mixins (`$obj but Role`, `$obj does Role`)

### Takeaway
A12 (2004) specified runtime role application to individual objects (`does` in place, `but` on a copy) by deriving an anonymous subclass. PEP 3133 described exactly this as a Perl 6 feature (2007) but was rejected. Groovy 2.3+ has a close semantic analog: runtime trait coercion with `as` / `withTraits` returns a new object, like `but`. Groovy does not state an inspiration.

### Cited Findings
- A12: "The "does" binary operator performs the feat of deriving a new class and binding the object to it", and "In contrast to C<does>, the C<but> operator works on a copy" — [A12 POD](https://web.do.metacpan.org/release/INGY/Perl6-Bible-0.20/source/lib/Perl6/Bible/A12.pod)
- S12 has a section "Anonymous Mixin Roles using C<but> or C<does>" with `$x = "Today" but Tue;` — [S12](https://raw.githubusercontent.com/Raku/old-design-docs/master/S12-objects.pod)
- PEP 3133 (2007), open issue: "Perl 6 allows instances to perform different roles than their class", "In Perl 6, this is done by creating an anonymous class that inherits from the instance’s original parent" — [PEP 3133](https://peps.python.org/pep-3133/)
- Groovy: an existing object can be given traits at runtime with `as`, or `withTraits` for several; "the result of the operation is not the same instance" — [Groovy docs](https://groovy-lang.org/objectorientation.html)

### Inferences
- **Verdicts**
  - Groovy runtime traits: independent / common ancestor (dynamic-proxy techniques on the JVM). No citation, and the 2014 date makes "first" claims meaningless.
  - Scala `new C with T`: common ancestor (mixin composition); unverified.
  - Ruby `Object#extend`: a pre-2000 ancestor (Ruby 1.x); unverified.
  - Python: documented awareness of Perl 6 (PEP 3133), but not adopted.

### Gaps
- Scala's anonymous-class mixin (`new C with T`) and Ruby's `extend` / singleton classes were not checked against primary documents.
- Self / Smalltalk `become:` and per-object behavior as ancestors were not researched.

---

## Q9. Meta-object protocol exposure (`.HOW`, `.WHAT`, `.^`)

### Takeaway
Python 2.2's metaclass overhaul (December 2001) predates A12 and cites Forman & Danforth's "Putting Metaclasses to Work" (1999), not Perl 6. The documented downstream user of Perl 6's MOP design is Moose / Class::MOP (Perl 5, 2006), built on Stevan Little's Perl 6 meta-model prototypes.

### Cited Findings
- A12 lists RFC 092 "Extensible Meta-Object Protocol" among the RFCs it considered. It says introspection "should generally be via the class's metaclass instance". No CLOS or Smalltalk inspiration is named — [A12 POD](https://web.do.metacpan.org/release/INGY/Perl6-Bible-0.20/source/lib/Perl6/Bible/A12.pod)
- S12: "Every class has a C<HOW> function/method"; "The C<^> metasyntax is equivalent to C<.HOW>"; HOW glossed as "Higher Order Workings". No named inspiration — [S12](https://raw.githubusercontent.com/Raku/old-design-docs/master/S12-objects.pod)
- Python 2.2 descrintro cites "Putting Metaclasses to Work" (Forman & Danforth, Addison-Wesley 1999) as the book "which inspired me to change the MRO", and as the source of the term "cooperative method". The example transcripts show Python 2.2c1, Dec 2001. No Perl — [Unifying types and classes in Python 2.2](https://www.python.org/download/releases/2.2/descrintro/)
- Moose: "Moose is based on the prototypes and experiments Stevan did for the Perl 6 meta-model" — [Moose::Manual](https://metacpan.org/pod/Moose::Manual). SCG: "Stevan Little implemented Traits in Perl 5" — [SCG Traits](https://scg.unibe.ch/research/traits)

### Inferences
- **Verdicts**
  - Python metaclasses: independent; common ancestor in the academic SOM / metaclass literature (Forman & Danforth), and earlier than A12.
  - Ruby's metaclass / singleton-class model: pre-2000 ancestor (Smalltalk); unverified here.
  - Moose / Class::MOP: borrowed from Perl 6 (documented). Moose also credits CLOS and Smalltalk, the common ancestors of all MOPs (*The Art of the Metaobject Protocol*, 1991).
- `.WHAT` (type object as value) and the `.^` sugar have no clear mainstream analog to compare. Python's `type(x)` and Ruby's `x.class` are ancestral idioms, not borrowings.

### Gaps
- Perl 6's own credit, if any, to CLOS / AMOP for its MOP design (Stevan Little's Pugs-era meta-model notes, about 2005) was not fetched.
- Class::MOP's own POD, which reportedly cites AMOP and the Perl 6 meta-model, was not fetched in this pass.
