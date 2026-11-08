# Perl 6 / Raku side: first-appearance dates for open items, and an audit of the Raku community's own influence claims

Scope: Part A pins first appearances in the design record for nine items left open by `perl6_design_chronology.md`. Part B audits what the Raku/Perl 6 community itself has claimed about other languages borrowing from it. All mailing-list citations give the perl6-language article URL on nntp.perl.org (each URL was fetched and its title checked in this session) plus the Message-ID. Quotations are kept to a minimum; most findings are paraphrased.

Method in one paragraph: the full perl6-language overview (36,873 articles, Aug 2000 – 2014) was pulled over NNTP from nntp.perl.org, and about 3,800 article bodies were fetched selectively (all of 26 Sep – 30 Nov 2002, all 1,664 Synopsis svn-commit mails, 649 Larry Wall posts from Oct 2001 – Sep 2004, and specific threads). The Pugs svn history was read from the `Raku/mu` git mirror, Rakudo's pre-2009 Parrot-era history from `rakudo/rakudo`, and the post-2008 spec from `Raku/old-design-docs`. For Part B, all 1,088 posts of p6weekly.wordpress.com (271), rakuweekly.blog (355, formerly rakudoweekly.blog), perl6advent.wordpress.com (256) and raku-advent.blog (206), plus Jonathan Worthington's 6guts blog (133), were downloaded through the WordPress.com public API and grepped; Hacker News comments were searched through the Algolia API. Reddit returned HTTP 403 to a scripted request and was not pursued further; a Rosetta Code diff page also returned 403.

## Where does the 2002–2008 design record survive?

### Takeaway
The pre-2008 gap in `Raku/old-design-docs` is not real: the complete svn.perl.org Synopsis history (948 commits, from S05 version 1 of July 2002 to the move into the Pugs repository on 26 Nov 2008) is preserved in the `Raku/mu` (Pugs svn) git mirror under `doc/trunk/design/syn/`, and from 23 Feb 2006 every Synopsis commit was also mailed, with its diff, to perl6-language as `[svn:perl6-synopsis] rNNNNN`.

