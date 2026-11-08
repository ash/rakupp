# Perl 6 / Raku in language-design forums and mailing lists (searched 7 Nov 2026)

Scope: the discussion venues that the proposal-repository grep could not reach. Search terms everywhere: "perl 6", perl6, raku, rakudo (and "perl-6" on Discourse), case-insensitive. Classifications used: **adopted-from**, **cited-as-prior-art**, **considered-and-rejected**, **cautionary-tale**, **passing mention**. Items the coordinator already holds are not re-reported: Swift "Declarative String Processing Overview" #46 and #100, the March 2022 regex-syntax pitch (Joe Groff #2), the Oct 2021 character-classes pitch, Ruby #12133 and #16894, ruby-core:16733/16734/16766.

## Q1. Which venues were searched, and how complete is the coverage?

### Takeaway
Seventeen Discourse forums, Ruby's Redmine, PHP internals (externals.io), csharplang Discussions, the D and Nim forums, and the python-ideas/python-dev archives were searched exhaustively for the four terms. Elixir Forum's search could not be used, news.php.net was unreachable, and the python-3000 list is not in the searchable archive.

### Cited Findings
- Method, Discourse: `search.json?q=<term>&page=N` was paged until `more_full_page_results` was empty. Discourse returns one best post per topic, so every topic with a hit was then fetched whole (`/raw/<id>?page=N`, 100 posts per page) and grepped. Author metadata came from `/posts/by_number/<topic>/<n>.json`, which gives the original e-mail date for swift-evolution posts imported from the mailing list. Requests to each host were spaced 2.5 s apart, and no 429 or 403 responses occurred. Example endpoint: [forums.swift.org search](https://forums.swift.org/search.json?q=raku).
- Topics with real (non-noise) hits:
  - forums.swift.org: 32 topics with an own-text mention, about 70 posts.
  - internals.rust-lang.org: 10 topics. users.rust-lang.org: 11.
  - contributors.scala-lang.org: 4. discuss.python.org: 6. discourse.julialang.org: about 11.
  - Passing mentions only: ziggit.dev 8, forum.crystal-lang.org 4, discourse.haskell.org 3, discuss.ocaml.org 2, clojureverse.org 2, discuss.kotlinlang.org 1.
  - Noise or zero: discourse.elm-lang.org returned only "Rakuten". users.scala-lang.org, erlangforums.com, racket.discourse.group and forum.modular.com returned nothing. Example: [Elm "Elm at Rakuten"](https://discourse.elm-lang.org/t/post-elm-at-rakuten/6846).
- Elixir Forum: anonymous `search.json` and `/search` both answer 302 to `/tags/search`, and `/search/query.json` returns 404. The block was not worked around. A site-restricted web search surfaced only passing mentions, such as an Advent of Code 2019 thread noting the rename ([elixirforum.com/t/advent-of-code-2019-day-4/27333](https://elixirforum.com/t/advent-of-code-2019-day-4/27333)). That result is unverified.
- bugs.ruby-lang.org: the Redmine JSON search API (`/search.json?q=…&issues=1`), which covers issue descriptions and journal notes. 15 issues matched, 13 of them real, and 2 already known. [search](https://bugs.ruby-lang.org/search?q=%22perl+6%22&issues=1)
- PHP internals: [externals.io search](https://externals.io/search?q=%22perl+6%22) is fuzzy and relevance-ranked. Pages were read until two consecutive pages had no real hit, which yielded about 26 real messages from 2003 to 2021. news.php.net timed out with no connection. Two 2003–04 search hits point at message ids that now render a different thread, and raw e-mail sources need a login, so those two are cited from the search snippet only.
- csharplang Discussions: GitHub HTML search pages (`discussions?discussions_q=…`) returned 6 discussions. Their "Load more" and reply-thread fragments were expanded through the page's own GET forms, with no API calls. GitHub does not treat `"perl 6"` as a phrase, so that search matched "perl" and "6" separately.
- forum.dlang.org (DFeed full-text search): 108 real posts from 2001 to 2025. [search](https://forum.dlang.org/search?q=perl6)
- forum.nim-lang.org (`search.json`): about 16 real posts, none by the core team. [search](https://forum.nim-lang.org/search.json?q=perl6)
- mail.python.org HyperKitty search: 170 real hits in python-ideas and python-dev. python-3000@ returns 404 from the search. [search](https://mail.python.org/archives/search?q=%22perl+6%22&mlist=python-ideas%40python.org)
- openjdk.org was not contacted.

### Inferences
- Swift, D and the Python lists are where Perl 6 was discussed in substance. Everywhere else the mentions are passing remarks or warnings.

### Gaps
- Elixir Forum (search blocked), news.php.net (unreachable), the python-3000 list (pipermail only, not searchable), and the Kotlin YouTrack/Slack and Zig GitHub/Codeberg issue trackers (not in scope) were not covered.
- externals.io's fuzzy ranking may have pushed a few real PHP hits past the pages read.

## Q2. Swift: forums.swift.org, including the imported 2015–2017 swift-evolution list

### Takeaway
Swift has the richest record, and it contains a **verdict-changing borrowing**. Swift 4's rule that the closing `"""` delimiter's indentation is stripped from every line was proposed on swift-evolution as Perl 6's heredoc rule. SE-0168's lead author adopted it "as per Perl6", and the core team accepted exactly that rule. Separately, three 2017 core-team members (Lattner, Groff, Abrahams) repeatedly urged Perl 6 regexes and grammars as the model. A 2023 Language Workgroup member weighed Raku junctions and advised against them.

### Cited Findings
**Multi-line strings (adopted-from, design-discussion stage; shipped)**
- On 25 Apr 2016 Brent (now Becca) Royal-Gordon wrote in the swift-evolution "multi-line string literals." thread: "During the Perl 6 redesign, they decided to use the delimiter's indentation to determine the base indentation for the heredoc." He then asked whether that "seem[s] like a good approach?" ([pipermail 015738](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160425/015738.html); [forum copy #25](https://forums.swift.org/t/optional-dictionary-subscripts/719/25)). Note that the Discourse import mis-titled this thread "Optional dictionary subscripts".
- On 5 May 2016 John Holdsworth replied in the same thread: "the lexer can strip the margin on the basis of the whitespace before the closing quote as per Perl6" ([pipermail 016646](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160502/016646.html); [forum #104](https://forums.swift.org/t/optional-dictionary-subscripts/719/104)).
- SE-0168 lists John Holdsworth, Becca Royal-Gordon and Tyler Cloutier as authors. It specifies that "any whitespace characters in front of the closing delimiter are removed from each of the lines" and does not credit Perl 6; its status is Implemented (Swift 4.0) ([SE-0168](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0168-multi-line-string-literals.md)).
- In the SE-0168 review on 8 Apr 2017, co-author Brent Royal-Gordon surveyed the prior art, writing that "Perl 6 has everything Perl 5 does, plus de-indenting" ([review #17](https://forums.swift.org/t/review-se-0168-multi-line-string-literals/5611/17)).
- Acceptance, 19 Apr 2017: review manager Joe Groff accepted the proposal "with revisions" and stated that "the closing delimiter's position always determines the indentation level of the entire literal". He added that under-indentation is an error, and the post does not mention Perl ([Accepted](https://forums.swift.org/t/accepted-se-0168-multi-line-string-literals/5715)).
- Earlier, in Mar–Apr 2016, Patrick Gili asked for "Perl6 style string literals" and Perl 6 quoting constructs ([#3](https://forums.swift.org/t/alternate-proposal-for-multi-line-string-literial/1916/3); [#6](https://forums.swift.org/t/optional-dictionary-subscripts/719/6)). Swift did not adopt `q`/`qq`-style adverbial quoting.

**Regex and grammars (cited-as-prior-art by the core team; partly shipped in Swift 5.7, not as grammars)**
- Chris Lattner, Swift's creator and then on the core team, 9 Dec 2015: first-class regexes would "support powerful extensions to the base regex model (e.g. Perl 6 style)" ([#4](https://forums.swift.org/t/empower-string-type-with-regular-expression/335/4)).
- Lattner, 1 Feb 2016: "I highly recommend checking out Perl 6’s regular expressions." He then listed what he liked: the standard operator grammar, free-form style, comments, Unicode correctness, and compiler checking ([#9](https://forums.swift.org/t/empower-string-type-with-regular-expression/876/9); [#23](https://forums.swift.org/t/empower-string-type-with-regular-expression/876/23)).
- Brent Royal-Gordon relayed core-team intent in 2016: the team wanted regexes given "a deep, Perl 6-style rethink" ([#53](https://forums.swift.org/t/optional-dictionary-subscripts/719/53); [#5, 26 Jul 2016](https://forums.swift.org/t/draft-swift-implementation-regex-added/3535/5)).
- Joe Groff (core team), 23 Jan 2017: "The Perl 6 folks recognized this … I think we'd do well to start at that level by looking at what they've done." ([#68](https://forums.swift.org/t/strings-in-swift-4/4939/68))
- Lattner, 25 Jan 2017: "Swift can learn a lot from Perl 6 grammar’s and we should take the time to do it right" ([#130](https://forums.swift.org/t/strings-in-swift-4/4939/130)).
- Dave Abrahams (core team), 26 Jan 2017, pointed to "the kinds of parsers made possible by Perl 6 grammars, which can be recursive" ([#153](https://forums.swift.org/t/strings-in-swift-4/4939/153)).
- Core-team roster in June 2017: Ben Cohen, Chris Lattner, Dave Abrahams, Doug Gregor, Joe Groff, John McCall and Ted Kremenek ([swift.org/community, Wayback 14 Jun 2017](https://web.archive.org/web/20170614185531/https://swift.org/community/)).
- Michael Ilseman (Apple's String/stdlib lead, not on the core team), 10 Aug 2017: "Perl 6 is the gold standard for string processing in programming languages." ([#6](https://forums.swift.org/t/draft-regular-expression-in-swift/6472/6))
- Lattner, 23 Nov 2017: "I’d like to consider some of the advances that Perl 6 added to its regex grammar" ([#8](https://forums.swift.org/t/pitch-raw-mode-string-literals/7120/8)).
- Lattner, 12 and 17 Jan 2018, wanted a Unicode property "for each of (e.g.) the Perl 6 character classes". He also found it surprising that Perl 6 had not fixed some pre-Unicode regex semantics ([#3](https://forums.swift.org/t/state-of-string-abi-performance-ergonomics-and-you/7397/3); [#13](https://forums.swift.org/t/state-of-string-abi-performance-ergonomics-and-you/7397/13)).
- Ilseman, Mar–Apr 2019, cited Perl 6's PEG-based grammars and ratcheting as the model for "String Consumption" ([#1](https://forums.swift.org/t/string-consumption/21907/1); [#8](https://forums.swift.org/t/let-s-chat-first-class-regular-expressions/22411/8)).
- Lattner, 15 Mar 2020, still on the core team ([roster, Mar 2020](https://web.archive.org/web/20200307040633/https://swift.org/community/)): "Perl 6 has some really great things in this department." ([#10](https://forums.swift.org/t/regular-expressions-in-swift/34373/10))
- Hamish Knight's 14 Oct 2021 regex-literal pitch deferred "Swiftier" literals "with non-semantic whitespace (e.g. Raku's)" as future work ([#1](https://forums.swift.org/t/pitch-regular-expression-literals/52820/1)).
- Ilseman in the second review of SE-0354, 18 May 2022, said non-semantic whitespace in single-line literals "is the default for some languages like Raku" ([#50](https://forums.swift.org/t/se-0354-second-review-regex-literals/57367/50)). Outcome: whitespace is significant in single-line literals, and extended syntax is mandatory only in `#/…/#` ([SE-0354](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0354-regex-literals.md)).
- Xiaodi Wu (Language Workgroup), 6 Mar 2022, asked whether Raku's experience with significant whitespace had been fine ([#21](https://forums.swift.org/t/pitch-regex-syntax/55711/21)).
- Outcome: no grammar facility exists in the language. Regex literals and RegexBuilder shipped in Swift 5.7 ([SE-0351](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0351-regex-builder.md)).

**Junctions (considered-and-rejected, by a Language Workgroup member)**
- Becca Royal-Gordon, 11 Apr 2023, said a substring-predicate pitch was "*very* like a feature from Perl 6/Raku called junctions". Her case against: in "Raku's unrestricted form, it created a lot of implicit loops, sometimes making it difficult to figure out the time complexity", and overloading would hurt type-checking. She recommended shipping a package instead ([#10](https://forums.swift.org/t/pitch-simplify-substring-searching-with-custom-operators-and-predicates/63915/10)).
- Membership: she was on the Language Workgroup in Jan 2023 with Doug Gregor, Joe Groff, Xiaodi Wu and others ([swift.org, Wayback 14 Jan 2023](https://web.archive.org/web/20230114082043/https://www.swift.org/community/)).
- Outcome: the pitch was not pursued ([#11](https://forums.swift.org/t/pitch-simplify-substring-searching-with-custom-operators-and-predicates/63915/11)).

**Other prior-art mentions (none adopted)**
- Brent Royal-Gordon, in the SE-0077 review of 18 May 2016: "The only language I know which has equivalently powerful custom operator specification is Perl 6", meaning Perl 6's `is tighter` / `is looser` / `is equiv` ([#5](https://forums.swift.org/t/review-se-0077-improved-operator-declarations/2661/5)).
  - SE-0077's relative `precedencegroup` design was already in the proposal and does not credit Perl 6 ([SE-0077](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0077-operator-precedence.md)). Classification: prior art cited in review, convergent design.
- Brent, the same day in the SE-0091 review: "Short of Perl 6's solution … full multimethods" ([#2](https://forums.swift.org/t/review-se-0091-improving-operator-requirements-in-protocols/2665/2)). Passing mention.
- Brent's "Splat" pitch (10 Feb 2016) proposed a `*` splat "Similar to usage in Ruby and Perl 6", which was not adopted ([#1](https://forums.swift.org/t/splat/1383/1)). He also described Perl 6's container traits in the SE-0030 property-behaviors review ([#4](https://forums.swift.org/t/review-se-0030-property-behaviors/1385/4)).
- Brent, 31 Mar 2017, offered Perl 6's `Stringy` role as precedent when naming the string protocol, and recommended `StringProtocol` instead ([#14](https://forums.swift.org/t/pitch-string-revision-proposal-1/5546/14)). Shipped as [StringProtocol](https://developer.apple.com/documentation/swift/stringprotocol).
- Becca in the SE-0200 review, 23 Mar 2018: Swift's string handling "more closely resembles scripting languages (especially Perl 6)". Becca argued for `'` as an alternative delimiter ([#127](https://forums.swift.org/t/se-0200-raw-mode-string-literals/11048/127)). Swift 5 shipped `#"…"#` instead ([SE-0200](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0200-raw-string-escaping.md)).
- jawbroken, 3 May 2019, cited "precedent in Julia, Matlab and Perl 6" for end-relative indexing ([#70](https://forums.swift.org/t/pitch-offsetting-indices-and-relative-ranges/23837/70)). The pitch did not ship.

**Cautionary tale from the language leadership**
- Douglas Gregor (Language Workgroup, forum staff), 18 Jan 2023: "Perl 5 -> 6 just didn't happen." He offered it as a cautionary tale, beside Swift 2→3 and Python 2→3, for the Swift 6 language mode ([#57](https://forums.swift.org/t/design-priorities-for-the-swift-6-language-mode/62408/57)).

### Inferences
- The closing-delimiter dedent rule now has a documented Perl 6 → Swift path:
  1. Perl 6 rule suggested on swift-evolution (Royal-Gordon, 25 Apr 2016).
  2. Adopted "as per Perl6" by Holdsworth, SE-0168's lead author (5 May 2016).
  3. Specified, uncredited, in SE-0168.
  4. Accepted unchanged by the core team (19 Apr 2017).
  5. Shipped in Swift 4.

  Unlike the regex case, this one shipped essentially intact; Swift adds a hard error for under-indentation where Raku only warns.
- Core-team engagement with Perl 6 regexes and grammars ran from Dec 2015 to Mar 2020 and involved Lattner, Groff and Abrahams, not only Ilseman. It never became grammar support.

### Gaps
- No post by a core-team member explicitly acknowledging that the SE-0168 dedent rule came from Perl 6 was found. The link runs through the authors and the review.
- Whether Lattner, the main respondent in that thread, read the 25 Apr 2016 suggestion is not documented.

## Q3. Rust: internals.rust-lang.org, users.rust-lang.org, and a Rust-team tooling artifact

### Takeaway
There is one real **adoption**, but in tooling, not the language. The Rust Grammar Working Group's grammar notation took Perl 6's `%` / `%%` separator quantifier modifiers, as stated by eddyb, then a language/compiler-team member. The rest is survey rows (chained comparisons, named arguments, safe navigation) and cautionary tales.

### Cited Findings
- eddyb, 21 Sep 2018, "Proposal: Grammar Working Group": the notation supports "`X* % Y` and `X+ % Y` … (the syntax using `%` is from Perl 6, IIRC, and was suggested by @eternaleye)" ([#8](https://internals.rust-lang.org/t/proposal-grammar-working-group/8442/8)).
  - The [gll README](https://github.com/lykenware/gll/blob/master/README.md) documents `A* % ","` and `A* %% ","` ("with an optional trailing comma"), which are Raku's `%` and `%%` semantics.
  - The rust-lang/wg-grammar files use `%%` throughout, for example `args:Expr* %% ","` ([expr.lyg](https://github.com/rust-lang/wg-grammar/blob/master/grammar/expr.lyg)).
  - eddyb was on the language design team in 2017 ([rust-lang.org team page, Wayback Mar 2017](https://web.archive.org/web/20170316062235/https://www.rust-lang.org/en-US/team.html)) and is a compiler/lang alumnus ([team repo](https://github.com/rust-lang/team/blob/master/teams/compiler.toml)).
  - Classification: **adopted-from** (tooling). The Rust Reference uses its own grammar notation, not `%%`.
- kennytm (now on the libs team, per the [team repo](https://github.com/rust-lang/team/blob/master/teams/libs.toml)), 12 Oct 2020, Pre-RFC "Python-like chained comparison operators": "for Python and Raku (Perl 6), in `a < b < c` when `a < b` is false the next expression `c` will *not* be evaluated" ([#20](https://internals.rust-lang.org/t/python-like-chained-comparison-operators/13191/20)).
  - Outcome: comparison operators still "Require parentheses" ([Rust Reference, operator precedence](https://doc.rust-lang.org/reference/expressions.html)). Classification: cited-as-prior-art; not adopted.
- Yehuda Katz (Rust core team in 2016–17, per the [team page](https://web.archive.org/web/20170316062235/https://www.rust-lang.org/en-US/team.html)), 19 Aug 2016, in the `?` operator debate: "Perl 6 uses `.?`" ([#9](https://internals.rust-lang.org/t/the-operator-will-be-harmful-to-rust/3882/9)). The thread opener had listed Perl 6 among languages with safe navigation ([#1](https://internals.rust-lang.org/t/the-operator-will-be-harmful-to-rust/3882/1)). Outcome: `?` shipped as error propagation. Passing mention.
- Steve Klabnik (core team at the time, per the [same page](https://web.archive.org/web/20170316062235/https://www.rust-lang.org/en-US/team.html)), quoted as TWiR quote of the week, 18 Dec 2015: "I don't want a Ruby 1.9/Python 3/Perl 6 situation." ([#187](https://users.rust-lang.org/t/twir-quote-of-the-week/328/187)) Cautionary tale.
- Other internals threads, none by a team member:
  - Kyle Strand on the await-syntax committee process (21 May 2019): cautionary ([#372](https://internals.rust-lang.org/t/a-final-proposal-for-await-syntax/10021/372)).
  - The named-arguments pre-RFC survey lists Raku (2 Apr 2022): prior art, not adopted ([#1](https://internals.rust-lang.org/t/pre-rfc-named-arguments/16413/1)).
  - Perl 6's switchable parser offered as a contrast (11 Jan 2018): passing ([#126](https://internals.rust-lang.org/t/pre-rfc-catching-functions/6505/126)).
  - `~` concatenation "used by Perl 6" (19 Sep 2016): passing ([#23](https://internals.rust-lang.org/t/implement-add-for-string-string/4088/23)).
  - Scott Olson suggested a Rust equivalent of Perl 6's Bisectable bot (23 Dec 2016) ([#9](https://internals.rust-lang.org/t/rust-ci-release-infrastructure-changes/4489/9)). The [cargo-bisect-rustc README](https://github.com/rust-lang/cargo-bisect-rustc/blob/master/README.md) credits no such origin. Passing.
- users.rust-lang.org, all passing mentions:
  - Raku's "whatever star" lambdas missed in Rust ([#14](https://users.rust-lang.org/t/things-you-thought-were-awful-but-changed-your-mind-about/83887/14)).
  - Perl 6 default rationals ([#19](https://users.rust-lang.org/t/floating-rationals-proposal/33160/19)).
  - Slangs ([#38](https://users.rust-lang.org/t/the-internationalization-of-rust-itself/14545/38)).
  - "Perl 6 style regex" crate `rules` ([#19](https://users.rust-lang.org/t/list-of-crates-that-improves-or-experiments-with-rust-but-may-be-hard-to-find/17806/19)).
  - Cautionary: khimru 2024 and 2026 ([#16](https://users.rust-lang.org/t/are-there-any-particular-reasons-for-rust-having-to-maintain-backwards-compatibility-and-not-deprecating-std-apis/105182/16); [#69](https://users.rust-lang.org/t/async-without-tokio/140342/69)).

### Inferences
- The `%`/`%%` borrowing is narrow: a regex quantifier modifier carried into a parser-generator notation, via a community member (eternaleye) and a team member (eddyb). It is real, documented, and inside a rust-lang repository.

### Gaps
- Whether rust-lang/wg-grammar is archived or still maintained was not checked.

## Q4. Python: discuss.python.org (2017–) and the python-ideas/python-dev archives (2000–)

### Takeaway
Python's core developers mentioned Perl 6 for twenty years, but almost always to dismiss an idea ("or because Perl 6 does it") or as the anti-model for Python 3000. The substantive prior-art citations, all of which Python declined, were `.assuming` (PEP 309), hyper-operators (PEP 335), feed operators, a given/when-style switch, Unicode operators, regex literals and grapheme-aware `reverse`. One message strengthens the existing PEP 3134 row.

### Cited Findings
- Ka-Ping Yee, PEP 344 author, 20 May 2005: "the only support for that i could find was the proposed @@ in Perl 6". This refers to his research on implicit exception chaining ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/GXDQLU2RAKBO35P2XOLRBR3YP6CPSGRB/)). Shipped as `__context__` in [PEP 3134](https://peps.python.org/pep-3134/). Classification: cited-as-prior-art, and the only precedent the author found.
- Gisle Aas, 24 Feb 2004, during the PEP 309 naming poll: "Perl 6 plans to use f.assuming() for this." ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/SC6A7IZTHCCGOAZ7J2RZBSW3G235PJNA/)) Python chose `functools.partial` ([PEP 309](https://peps.python.org/pep-0309/)). Considered, not adopted.
- James Y Knight, 15 Sep 2004, on PEP 335: "Perl6 has distinct element-wise operators ("hyper" operators)." ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/EJ6SPHEID4NDCREDZFEWBZHN6W6V4UEU/)) [PEP 335](https://peps.python.org/pep-0335/) was rejected. Prior art.
- Georg Brandl (core developer), 17 Feb 2006, answered a `foo(x) -> bar()` chaining idea with a link to S03's piping operators ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/ECSDTVX26S5NWMHUWTCOQIO362BP4UM3/)). Not adopted. Passing mention.
- Guido van Rossum, 19 Jun 2006, in the switch-statement debate: Perl 6's general matching syntax is worth studying "just to see how far one can stretch the insanity" ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/5OV3B3WNDUAEWR3DHDUHZHMBHVQRQV7U/)). [PEP 3103](https://peps.python.org/pep-3103/) was rejected. Cautionary tale.
- Guido, 31 Jan 2005: "the Python 3000 effort shouldn't be as pervasive as the Perl 6 design" ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/HA6U5GSTAKNUH2BNZTEDEUCN7ARPDUAX/)). Cautionary tale; it predates the 2006 Artima post the report cites.
- Guido, 16 Nov 2017, on making `re.VERBOSE` ignore Unicode Pattern_White_Space: "Let's not change things just because we can, or because Perl 6 does it." ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/KW5LHNXQEGHEK5SB3FTK7DKPVQHFJGCO/)) The verbose-mode whitespace set is still ASCII-only (`WHITESPACE = frozenset(" \t\n\r\v\f")` in [Lib/re/_parser.py](https://github.com/python/cpython/blob/main/Lib/re/_parser.py)). Considered-and-rejected.
- Guido, 9 Jun 2020, on paren-less calls: "(Ruby? R? Raku?)", quoted in [python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/U42BCLKSM4PM2BBU6PLBLFZXRVMVUNI4/). Passing mention.
- Brett Cannon (core developer), 26 Feb 2003: "There is obviously always the option to follow Perl 6's suggested new syntax." He also floated a more Pythonic regex syntax ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/GO6CK4DYVMNH2D32RKR55J3YWZCB5OQO/)). Not pursued.
- Steven D'Aprano, CPython core developer since 2014 per the [devguide core-team.csv](https://github.com/python/devguide/blob/main/core-team/core-team.csv):
  - 31 Oct 2016 cited Perl 6's Unicode operators ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/R4T6Y7XKNELHCCLEBEB6VAF2GMRVBZ3Z/)). Stephen J. Turnbull pushed back ([reply](https://mail.python.org/archives/list/python-ideas@python.org/message/Q2GTNVLWEBLZPM3IZRKJ6ZHNS4USKHG4/)), and Python has no Unicode operators.
  - 28 Mar 2017, on regex literals: "Perl 6 uses m/regex/ … I doubt that this will actually be useful." ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/AX76DGFWMXFRYRYQFXZWRAQSJOFWKP2D/)) Considered-and-rejected.
  - 13 Feb 2023 listed "Raku (Perl 6)" among gradual-typing languages ([discuss](https://discuss.python.org/t/extend-type-hints-to-cover-exceptions/23788/24)). Passing.
  - 1 Aug 2022 quoted A5 on regex culture ([discuss](https://discuss.python.org/t/how-do-i-remove-xxx-from-dir-printing/17811/11)). Passing.
- Dismissals by core developers, in the context of PEP 505 and PEP 584:
  - Terry Reedy, 19 Jul 2018, on PEP 505: "it looks like noise (or like Perl 6, which is the same)" ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/KLAOSSZRYCK7EVSZPOS34VAMISFZUJMJ/)). [PEP 505](https://peps.python.org/pep-0505/) is deferred.
  - Antoine Pitrou, 15 Mar 2019, on PEP 584: "This is an argument for Perl 6, not for Python." ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/QQLK3T7UPZ4KGX7XFR367QTNCCHRERFJ/)) [PEP 584](https://peps.python.org/pep-0584/) was accepted anyway.
- Cautionary tales:
  - Barry Warsaw, 24 Jul 2000: "The Perl6 and Tcl crews should take a lesson here." ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/RP3CRS7I2GETZQ33XBD7FQ2H5PMNGGQP/))
  - Łukasz Langa, 17 Aug 2014: "please make them not follow the Perl 6 example!" ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/PGJMNZ5GVLAMOW3NFTRTP6OALAQFEHFY/))
- Prior art raised in passing, none adopted:
  - Reinhold Birkenfeld (Georg Brandl), 11 Mar 2005, joked about saving `any()`/`all()` "for junctions, like those in Perl 6" ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/O6ZH3XMMWSH3NEQOBJOJRP3BSMKUPAGB/)).
  - A Dec 2007 multiple-dispatch thread quoted Perl 6 RFC 256 ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/ALC77SFIJAO5A3CWH6YLGKIZ64KMVXYC/)).
  - Sep 2018: a grapheme-aware `str.reverse` proposal citing Perl 6 `flip`, with a reply by core developer M.-A. Lemburg ([python-ideas](https://mail.python.org/archives/list/python-ideas@python.org/message/LWKCIF4WOMBHVMRX66VR5IRD6MD6FDBP/)).
  - Jul 2008, James Y Knight, before PEP 393: "maybe with the exception of the unreleased perl6" ([python-dev](https://mail.python.org/archives/list/python-dev@python.org/message/E4CW2D252J64O2B3LGS3CXAMFQTFHKWR/)).
- discuss.python.org, other posts:
  - Karthikeyan Singaravelan (badge: CPython core developer) on Perl 6 grants ([#2](https://discuss.python.org/t/howto-engage-python-contributors-in-the-long-term/771/2)). Passing.
  - Peter Lovett on PEP 505 and on safe navigation ([#9](https://discuss.python.org/t/add-none-coalescing-operator-in-python/8292/9); [#54](https://discuss.python.org/t/introducing-a-safe-navigation-operator-in-python/35480/54)). Cautionary.

### Inferences
- Python's record is the strongest evidence that core developers knew Perl 6's design and used it mainly as an argument against features.

### Gaps
- The python-3000 list (2006–08), where the PEP 3102 and PEP 3119/3133 debates happened, could not be searched.

## Q5. Ruby: bugs.ruby-lang.org (Redmine issues and journals)

### Takeaway
There are eleven new real hits. Matz argued from (a mis-described) Perl 6 `use` when naming refinements' `using`. Perl 6's MAIN, `assuming`, multi-dispatch and named parameters were all put to the core team and declined, or, for named parameters, already planned independently. nobu still mentions `^..` in 2025.

### Cited Findings
- Matz, 27 Nov 2010, defended the `using` keyword for refinements: no language uses "use" for namespaces, and "Perl6's 'use' is to mix-in traits, just like Ruby's 'include'" ([#4085 note-42](https://bugs.ruby-lang.org/issues/4085#note-42)). `using` shipped with refinements in Ruby 2.0 ([Ruby 2.0.0 release](https://www.ruby-lang.org/en/news/2013/02/24/ruby-2-0-0-p0-is-released/)). Classification: passing mention by the language's creator. His description is inaccurate, since Perl 6's `use` imports modules lexically and roles are applied with `does`/`but`.
- #5011, 10 Jul 2011, "Add Perl 6-like main method definition support: accept command-line arguments as method arguments" ([#5011](https://bugs.ruby-lang.org/issues/5011)). Mame relayed the decision on 23 Jul 2012: "rejected at the developer meeting (7/21). Matz said that it was not a bad idea to improve optparse, but that this proposal was not in the right direction." ([note-7](https://bugs.ruby-lang.org/issues/5011#note-7)) Considered-and-rejected.
- #7939, 24 Feb 2013, "I really like the new 'assuming' method used for currying in Perl 6." ([#7939](https://bugs.ruby-lang.org/issues/7939))
  - nobu: "It's Partial Application, not currying." ([note-1](https://bugs.ruby-lang.org/issues/7939#note-1))
  - Matz, 24 Feb 2014: "we need new name" ([note-8](https://bugs.ruby-lang.org/issues/7939#note-8)). The status is still Feedback.
  - Considered, not adopted.
- #4172, 20 Dec 2010, named parameters, citing that "Perl 6 supports named arguments … out-of-the-box" ([#4172](https://bugs.ruby-lang.org/issues/4172)). Matz: "Ruby 2.0 will have named arguments. The spec is bit different from your proposal though." ([note-2](https://bugs.ruby-lang.org/issues/4172#note-2)) Keyword arguments shipped in 2.0. Prior art; Ruby's design was independent.
- #6711, 10 Jul 2012: Perl 6 "multi dispatcher" offered as a model; rejected ([note-5](https://bugs.ruby-lang.org/issues/6711#note-5)).
- #5372, 4 Oct 2011: Perl 6's Mu/Failure/`.defined` offered as precedent for `blank?`; rejected ([note-9](https://bugs.ruby-lang.org/issues/5372#note-9)).
- #14777, 19 May 2018: a commenter showed that Perl 6 Range arithmetic is the behaviour requested for `Range#+`; the issue is still open ([note-2](https://bugs.ruby-lang.org/issues/14777#note-2)). Prior art.
- #21284, 27 Apr 2025, nobu (core committer): "Current Ruby doesn't have start-exclusive range. `^..` is so-called neko-operator in Perl 6" ([note-3](https://bugs.ruby-lang.org/issues/21284#note-3)). Passing mention; this extends the known #12133 to 2025.
- #2034, 6–7 Sep 2009: "Perl 6 is optionally linking against ICU". naruse (core) replied, "Ruby hates compile options which change behaviors over Ruby Layer." ([note-8](https://bugs.ruby-lang.org/issues/2034#note-8)) The issue was rejected.
- Passing: #17496 lists Raku among languages with `TAU` ([note-23](https://bugs.ruby-lang.org/issues/17496#note-23)); #15317 asks how perl6 handles obsolete Unicode values ([note-2](https://bugs.ruby-lang.org/issues/15317#note-2)). #6546 was noise (libwww-perl-6.04).

### Inferences
- Ruby's core team considered, and declined, a Perl 6-style MAIN in 2012. That fits the report's "CLIs generated from signatures" row as a near miss.

### Gaps
- The ruby-core and ruby-dev mailing-list archives outside Redmine were not searched.

## Q6. PHP internals (externals.io)

### Takeaway
PHP internals cite Perl 6 mostly as the canonical warning: "this decade's Perl 6", and "too much towards Perl 6 direction" from Stas Malyshev against the 2016 pipe operator. A few prior-art mentions (junctions, NFG, `//`/`err`, Str decrement) led nowhere. A 2013 thread is an early precursor of the PHP 8.3 string-increment work.

### Cited Findings
- [Thread](https://externals.io/message/68160), 18–19 Jul 2013, "Allow letter decrementing". On 18 Jul Sara Golemon (core) answered "why … doesn't it?" with "Because Perl." ([68176](https://externals.io/message/68160#68176)). On 19 Jul Peter Lind cited S03, noting that "although Perl 6 is apparently capable of decrementing strings, it doesn't fully mirror the incrementing" ([68184](https://externals.io/message/68160#68184)). No change was made in 2013, and the 2023 saner-inc-dec RFC is already in the report. Prior art.
- Stanislav Malyshev (core), 30 Apr 2016, on the pipe operator RFC: "I think this takes the syntax too much towards Perl 6 direction." ([message 92968](https://externals.io/message/92960#92968)) On 9 May 2016 he added: "maybe Perl 6 has something like that because what doesn't it have?" ([message 93124](https://externals.io/message/92960#93124)). The [2016 RFC](https://wiki.php.net/rfc/pipe-operator) stayed "Under Discussion". `|>` finally shipped through [pipe-operator-v3](https://wiki.php.net/rfc/pipe-operator-v3) (Implemented, PHP 8.5). Cautionary tale that did not ultimately prevail.
- Adam Harvey (core), 21 Aug 2014, on the Integer Semantics RFC: "PHP 7 will risk becoming this decade's Perl 6: the cautionary tale for what happens when you burn all the boats." Levi Morrison replied that the RFCs were not breaking enough "to create the next Perl 6" ([76755](https://externals.io/message/76753#76755); [thread](https://externals.io/message/76753)). Cautionary tale.
- Rowan Collins, 28 Apr 2016, in the intersection-types RFC thread: "It would be amazing to have something like Perl6's 'Junctions'", for example `$foo instanceof any(Foo, Bar)` ([message](https://externals.io/message/92857#92870)). PHP 8.1 shipped `A&B` [pure intersection types](https://wiki.php.net/rfc/pure-intersection-types). Considered, not adopted.
- Andrea Faulds (core, 21 Oct 2014) and Rowan Collins (22 Oct 2014), on the UString RFC, cited Perl 6's NFG as a possible internal encoding ([message](https://externals.io/message/78174#78213); [message](https://externals.io/message/78219#78219); attributions from the search results). The [RFC](https://wiki.php.net/rfc/ustring) never left "Under Discussion". Prior art.
- Christian Schneider, 10 Jul 2004: "I'd even prefer the Perl 6 err operator over it which is '//' or 'err'" ([message](https://externals.io/message/10963#11062)). PHP later shipped `?:` and then `??` ([isset_ternary RFC](https://wiki.php.net/rfc/isset_ternary)). Prior art, not adopted in that spelling.
- Rowan Collins, 3 Jan 2018: "I borrowed the term 'container' from Perl6" for his typed-variables write-up ([message](https://externals.io/message/101477#101526)). Passing mention.
- Earlier cautionary items, attributions from search snippets only:
  - "Proposal: Array syntax", 5 Nov 2003: replies quoted the warning "ever closer to Perl 6 where there are dozens of way to do the same thing" ([search hit](https://externals.io/message/5194#5220)).
  - Andi Gutmans joked in "PHP 5.5 or 6.0" on 9 Aug 2005 ([search hit](https://externals.io/message/17668#17734)).
  - Later: [2019](https://externals.io/message/103779#103902), [2019](https://externals.io/message/107096#107101).
- Bruce Weirdan, 13 May 2021, corrected the PFA RFC's "renamed to Racket" ([message](https://externals.io/message/114157#114468)). Passing mention.

### Gaps
- The author of the 2003 "ever closer to Perl 6" line, and the 2004 ifsetor message (`/message/9208#9277`, search snippet: "That doesn't look perlish, it is Perl. Perl 6 actually"), could not be opened. news.php.net was unreachable and raw sources need a login.

## Q7. C#: dotnet/csharplang Discussions

### Takeaway
Only community posts were found, and no LDM member argued from Raku. One 2023 discussion asked for comparison chaining "after reading about Raku", which is the prehistory of the approved 2026 proposal.

### Cited Findings
- [#6899](https://github.com/dotnet/csharplang/discussions/6899), 20 Jan 2023, The-Futurist: "I was recently looking at this capability for a new language after reading about Raku". HaloFour pointed to the existing #4108. The champion issue [#8861](https://github.com/dotnet/csharplang/issues/8861) (Dec 2024) links discussion #8643, not #6899. Prior art; the decision is already in the report.
- [#198](https://github.com/dotnet/csharplang/discussions/198), 27 Feb 2017, HaloFour's range-operator questions list `var perl = 5 ^.. 10; // 6, 7, 8, 9, 10`, Perl 6 syntax labelled "perl". This is the companion to the known #185 comment. C# 8 shipped `..` and `^`. Considered, not adopted.
- [#4744](https://github.com/dotnet/csharplang/discussions/4744#discussioncomment-1902158), 3 Jan 2022: Raku is listed among languages whose `%` is floored modulo. Passing mention.
- [#9552](https://github.com/dotnet/csharplang/discussions/9552), 27 Jul 2025: Raku is listed as having native partial application. The outcome is pending.
- [#226](https://github.com/dotnet/csharplang/discussions/226), 4 Mar 2017: "Perl6 … the parser itself is just a Perl6 object accessible (and mutable!) at run-time", in an inline-languages proposal. Passing mention.
- [#5735](https://github.com/dotnet/csharplang/discussions/5735), Feb 2022, the `!!` debate: orthoxerox wrote "Perl 6 was a failure" (cautionary), and JesperTreetop listed Raku among languages with macros (passing). Other "Perl" remarks there are generic.

## Q8. D: forum.dlang.org (2001–2025)

### Takeaway
D's leaders knew Perl 6 well and said so. Walter Bright disclaimed influence in 2001. Andrei Alexandrescu cited Perl 6 chained comparisons while D made `a < b < c` illegal (2007). Don Clugston called A5 "a must-read" for D's regex design. Later posts are warnings.

### Cited Findings
- Walter Bright, 21 Aug 2001, asked whether D's design echoed Perl 6: "I haven't followed Perl 6, and so any similarities are entirely coincidental." ([post](https://forum.dlang.org/post/9lsnrc$1kdo$1@digitaldaemon.com); [question](https://forum.dlang.org/post/3B81D17D.F9CCF518@estarcion.com)) This supports convergent verdicts for D-era features.
- Andrei Alexandrescu, 8 Feb 2007, thread "make (a < b < c) illegal?": "BTW, Perl 6 is slated to implement chain comparisons as well" ([post](https://forum.dlang.org/post/45CAADBF.4000100@erdani.org)).
  - In the same thread Walter wrote that "Andrei has proposed (and I agreed) that … comparison operators should no longer be associative" (quoted on the same page).
  - D's grammar makes `RelExpression` non-associative (`ShiftExpression < ShiftExpression`) ([D spec, expressions](https://dlang.org/spec/expression.html)).
  - Considered-and-rejected: D chose a ban, which keeps chaining possible later.
- Andrei, 8 Feb 2007, in the compile-time regex thread: "Perl 6 is going to be great. The regexes had a few warts that were overdue for a fix." ([post](https://forum.dlang.org/post/45CA8BF2.3050107@erdani.org))
- Don Clugston (core contributor), 21 Feb 2006: template regexes should make it "possible to implement Perl 6-style regexps" ([post](https://forum.dlang.org/post/dtev42$1g98$1@digitaldaemon.com)). On 19 Feb 2009: "I believe this Perl6 document is a must-read", meaning A5 ([post](https://forum.dlang.org/post/gnjiel$2b7u$1@digitalmars.com)). Phobos `std.regex` kept conventional syntax. Prior art, not adopted.
- Andrei, 15 Dec 2006: Perl 6 RFC 149 (lvalue subs) as a cautionary data point in the lvalue-return debate ([post](https://forum.dlang.org/post/elteht$1kcg$3@digitaldaemon.com)).
- Andrei, 20 Feb 2010: Damian Conway's Perl6::Slurp as name precedent for a Phobos function ([post](https://forum.dlang.org/post/hlp3bv$1als$1@digitalmars.com)). [`std.file.slurp`](https://dlang.org/phobos/std_file.html#slurp) exists. Cited as naming prior art, though the module is a Perl 5 CPAN module.
- Const-by-default parameters, June 2007: Deewiant told Walter that "Perl 6 has const by default for subroutine parameters" ([post](https://forum.dlang.org/post/f4gv5a$kvd$1@digitalmars.com)), and David B. Held credited Larry with "first-class constants" ([post](https://forum.dlang.org/post/f42qrl$1vrr$1@digitalmars.com)). D2 did not make parameters const by default. Prior art.
- Junctions: Mafi, 9 Jan 2011, "Just tried to implemnt Perl6-like junctions" as a library ([post](https://forum.dlang.org/post/igcoqh$1tir$1@digitalmars.com)). Not taken into Phobos.
- Unicode and autodecoding, May–June 2016: Guillaume Chatelet, replying to Walter on autodecoding, wrote "It looks like Perl 6 got it right" and linked the Perl 6 Unicode advent post ([post](https://forum.dlang.org/post/riezoslhyyqbnzyupntl@forum.dlang.org); [thread](https://forum.dlang.org/post/gadvnjanamgwetgritgz@forum.dlang.org)). Prior art.
- Elronnd, 4 Nov 2021, proposed Raku's utf8-c8 for lossless invalid-UTF handling ([post](https://forum.dlang.org/post/izjemswmdaagrmktqroh@forum.dlang.org)). Prior art.
- bearophile's 2013 Phobos request (Issue 5502): "in Perl6 it's named 'classify'" ([post](https://forum.dlang.org/post/knq5q5$9o7$1@digitalmars.com)). Prior art.
- Cautionary tales:
  - Don Clugston, 14 Dec 2011: "Look at Python2 vs Python3, Perl6 vs Perl5." ([post](https://forum.dlang.org/post/jc97mn$257t$1@digitalmars.com))
  - Jonathan M Davis, Nov 2012 ([post](https://forum.dlang.org/post/mailman.1572.1352139623.5162.digitalmars-d@puremagic.com)).
  - Walter Bright, 14 Mar 2015: "Didn't Perl 6 go through a paroxysm, too?" ([post](https://forum.dlang.org/post/me239u$1uj0$1@digitalmars.com))
  - Paul Backus, 25 Nov 2024, against a LINQ-like DIP idea: "This looks like something straight out of Perl 6/Raku--an entire DSL built into the language" ([post](https://forum.dlang.org/post/uxugcjqyhvuylklvwzrk@forum.dlang.org)).
- Kublai Kahn, 26 Feb 2003, posted Perl 6's given/when as a switch model ([post](https://forum.dlang.org/post/b3h655$1ng7$1@digitaldaemon.com)). Not adopted.

### Gaps
- Team roles for Don Clugston, Jonathan M Davis and Paul Backus are given from general knowledge, not from a roster page.

## Q9. Other forums: Scala, Julia, Kotlin, Haskell, OCaml, Crystal, Zig, Clojure, Nim, Elm, Erlang, Racket, Modular, Elixir

### Takeaway
Nothing here borrows from Raku. The only leadership mentions are Martin Odersky's cautionary use of Perl 6 in the Scala 3 migration debate and Stefan Karpinski's aside on multiple dispatch.

### Cited Findings
- Martin Odersky (Scala's creator; forum title "SIP Committee member"), 4 Mar 2019: "Perl 6: Was in announcement state forever. Scala 3 will go into feature freeze this summer" ([#23](https://contributors.scala-lang.org/t/move-slowly-and-iterate/2888/23)). Cautionary tale. Other Scala posts compare Scala 3 with "Raku to Perl" ([#570](https://contributors.scala-lang.org/t/feedback-sought-optional-braces/4702/570); [#94](https://contributors.scala-lang.org/t/meanings-of-underscore-including-wildcard-imports/4843/94); [#1](https://contributors.scala-lang.org/t/lets-have-a-fresh-start-by-keeping-the-dotty-name/3896/1)).
- Julia:
  - Stefan Karpinski (co-creator; forum admin, "Steward"), 23 Feb 2017: "Dylan is the only language that’s taken multiple dispatch nearly as seriously as Julia. (And apparently Perl 6, from what I hear.)" ([#10](https://discourse.julialang.org/t/julia-motivation-why-werent-numpy-scipy-numba-good-enough/2236/10)). Passing; it confirms Julia's dispatch lineage runs through Dylan.
  - Matt Bauman (admin) listed Raku's identifier docs in a Unicode-identifier survey, 5 Jan 2024 ([#22](https://discourse.julialang.org/t/syntax-escape-hatch-for-unicode-haters/108363/22)).
  - Páll Haraldsson repeatedly cited Raku's default rationals (2020–24) ([#24](https://discourse.julialang.org/t/surprise-with-rational/88694/24)). Not adopted.
- Kotlin: one name-list mention only ([#3](https://discuss.kotlinlang.org/t/kotlin-adinin-yer-adiyla-baglantisi-uzerine-iyi-amacli-soru-good-faith-question-about-kotlin-s-place-name-based-naming/31106/3)).
- Haskell: passing mentions ([#19](https://discourse.haskell.org/t/learning-from-the-no-in-eq-debate/3651/19)).
- OCaml: passing mentions ([#74](https://discuss.ocaml.org/t/expanding-ocaml-appeal-to-future-open-source-application-developers/7738/74); [#90](https://discuss.ocaml.org/t/what-are-the-biggest-reasons-newcomers-give-up-on-ocaml/10958/90)).
- Crystal: passing and cautionary mentions ([#22](https://forum.crystal-lang.org/t/unexpected-arithmetic-behavior/8276/22)).
- Clojure: a user suggests MoarVM as a backend ([#1](https://clojureverse.org/t/clojure-should-consider-moarvm-as-a-backend/8709/1)).
- Zig: a Rakudo core developer's Zig experiments ([ziggit](https://ziggit.dev/t/an-initial-investigation-into-using-zig-to-speed-up-raku-code/2289/1)), and a user suggesting Raku-style `CATCH` placement ([#5](https://ziggit.dev/t/try-for-a-block-not-a-function/10278/5)). No Zig team response.
- Nim: no Araq or core posts. Users offered Perl 6 phasers as a loop model ([post](https://forum.nim-lang.org/t/1990#12433)) and used Perl 6 as a cautionary tale for Nim 2 ([post](https://forum.nim-lang.org/t/9132#59644)).

## Q10. Where did core or design-team members argue from Raku's design?

### Takeaway
Core-team members engaged most seriously in Swift (Lattner, Groff, Abrahams on regexes and grammars; Royal-Gordon on junctions; the SE-0168 authors on heredoc dedent), in D (Alexandrescu, Clugston) and in Rust (eddyb on `%`). In Python, Scala and PHP the leadership invoked Perl 6 almost only as something to avoid.

### Cited Findings
- **Argued from Raku's design, positively or as a model:**
  - Swift: Lattner, 2015–2020 ([335/4](https://forums.swift.org/t/empower-string-type-with-regular-expression/335/4), [876/23](https://forums.swift.org/t/empower-string-type-with-regular-expression/876/23), [7120/8](https://forums.swift.org/t/pitch-raw-mode-string-literals/7120/8), [34373/10](https://forums.swift.org/t/regular-expressions-in-swift/34373/10)); Groff 2017 ([4939/68](https://forums.swift.org/t/strings-in-swift-4/4939/68)); Abrahams 2017 ([4939/153](https://forums.swift.org/t/strings-in-swift-4/4939/153)); the SE-0168 authors Royal-Gordon and Holdsworth, 2016–17 ([015738](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160425/015738.html), [016646](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160502/016646.html)).
  - Rust: eddyb, 2018 ([8442/8](https://internals.rust-lang.org/t/proposal-grammar-working-group/8442/8)).
  - D: Clugston 2006/2009 ([post](https://forum.dlang.org/post/gnjiel$2b7u$1@digitalmars.com)); Alexandrescu 2007, regexes ([post](https://forum.dlang.org/post/45CA8BF2.3050107@erdani.org)).
- **Weighed Raku's design and decided against it:**
  - Swift: Becca Royal-Gordon, Language Workgroup, on junctions, 2023 ([63915/10](https://forums.swift.org/t/pitch-simplify-substring-searching-with-custom-operators-and-predicates/63915/10)).
  - D: Alexandrescu and Bright on chained comparisons, 2007 ([post](https://forum.dlang.org/post/45CAADBF.4000100@erdani.org)).
  - Python: Guido on re.VERBOSE whitespace, 2017 ([message](https://mail.python.org/archives/list/python-ideas@python.org/message/KW5LHNXQEGHEK5SB3FTK7DKPVQHFJGCO/)) and on switch/matching, 2006 ([message](https://mail.python.org/archives/list/python-dev@python.org/message/5OV3B3WNDUAEWR3DHDUHZHMBHVQRQV7U/)); D'Aprano on regex literals, 2017 ([message](https://mail.python.org/archives/list/python-ideas@python.org/message/AX76DGFWMXFRYRYQFXZWRAQSJOFWKP2D/)).
  - Ruby: Matz and the developer meeting on MAIN, 2012 ([note-7](https://bugs.ruby-lang.org/issues/5011#note-7)); Matz on `assuming`, 2014 ([note-8](https://bugs.ruby-lang.org/issues/7939#note-8)).
- **Passing mentions by leaders:**
  - Matz on Perl 6 `use`, 2010 ([note-42](https://bugs.ruby-lang.org/issues/4085#note-42)); nobu on `^..`, 2025 ([note-3](https://bugs.ruby-lang.org/issues/21284#note-3)).
  - Katz on `.?`, 2016 ([3882/9](https://internals.rust-lang.org/t/the-operator-will-be-harmful-to-rust/3882/9)); kennytm on chained comparisons, 2020 ([13191/20](https://internals.rust-lang.org/t/python-like-chained-comparison-operators/13191/20)).
  - Karpinski on multiple dispatch, 2017 ([2236/10](https://discourse.julialang.org/t/julia-motivation-why-werent-numpy-scipy-numba-good-enough/2236/10)).
- **Cautionary tale:**
  - Swift: Gregor, 2023 ([62408/57](https://forums.swift.org/t/design-priorities-for-the-swift-6-language-mode/62408/57)).
  - Scala: Odersky, 2019 ([2888/23](https://contributors.scala-lang.org/t/move-slowly-and-iterate/2888/23)).
  - Rust: Klabnik, 2015 ([328/187](https://users.rust-lang.org/t/twir-quote-of-the-week/328/187)).
  - Python: Warsaw 2000, Guido 2005, Langa 2014, Reedy 2018, Pitrou 2019 (links in Q4).
  - PHP: Harvey 2014, Malyshev 2016 ([76753](https://externals.io/message/76753), [92960](https://externals.io/message/92960)).
  - D: Bright 2015, Clugston 2011, Backus 2024 (links in Q8).

## Q11. Which findings should change a verdict in the report?

### Takeaway
One finding changes a table verdict: Swift's heredoc dedent. One adds a new, if minor, adoption: Rust's grammar-WG `%`/`%%`. Several add near-miss rows or strengthen existing ones. None touches the "junctions, hyper-operators, Whatever, NFG, slangs have no adopter" conclusion, except that the `%` modifier part of "grammars" now has a tooling adopter.

### Cited Findings
1. **Heredoc dedent row (report line 94 and paragraph at line 112): change Swift from "Independent (convergent)" with source "Nothing" to "Borrowed at the design-discussion stage, uncredited in the proposal".**
   - Evidence: Royal-Gordon, 25 Apr 2016 ([015738](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160425/015738.html)); Holdsworth, 5 May 2016, "as per Perl6" ([016646](https://lists.swift.org/pipermail/swift-evolution/Week-of-Mon-20160502/016646.html)); co-author survey in review ([5611/17](https://forums.swift.org/t/review-se-0168-multi-line-string-literals/5611/17)); acceptance of exactly the closing-delimiter rule ([5715](https://forums.swift.org/t/accepted-se-0168-multi-line-string-literals/5715)).
   - The sentence "Swift 4, PHP 7.3, Java 15 and C# 11 all settled on a closing-delimiter rule … without citing Perl 6" becomes false for Swift. The C# 11 raw-string proposal does not cite Swift or Perl ([proposal](https://github.com/dotnet/csharplang/blob/main/proposals/csharp-11.0/raw-string-literal.md)), so no transitive link to C# was found.
   - This raises the lead's "only four borrowings" outside the Perl family to five. Unlike Swift regex, this one shipped intact.
2. **Grammars / `%` separator:** Rust's Grammar Working Group notation (gll; rust-lang/wg-grammar) took Raku's `%` and `%%`, attributed to Perl 6 by eddyb ([8442/8](https://internals.rust-lang.org/t/proposal-grammar-working-group/8442/8); [expr.lyg](https://github.com/rust-lang/wg-grammar/blob/master/grammar/expr.lyg)). It is tooling, not the Rust language, so it belongs in a footnote or a "transmissions outside languages" note beside Joose. It qualifies "grammars … have no documented adopter" only for this sub-feature.
3. **Chained comparisons:** add D. In 2007 Alexandrescu cited Perl 6, and D made `a < b < c` illegal ([post](https://forum.dlang.org/post/45CAADBF.4000100@erdani.org); [D spec](https://dlang.org/spec/expression.html)). Add Rust's 2020 pre-RFC (Raku cited; comparisons still need parentheses, per [the Reference](https://doc.rust-lang.org/reference/expressions.html)). The 2023 csharplang discussion inspired by Raku ([#6899](https://github.com/dotnet/csharplang/discussions/6899)) precedes the champion issue but is not linked from it.
4. **Junctions:** a new near-miss row. A Swift Language Workgroup member explicitly weighed Raku junctions and advised against them in 2023 ([63915/10](https://forums.swift.org/t/pitch-simplify-substring-searching-with-custom-operators-and-predicates/63915/10)). PHP (2016, [92857#92870](https://externals.io/message/92857#92870)) and D (2011, library experiment) saw wishes only. The "no adopter" verdict stands and is strengthened.
5. **`.assuming` / partial application:** Python's PEP 309 naming poll (2004, [message](https://mail.python.org/archives/list/python-dev@python.org/message/SC6A7IZTHCCGOAZ7J2RZBSW3G235PJNA/)) and Ruby #7939 (2013–14, [note-8](https://bugs.ruby-lang.org/issues/7939#note-8)) join PHP's PFA RFC as designers who saw `assuming` and did not take it.
6. **Exception chaining, PEP 3134 row:** Ka-Ping Yee's 2005 message says Perl 6's `@@` was the *only* precedent he found for implicit chaining ([message](https://mail.python.org/archives/list/python-dev@python.org/message/GXDQLU2RAKBO35P2XOLRBR3YP6CPSGRB/)). This strengthens "cited as prior art" but stops short of "borrowed", because the PEP presents RFC 88 among other prior art.
7. **Swift regex paragraph and "Lattner's list of Swift influences omits Perl entirely":** add that Lattner, Groff and Abrahams, all core-team members, urged Perl 6 regexes and grammars as the model from 2015 to 2020 (Q2 links). The influence list covers Swift 1.0, not the later string work.
8. **"CLIs generated from signatures" row:** add Ruby #5011, a Perl 6-like MAIN rejected at the 21 Jul 2012 developer meeting ([note-7](https://bugs.ruby-lang.org/issues/5011#note-7)).
9. **PHP string-increment row:** add the 2013 precursor thread that cited S03's decrement asymmetry ([68160](https://externals.io/message/68160)).
10. **"Perl 6 is mostly a warning" paragraph:** add leadership quotes from Gregor (Swift, 2023), Odersky (Scala, 2019), Klabnik (Rust, 2015), Langa and Warsaw (Python), Harvey and Malyshev (PHP), and Bright and Clugston (D) (links in Q10). Walter Bright's 2001 statement that "any similarities are entirely coincidental" ([post](https://forum.dlang.org/post/9lsnrc$1kdo$1@digitaldaemon.com)) supports the convergent-reinvention verdicts for D.

### Inferences
- With the Swift heredoc case added, the documented borrowings outside the Perl family share a profile. Each was a small, cleanly reasoned detail (string increment, the dedent rule, the `%` separator, the `->` token) carried by an individual who had been inside the Perl 6 process: Brent Royal-Gordon was on perl6-language and a Parrot pumpking ([719/21](https://forums.swift.org/t/optional-dictionary-subscripts/719/21)).

### Gaps
- Not checked: whether PHP 7.3's flexible-heredoc discussion or Java's text-block design saw the Swift thread. No Perl 6 mention appeared in the PHP results for 2017, and openjdk.org was not contacted.
