# Documented origins of the Perl 6 look-alikes the report labelled "independent"

Scope: the features that `docs/dev/PERL6-FEATURES-IN-OTHER-LANGUAGES.md` says had no primary design notes (C# 2.0 `??`, ES2015 shorthand properties, Swift 1.0's `$0`, `\()` and `??`, Kotlin's `it`), plus the related items in the brief. Method, 7 November 2026: primary sources first. These were the public git histories of Swift (they reach back to Lattner's first commit on 17 July 2010), Go (the 2008–09 spec drafts), Groovy (CVS import from August 2003) and Scala (to the v2.5.0 tag), read through treeless or blobless clones of github.com, never api.github.com. Also read: Wayback snapshots of the ES wiki, the Kotlin Confluence and the Groovy codehaus site; the ECMA-334 3rd-edition PDF; the D changelog; the swift-evolution, digitalmars-d, groovy-dev and python-dev archives. Downloads are under the session scratchpad `origins/` directory. Wording from sources is paraphrased except for one short quotation. Every claim links to its primary text.

## C# 2.0 `??`: when did it appear, who said what, and was Perl's `//` mentioned?

### Takeaway
`??` first appeared in public with nullable value types in the updated C# 2.0 specification of late May 2004, 31 months after Apocalypse 3 specified `//`, and ECMA-334 3rd edition (June 2005) standardised it as §14.12. No Microsoft statement about where it came from was found, and no C# document checked mentions Perl. The report's verdict, independent and of C# lineage for later adopters, stands. Only the date gets sharper.

### Cited Findings
- The four C# 2.0 features Hejlsberg previewed at OOPSLA in November 2002 were generics, iterators, anonymous methods and partial types; nullable types were not among them — [Chris Sells via intertwingly, 9 Nov 2002](https://intertwingly.net/blog/2002/11/09/New-C-Language-Features-someday)
- On 24 May 2004 Eric Gunnerson announced an updated C# 2.0 specification. He credited the work to Anders and named nullable types as its biggest new area beyond the original four features. A commenter asked what had changed since the October 2003 version, and Gunnerson replied that no change list existed — [Gunnerson, 24 May 2004](https://learn.microsoft.com/en-us/archive/blogs/ericgu/updated-c-v2-0-specifications-now-available)
- A patent's bibliography cites the "C# Version 2.0 Specification" as published May 2004. This is secondary evidence for the date — [USPTO 7647580](https://uspto.report/patent/grant/7647580)
- On 28 May 2004 Scott Allen tried the May 2004 CTP compiler and reported that the operator, which he already called the null coalescing operator, did not yet compile on two nullable ints — [OdeToCode, 28 May 2004](https://odetocode.com/blogs/scott/archive/2004/05/28/nullable.aspx)
- ECMA-334 3rd edition (June 2005) defines `??` in §14.12. Its informative overview presents it as a new operator that supplies a default when a nullable value is null and that also works on reference types. Annex F, the bibliography, lists only REXX, ISO 31-0, C and C++ — [ECMA-334 3rd ed.](https://ecma-international.org/wp-content/uploads/ECMA-334_3rd_edition_june_2005.pdf)
- The C# 8 proposal for `??=` has no prior-art section and does not mention Perl's `//=` — [csharplang: null-coalescing-assignment](https://github.com/dotnet/csharplang/blob/main/proposals/csharp-8.0/null-coalescing-assignment.md)
- Eric Lippert's post on `??` deals only with its precedence, not its origin — [ericlippert.com, 23 Oct 2013](https://ericlippert.com/2013/10/23/bug-guys-null-coalescing-precedence/)
- Perl-side dates: A3 specified `//` as defined-or on 2 Oct 2001 — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/). Brent Dax's defined-or patch reached Perl 5 on 26 Jul 2002 and shipped in 5.10 (sibling notes, `syntax_and_operators.md` §1)

### Inferences
- Perl 6 could have been an input: A3 and the Perl 5 patch both predate the May 2004 spec. But no C# text found names Perl, and the operator arrived as part of the nullable-value-types package. The commenters on Gunnerson's post discuss that package only in terms of database NULLs, and the name parallels SQL's `COALESCE`. That is the most economical origin story, though it is an inference, not a documented statement.
- Verdict: **independent, with no documented origin**, tied to nullable value types. The report's row stays as it is. Replace "C# 2.0 `??` (2005)" with "first public in the May 2004 C# 2.0 spec; ECMA-334 3rd ed., June 2005".

### Gaps
- The May 2004 specification itself was not retrieved, nor were the October 2003 or February 2004 printed versions, so I cannot confirm whether `??` was absent from them. Gunnerson's framing makes nullable types new in May 2004.
- No statement on the operator's origin was found from Hejlsberg, Lippert, Torgersen or Peter Hallam. *The C# Programming Language* (annotated editions) was not checked, and no C# design notes from 2003–04 were found online.

## C# 2.0 iterators (`yield return`): origin and designers

### Takeaway
Iterators were announced at OOPSLA in November 2002. They first used a bare `yield`, which became `yield return` so that `yield` could stay a contextual keyword. Microsoft's own accounts frame them as a deliberately restricted coroutine and name no ancestor language. C# iterators predate Perl 6's gather/take (A12, April 2004), so Perl 6 cannot have been an input.

### Cited Findings
- Iterators were one of the four features unveiled at OOPSLA in November 2002, described as making collection traversal easier — [intertwingly, 9 Nov 2002](https://intertwingly.net/blog/2002/11/09/New-C-Language-Features-someday)
- Gunnerson (8 Mar 2006) says the first version of iterators used `yield` alone for a long period. The team switched to `yield return` once generics turned out to need no new keyword, which left `yield` as the only one, and pairing it with `return` let C# 2.0 compile all existing code — [Gunnerson](https://learn.microsoft.com/en-us/archive/blogs/ericgu/why-yield-return-rather-than-yield)
- Lippert (9 Jul 2009) calls iterator blocks a weak kind of coroutine. He says the team could have built full coroutines or continuations and deliberately targeted iteration over collections instead — [Iterator Blocks, Part One](https://learn.microsoft.com/en-us/archive/blogs/ericlippert/iterator-blocks-part-one)
- Microsoft Research's Cω overview says Cω iterator functions behave like the iterators planned for C# 2.0 but produce streams; this comes from a search summary, the page was not fetched — [An Overview of Cω](https://msdn.microsoft.com/en-us/library/ms974195.aspx)
- Sather's designers describe Sather iters as derived from CLU iterators — [Sather TOPLAS page](https://ftp.gwdg.de/pub/gnu/www/software/sather/ICSI_Sather/Publications/toplas.html)
- ECMA-334 3rd ed. specifies iterators in chapter 26, and its bibliography names no language with generators — [ECMA-334 3rd ed.](https://ecma-international.org/wp-content/uploads/ECMA-334_3rd_edition_june_2005.pdf)
- Hejlsberg's February 2002 group interview mentions no iterators or generator languages — [The Register, 14 Feb 2002](https://www.theregister.com/2002/02/14/anders_heljsberg_on_whats_next/)
- Miguel de Icaza, implementing C# iterators for Mono in April 2003, remarks in passing that the Lisp, Scheme and Icon communities have better examples. This is a passing comment, not a design statement — [tirania.org, 22 Apr 2003](https://tirania.org/blog/archive/2003/Apr-22.html)

### Inferences
- Verdict: **common ancestor (the CLU → Sather/Icon/Python-generator line), with no designer statement naming it**. Perl 6 is excluded by chronology: gather/take is A12 (April 2004), 17 months after the C# announcement. The report's "Lazy lists, gather/take — common ancestor" row is unaffected. If anything, C# 2.0 joins PEP 255 as a shipped design that predates gather/take.

### Gaps
- No primary statement was found on who designed C# iterators beyond Hejlsberg as author of the spec. Nothing ties them to CLU, Sather, Icon, Python or Cω by name. An untested lead is whether Microsoft Research staff with Icon backgrounds took part; no source was found.

## Swift: `$0`, `\( )`, `??`, the `..<` rename and `defer`

### Takeaway
The public Swift repository holds the pre-open-source design history back to July 2010, and it settles all five cases without mentioning Perl:
- `$0` began as `_0`/`_1` and was renamed to `$0` in September 2010 on Bertrand Serlet's preference.
- `\( )` arrived in May 2012, and the team later explained it as reuse of the escape character.
- Joe Groff added `??` in July 2014 as an instance of nil-coalescing operators in other languages. It was not in the June 2014 WWDC build.
- Apple's own notes credit Groovy for `..<`.
- `defer` was explicitly designed as a Go-style `defer` scoped to the enclosing scope.

### Cited Findings
- The first commit in the public Swift history is Lattner's, dated 17 Jul 2010 — [swiftlang/swift 18844bc](https://github.com/swiftlang/swift/commit/18844bc65229786b96b89a9fc7739c0fc897905e)
- **`$0`.** On 28 Jul 2010 Lattner implemented anonymous closure arguments spelled `_0`, `_1` — [6670786](https://github.com/swiftlang/swift/commit/667078650a35edaaec55c75b8b6a2bdfceb2eedb). On 18 Sep 2010 his commit message records that Bertrand preferred `$0` to `_1`, and the spelling was changed — [1bef5bd](https://github.com/swiftlang/swift/commit/1bef5bd648352c48baedc77f769c03a3a402fca0). In a 2019 podcast Lattner says the early 2010 design discussions were whiteboard sessions with Bertrand Serlet, then head of Apple's software organisation — [oleb.net transcript, 18 Feb 2019](https://oleb.net/2019/chris-lattner-swift-origins/). By 10 Jul 2013 the changelog shows closures using `$0`/`$1` without `in` — [CHANGELOG at swift-5.0-RELEASE](https://github.com/swiftlang/swift/blob/swift-5.0-RELEASE/CHANGELOG.md)
- **`\( )`.** Lattner's commit of 4 May 2012 implements string interpolation of the form `"foo is \(i+j)"`, credits Doug Gregor with the hard parts, and closes rdar://11223686 — [2f44c00](https://github.com/swiftlang/swift/commit/2f44c0038c5a53e6ee4bb8f0a0fec4c3bd5d79a2). In June 2016 Jordan Rose explained the original logic: backslash was already the string escape character, and the parentheses matched function calls because interpolation once lowered to `String` initializer calls. Lattner replied that he agreed completely. Neither message names another language — [swift-evolution, 21 Jun 2016](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160620/021613.html)
- **`??`.** On 26 Jul 2014 Joe Groff lexed `??` as an operator and added it to the stdlib. His commit message describes it only as similar to nil-coalescing operators in other languages, citing rdar://15247356 — [0ea3dbc](https://github.com/swiftlang/swift/commit/0ea3dbc652a94ef2a6c47199ad453655c0be6a34); [61f64f3](https://github.com/swiftlang/swift/commit/61f64f3323d731d48716affffed70fa2fe8efbbf). The internal release notes list it under 2014-08-04, which is Xcode 6 beta 5 — [buildbot-release-notes.txt](https://github.com/swiftlang/swift/blob/swift-2.2-SNAPSHOT-2015-12-01-a/utils/buildbot-release-notes.txt). Contemporary users dated it to beta 5 as well — [mjtsai, 25 Jul 2014](https://mjtsai.com/blog/2014/07/25/nil-coalescing-operator-in-swift/)
- **Ranges, in the internal notes** — [buildbot-release-notes.txt](https://github.com/swiftlang/swift/blob/swift-2.2-SNAPSHOT-2015-12-01-a/utils/buildbot-release-notes.txt):
  - 2014-02-12: half-open ranges became `...` for consistency with Ruby.
  - 2014-02-26: `..` became closed and `...` half-open.
  - 2014-05-19: the two operators were switched.
  - 2014-06-23: `..` was renamed `..<` to reduce confusion. The note adds: "The ..< operator is precedented in Groovy (among other languages)".
- Lattner's commit adding `..<` as an alias for `..` is dated 19 Jun 2014 — [62cad3d](https://github.com/swiftlang/swift/commit/62cad3dce87cef6ec049a39c04b4d33d79509592). The public notes for Xcode 6 beta 3 (July 2014) gave clarity next to `...` as the reason — [AppDeveloperMagazine](https://appdevelopermagazine.com/apple-opens-new-swift-blog,-announces-xcode-6-beta-3-for-ios-development/)
- **Groovy's `..<`.** Shijun He proposed `..<`, `<..` and `<..<` on groovy-dev on 10 Jan 2005 to replace Groovy's Ruby-style `...`, which he found too easy to confuse with `..`. He mentions Ruby and no other language — [marc.info, groovy-dev](https://marc.info/?l=groovy-dev&m=113684773506831). John Rose's grammar notes of 1 Apr 2005 record that right-exclusive ranges are now spelled `x..<y`, with `x...y` accepted for a transition period — [Wayback: GroovyJSR grammar notes](https://web.archive.org/web/20051220143509/http://docs.codehaus.org/display/GroovyJSR/2005/04/01/grammar+notes); [apache/groovy 13b6224](https://github.com/apache/groovy/commit/13b622472daad530d8adcf4719ca4a13335ebfae). Shijun He later claimed credit for the spelling and noted that Swift arrived at the same design — [hax.github.com #25, 15 Jul 2015](https://github.com/hax/hax.github.com/issues/25)
- **`defer`.** Lattner implemented parsing, AST and Sema support for `defer` on 26 Apr 2015 — [ee96164](https://github.com/swiftlang/swift/commit/ee96164996e4c4865b958bc4769487cf72d12fc1). It shipped in Swift 2.0 (2015-09-17, Xcode 7.0), presented as cleanup for the new error-handling model — [CHANGELOG](https://github.com/swiftlang/swift/blob/main/CHANGELOG.md). The Swift error-handling rationale surveys Go's `defer`. It recommends a Go-style `defer` over Java-style `try`/`finally`, but tied to scope exit rather than function exit, and considers `finally` as an alternative name — [ErrorHandlingRationale.md](https://github.com/swiftlang/swift/blob/main/docs/ErrorHandlingRationale.md)
- Joe Groff's 2019 history of closure syntax: early Apple adopters demanded Ruby-style trailing closures, the team tried Ruby's `{|args| }` among other forms, and `in` was chosen to echo `for x in xs`. He does not discuss `$0` — [Swift Forums, 21 Mar 2019](https://forums.swift.org/t/history-why-does-closure-syntax-use-the-keyword-in/21885)

### Inferences
- **`$0`**: **independent (documented decision; no source cited)**. The C++-placeholder-like `_1` spelling was replaced by an executive preference for `$0`. `$0` resembles shell positional parameters more than Raku's `$^a`, and nobody cites either. Raku's placeholders (A3, Oct 2001) were nine years older, but no link is documented.
- **`\( )`**: **independent (documented rationale)**. Escape-character reuse is unrelated to Perl's `"$(expr)"` or `"{...}"`.
- **`??`**: **C# lineage (the commit cites only nil-coalescing operators in other languages)**. The report should date it to 26 Jul 2014, public in Xcode 6 beta 5 (August 2014). It was not part of the June 2014 Swift.
- **`..<`**: **borrowed from Groovy (documented)**. This changes the report's sentence that Swift renamed its operator "with no stated source". The source is Groovy, where the spelling was proposed in January 2005 and adopted in April 2005. The chain is Ruby's confusable `..`/`...` → Groovy `..<` (2005) → Swift (June 2014) → Kotlin `..<` (whose KEEP already cites Swift and Groovy). Raku's `..^` is not in the chain.
- **`defer`**: **borrowed from Go, with scope-exit semantics (documented)**. This upgrades the sibling notes' "plausibly Go-influenced (circumstantial)". Swift's choice of scope exit over function exit matches D's `scope(exit)` and Raku's LEAVE, but the rationale cites only Go and Java.
- The report's statement that Swift 1.0's pre-open-source choices lack primary design notes is out of date. The commit history from 2010–2014 is public, and the internal release notes from 29 Jan 2014 to Oct 2014 survive in the repository.

### Gaps
- The Bertrand of the 2010 commit is identified as Serlet by inference from Lattner's account; the commit gives only the first name, and no reason for preferring `$0` was found.
- The radars cited in commits are not public.
- Not checked: the WWDC 2014 sessions, *The Swift Programming Language* 1.0, ATP 205, Lex Fridman and Swift by Sundell. The commit history supersedes them for dating, but they might hold rationale.

## Kotlin: `it`, `${}` templates, `..` ranges and `?:`

### Takeaway
Kotlin's designer credits Groovy for `it` and for the Elvis operator, and credits C# alongside Groovy for the Elvis family. `it` was in Kotlin by September 2011, two months after the language was announced. No designer statement was found for `${}` or `..`, which have obvious Groovy precedents. Nothing links any of them to Perl 6.

### Cited Findings
- Andrey Breslav's talk "Shoulders of Giants: Languages Kotlin learned from" shows `mylist.filter { it.foo }.map { it.bar }` on a slide whose title attributes it to Groovy. It sets that against Scala's `_.foo`, a Java stream lambda and C#'s `Where(it => …)`, which are marked as not Kotlin, and a Scala for-comprehension, a LINQ query and a Python comprehension, which are marked as not Kotlin yet. A later slide credits Groovy and C# for `?:`, `?.`, `!!` and `as?`. Other slides credit Java, Scala, C# and Gosu for other features — [slides PDF](https://downloads.ctfassets.net/oxjq45e8ilak/24vVb20Rpam2MksEU4QCOe/511bcd2575ba6e8af5cf82896db71570/AndreyBreslav_ShouldersOfGiants.pdf); [SlideShare](https://www.slideshare.net/slideshow/shoulders-of-giants-languages-kotlin-learned-from/96645250). The page gives no date or event. The slide image filenames point to JPoint, April 2018 (inference), and a later version was given in Amsterdam in May 2022 — [SlideShare 2022](https://www.slideshare.net/slideshow/2022-may-shoulders-of-giants-amsterdam-kotlin-dev-daypdf/251861368)
- Breslav's Strange Loop slides of 19 Sep 2011 already use `filter(list) { it.length < 3 }` — [SlideShare 9327348](https://www.slideshare.net/slideshow/kotlin-strangeloop-2011/9327348)
- The Kotlin "Function literals" page, last edited by Breslav on 21 Oct 2011, says that when Kotlin can infer a one-parameter literal's signature it declares the parameter implicitly under the name `it`. The same page calls Kotlin's builders "Type-safe Groovy-style builders" — [Wayback, 23 Oct 2011](https://web.archive.org/web/20111023184052/http://confluence.jetbrains.net:80/display/Kotlin/Function+literals)
- The 2011 Kotlin FAQ positions the language against Scala (too complex), Groovy (dynamic and slow) and Gosu — [Wayback, Nov 2011](https://web.archive.org/web/20111104193251/http://confluence.jetbrains.net/display/Kotlin/FAQ)
- Groovy's Elvis operator was implemented on 5 Sep 2007 (GROOVY-2100) by Jochen Theodorou — [apache/groovy 20b4070](https://github.com/apache/groovy/commit/20b4070f83261f25c3f17ce7aaccb6259a33f7e9)
- Groovy's `${}` GString interpolation was on the Groovy home page by October 2003 — [Wayback, 8 Oct 2003](https://web.archive.org/web/20031008181010/http://groovy.codehaus.org/)

### Inferences
- **`it`**: **borrowed from Groovy (documented by the designer, retrospectively)**. This upgrades the sibling notes' "plausibly Groovy (circumstantial)".
- **`?:`**: **Groovy and C# (documented by the designer)**.
- **`${}` and `..`**: **plausibly Groovy (circumstantial)**. Kotlin's 2011 documentation already credits Groovy for builders.
- None of this changes a Perl 6 verdict. It replaces "no primary notes" with a documented non-Perl source.

### Gaps
- The JVMLS slides of July 2011 were not retrieved (the JetBrains blog returned 403), and Wayback's earliest copy of "Function literals" is October 2011. Whether `it` was in the July 2011 announcement is unconfirmed.
- No 2011–2016 Breslav statement on `${}` or `..` was found.

## Groovy `it`: origin

### Takeaway
James Strachan added `it` to Groovy on 29 October 2003 to make simple closures more concise. The commit and the documentation cite no source. It came a month after closures stopped requiring Ruby-style pipe parameter lists.

### Cited Findings
- Strachan's commit of 29 Oct 2003 adds a default closure parameter called `it`, with the example `[1, 2, 3, 4].findAll { it > 1 }.map { it * 2 }` — [apache/groovy c525bfb](https://github.com/apache/groovy/commit/c525bfb600998c4e07805b4647a358153ff94174). The closures documentation was updated the same day — [6b9f84a](https://github.com/apache/groovy/commit/6b9f84a87e37ec0a6d73f79d8ad1ec128ff4ed38)
- On 4 Oct 2003 Bob McWhirter removed the requirement for pipes and argument lists on closures — [db72f77](https://github.com/apache/groovy/commit/db72f77267e574dd8a5a2c518b7a3838826faf1d). The October 2003 home page still showed Ruby-style `collection.each { |i| ... }` — [Wayback, 8 Oct 2003](https://web.archive.org/web/20031008181010/http://groovy.codehaus.org/)
- The February 2004 user guide says a closure takes one parameter that defaults to the name `it`, and declares explicit parameters with `{ a, b | ... }` — [Wayback, 24 Feb 2004](https://web.archive.org/web/20040224101957/http://groovy.codehaus.org/closures.html)
- John Rose's grammar patch of 1 Feb 2005 made `it` a regular identifier — [e7c13d0](https://github.com/apache/groovy/commit/e7c13d08c1272b65b2223834bfcfda3caac4dec7)

### Inferences
- Verdict: **independent, with no documented origin**. Groovy's `it` (2003) predates Kotlin's (2011) and Ruby 3.4's (2024).
- A taxonomy point for the report. Groovy's and Kotlin's `it` is a single, implicitly declared, named parameter that can be used repeatedly (`{ it * it }`). That is the semantics of Raku's `$_` in a bare block (A4, 15 Jan 2002), whose ancestor is Perl 5's `$_` in `map`/`grep` blocks. It is not Whatever-currying. Scala's `_`, where each occurrence is a new parameter (`_ * _` takes two), is the WhateverCode analogue. The report's paragraph pairing Kotlin's and Ruby's `it` with Whatever-currying should pair them with `$_` instead. This is my analysis of documented semantics, not a sourced claim. Even under the corrected pairing, no Groovy or Kotlin source cites Perl's `$_`.

### Gaps
- Strachan's 2003 blog posts and the JSR-241 drafts were not read. No statement on why `it` was chosen was found.

## Scala 2.5 `_` placeholder syntax: Odersky's rationale

### Takeaway
No rationale was found. The feature is documented only as a changelog entry for Scala 2.5 (2 May 2007), and the commit messages of that period are terse.

### Cited Findings
- The Scala changelog lists "Implicit anonymous functions" under version 2.5 (2 May 2007): underscores in parameter position define anonymous functions, `_ + 1` means `x => x + 1`, and `_ * _` means `(x1, x2) => x1 * x2`. It cites no source — [Scala 2.12 spec, changelog](https://scala-lang.org/files/archive/spec/2.12/15-changelog.html)
- The scala/scala history up to the v2.5.0 tag (7,148 commits) has no commit message naming the placeholder feature or a source, and Odersky's messages from January to May 2007 give no rationale — [scala/scala v2.5.0](https://github.com/scala/scala/tree/v2.5.0)

### Inferences
- Verdict unchanged: **independent (undocumented)**. Semantically, Scala's `_` is the WhateverCode analogue: each `*` or `_` is a fresh parameter. Whatever (September 2006 in the spec) predates Scala 2.5 by eight months, but nothing connects them.

### Gaps
- The scala-lang mailing lists of 2006–07 and *Programming in Scala* (2008) were not consulted.

## ES2015 shorthand properties and template literals

### Takeaway
The shorthand-property strawman gives its own lineage: symmetry with destructuring patterns, and SpiderMonkey/Rhino's existing JS 1.8.1 support for `{x, y}`. Template literals came out of Mike Samuel's quasi-literal strawman of December 2009. It cites E quasi-literals, PHP, Scheme quasiquote, Scribble and SML/NJ, and justifies `${}`/`$x` by PHP, jQuery templates, Bash, Perl and Ruby. Neither mentions Perl 6.

### Cited Findings
- Strawman `object_initialiser_shorthand` (Wayback snapshots of 1 May and 2 Jul 2011):
  - Its goal is a shorthand for object initialisers whose keys come from same-named variables, with reversibility or symmetry with the destructuring shorthand patterns.
  - Its listed precedent is that JS 1.8.1+ (SpiderMonkey and Rhino) already accepts `{x, y}` in expressions.
  - Its earliest dated comment is Allen Wirfs-Brock's of 8 Sep 2010. Erik Arvidsson and Brendan Eich discussed it in April–May 2011, and Brendan last edited it on 20 May 2011.
  - Source: [Wayback snapshot](https://web.archive.org/web/20110702063518/http://wiki.ecmascript.org/doku.php?id=strawman:object_initialiser_shorthand)
- Mike Samuel announced `strawman:quasis` on es-discuss on 14 Dec 2009 as a concrete syntax for the string interpolation described in `strawman:string_interpolation` — [esdiscuss](https://esdiscuss.org/topic/quasi-literal-strawman)
- `harmony:quasis` (last edited by Mark Miller on 17 Jun 2011; Wayback, Dec 2011) — [Wayback snapshot](https://web.archive.org/web/20111223164326/http://wiki.ecmascript.org/doku.php?id=harmony:quasis):
  - It chooses the backquote because it is otherwise unused in JS and is already a quoting character in Perl, PHP and Ruby (with interpolation) and in Scheme (quasiquote).
  - It chooses `${...}` because PHP and jQuery templates use it, and offers an abbreviated `$ident` for compatibility with Bash, Perl, PHP and Ruby.
  - Its references are E quasi-literals, Secure String Interpolation, PHP string variable parsing, PLT Scheme Scribble, SML/NJ quote/antiquote, Scheme hygienic macros, Paradigm Regained and work on safe web templates.

### Inferences
- **Shorthand properties**: **independent (documented)**. They came from SpiderMonkey's own destructuring shorthand. The report's "Independent / common ancestor (ES6)" verdict for named-argument punning stands, and the ES6 node now has a documented origin.
- **Template literals**: **common ancestor (documented)**: shell, Perl 5 and PHP interpolation for the surface syntax, E and Lisp quasiquote for tagged templates. "Perl" in the strawman means Perl 5-style `$ident` interpolation. Perl 6's `"$(expr)"`/`"{...}"` is not mentioned.

### Gaps
- The shorthand strawman's creation date and author are not recoverable from the snapshots, which start in May 2011. The SpiderMonkey bug that added expression-position `{x, y}` was not traced.

## Go `defer` (2009): stated origin

### Takeaway
`defer` entered the Go spec on 27 January 2009 in a commit by Robert Griesemer. It replaced an open to-do item for an `onreturn/undo` statement that had appeared in the spec draft on 16 January. Go's designers have never stated an external source. Griesemer later listed `defer` among Go's features that had no competing design.

### Cited Findings
- Griesemer's to-do cleanup of 16 Jan 2009 lists a missing `onreturn/undo` statement — [golang/go 1593ab6](https://github.com/golang/go/commit/1593ab6bb4d5bf59bed1d66f292992572555b999)
- Griesemer's commit "defer statement" of 27 Jan 2009 (reviewed by Rob Pike) added `defer` to the reserved words, the grammar and a new section. The section defines it as a call whose execution waits until the surrounding function returns, with arguments evaluated when the `defer` statement executes, LIFO order and a lock/unlock example — [golang/go 4a903e0](https://github.com/golang/go/commit/4a903e0b32be5a590880ceb7379e68790602c29d). A later draft marks the to-do item as now the `defer` statement — [go_spec.html @ ff70f09](https://go.googlesource.com/go/+/ff70f09d2704cf9e78cded8975141bb9de1520fa/doc/go_spec.html)
- Griesemer's GopherCon 2015 talk lists goroutines, interfaces and `defer` as language features without competition. It gives no external origin for `defer`, though it does trace other features to Algol, Pascal, Oberon, Smalltalk, Newsqueak and CSP — [The Evolution of Go](https://go.dev/talks/2015/gophercon-goevolution.slide)
- In 2020 Ian Lance Taylor wrote that he assumed Swift's `defer` was based on Go's — [golang-nuts, 21 Aug 2020](https://groups.google.com/g/golang-nuts/c/4Lzf-uPYjY8/m/09C8YZ6OAwAJ)

### Inferences
- Verdict unchanged: **independent (undocumented)**. The working name `onreturn/undo` points at transactional undo stacks, an idea the Swift rationale also discusses. Its similarity to Perl 6's UNDO phaser (A4, January 2002) is lexical only, and no evidence links them.

### Gaps
- No Pike, Cox or Thompson statement on where `defer` came from was found. Whether the `onreturn/undo` item existed before 16 Jan 2009 in a separate to-do file was not checked; googlesource rate-limited this machine, so the GitHub mirror was used.

## D `scope(exit|success|failure)`: Alexandrescu origin and the exact changelog entries

### Takeaway
DMD 0.148 (25 February 2006) added On Scope Statements, spelled `on_scope_exit`, `on_scope_success` and `on_scope_failure`. The `scope(exit)`, `scope(success)` and `scope(failure)` spelling arrived in DMD 0.149 on 7 March 2006, after Chris Miller proposed it. Walter Bright credited Andrei Alexandrescu's C++ scope-guard macros and believed no other language had the feature, which suggests Perl 6's KEEP/UNDO was unknown to him.

### Cited Findings
- The D changelog lists DMD 0.148 (25 Feb 2006) as adding On Scope Statements. DMD 0.149 (7 Mar 2006) changed `on_scope_exit`, `on_scope_success` and `on_scope_failure` to `scope(exit)`, `scope(success)` and `scope(failure)` and fixed code generation bugs for them — [D changelog 0.140–1.0](https://digitalmars.com/d/1.0/changelog2.html)
- Chris Miller endorsed the `scope(exit)`/`scope(success)`/`scope(failure)` form on 27 Feb 2006, by analogy with `extern(...)` and `pragma(...)` — [digitalmars-d, 27 Feb 2006](https://lists.puremagic.com/pipermail/digitalmars-d/2006-February/000547.html)
- Bright's announcement post says the feature is based on Alexandrescu's scope-guard macros and calls it a novel feature no other language has; it is quoted in replies — [digitalmars-d, 27 Feb 2006](https://lists.puremagic.com/pipermail/digitalmars-d/2006-February/000545.html); [forum.dlang.org](https://forum.dlang.org/post/du106f$ptt$1@digitaldaemon.com). In the same thread Derek Parnell says one other language almost implements it, without naming it, and Andrew Fedoniouk points to Ruby's exception handling. Bright replies that Ruby's version looks like try/catch — [forum.dlang.org](https://forum.dlang.org/post/du106f$ptt$1@digitaldaemon.com)
- D's "Exception Safe Programming" article credits Alexandrescu with arguing for these constructs and defining their semantics on comp.lang.c++.moderated, in posts beginning 6 Dec 2005. It says D's syntax came from its creator's experiments and suggestions by Dawid Ciężarkiewicz and Chris Miller. Its references are Alexandrescu and Marginean's "Generic<Programming>: Change the Way You Write Exception-Safe Code — Forever" and Meyers's *Effective C++*, 3rd ed. — [dlang.org](https://dlang.org/articles/exception-safe.html)

### Inferences
- Verdict unchanged: **independent, with a documented C++ ancestor (ScopeGuard)**. Correction for the report's common-ancestors table: the `scope(...)` spelling dates from DMD 0.149 (7 Mar 2006). DMD 0.148 shipped `on_scope_*`. Bright's novelty claim, made four years after A4's KEEP/UNDO, is direct evidence that D's designer did not know the Perl 6 design.

### Gaps
- Alexandrescu's December 2005 thread on comp.lang.c++.moderated was not read, so whether anyone mentioned Perl 6 there is unknown.
- The CUJ article's December 2000 date was not re-verified, because drdobbs.com no longer resolves. Derek Parnell's unnamed language was not identified; a search snippet suggesting Progress 4GL is unverified.

## Python `any()`/`all()` (2005): Guido's motivation

### Takeaway
Guido proposed `any()` and `all()` on 10 March 2005 as part of retiring `reduce()`. He presented them as mostly restoring the quantifiers of ABC, Python's own ancestor. That is a documented origin, so the independent verdict now has a named ancestor.

### Cited Findings
- In "The fate of reduce() in Python 3000" (10 Mar 2005), Guido argues that `reduce()` is mostly limited to associative operators. He proposes `any()` and `all()` for short-circuiting boolean tests, combined with generator expressions, and says they would mostly give back ABC's quantifiers. He names no other language for them; Lisp is mentioned only for lambda/map/filter/reduce — [Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=98196)
- The python-dev thread "Adding any() and all()" (11–12 Mar 2005) has Guido defending the short names and trading them for `reduce`, `map` and `filter`. Other participants debate builtins versus a module — [python-dev](https://mail.python.org/pipermail/python-dev/2005-March/052044.html); [052073](https://mail.python.org/pipermail/python-dev/2005-March/052073.html)

### Inferences
- Verdict: **independent, with a documented ancestor in ABC** (its SOME/EACH/NO quantifiers). The report's row "Python `any()`/`all()` — Independent; eager reductions, not junctions" stands and should cite ABC. The names coincide with Conway's `any`/`all` (Quantum::Superpositions, August 2000; RFC 225), but Guido's account points elsewhere.

### Gaps
- None material.

## Perl 5 Moose/Moo `has` versus Raku `has`: who chose it and why

### Takeaway
Stevan Little's Moose 0.01 (March 2006) exports `has`, `extends`, `before`, `after` and `around`, with no stated reason for `has`. Its foundation, Class::MOP, lists the Pugs Perl 6 meta-model as its prior art. Moose `has 'x' => (is => 'rw')` mirrors A12's `has $.y is rw`, and Larry Wall's rationale for `has` (an attribute is a HASA relationship) is documented on the Perl 6 side. Verdict: borrowed from Perl 6, documented for the design overall and circumstantial for the keyword.

### Cited Findings
- Moose 0.01's source installs `extends`, `has`, `before`, `after` and `around` into the caller and maps `is => 'ro'|'rw'` to reader and accessor. Its synopsis is `has 'x' => (isa => Int(), is => 'rw')`, and its description gives no source for the keywords — [Moose-0.01 Moose.pm](https://fastapi.metacpan.org/source/STEVAN/Moose-0.01/lib/Moose.pm)
- Class::MOP 0.01 lists, under "Prior Art", the Perl 6 MetaModel work in the Pugs project (Perl6-MetaModel, Perl6-ObjectSpace). Its reading list is CLOS/AMOP and Smalltalk works — [Class-MOP-0.01 MOP.pm](https://fastapi.metacpan.org/source/STEVAN/Class-MOP-0.01/lib/Class/MOP.pm)
- A12 (16 Apr 2004) declares attributes as `has $.x; has $.y is rw;`. Larry Wall says the `has` declarator was chosen to remind people that attributes stand in a HASA rather than an ISA relationship to the object — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- Later Moose documentation says Moose is based on Little's prototypes for the Perl 6 meta-model — [Moose 0.18](https://metacpan.org/pod/release/STEVAN/Moose-0.18/lib/Moose.pm). Moose also ships a Perl6Attribute example metaclass implementing `$.foo`-style attributes — [Perl6Attribute](https://metacpan.org/pod/release/DOY/Moose-2.0002/examples/Perl6Attribute.pod)

### Inferences
- Verdict: **borrowed from Perl 6**: documented at the level of the object system, circumstantial but strong for the keyword. The keyword `has` plus an `is => 'rw'` option is a direct transliteration of A12's `has $.y is rw` into Perl 5 function-call syntax. Moo, Mouse and Joose inherited `has` from Moose. Object::Pad and the 5.38 `class` feature later moved to `field` (sibling notes).

### Gaps
- No statement by Stevan Little about why he chose `has` was found; his 2006 use.perl.org journal and talks were not read. Whether the Pugs Perl6-MetaModel prototypes (2005) already used a `has` function was not checked.

## Does the evidence change the report's verdicts?

### Takeaway
No new Perl 6 borrowing turned up. Documented non-Perl origins now replace "no primary notes" for Swift's `$0`, `\()`, `??`, `..<` and `defer`, Kotlin's `it` and `?:`, ES2015 shorthand and templates, Python's `any`/`all` and Groovy's `it`. C#'s `??` remains undocumented as to origin, but is now dated to May 2004. Several dates and one taxonomy in the report need correcting.

### Cited Findings
- Swift's internal notes credit Groovy for `..<` — [buildbot-release-notes.txt](https://github.com/swiftlang/swift/blob/swift-2.2-SNAPSHOT-2015-12-01-a/utils/buildbot-release-notes.txt)
- Swift's rationale names Go as the model for `defer` — [ErrorHandlingRationale.md](https://github.com/swiftlang/swift/blob/main/docs/ErrorHandlingRationale.md)
- Breslav names Groovy as the model for `it` and Groovy/C# for Elvis — [slides PDF](https://downloads.ctfassets.net/oxjq45e8ilak/24vVb20Rpam2MksEU4QCOe/511bcd2575ba6e8af5cf82896db71570/AndreyBreslav_ShouldersOfGiants.pdf)
- Guido names ABC as the source of `any`/`all` — [Artima](https://www.artima.com/weblogs/viewpost.jsp?thread=98196)
- The ES strawmen name SpiderMonkey destructuring, E, PHP, shell and Perl 5-style interpolation — [shorthand](https://web.archive.org/web/20110702063518/http://wiki.ecmascript.org/doku.php?id=strawman:object_initialiser_shorthand); [quasis](https://web.archive.org/web/20111223164326/http://wiki.ecmascript.org/doku.php?id=harmony:quasis)
- Walter Bright believed scope guards were new to programming languages in 2006 — [digitalmars-d](https://lists.puremagic.com/pipermail/digitalmars-d/2006-February/000545.html)

### Inferences
- Corrections for the report:
  1. The convergent-reinvention paragraph should say Swift's `..<` cites Groovy (spelling proposed January 2005, adopted April 2005), not that it has no stated source. Swift went from Ruby's `...` (February 2014) through a swap (May 2014) to `..<` (19–23 June 2014).
  2. Swift's `??` dates from 26 Jul 2014 and was public in Xcode 6 beta 5 (August 2014), not the June 2014 release.
  3. Swift's pre-2015 decisions have primary notes in the public repo. `$0` was renamed from `_0`/`_1` on 18 Sep 2010 on Bertrand Serlet's preference, and `\()` dates from 4 May 2012.
  4. D's `scope(...)` spelling is DMD 0.149 (7 Mar 2006); 0.148 had `on_scope_*`.
  5. Go's `defer` entered the spec on 27 Jan 2009, evolving from an `onreturn/undo` to-do item.
  6. C#'s `??` was first public in the May 2004 C# 2.0 spec.
  7. Kotlin's `it` (2011, credited to Groovy by Breslav) and Groovy's `it` (29 Oct 2003) correspond to Raku's implicit `$_` in bare blocks, not to Whatever-currying. Scala's `_` corresponds to WhateverCode.
- Verdict changes from circumstantial to documented, all to non-Perl sources:
  - Swift `defer` → Go.
  - Kotlin `it` → Groovy.
  - Swift `..<` → Groovy.
  - Python `any`/`all` → ABC.
- Moose `has` → Perl 6 is the only item here where Perl 6 is the likely source. Moose is already in the report as the Perl 5 importer.
- Chronology permitted a Perl 6 input in every case except C# iterators, where gather/take came later. No designer cites Perl 6, and the D case shows a designer unaware of it. Those two facts support the report's thesis that designers cite what has shipped.

### Gaps
- Still unsearched, from the brief: csharplang Discussions, forums.swift.org (only one thread read), Lex Fridman/ATP/Sundell transcripts, the WWDC 2014 sessions, *The Swift Programming Language* 1.0, the Kotlin JVMLS 2011 slides, the Groovy JSR-241 drafts and Alexandrescu's December 2005 C++ thread.