### Cited Findings
- The `Raku/mu` mirror's path `doc/trunk/design/syn` has 948 commits. The earliest is Allison Randal's import of Synopsis 5 (file header: version 1, last modified 7 Jul 2002); the last is Patrick Michaud's move of the Synopses to their new home on 26 Nov 2008 — [first commit ddda1b02a](https://github.com/Raku/mu/commit/ddda1b02a); [Raku/mu](https://github.com/Raku/mu)
- The first ~60 commits all carry git timestamps of 21 Feb 2005 06:16–06:19 UTC, seconds apart: they are a retroactive import of earlier revisions with their original log messages. Their real dates are the `Last Modified` lines inside each file revision, e.g. S03 version 1 (Luke Palmer, 8 Mar 2004), S02 version 0 (dated 10 Aug 2004, last modified 18 Aug 2004), S03 version 2 (13 Sep 2004) — [S03 v1, 3c26898](https://github.com/Raku/mu/commit/3c268982895a37de591536e350a32b83d5e0f5a9); [S01/S02 v0, f3422f9](https://github.com/Raku/mu/commit/f3422f9ffa57cfc6603b4809f984058fa4240394); [S03 v2, 242d926](https://github.com/Raku/mu/commit/242d9261f40875ea292f450258f59a9719732231)
- Commits after Feb 2005 carry their original svn.perl.org dates (e.g. Larry Wall's MAIN commit is dated 11 Aug 2006 00:11 UTC in git and 10 Aug 2006 17:11 PDT in the matching commit mail) — [4a841a8](https://github.com/Raku/mu/commit/4a841a8c2d40fdc2dddc25f23a786f2876ee9737); [p6l msg25795](https://www.nntp.perl.org/group/perl.perl6.language/2006/08/msg25795.html)
- perl6-language archive: the first Synopsis commit mail is article 24749, `r7784`, 23 Feb 2006; the series continues as `rNNNNN - docs/Perl6/Spec` until article 33317 (7 Mar 2010). 1,664 such mails exist — [perl.perl6.language on nntp.perl.org](https://www.nntp.perl.org/group/perl.perl6.language/)
- Weekly "Perl 6 Summary" posts by Piers Cawley are on the same list and corroborate decisions week by week (e.g. 1, 6 and 13 Nov 2002) — [msg12116](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12116.html); [msg12285](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12285.html); [msg12418](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12418.html)

### Inferences
- Statements in `PERL6-FEATURES-IN-OTHER-LANGUAGES.md` and the chronology notes that pre-2008 spec dates "rely on the Apocalypses" can be replaced: any Synopsis-level change from mid-2002 on can be dated to the revision, by `Last Modified` header (2002–Feb 2005) or by commit date and commit mail (Feb 2005 – Nov 2008).
- Wayback snapshots of dev.perl.org were therefore not needed for these items.

### Gaps
- The retroactive 2002–2004 revisions have no individual commit dates beyond their `Last Modified` headers; where a revision has no header change, it can only be bracketed by its neighbours.
- Some 2002–2004 Synopses (S04, S06, S09–S13) existed before the import only as perl.com articles; whether every intermediate draft made it into svn was not checked.

## When did `»op«` replace A3's `^op`, and when were hyper operators called "vector" operators?

### Takeaway
Larry Wall proposed French quotes for hyper operators on 30 Oct 2002, first as `«+»`, and two days later (1 Nov 2002) switched to the inward-pointing `»*«` with the ASCII fallback `>>*<<`. The "vector" name was in use on the list from 28–29 Oct 2002 (Uri Guttman moved the rename formally), became the heading of S03 version 1 (8 Mar 2004), and A12 (16 Apr 2004) switched back to "hyper".

### Cited Findings
- A3 (2 Oct 2001) spelled hyper operators with a leading caret, `@a ^* @b`, and reserved unary `^` for them — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- The caret clashed with xor once the October 2002 operator-list threads (Michael Lazzaro's "Perl6 Operator List", 25 Oct 2002 onward) tried to free `^` for xor and `~` for concatenation; Piers Cawley's 1 Nov 2002 summary describes exactly this knot — [Lazzaro, msg11653, <7A748E43-E847-11D6-A7D4-00050245244A@cognitivity.com>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11653.html); [Summary, msg12116, <84pttpbolv.fsf@despairon.bofh.org.uk>](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12116.html)
- Larry Wall, 28 Oct 2002: a vector operator is just a hyper version of a scalar operator (first use of "vector" for these by Larry found) — [msg11752, <Pine.LNX.4.44.0210280948300.15960-100000@london.wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11752.html)
- Damian Conway's "Perl6 Operator List, Damian's take" (30 Oct 2002 06:21 +1100) proposed square brackets, `[op]`, as the marker that "vectorizes" an operator — [msg11821, <3DBEDFD7.8020005@conway.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11821.html)
- Uri Guttman, 29 Oct 2002, thread "vector vs. hyper": formally moved to rename hyper operators to vector operators, citing Damian's table — [msg11834, <x7ptttrpde.fsf@mail.sysarch.com>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11834.html)
- Larry Wall, 30 Oct 2002 11:09 PST: proposed French quotes for hyperoperators, with examples `@a «+» @b`, `@a«++»`, `@a«.anymethod(args)»` (first French-quote hyper proposal found; no earlier article in the 26 Sep – 30 Oct 2002 range contains « or ») — [msg11936, <Pine.LNX.4.44.0210301102450.1243-100000@london.wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11936.html)
- Larry Wall, 30 Oct 2002 11:58 PST: would give up `«foo bar baz»` as qw for this; `^[+]` could be the workaround for those who cannot type the quotes — [msg11940, <Pine.LNX.4.44.0210301148410.7776-100000@london.wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11940.html)
- Larry Wall, 1 Nov 2002 11:00 PST: keep `«foo bar baz»` for qw and use `@a »*« @b` for vector multiply, with `>>*<<` as the ASCII circumlocution (and bitshifts then qualified as `+>>`, `~>>`) — [msg12138, <20021101190039.GB25694@london>](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12138.html)
- Lazzaro's "TAKE 6" list the same afternoon still listed `«op»` as preferred and `»op«` as a maybe, so the orientation was not settled on 1 Nov — [msg12144](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12144.html); the 6 Nov 2002 summary noted that "vectorizing" had replaced "hyperizing" — [msg12285](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12285.html)
- By 20 Jan 2004 Larry used `»+«` routinely and discussed one-sided forms like `+«` ("Semantics of vector operations") — [msg16373, <20040121004312.GA27628@wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2004/01/msg16373.html); by 11 Mar 2004 he wrote "hyperoperators" again — [msg16764](https://www.nntp.perl.org/group/perl.perl6.language/2004/03/msg16764.html)
- E7 (27 Feb 2004) uses `»`/`«` — [E7](https://www.perl.com/pub/2004/02/27/exegesis7.html/); S03 version 1 (Luke Palmer, 8 Mar 2004) has the heading "Vector operators" with `»+«` and ASCII `>>-<<` — [3c26898](https://github.com/Raku/mu/commit/3c268982895a37de591536e350a32b83d5e0f5a9)
- A12 (16 Apr 2004) renamed them back to hyper operators because "vector" confused mathematicians — [A12](https://www.perl.com/pub/2004/04/16/a12.html/); S03 version 2 (13 Sep 2004) carries the heading "Hyper operators" — [242d926](https://github.com/Raku/mu/commit/242d9261f40875ea292f450258f59a9719732231)
- First implementation: Pugs svn r588, 11 Mar 2005 (patch by malaire adding `»+« »*« »/« »x« »xx«`) — [5be91f9](https://github.com/Raku/mu/commit/5be91f97ccd2babd6416f2d5f07787fcae286c64)

### Inferences
- Credit for `»op«` belongs to Larry Wall; the "vector" interlude was a list-level rename (Uri Guttman's motion, Damian's "vectorizes") that reached only one Synopsis revision before A12 reverted it.
- A6 (Mar 2003) and E6 (Jul 2003) contain no `»`, so the 2002 decision was not reflected in a published design document until E7 (Feb 2004).

### Gaps
- No single message records Larry ruling between `«op»` and `»op«`; the inward form is his 1 Nov 2002 proposal and is what he uses from then on.

## When did smartmatch move from A4's `=~` to `~~`?

### Takeaway
Larry Wall first suggested `~~` for smart matching on 24 Oct 2002; Lazzaro's and Damian's operator lists adopted it within five days; the 1 Nov 2002 Perl 6 Summary listed it as settled. A6 (Mar 2003) used it, and the Synopses caught up in Sep 2004.

### Cited Findings
- A3 (2 Oct 2001) defined binary `~~` as logical xor; A4 (15 Jan 2002) defined the smartmatch table for `=~` — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/); [A4](https://www.perl.com/pub/2002/01/15/apo4.html/)
- As late as 26 Sep and 16 Oct 2002, `~~` was still xor in list discussion (John Williams' precedence table; Larry suggesting xor become `!!` rather than `~~`) — [msg11246](https://www.nntp.perl.org/group/perl.perl6.language/2002/09/msg11246.html); [msg11571, <Pine.LNX.4.44.0210161544360.6376-100000@london.wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11571.html)
- Larry Wall, 24 Oct 2002, "Re: perl6 operator precedence table": smart matching is heavyweight enough to deserve a larger operator, and with `~` as concatenation there is no "logical concat", so `~~` could be used and nobody would confuse `~=` with `=~`; he also floated `===`, keeping `=~`, `:~` and `=~=` — [msg11632, <Pine.LNX.4.44.0210241035340.8649-100000@london.wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11632.html)
- No earlier `~~`-as-smartmatch proposal appears in the downloaded range (26 Sep – 24 Oct 2002), where `~~` meant xor — same articles as above.
- Michael Lazzaro's first "Perl6 Operator List" (25 Oct 2002) listed `~~ !~` as smartmatch "and/or perl5 =~ (?)"; Larry's reply suggested `like`/`unlike` as alternatives — [msg11653](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11653.html); [msg11654](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11654.html)
- Damian's take (30 Oct 2002 +1100) lists `~~ !~` as smart match / smart non-match — [msg11821](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11821.html)
- Perl 6 Summary, 1 Nov 2002: among things that appear set in stone, `~~` is the new `=~` (and `!~` stays) — [msg12116](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12116.html)
- A6 (7 Mar 2003) uses `want ~~ List & 2 & Lvalue` — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- S03 version 1 (8 Mar 2004) mentions no smartmatch at all; S03 version 2 (13 Sep 2004) puts `~~ !~` in the chaining precedence level — [3c26898](https://github.com/Raku/mu/commit/3c268982895a37de591536e350a32b83d5e0f5a9); [242d926](https://github.com/Raku/mu/commit/242d9261f40875ea292f450258f59a9719732231)
- Herbert Snorrason, 15 Sep 2004, "[S3, S4, S5]: =~ becomes ~~", pointed out the Synopses still used `=~`; Luke Palmer's S03 revision adding an explicit "=~ becomes ~~" item is sequenced between the 15 and 16 Sep 2004 revisions — [msg18385, <c1b58215040915164324285f17@mail.gmail.com>](https://www.nntp.perl.org/group/perl.perl6.language/2004/09/msg18385.html); [e0cb453](https://github.com/Raku/mu/commit/e0cb453c8473c828f0ac4ccf931145cecef79b15)
- Pugs 6.0.11 (14 Mar 2005) had a stub `~~`; 6.0.12 (21 Mar 2005) smartmatch for regexes — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)

### Inferences
- The doc's sentence that smartmatch "became `~~` only by A6 in March 2003" should be tightened to: proposed by Larry Wall on 24 Oct 2002, accepted on the list by 1 Nov 2002, first used in an Apocalypse in A6.
- The `~~` spelling was a by-product of the 2002 concatenation change (`_` → `~`): once `~` meant strings, `=~` next to `~=` became ambiguous.

### Gaps
- Articles between Jan and Sep 2002 were not all downloaded; an earlier informal `~~` suggestion cannot be fully excluded, though the late-2002 threads treat `~~` as xor until 24 Oct.

## Who coined "junction", and is the 8 Dec 2002 Damian Conway post the coinage?

### Takeaway
No. The word was coined between 29 and 31 Oct 2002: Larry Wall asked for a replacement for "superposition", Damian Conway put "junction" in a list of candidates, Larry preferred "junctions", and Damian adopted it the next day. The 8 Dec 2002 Damian post is a later explanatory post that named the four junction types.

### Cited Findings
- Larry Wall, 29 Oct 2002 09:58 PST, thread "Wh<[ie]>ther Infix Superposition ops": "quantum superpositions" is a funny name, he wanted something a toddler could understand, and he would look favourably on a replacement — [msg11812, <Pine.LNX.4.44.0210290937590.20960-100000@london.wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11812.html)
- Damian Conway, 30 Oct 2002 07:51 +1100 (29 Oct 20:51 UTC), reply: a list of candidate names written as an `any(...)` junction — multivalue, opval/andval/orval, set/andset/orset, junction (with conjunction, disjunction, exclusion, rejection), junctive. No article from 26 Sep 2002 (msg11240) up to this one contains the word, even in quoted text, so this is the first occurrence in that range — [msg11842, <3DBEF4C9.4080706@conway.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11842.html)
- Larry Wall, 30 Oct 2002 10:14 PST: he disliked "flexprs" and would rather call them something ugly like "junctions" if that communicates better, maybe "junks" for short — [msg11927, <Pine.LNX.4.44.0210300942240.1243-100000@london.wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg11927.html)
- Damian Conway, 31 Oct 2002 11:40 +1100: he does not mind "junctions" and will use the word from now on — [msg12007, <3DC07BE7.2090203@conway.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/10/msg12007.html)
- Perl 6 Summary, 13 Nov 2002: junctions are what the list is calling superpositions this week — [msg12418, <84vg314z2v.fsf@despairon.bofh.org.uk>](https://www.nntp.perl.org/group/perl.perl6.language/2002/11/msg12418.html)
- Damian Conway, 8 Dec 2002 (Sun 08 Dec 2002 19:10 +1100), "Re: Perl 6 and Set Theory": sets out four junction types — conjunction (`all`), disjunction (`any`), abjunction (`one`), injunction (`none`) — and the semantics of comparisons on junctions — [msg12786, <3DF2FE76.6050602@conway.org>](https://www.nntp.perl.org/group/perl.perl6.language/2002/12/msg12786.html)
- A6 (7 Mar 2003) is the first Apocalypse using "junction" — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)

### Inferences
- "Junction" is jointly Damian's (as proposer, in a list of ten) and Larry's (as the one who chose it); "8 Dec 2002" is the date of the type taxonomy (abjunction/injunction names), and is almost certainly the post the earlier web-search lead pointed to.
- The four type names changed between 30 Oct (conjunction/disjunction/exclusion/rejection) and 8 Dec 2002 (…/abjunction/injunction).

### Gaps
- The Google Groups thread `QbGOWSCrLXg` named in the chronology notes was not opened; the identification with msg12786 rests on date, author and topic.
- Bodies before 26 Sep 2002 were not downloaded, so an isolated earlier use of the bare word "junction" (outside the con-/dis-junction vocabulary of RFC 225) cannot be fully excluded.

## When did the sequence (series) operator `...` with arithmetic/geometric deduction appear?

### Takeaway
Larry Wall added infix `...` as "the series operator" in one S03 revision on 5 Oct 2008, and that same revision already contained the rule that a `*` right operand deduces an arithmetic or geometric progression. Rakudo implemented an eager version on 25 Jun 2009.

### Cited Findings
- Before 2008, `...` was a postfix semi-infinite-list operator and the yada term (S03 version 1, 8 Mar 2004) — [3c26898](https://github.com/Raku/mu/commit/3c268982895a37de591536e350a32b83d5e0f5a9)
- svn r14586, Larry Wall, 5 Oct 2008 17:05 PDT (S03 version 141): adds `infix:<...>`, the series operator, iterating a function over the tail of a list (Fibonacci via a two-arity block), 0-ary functions, list-associative chaining of several generators, and: if the right operand is `*` and the sequence is obviously arithmetic or geometric, the function is deduced (`1, 3, 5 ... *`; powers of 2) — [commit mail msg29819, <20081006000542.5EE1FCB9B0@x12.develooper.com>](https://www.nntp.perl.org/group/perl.perl6.language/2008/10/msg29819.html); [18d3149](https://github.com/Raku/mu/commit/18d31499442176bc34d608458348399b250a35c8)
- Spec tests followed the next day (moritz, 6 Oct 2008) — [01251d7](https://github.com/Raku/mu/commit/01251d782)
- 21 Sep 2009: Larry extended the series rules to steps and limits — [9726a41](https://github.com/Raku/mu/commit/9726a4123ec1116fb99ce5c75ce1a70f74f13bd3); 23 Sep 2010: renamed "series" to "sequence" — [1779592](https://github.com/Raku/old-design-docs/commit/17795925bc48affda10275bd2768e1f2d9ef6345)
- Rakudo: eager series operator, Moritz Lenz, 25 Jun 2009 — [62094c1](https://github.com/rakudo/rakudo/commit/62094c10e9ab4b946937a83c93de1d87747a59a2); revamped in 2010.09 per the Rakudo ChangeLog — [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- No perl6-language thread proposing a series operator was found in subjects from 2004–2008; the commit itself gives readability as the motive — [msg29819](https://www.nntp.perl.org/group/perl.perl6.language/2008/10/msg29819.html)

### Inferences
- The deduction feature is not a later refinement: it was born with the operator, on 5 Oct 2008, by Larry Wall.

### Gaps
- Whether the idea was discussed first on IRC (#perl6) was not checked.

## When did the `X` cross and `Z` zip metaoperators appear?

### Takeaway
Larry Wall specified `X` as both a cross-product operator and a prefix metaoperator (`X~`, `X*`, `X,`) on 12 Sep 2006, changed the metaoperator to the circumfix form `X~X` the next day, renamed zip from `¥` to `Z` on 21 Feb 2007, returned to the prefix form `X~` on 29 Jan 2009, and added `Z` as a metaoperator on 15 Mar 2010. List members had proposed an outer/cross product three times before.

### Cited Findings
- Precursor proposals: Luke Palmer's `outer` function for dynamically nested loops (22 Mar 2004) — [msg16864](https://www.nntp.perl.org/group/perl.perl6.language/2004/03/msg16864.html); Michele Dondi asking for an infix Cartesian-product operator (13 Jul 2004) — [msg17807](https://www.nntp.perl.org/group/perl.perl6.language/2004/07/msg17807.html); Darren Duncan proposing `cross`, by analogy with an SQL cross join (27 Oct 2005) — [msg23878](https://www.nntp.perl.org/group/perl.perl6.language/2005/10/msg23878.html)
- Zip: S03 version 2 (13 Sep 2004) gives `zip` the Unicode infix synonym `¥` — [242d926](https://github.com/Raku/mu/commit/242d9261f40875ea292f450258f59a9719732231)
- svn r11969, Larry Wall, 12 Sep 2006 11:07 PDT (S03 version 59): a fifth metaoperator, `X`, applying an operator across all permutations of its list arguments; bare `X` is an operator in its own right; examples `<a b> X~ <1 2>`, `1,2 X* 3,4`, `X,` — [msg26144, <20060912180701.EEAF7CBA4D@x12.develooper.com>](https://www.nntp.perl.org/group/perl.perl6.language/2006/09/msg26144.html); [088e2dc](https://github.com/Raku/mu/commit/088e2dcaee37c436ff3439c4ce5e01dce6a9df27)
- svn r11975, 12 Sep 2006 21:09 PDT: syntax changed to `X~X`, `XX` — [msg26152](https://www.nntp.perl.org/group/perl.perl6.language/2006/09/msg26152.html); [5be8e94](https://github.com/Raku/mu/commit/5be8e947aaac732aa7e87765ca465e485b97feb5)
- svn r13699, 21 Feb 2007 (S03 version 101): `¥` replaced by `Z`; `XX` becomes plain `X` — [msg27125, <20070221172538.C41ADCBA1B@x12.develooper.com>](https://www.nntp.perl.org/group/perl.perl6.language/2007/02/msg27125.html); [c761027](https://github.com/Raku/mu/commit/c761027ac84540afeb103e20cc02e20a9f42e1fa)
- 29 Jan 2009, Larry Wall, "slaughter of the LTM metatokens": `X~X`, `X*X`, `XeqvX` become `X~`, `X*`, `Xeqv` — [d59c18c](https://github.com/Raku/old-design-docs/commit/d59c18c1443c9813eec7d62e4a2b8d45d8b212a7)
- 15 Mar 2010: `Z` added as a metaoperator alongside `X` (desugaring to `crosswith`/`zipwith`) — [63679e1](https://github.com/Raku/old-design-docs/commit/63679e18654b6dfbc59599ca936e720749dde058)
- Implementations (Parrot's `languages/perl6`, i.e. Rakudo before and after its Jan 2008 naming): non-hyper `X` by Jonathan Worthington, 3 Jan 2008 (Parrot r24484) — [690abac](https://github.com/rakudo/rakudo/commit/690abac7dd907fb6c0f8b2e734810e0f56d9b193); `min`, `max` and `Z`, 6 Jan 2008 (r24628) — [4a000e3](https://github.com/rakudo/rakudo/commit/4a000e3a28f365ede896a56583f1c20f1bf63c71); cross metaoperator, 9 Dec 2008 (r33733) — [16c4ef1](https://github.com/rakudo/rakudo/commit/16c4ef19cc9c150b4c5532bb5ffa7045539cb14f); lazy `Z` 2010.03 per ChangeLog. Pugs had an infix `Y` (zip) in 6.0.11 (14 Mar 2005) — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)

### Inferences
- The modern spelling `X~` is the original Sept 2006 spelling; the 2006–2009 `X~X` form explains why some 2007–2008 material shows doubled X's.

### Gaps
- No Pugs implementation of `X` was found in the Pugs ChangeLog or svn messages.

## When were `sub MAIN` and `USAGE` specified?

### Takeaway
Both entered S06 in one commit by Larry Wall, svn r10804, on 10 Aug 2006 (PDT), including multi-dispatch over `MAIN` candidates, generated usage text and a `USAGE` override. Rakudo had a basic `MAIN` on 11 Nov 2008 and the full `MAIN`/`USAGE` in June 2010.

### Cited Findings
- svn r10804, Larry Wall, Thu 10 Aug 2006 17:11 PDT: new S06 section "Declaring a MAIN subroutine": `MAIN` is called after the mainline only if the file was run directly, declares a `MAIN` routine, and has not called it; command-line arguments become a Capture so switches bind to named parameters; a `MAIN` method gets the program name as invocant; multi `MAIN` subs use MMD; a failed dispatch calls `USAGE`, or prints a default message generated at compile time from the signatures; a table maps Unix switch conventions (`-name`, `-name=value`, …) to pairs. The text likens the not-run-when-loaded rule to Perl 5's `unless caller` idiom and credits a typo fix to Aaron Crane — [msg25795, <20060811001155.CB437CB9BB@x12.develooper.com>](https://www.nntp.perl.org/group/perl.perl6.language/2006/08/msg25795.html); [4a841a8](https://github.com/Raku/mu/commit/4a841a8c2d40fdc2dddc25f23a786f2876ee9737)
- 14 Aug 2006 (r10942): minor cleanup of the MAIN text in the commit that renamed FIRST to START — [7dfeae0](https://github.com/Raku/mu/commit/7dfeae0341ac5757bbd2ff19155c64324404483b)
- No perl6-language thread on MAIN or command-line parsing appears in subjects from 2005–2006, so the proposal reached the list as the spec commit itself — [perl.perl6.language](https://www.nntp.perl.org/group/perl.perl6.language/)
- Rakudo, Jonathan Worthington, 11 Nov 2008 (Parrot r32554): very basic `MAIN` entry-point support, passing all arguments as positionals — [41d13f8](https://github.com/rakudo/rakudo/commit/41d13f8eac2870031741179106cb35df668936dd); full implementation by Moritz Lenz with code by Patrick Abi Salloum, 8–11 Jun 2010 (Rakudo 2010.06) — [f7ddcf5](https://github.com/rakudo/rakudo/commit/f7ddcf57efbf25eb49d2a9b66b47aa80d8dd67df); [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- Pugs: the May 2007 commits about a `Main::MAIN` sub concern how Pugs compiled the mainline, not command-line dispatch — [5ea5ed4](https://github.com/Raku/mu/commit/5ea5ed410)

### Inferences
- The doc's table row for `sub MAIN` ("between April 2006 and 14 October 2008") can be replaced with "10 Aug 2006 (S06, svn r10804)"; `USAGE` has the same date.
- The only stated sources in the original text are Unix switch conventions and Perl 5's `unless caller`; no other language is credited.

### Gaps
- IRC discussion preceding r10804 was not searched.

## When did `"{ expr }"` interpolation replace A2's `"$(expr)"`?

### Takeaway
Larry Wall proposed closure interpolation, `{EXPR}`, on 20 Jul 2004, describing it as turning up the "Ruby-o-meter"; Damian Conway objected and Larry held firm the next day; S02 draft 1, posted 14 Aug 2004, records that `$(...)`/`@(...)` were replaced by interpolated curlies.

### Cited Findings
- A2 (3 May 2001) introduced `"$(expr)"` and `"@(expr)"` for expression interpolation — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- S03 version 1 (8 Mar 2004) still listed `$(...)`, `@(...)`, `%(...)` as interpolable context operators — [3c26898](https://github.com/Raku/mu/commit/3c268982895a37de591536e350a32b83d5e0f5a9)
- Larry Wall, 20 Jul 2004 16:06 PDT, "Re: String interpolation" (a thread that began as a reply to that week's Perl 6 Summary): after a week of rethinking he would make the only double-quote interpolators backslash escapes, `$foo`, `@foo[$i]`, `%foo{$k}` and `{EXPR}` (list context), so that `{foo()}`, `{$foo.bar}`, `{.count}`, `{+@foo}` all need curlies; he says this fixes the sprintf `%` problem and Perl 5's `@`-in-email problem, generalises curlies as closures, and reverses his A12 rule that `$x.foo` interpolates wherever `$foo` does — [msg17885, <20040720230640.GA18954@wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2004/07/msg17885.html)
- Larry Wall, 21 Jul 2004, replying to Damian Conway (who preferred `$(...)`/`@(...)`): `$()` no longer felt right; `{...}` gives list context by default and `~ + ?` select scalar contexts, removing three FAQs — [msg17896, <20040721164244.GA20067@wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2004/07/msg17896.html)
- S02 draft 1, posted by Larry Wall on 14 Aug 2004: A2's `$(...)` and `@(...)` have been replaced with interpolation of curlies (closures) — [msg18037, <20040814183225.GB2827@wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2004/08/msg18037.html); the same sentence is in S02 version 0 (dated 10 Aug 2004, last modified 18 Aug 2004) — [f3422f9](https://github.com/Raku/mu/commit/f3422f9ffa57cfc6603b4809f984058fa4240394)
- The `$rubyometer` joke recurs in Larry's posts from Nov 2003 on, e.g. as an increment for a Ruby-like change — [msg16055, 21 Nov 2003, <20031122015024.GB3475@wall.org>](https://www.nntp.perl.org/group/perl.perl6.language/2003/11/msg16055.html)

### Inferences
- `"{ expr }"` is a documented case of Perl 6 moving toward Ruby (Ruby's `"#{expr}"`), in Larry's own words; it belongs with the "flowed into Perl 6" items.

### Gaps
- Which earlier thread Larry meant by "since last week" was not identified; the 20 Jul 2004 post is the first proposal found in his 2001–2004 posts.

## When were `subset` and pre-2009 gather/take, MAIN and Rat first implemented?

### Takeaway
`subset` was first parsed by Pugs (Oct 2006, a no-op), first worked in kp6 (Aug 2007) and first worked in Rakudo on 27 Mar 2008. In Parrot-era Rakudo, gather/take arrived 13 Sep 2008 and a basic `MAIN` 11 Nov 2008; Rakudo had no `Rat` before 30 Aug 2009, although Pugs had a rational value type from Feb 2005.

### Cited Findings
- `subset` (spec): A12 (Apr 2004) Ada-style subtypes; `subset` already in the April 2006 Synopses mirror — [A12](https://www.perl.com/pub/2004/04/16/a12.html/); [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108)
- Pugs, audreyt, 9 Oct 2006 (Pugs r14107): a kluged-up parser for subsets that does nothing yet — [16a0ecd](https://github.com/Raku/mu/commit/16a0ecd5eae2a474f9b075a441fc2e2dc531c71a)
- kp6 (KindaPerl6, Flavio Glock), 14 Aug 2007 (Pugs r17250–17254): grammar and AST for `subset`, instantiable subset types, an example that compiles — [13d8c0d](https://github.com/Raku/mu/commit/13d8c0de1bf5f060301bc37ae61ddde7baf09af9); [aee30a0](https://github.com/Raku/mu/commit/aee30a035aba9b108fa1349029f9bbca0aed22c0)
- Rakudo, Jonathan Worthington, 27 Mar 2008 (Parrot r26570): first-cut `subset … of … where …` — [aa2db27](https://github.com/rakudo/rakudo/commit/aa2db27a7d0ce7f4061617f4aeda3bcd9a0ebcce); followed by a multi-dispatch-on-subset-types test (11 Nov 2008) and `where /regex/` subsets (3 Dec 2008) — [bf2bd90](https://github.com/rakudo/rakudo/commit/bf2bd90187e5ea5f5f5afb3155a9908a90a7fbd6); [528ae12](https://github.com/rakudo/rakudo/commit/528ae12fb60ed93bfe208d5dc0d267515f49d3c6)
- gather/take: Pugs 6.2.4 (24 May 2005) — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog); Rakudo first draft by Stephen Weeks, 13 Sep 2008 (Parrot r31049) — [dbadff7](https://github.com/rakudo/rakudo/commit/dbadff78cbc04af1fc41b306df63da7ce0f4259b); an eager reimplementation on 23 Nov 2009 — [90471b1](https://github.com/rakudo/rakudo/commit/90471b186)
- MAIN: Rakudo basic support 11 Nov 2008 (r32554) — [41d13f8](https://github.com/rakudo/rakudo/commit/41d13f8eac2870031741179106cb35df668936dd)
- Rat: Pugs had a `VRat` value type by its svn r83, 19 Feb 2005 — [a8389eb](https://github.com/Raku/mu/commit/a8389eb1d81098de898c82a521d2affbefeb26ba); Rakudo's first-draft `Rat` is by Patrick Michaud, 30 Aug 2009 — [9fff158](https://github.com/rakudo/rakudo/commit/9fff15880d11473fc480b6c167eca0df8bd5b02b); `Int/Int` returning `Rat` per S03, Solomon Foster, 31 Aug 2009 — [39c3f4b](https://github.com/rakudo/rakudo/commit/39c3f4b6d5f2a702f336d33e79d23391adf36980)
- The `rakudo/rakudo` repository holds Parrot-era history back to 24 Mar 2006 (git-svn ids under `svn.perl.org/parrot/trunk/languages/perl6`), which is why these pre-ChangeLog dates are recoverable — [rakudo/rakudo](https://github.com/rakudo/rakudo)

### Inferences
- The chronology gap "first Pugs/Rakudo implementation of `subset` not pinned" closes as: Rakudo 27 Mar 2008; earliest working anywhere, kp6 Aug 2007.

### Gaps
- Whether Pugs evaluated `1/3` to a `Rat` by default in 2005 was not tested; only the existence of the `VRat` type is documented here.

## What does the 2010 Audrey Tang source behind Wikipedia's "Influenced: Perl, Haskell" actually claim?

### Takeaway
It is not a talk but a blog post (a PerlMonks reply re-posted on pugs.blogs.com, 21 Apr 2010). It supports influence on Perl 5 through CPAN modules, but it makes no claim that Perl 6 or Pugs influenced Haskell: it says Pugs needed host-language features that later appeared in Haskell 2010 and GHC, and those features have independent, earlier origins.

### Cited Findings
- Wikipedia's Raku infobox lists Influenced: Perl [7], Haskell [7], AntLang (no footnote); [7] is Audrey Tang, "How to Implement Perl 6 in '10", 21 Apr 2010, archived 25 Apr 2010 — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language)); [archived post](https://web.archive.org/web/20100425002903/http://pugs.blogs.com/pugs/2010/04/how-to-implement-perl-6-in-10.html)
- The post is a reply to chromatic's PerlMonks node "How to Implement Perl 6 in 10 Years" (PerlMonks node 835936, replying to node 835731) — [archived post](https://web.archive.org/web/20100425002903/http://pugs.blogs.com/pugs/2010/04/how-to-implement-perl-6-in-10.html)
- Perl side: Tang says the 2006 shift of #perl6 toward the Perl 5 runtime produced the first CPAN releases of Moose, Module::Compile, v6-alpha (later Perlito) and Pugs::Compiler::Rule, ports of Pugs structures (signatures, metaobject logic, grammar parser) to Perl 5, and that MooseX::Declare and Perl 5.12.0 went beyond that vision — [archived post](https://web.archive.org/web/20100425002903/http://pugs.blogs.com/pugs/2010/04/how-to-implement-perl-6-in-10.html)
- Moose's own documentation says it is built on Stevan Little's Perl 6 meta-model prototypes — [Moose 0.18](https://metacpan.org/pod/release/STEVAN/Moose-0.18/lib/Moose.pm)
- Haskell side: Tang says coding the Pugs 6.28 series (compile-time gradual typing) required significant changes to Haskell, some codified in Haskell 2010 (pattern guards) and some implemented in GHC (type families, quasi-quoting), plus better theory (GADTs, Odersky's work, Siek's gradual typing). She credits these to the Haskell world and ends by preferring to do the analysis in Perl 6 itself — [archived post](https://web.archive.org/web/20100425002903/http://pugs.blogs.com/pugs/2010/04/how-to-implement-perl-6-in-10.html)
- Pattern guards were already documented in GHC 4.08.1 (2000), as an abbreviated version of Simon Peyton Jones's proposal, five years before Pugs — [GHC 4.08.1 docs](https://downloads.haskell.org/~ghc/old_docs/old_4.08.1_head/set/pattern-guards.html)
- Geoffrey Mainland's quasi-quoting paper (Haskell Workshop 2007) cites Template Haskell, Baars & Swierstra, Taha and others; a text search of the PDF finds no "Perl" or "Pugs" — [Mainland 2007 PDF](https://www.cs.tufts.edu/comp/150PLD/Papers/QuasiQuotation.pdf)

### Inferences
- "Influenced: Perl" is supported (through Moose and the Perl 5 record documented in the main doc). "Influenced: Haskell" is not supported by its own citation; at most Pugs was an early heavy user of GHC extensions.

### Gaps
- The origins of type families (associated types, 2005) were not checked against Pugs dates in this session.
- The PerlMonks originals (nodes 835731, 835936) were not fetched; the archived blog copy was used.

## What has the Raku community itself claimed about other languages borrowing from Perl 6 / Raku?

### Takeaway
Very little. Across 1,088 Weekly and Advent posts (2009–2026), 133 6guts posts, raku.org and the docs FAQ, no author claims that a non-Perl language borrowed a feature from Perl 6/Raku. Claims of influence on Perl 5 are common and documented. The remaining claims are about small languages (Sidef), tooling (a Rust bisect bot), Larry Wall's 2006 prediction about regexes, and the Wikipedia infobox.

### Cited Findings
- raku.org's home page and the docs FAQ make no influence claims; the FAQ calls Perl and Raku sister languages — [raku.org](https://raku.org/); [Raku FAQ](https://docs.raku.org/language/faq)
- Weekly/Advent full-text search: sentences pairing another language with borrow/steal/inspire/influence/adopt and Perl 6/Raku turned up only Perl 5 items, e.g. a 2016 talk on a module that brings more Perl 6 features to Perl 5 — [2016.15](https://p6weekly.wordpress.com/2016/04/11/2016-15-wow-blogs/); Raku borrowing from others, e.g. 007 borrowing from Python and JavaScript — [Advent 2015 day 23](https://perl6advent.wordpress.com/2015/12/23/day-23-macros-and-secret-agents/); and Go and Perl 6 both drawing on Hoare's CSP — [Advent 2017 day 15](https://perl6advent.wordpress.com/2017/12/15/a-simple-web-spider-with-promises/)
- The Weekly reported no other language's proposal that cites Raku (PHP's increment RFC, Swift's regex pitches, C#'s chained-comparison proposal and Kotlin's design note are absent); its only links into another language's design forums are a 2021 Swift-forum comment by John Siracusa preferring Raku grammars and two 2024 Julia Discourse posts that mention Raku in passing — [2021.40](http://rakuweekly.blog/2021/10/06/2021-40-its-here/); [Swift forum #12](https://forums.swift.org/t/declarative-string-processing-overview/52459/12); [Julia #22](https://discourse.julialang.org/t/syntax-escape-hatch-for-unicode-haters/108363/22); [Julia #5](https://discourse.julialang.org/t/error-for-396-n/110099/5)
- 2018: the Weekly highlighted the question of which new Perl 6 features other languages should adopt (Quora and Reddit), an aspiration rather than a claim — [2018.29](https://p6weekly.wordpress.com/2018/07/16/2018-29-cross-pollination/)
- 6guts (Jonathan Worthington, 2010–2023): no claim that another language borrowed from Perl 6; the cross-language remarks concern borrowing Java strings on the JVM backend and his teaching of Rx.NET — [6guts 2013-10-11](https://6guts.wordpress.com/2013/10/11/nqp-gets-moarvm-support-cursor-reduction-and-other-news/); [6guts 2015-07-23](https://6guts.wordpress.com/2015/07/23/this-week-concurrency-stuff-multi-dimensional-stuff-stuff-stuff/)
- Larry Wall, OSDC::Israel, 26 Feb 2006: everyone borrowed Perl 5 regexes, and he predicted that everyone would move from PCRE to Perl 6 regexes — [transcript](https://perl.org.il/presentations/larry-wall-present-continuous-future-perfect/transcript.html)
- Larry Wall, Slashdot interview, 14 Jul 2016 (full text now read, including the comments section): the only statements about others taking from Perl concern Perl 5 regexes; on PHP he jokes that borrowing enough would amount to reimplementing Perl 6; he says Perl 6 borrows its concurrency ideas from Haskell, Go, Erlang and C# — [Slashdot](https://developers.slashdot.org/story/16/07/14/1349207/the-slashdot-interview-with-larry-wall)
- Hacker News (Algolia full-text search of comments for "borrowed/stolen/inspired/taken/copied from Perl 6/Raku", "influenced by…", and language-plus-verb combinations): every directional claim concerns Perl 5 (chromatic 2009, kamaal 2012, maxlybbert 2015, topspin 2025) — [HN 927281](https://news.ycombinator.com/item?id=927281); [HN 3779551](https://news.ycombinator.com/item?id=3779551); [HN 9004159](https://news.ycombinator.com/item?id=9004159); [HN 44638287](https://news.ycombinator.com/item?id=44638287); the one exception is raiph's hedged RubySpec claim (below) — [HN 7855946](https://news.ycombinator.com/item?id=7855946)
- A Weekly link labelled "Influenced Haskell as well" points to an HN comment saying the reverse, that Haskell influenced Perl 6 through Pugs — [2020.39](http://rakuweekly.blog/2020/09/28/2020-39-the-releaser/); [HN 24571025](https://news.ycombinator.com/item?id=24571025)

### Inferences
- The community's own record matches the main doc: Perl 6 influence claims inside the community are about Perl 5, and outward claims mostly come from Wikipedia editors or third-party commenters.
- The Weekly's "Comments about Raku" lists curate mentions and comparisons, not borrowings; their link labels (e.g. "Inspired by Raku", "Influenced Haskell as well") can mislead if read without the target.

### Gaps
- Reddit (r/rakulang, r/perl6) could not be searched directly (HTTP 403); only Reddit threads that the Weekly linked or that search engines surfaced were seen.
- IRC logs (irclogs.raku.org), conference video talks (Larry's State of the Onion series, Jonathan Worthington's talks, Damian Conway's talks) and Zoffix Znet's rakudo.party blog were not systematically searched.
- Blogs outside WordPress.com (perlgeek.de, strangelyconsistent.org, pmthium.com) were not searched.

## How do those claims hold up against the other language's own record?

### Takeaway
Of the specific outward claims found, two are confirmed (Sidef; Perl 5 via Moose), one is confirmed only at the design stage (Swift regex), the rest are unsupported (Wikipedia's Haskell and AntLang entries, the Rust bisect-bot "inspiration", the RubySpec suggestion), and Larry Wall's 2006 prediction that regex engines would move to Perl 6 syntax is contradicted by Swift's choice.

### Cited Findings
| Claim (who, where, when) | Other side's primary record | Verdict |
|---|---|---|
| Raku influenced Perl (Wikipedia infobox via Tang 2010; many HN and Weekly items) | Moose says it is built on Perl 6 meta-model prototypes; Perl 5's own docs for given/when/smartmatch (see main doc) — [Moose 0.18](https://metacpan.org/pod/release/STEVAN/Moose-0.18/lib/Moose.pm) | **Confirmed** |
| Raku influenced Haskell (Wikipedia infobox, citing Tang 2010) | Tang's post claims no influence on Haskell; pattern guards were in GHC by 2000; Mainland's 2007 quasi-quoting paper cites no Perl — [Tang](https://web.archive.org/web/20100425002903/http://pugs.blogs.com/pugs/2010/04/how-to-implement-perl-6-in-10.html); [GHC 4.08.1](https://downloads.haskell.org/~ghc/old_docs/old_4.08.1_head/set/pattern-guards.html); [Mainland](https://www.cs.tufts.edu/comp/150PLD/Papers/QuasiQuotation.pdf) | **Unsupported** by its citation; contradicted for pattern guards |
| Raku influenced AntLang (Wikipedia infobox, no footnote) | Only a search-engine snippet of a Rosetta Code revision (influenced mainly by APL, also by Perl 6, Haskell, LISP); the page returned 403 and AntLang's own site was not found | **Unsupported** (unverified) |
| Sidef "inspired by Raku" (Weekly 2022.15 linking trizen's Sidef book; 2022.07 linking Tomasz Wegrzanowski's Sidef episode) | Sidef's CPAN readme says it is inspired by Ruby, Raku and Julia; its GitHub README cites Raku's versatility — [Sidef 23.03 readme](https://cpan.mirror.beocat.ksu.edu/modules/by-module/Lingua/TRIZEN/Sidef-23.03.readme); [Sidef README](https://github.com/trizen/sidef); [2022.15](http://rakuweekly.blog/2022/04/11/2022-15-wordling/) | **Confirmed** (small, single-author language; features not itemised) |
| Bisectable bot "inspirational to the Rust community" (Weekly 2017.02) | A 23 Dec 2016 Rust Internals post by "scott" suggests building a Rust equivalent of the Perl 6 Bisectable bot; cargo-bisect-rustc (Mark Simulacrum, first commit 15 Mar 2018) credits no Perl 6 tool in its README or commit messages — [Rust Internals #9](https://internals.rust-lang.org/t/rust-ci-release-infrastructure-changes/4489/9); [cargo-bisect-rustc](https://github.com/rust-lang/cargo-bisect-rustc); [2017.02](https://p6weekly.wordpress.com/2017/01/09/2017-02-dogfooding-and-powerbotting/) | Suggestion **confirmed**; adoption **unsupported** |
| Perl 6's spec-test approach "may have been one of the inspirations for RubySpec" (raiph, HN, 6 Jun 2014; hedged, no source) | RubySpec's history credits Rubinius tests converted to spec style, started 2006 with JRuby help; no Perl 6 mention found — [HN 7855946](https://news.ycombinator.com/item?id=7855946); [Wikipedia: RubySpec](https://en.wikipedia.org/wiki/RubySpec) | **Unsupported** |
| Everyone will move from PCRE to Perl 6 regex (Larry Wall, OSDC::Israel, 26 Feb 2006) | Swift's team evaluated Perl 6 regex features in 2018 and in Oct 2021 chose traditional syntax for familiarity; SE-0355 standardised PCRE-style syntax — [transcript](https://perl.org.il/presentations/larry-wall-present-continuous-future-perfect/transcript.html); [Swift forum #46](https://forums.swift.org/t/declarative-string-processing-overview/52459/46); [SE-0355](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0355-regex-syntax-run-time-construction.md) | **Contradicted** (prediction not borne out) |
| Swift regex drew on Raku (Weekly 2021.40/2021.41 comment links) | Ilseman's 2018 gist and 2021 forum post name Raku as the inspiration of an earlier literal design; accepted proposals cite PEGs, PCRE2 and Perl 5 — see main doc — [forum #46](https://forums.swift.org/t/declarative-string-processing-overview/52459/46) | **Confirmed at design stage**; shipped syntax not Raku's |
| NGS (Ilya Sher's shell language) and Perl 6 | Sher's 2018 post lists Perl 6 features he declined (metaoperators, slips, `when`, pointy blocks, `once`, `.WHAT`) and says grammars would be nice to have — [dev.to repost of 2018 post](https://dev.to/ilya_sher_prog/what-i-did-not-steal-from-perl-6-5b0g) | **Considered, rejected** (not a community claim) |
| Raku NFG vs Swift graphemes (Weekly 2021.32 linking an LWN thread) | The LWN thread compares designs and claims no borrowing in either direction — [LWN comments](https://lwn.net/Articles/865037/); [2021.32](http://rakuweekly.blog/2021/08/09/2021-32-firstcon-done/) | No influence claim (consistent with **independent**) |
| "Swift took X from Raku", "C# ranges from Raku", "PHP took Y" as Raku-community claims | No such claim found in the Weekly, Advent, 6guts, raku.org or HN corpora. The documented cases (PHP 8.3 increment RFC; C# #185 comment rejected; C# chained-comparison prior-art row) come from the other communities' own records, not from Raku advocacy — see main doc | **Not claimed** by the community in the sources searched |

### Inferences
- The Raku community has been notably restrained: the strongest outward claims (Haskell, AntLang) sit in Wikipedia's infobox rather than in any community publication, and the one documented foreign adoption that a Raku author could have trumpeted, PHP 8.3's increment RFC, was never reported in the Weekly.
- For the report: "Influenced: Haskell" should be treated as unsupported; "Influenced: Perl" as confirmed; Sidef can be added as a small confirmed borrower.

### Gaps
- AntLang's own README or site, and the Rosetta Code revision text, could not be read (403 / not located).
- Whether "scott" on Rust Internals is a Rust contributor, and whether any Rust bisection tool before cargo-bisect-rustc cited Bisectable, was not established.
