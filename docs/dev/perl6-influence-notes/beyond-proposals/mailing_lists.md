# Perl 6 / Raku in the design discussions of perl5-porters, python-3000, python-ideas, python-dev, es-discuss and ruby-dev (2000 – Nov 2026)

Method. All archives were downloaded on 7 Nov 2026 and grepped locally. Nothing below comes from search-engine snippets.
- **perl5-porters**: the complete NNTP overview (Subject/From/Date/Message-ID) of all 271,342 articles in `perl.perl5.porters` (Aug 1999 – 7 Nov 2026) was fetched from nntp.perl.org. About 2,000 full articles were then fetched for every thread whose subject touches the features in question, plus every subject containing Perl 6/Perl6/Raku, and grepped.
- **python-3000**: all 35 monthly pipermail files (Mar 2006 – Jul 2009), full text.
- **python-ideas and python-dev**: every pipermail month from 2000 (python-dev) or Dec 2006 (python-ideas) to Jun 2019, plus every Mailman 3/HyperKitty mbox export from Jul 2019 to Nov 2026, full text. Both lists are effectively dead after 2023: their 2024–2026 exports are empty.
- **es-discuss**: the mail-archive.com mirror's search for Jun 2007 – Mar 2021, plus full text of 121 original mail.mozilla.org pipermail monthly files from the Wayback Machine, covering Jun 2006 – Oct 2016 (four 2016 months missing).
- **ruby-dev and ruby-core**: individual blade.ruby-lang.org pages around the July 2005 commit, and Matz's diary.

Search terms were `perl 6`, `perl6`, `perl-6`, `raku`, `rakudo`, case-insensitive. Hits on `parrot`, `apocalypse`, `larry wall`, `junction`, `hyper operator` and `smart match` were checked by hand. Hits only in quoted text are not counted separately.

Quotes are short fragments of under 15 words, and paraphrase is used otherwise.

Classification tags:
- **[adopted-from]**: a feature taken from Perl 6.
- **[cited-as-prior-art]**: Perl 6 named as prior art.
- **[considered-and-rejected]**: Perl 6's design was weighed and not taken.
- **[cautionary-tale]**: Perl 6 cited as a warning.
- **[passing]**: a passing mention.

Every p5p URL below was checked to return HTTP 200 at `https://www.nntp.perl.org/group/perl.perl5.porters/YYYY/MM/msgN.html`.

## 1. perl5-porters: do the threads behind `//`, the 5.9.x features, `<<~`, chained comparisons, `isa` and signatures credit Perl 6?

### Takeaway
They do, far more explicitly than the commits and perldeltas. Brent Dax first posted `//` on 21 Apr 2002 under the subject "[PATCH] Implement Perl 6's // operator". The 2005–2007 `say`, given/when, `~~`, `state` and `\h`/`\v` work was openly a port of Synopsis 3/4 and Perl 6 features: Robin Houston said he followed the Perl 6 spec, Damian Conway pushed for it, and `use feature` itself grew out of Rafael Garcia-Suarez's 2002 `perl6ish` pragma. Larry Wall then apologized in January 2008 that "old misfeatures of the early Perl 6 design" had leaked into 5.10's smartmatch.

The later features differ:
- **`<<~`**: Matthew Horsfall's 2016 thread took the spelling from Ruby and proposed Perl 6's indentation rule, but p5p made mismatches fatal instead.
- **Chained comparisons**: first suggested in July 2002 as a Perl 6 backport, but Zefram's 2017 ticket and 2020 patch name no precedent.
- **`isa`**: no Perl 6 mention; the discussion moved to GitHub.
- **2012–2015 signatures**: Perl 6 was argued over constantly, mostly as something not to copy ("Perl 6 is not a menu").

### Cited Findings

#### 1a. `//`, `//=` and `err` (1999–2007)
- **Defined-or predates Perl 6.**
  - On 1 Oct 1999 Wilson Snyder posted "A plea for Or-Defined: ordef, |||, |||=", and Larry Wall took part in the thread. — [p5p msg1794](https://www.nntp.perl.org/group/perl.perl5.porters/1999/10/msg1794.html); [Larry, msg1822](https://www.nntp.perl.org/group/perl.perl5.porters/1999/10/msg1822.html)
  - On 23 Feb 2000 John Tobey posted a working patch adding `|||`, `&&&`, `||||` and `&&&&` (defined- and exists-flavoured logops). — [msg7802](https://www.nntp.perl.org/group/perl.perl5.porters/2000/02/msg7802.html)
  - In 2012 Johan Vromans asserted that H. Merijn Brand ("Tux") invented `//` long before Perl 6. — [msg191666](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191666.html)
  - Abigail noted that people had talked about such an operator long before "a certain mug was thrown" (the July 2000 Perl 6 kickoff). — [msg191643](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191643.html)
  - Classification: the idea is native to p5p; the spelling `//` is Perl 6's (A3).
- **21 Apr 2002, Brent Dax (Parrot developer; his signature lists his Parrot roles as embedding, regex engine and Configure), subject "[PATCH] Implement Perl 6's // operator"**, msg-id `<000a01c1e9c3$43821e60$9865fea9@deepblue>`.
  - The body says the patch implements "Perl 6's new" `//`, `//=` and `err` ops for Perl 5. **[adopted-from]**, explicit.
  - Outcome: reposted for 5.8.0 on 20 Jul 2002 and applied to bleadperl in July 2002 (commit c963b15, already known). — [msg57437](https://www.nntp.perl.org/group/perl.perl5.porters/2002/04/msg57437.html)
- **20 Jul 2002, Brent Dax, "[PATCH 5.8.0] Implement a // operator"** (the repost drops "Perl 6's" from the subject).
  - Its second discussion point asks whether p5p should backport other unobtrusive features from Perl 6, with chained comparison ops as the example. **[adopted-from]**; it proposes **chained comparisons as a Perl 6 backport 18 years before 5.32**. — [msg63819](https://www.nntp.perl.org/group/perl.perl5.porters/2002/07/msg63819.html)
- **30 Jul 2002, Hugo van der Sanden (5.9 development pumpking) replying.**
  - He had spoken to Larry, who suggested that "p6ish features" go through something like a `use perl6ish` pragma taking an import list of features.
  - Hugo asked for volunteers and wanted the existing Perl6:: modules supported the same way.
  - This is the conceptual origin of `use feature`. — [msg64178](https://www.nntp.perl.org/group/perl.perl5.porters/2002/07/msg64178.html)
- **9 Aug 2002, Larry Wall in the "shift // 0; not feeling well" thread.** On the ambiguity of `shift // 0`, he pointed out that `//` is not a valid pattern in Perl 6. **[passing]**, by the Perl 6 designer. — [msg64833](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg64833.html)
- **12–13 Aug 2002: backlash in the same thread.** **[cautionary-tale]** (Perl 6 as vaporware).
  - Robert Spier (perl.org infrastructure) asked whether the goal was to make perl5 look like perl6. — [msg65001](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65001.html)
  - Brent Dax answered that Perl 5 should *not* add a feature that would be totally different in Perl 6, which is why `|||` was unacceptable. — [msg65002](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65002.html)
  - Andy Lester declared that any discussion of Perl 6 as the basis of a Perl 5 decision is "null and void". — [msg65005](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65005.html)
  - Hugo said he would rather defer `//` to 5.12 than ship it under another name. — [msg65012](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65012.html)
- **27 Aug 2002, Rafael Garcia-Suarez (later the 5.10 pumpking), "[PATCH] new pragma perl6ish".**
  - The pragma optionally enabled "Perl 6-like constructs to come" and gated `//`/`//=` lexically through `%^H`.
  - Hugo replied that he wanted new syntax available on request only. — [msg65716](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65716.html); [msg65745](https://www.nntp.perl.org/group/perl.perl5.porters/2002/08/msg65745.html)
- **24 Oct 2004, Hugo, "dor/err naming".** Defending the `err` keyword, he said it had been approved by Larry and would be in Perl 6. **[adopted-from]**. — [msg95198](https://www.nntp.perl.org/group/perl.perl5.porters/2004/10/msg95198.html)
- **3–6 Sep 2007, "down with err, up with dor".**
  - Abigail said the name `err` comes from Perl 6 and that Larry preferred Perl 5 not to be inconsistent with it. — [msg128190](https://www.nntp.perl.org/group/perl.perl5.porters/2007/09/msg128190.html)
  - Rafael reported that Larry had renamed `err` to `orelse` in Perl 6 and added `andthen`, pointing to S03. — [msg128324](https://www.nntp.perl.org/group/perl.perl5.porters/2007/09/msg128324.html)
  - Larry said `//` should be pronounced "defaults to", and that the new `andthen`/`orelse` aim at logic programming, not default setting. — [msg128333](https://www.nntp.perl.org/group/perl.perl5.porters/2007/09/msg128333.html)
  - Outcome: `err` was dropped before 5.10.0, so Perl 5 shipped `//` and `//=` with no low-precedence form.
  - In 2012 demerphq (Yves Orton, regex engine maintainer) cited this as a cautionary example: threads.pm had to rename its `err` method to match a Perl 6 name that Perl 6 later abandoned. — [msg191644](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191644.html)

#### 1b. `say`, `use feature`, given/when, `~~`, `state`, `\h`/`\v` (2005–2008)
- **10 Mar 2005, Nigel Sandever, "[PATCH] to provide a Perl6-style 'say' keyword."** — [msg98796](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98796.html)
  - Michael Schwern (Test::More maintainer) was uneasy about enshrining an evolving Perl 6 feature that could change underneath Perl 5. This was prescient, given smartmatch. — [msg98850](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98850.html)
  - Sandever replied that he wanted `say` for itself, not because Perl 6 has it. — [msg98852](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98852.html)
  - Rafael said he was also thinking about "pulling the 'state' keyword from Perl 6". — [msg98858](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98858.html)
  - Nicholas Clark (5.8.x maint pumpking) explained that, since Larry is also in charge of Perl 6, p5p tends to view keeping Perl 5 consistent with Perl 6 as a good thing. — [msg98860](https://www.nntp.perl.org/group/perl.perl5.porters/2005/03/msg98860.html)
- **5 Nov 2005, Steve Peters (core committer), "[PATCH] Adding the Perl 6 say() built-in to Perl 5".** It follows Perl6::Say.
  - Rafael (5.10 pumpking) replied that he could be convinced, partly because he missed `say` "since I play with pugs". **[adopted-from]**. — [msg106335](https://www.nntp.perl.org/group/perl.perl5.porters/2005/11/msg106335.html); [msg106378](https://www.nntp.perl.org/group/perl.perl5.porters/2005/11/msg106378.html)
- **28–30 Nov 2005, Damian Conway (Perl 6 design team; author of Switch.pm), "Switch in the core".**
  - He preferred any core switch to follow Perl 6 given/when semantics, not Switch.pm's. — [msg106924](https://www.nntp.perl.org/group/perl.perl5.porters/2005/11/msg106924.html)
  - He named Synopsis 4 as "the best (i.e. only valid) reference". — [msg106978](https://www.nntp.perl.org/group/perl.perl5.porters/2005/11/msg106978.html)
- **9 Dec 2005, Robin Houston (implementer of feature.pm, given/when and `~~`), "switch".**
  - He did not ape Switch.pm but "tried to follow the Perl 6 spec as closely as seemed reasonable".
  - He listed two forced differences: `$_` is dynamically rather than lexically scoped, and parentheses are required.
  - The patch's feature.pm text says `use feature 'switch'` enables "the Perl 6 given/when construct". — [msg107265](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107265.html)
  - In a follow-up he called `~~` "the Perl 6 smart match operator" and floated a feature bundle `use feature ":perl6ish"`. — [msg107273](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107273.html)
- **16 Dec 2005, Robin Houston, "Smart match is done".** It is "based on the spec for Perl 6", with a dispatch table "adapted from the one in Synopsis 4". **[adopted-from]**. — [msg107579](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107579.html)
- **20–21 Dec 2005, "Should err be a feature?" and "[PATCH] Add feature bundles and featurize err".** This is the birth of `use feature` bundles. — [msg107692](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107692.html); [msg107704](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107704.html)
  - Dave Mitchell suggested a `-6` switch for Perl6-ish features. — [msg107716](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107716.html)
  - Rafael justified enabling features under `-e` by noting that Perl 6 also changes defaults for one-liners. — [msg107782](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107782.html)
  - Rafael said the keyword is `err` "because it's called err in Perl 6". — [msg107743](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107743.html)
  - H. Merijn Brand argued that backported Perl 6 features should keep Perl 6's words. — [msg107756](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg107756.html)
- **30 Dec 2005, Robin Houston, "[PATCH] Differences from Perl 6".** This is the origin of perlsyn's "Differences from Perl 6" section.
  - It notes that the S04 table "cannot yet be regarded as definitive". For example, S04 implied `$string ~~ qr//` tests string equality. — [msg108013](https://www.nntp.perl.org/group/perl.perl5.porters/2005/12/msg108013.html)
- **5 Apr 2006, "state variables, first draft".**
  - Asked whether `state` was a Perl 6 thing, Rafael said it comes from Perl 6, citing perltodo's `state $x = …` "the syntax from Perl 6". — [msg111439](https://www.nntp.perl.org/group/perl.perl5.porters/2006/04/msg111439.html)
  - Nick Ing-Simmons was happy if it was "essentially Larry endorsed". **[adopted-from]**. This settles the earlier "circumstantial" verdict for `state`. — [msg111469](https://www.nntp.perl.org/group/perl.perl5.porters/2006/04/msg111469.html)
- **29 Jun – 6 Jul 2006, Steve Peters tested smartmatch against Pugs**, and Yitzchak Scott-Thoennes argued that matching what Perl 6 will do matters more than making perfect sense. **[adopted-from]**, the attitude Larry later regretted. — [msg114499](https://www.nntp.perl.org/group/perl.perl5.porters/2006/06/msg114499.html); [msg114500](https://www.nntp.perl.org/group/perl.perl5.porters/2006/06/msg114500.html); [msg114691](https://www.nntp.perl.org/group/perl.perl5.porters/2006/07/msg114691.html)
- **26 Mar 2007, Rafael on "Differences between state variables and smart matches in 5.10 and 6".** He declined Perl 6's once-only `state` initializer semantics as too hard in Perl 5, and invited `~~ s///` patches. **[considered-and-rejected]**, in part. — [msg122546](https://www.nntp.perl.org/group/perl.perl5.porters/2007/03/msg122546.html)
- **22 Apr 2007, demerphq, "[PATCH] Change meaning of \v, \V, and add \h, \H to match Perl6, add \R to match PCRE and unicode tr18".**
  - The body says the change to `\v` was "requested by Larry a while back" and that `\v\V` and `\h\H` "are from Perl6".
  - **[adopted-from]**. This is a borrowing absent from the doc's table: perl5100delta lists `\v`, `\h` and `\R` without credit. — [msg123648](https://www.nntp.perl.org/group/perl.perl5.porters/2007/04/msg123648.html); [perl5100delta](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- **3 Jan 2008, two weeks after 5.10.0, Larry Wall, "Re: more ~~ concerns: object ~~".**
  - He reported that Perl 6 had decided a year earlier that `~~` must *not* be symmetrical, and that the right operand alone selects the match.
  - He added that arrays no longer imply `any` semantics.
  - He was "sorry these old misfeatures of the early Perl 6 design slipped out into Perl 5". He had assumed p5p would track the design's *principles*, not just its features. **[cautionary-tale]**, from the designer himself.
  - Outcome: perl5101delta (22 Aug 2009) says `~~` "is no longer commutative" and dispatches primarily on the right operand. — [msg132467](https://www.nntp.perl.org/group/perl.perl5.porters/2008/01/msg132467.html); [perl5101delta](https://github.com/Perl/perl5/blob/blead/pod/perl5101delta.pod)
- 2008 follow-ups:
  - brian d foy asked perl6-language for Larry's ruling on `when (… && …)`. — [msg134346](https://www.nntp.perl.org/group/perl.perl5.porters/2008/02/msg134346.html)
  - Matt S Trout proposed diffing the Perl 6 spec version 5.10 implemented against the current one. — [msg136191](https://www.nntp.perl.org/group/perl.perl5.porters/2008/04/msg136191.html)
- **9 May 2008, chromatic (Parrot/Perl 6 developer), "[PROPOSED PATCH] The yada yada yada operator".**
  - He implemented `...` per a perltodo entry, and the follow-up added Perl 6's companion stubs `???` and `!!!`.
  - The thread itself never says "Perl 6". **[adopted-from]**, by inference: the trio is Perl 6's (S06). — [msg136693](https://www.nntp.perl.org/group/perl.perl5.porters/2008/05/msg136693.html)

- **Lexical `my $_` (5.10.0), a further import that failed.**
  - Rafael's 8 Jan 2004 "[PATCH] my $_" presents lexical `$_` without crediting Perl 6. In the thread he only remarks that Perl 6 will fix the map/grep `$_` problem. — [msg86993](https://www.nntp.perl.org/group/perl.perl5.porters/2004/01/msg86993.html); [msg87054](https://www.nntp.perl.org/group/perl.perl5.porters/2004/01/msg87054.html)
  - On 6 Sep 2012 Nicholas Clark opened "Perl 6 is not a menu" by saying `my $_` "was blindly taken from Perl 6". It works badly because the rest of the design it needs (such as `OUTER::`) is missing. **[adopted-from]** (retrospective), then **[cautionary-tale]**. — [msg191632](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191632.html)
  - Outcome: experimental in 5.18, removed in 5.24.0. — [perl5240delta](https://github.com/Perl/perl5/blob/blead/pod/perl5240delta.pod)

#### 1c. Subroutine signatures (2012–2015)
- **Sep 2012, "Named prototypes (again)" and "Perl 6 is not a menu"**, the debate that framed Peter Martini's 2012 signatures work.
  - Reini Urban (B::C maintainer, later cperl author) repeatedly urged following Perl 6 and S06 and talking to the Perl 6 people. — [msg194385](https://www.nntp.perl.org/group/perl.perl5.porters/2012/10/msg194385.html)
  - Jesse Luehrs (Moose core) said switch and smartmatch were attempts to "blindly take things from Perl 6". — [msg191552](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191552.html)
  - Nicholas Clark started "Perl 6 is not a menu". His argument: parts of the Perl 6 design cannot be picked and brought back verbatim, because it is a coherent whole that diverged from Perl 5. — [msg191632](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191632.html)
  - demerphq judged nearly every Perl 6-inspired Perl 5 feature a failure and asked for counter-examples. — [msg191641](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191641.html)
  - Leon Timmermans offered `say`, `//` and possibly named captures as counter-examples. — [msg191642](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191642.html)
  - David Golden credited Perl 6's object model with partly inspiring Moose. — [msg191651](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191651.html)
  - Chris Prather (Moose core) countered that Stevan Little threw away his Pugs meta-models. Only minor syntax such as `has` came from Perl 6, and Moose's meta-model is "very different" from Perl 6's. — [msg191706](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191706.html)
  - Ricardo Signes (then the Perl 5 pumpking) said porting Larry's Perl 6 designs blindly to Perl 5 "would be a disaster". — [msg191662](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191662.html)
  - Nicholas Clark cited S06's read-only-alias default, but noted that Perl 6 can change its design and break code while Perl 5 cannot. — [msg191894](https://www.nntp.perl.org/group/perl.perl5.porters/2012/09/msg191894.html)
  - **[considered-and-rejected]** as a model, with S06 as **[cited-as-prior-art]**.
- **Oct 2012, "sub signatures - status / too many arguments".**
  - demerphq said Perl 6's decisions matter to Perl 5 about as much as "a court ruling by a German judge" in a US trial. — [msg194427](https://www.nntp.perl.org/group/perl.perl5.porters/2012/10/msg194427.html)
  - Alexander Hartmaier wanted Perl 5 signatures to match Perl 6's so switching languages would be easier. — [msg194545](https://www.nntp.perl.org/group/perl.perl5.porters/2012/10/msg194545.html)
- **Jan–Feb 2014, Zefram's design ("signatures again", 87 msgs; "acceptable signatures", 116; "accepted signatures", 55).** These threads produced 5.20's signatures and contain **no Perl 6 mention**. The only one in the 2014 set is Smylers pointing to S06 for trait placement in "signatures placement" (Feb 2014).
- **15 Aug 2015.** Reini Urban said Perl 5 "generally should follow perl6 design decisions", complaining that it had not done so for signatures and refaliasing. **[considered-and-rejected]**. — [msg230146](https://www.nntp.perl.org/group/perl.perl5.porters/2015/08/msg230146.html)

#### 1d. `<<~` indented heredocs (2016)
- **30 Jun 2016, Matthew Horsfall (core developer), "Indented here docs?"** The rough patch did not yet strip indentation, but could, "following Perl 6's rules perhaps". He then summarized Raku's rule:
  - lines indented more than the delimiter keep their extra indentation;
  - lines indented less are treated as indented to the delimiter, with a warning.
  - **[cited-as-prior-art]** for the semantics. — [msg237477](https://www.nntp.perl.org/group/perl.perl5.porters/2016/06/msg237477.html), msg-id `<CAJ0K8bjxkyQ-YpsFpQPYZ6QziGxDO9vs1eBQL38UkGEzg7jrJw@mail.gmail.com>`
- **Same day, Horsfall's second patch (topic/indented-here-docs).** It stripped the delimiter's whitespace and only *warned* on mismatched lines, matching Raku's leniency. — [msg237489](https://www.nntp.perl.org/group/perl.perl5.porters/2016/07/msg237489.html)
- **1 Jul 2016.**
  - Zefram argued that improper indentation should be an error, since no lax usage was established. — [msg237505](https://www.nntp.perl.org/group/perl.perl5.porters/2016/07/msg237505.html)
  - Father Chrysostomos agreed that the exact whitespace sequence should be required. — [msg237526](https://www.nntp.perl.org/group/perl.perl5.porters/2016/07/msg237526.html)
  - Outcome: perldiag classifies "Indentation on line %d of here-doc doesn't match delimiter" as **(F)** fatal. — [perldiag](https://github.com/Perl/perl5/blob/blead/pod/perldiag.pod)
  - So Perl 5 **diverged from Raku's rule** (Raku warns and continues).
- **Spelling.**
  - Horsfall: "I modelled mine lightly after ruby 2.3.x", which added `<<~` because `<<-` was taken. — [msg237510](https://www.nntp.perl.org/group/perl.perl5.porters/2016/07/msg237510.html)
  - Aristotle Pagaltzis wanted bash/zsh `<<-` instead. — [msg237507](https://www.nntp.perl.org/group/perl.perl5.porters/2016/07/msg237507.html)
  - Verdict: **Ruby's spelling, Raku's rule as the starting point, final rule stricter than both**. Ruby's `<<~` silently uses the least-indented line.

#### 1e. Chained comparisons (2002, 2017, 2020)
- The only Perl 6 link is Brent Dax's 20 Jul 2002 backport suggestion in 1a. — [msg63819](https://www.nntp.perl.org/group/perl.perl5.porters/2002/07/msg63819.html)
- **18 Dec 2017, Zefram, RT #132607 "chained comparison".** It is a wishlist ticket proposing that `$a le $b lt $c` mean `$a le $b && $b lt $c` with a single evaluation of the middle term. It names no precedent language. — [msg248224](https://www.nntp.perl.org/group/perl.perl5.porters/2017/12/msg248224.html)
- **5 Feb – 28 Apr 2020, "chained comparisons", 67 messages.** It opens with Zefram pointing to branch `zefram/cmpchain`. — [msg257005](https://www.nntp.perl.org/group/perl.perl5.porters/2020/02/msg257005.html)
  - A scan of all 67 messages finds **no mention of Perl 6, Raku, Python or any other language**. The debate is about precedence levels, `FETCH` counts and `==` mixing.
  - Verdict: the 2002 suggestion is documented, but the 2020 implementation shows no Raku credit.

#### 1f. `isa` (2019)
- **22 Oct 2019, Paul "LeoNerd" Evans, "[feature] an infix `isa` operator #17200".** On 24 Oct he followed with "PR: Add an `isa` operator #17212". Both posts only redirect discussion to GitHub. — [msg256501](https://www.nntp.perl.org/group/perl.perl5.porters/2019/10/msg256501.html); [msg256521](https://www.nntp.perl.org/group/perl.perl5.porters/2019/10/msg256521.html)
- The p5p follow-ups (Nov–Dec 2019) and the rendered HTML of [issue #17200](https://github.com/Perl/perl5/issues/17200) and [PR #17212](https://github.com/Perl/perl5/pull/17212) contain no "Perl 6", "Raku", "Python" or "instanceof". This was checked by grep over the fetched HTML, not via api.github.com.
- No Raku link is documented.

#### 1g. Other Perl 6 threads in p5p
- **15 Feb 2015, Brad Gilbert, "RFC: extended regex modifiers similar to Perl 6 modifiers".** Others liked Perl 6's leading-modifier placement.
  - Outcome: not adopted. — [msg225807](https://www.nntp.perl.org/group/perl.perl5.porters/2015/02/msg225807.html)
- **Apr–Jun 2015, accommodating Perl 6 in perl itself.**
  - Stefan Seifert (Inline::Perl5 author, Rakudo core developer) proposed making `perl` exec `perl6` on `use v6` or on `module`/`class`. — [msg227427](https://www.nntp.perl.org/group/perl.perl5.porters/2015/04/msg227427.html)
  - Ricardo Signes declined, saying it would turn a clear error into "a bizarre exec". — [msg228654](https://www.nntp.perl.org/group/perl.perl5.porters/2015/06/msg228654.html)
  - Leon Timmermans' "Respect perl6 in hashbang" patch became 5.24.0's change, which 5.24.1 reverted. Zefram had called the `#!` hack obsolete. — [msg227445](https://www.nntp.perl.org/group/perl.perl5.porters/2015/04/msg227445.html); [msg227446](https://www.nntp.perl.org/group/perl.perl5.porters/2015/04/msg227446.html)
- **25–29 Sep 2026, Darren Duncan, "New Perl switch proposals avoiding Raku keywords on purpose?"** He asked whether Raku's `given`/`when` had been explicitly rejected for the current match/case PPC proposals. — [msg271262](https://www.nntp.perl.org/group/perl.perl5.porters/2026/09/msg271262.html)
  - Smylers: Perl 5 tried to copy Perl 6's given/when, it didn't work out, and reusing the names would confuse searches. — [msg271264](https://www.nntp.perl.org/group/perl.perl5.porters/2026/09/msg271264.html)
  - Dan Book: given/when are now "permanently discouraged", so neither the names nor `~~` can be reused. — [msg271265](https://www.nntp.perl.org/group/perl.perl5.porters/2026/09/msg271265.html)
  - Leon Timmermans: any candidate must solve the number/string ambiguity that smartmatch suffers from. — [msg271274](https://www.nntp.perl.org/group/perl.perl5.porters/2026/09/msg271274.html)
  - **[cautionary-tale]**: the Raku names are implicitly rejected because of the 5.10 experience.

### Inferences
- The doc's "circumstantial" verdicts can be upgraded:
  - **`//`**: documented in the patch subject itself.
  - **`state`**: documented by Rafael.
  - **chained comparisons**: proposed as a Perl 6 backport in 2002, but implemented in 2020 without credit.
- **`\h`/`\v`** is a new documented borrowing (5.10.0).
- **Lexical `my $_`** (5.10.0, removed in 5.24) is a further import. p5p identified it as Perl 6-derived only in retrospect.
- **`<<~`** is "Raku's rule considered, then tightened; Ruby's spelling".
- The 5.10 credit pattern is clear: p5p's threads and feature.pm/perlsyn credited Perl 6 openly, while perl5100delta did not. The perldelta silence reflects writing convention, not the absence of borrowing.
- `use feature` descends directly from Hugo's 2002 `use perl6ish` idea, which he attributed to a conversation with Larry, and from Rafael's 2002 `perl6ish` pragma. The mechanism Perl 5 uses for all later experiments was designed to import Perl 6 features.
- The 2008 Larry message and the 2012 "not a menu" thread mark the turn. After them, Perl 6 appears on p5p mainly as a reason to diverge, ending with the 2026 thread where Raku's keywords are avoided on purpose.

### Gaps
- **Coverage.** Full bodies were fetched only for subject-selected threads (~2,000 articles), not for all 271k. A Perl 6 credit buried in an unrelated-subject message would be missed.
  - Subject lines containing Perl 6/Raku were checked across the whole 1999–2026 span.
  - The `isa` and 2019+ design discussions moved to GitHub issues/PRs; only two rendered pages were checked.
  - PPC discussions on GitHub (Perl/PPCs) were not read.
- The 2005 `say` thread's first message is by Nigel Sandever. Who wrote the final applied `say` patch (Robin Houston's Dec 2005 bundle vs. Steve Peters' Nov 2005 patch) was not resolved from the commit log; no local perl5 clone was available.
- Whether H. Merijn Brand ("Tux") had an earlier `//` patch (Johan Vromans' 2012 claim) was not verified. His later "dor" patches for 5.8.x (2003–2006) appear in subjects, for example "defined-or for perl-5.8.x-20277", Jul 2003.

## 2. python-3000 (2006–2008): PEP 3102 keyword-only arguments, PEP 3107 annotations, PEP 3133 roles, PEP 3119 ABCs

### Takeaway
No one on python-3000 cited Perl 6 for keyword-only parameters or annotations.

The only Perl 6 remark in the threads that produced PEP 3102 is Guido's "Remember, we don't want to emulate Perl 6!", rejecting a bracketed parameter-list syntax. A day later he pronounced `def foo(a, b, *, x=1, y=2)`, and Talin wrote the PEP.

Roles are the exception. Collin Winter proposed Perl 6 roles explicitly in April and May 2007. Guido engaged on the merits, preferring ABCs because they overload `isinstance()` and can carry concrete methods, and rejected PEP 3133 on 24 May 2007.

Elsewhere on the list Perl 6 was a warning: on Unicode identifiers, "Down that road lies Perl 6", and on the yada operator, "Blah, bah."

### Cited Findings
- **Genesis of PEP 3102: the "Type Expressions" and "parameter lists" threads, 18–22 Apr 2006.**
  - Greg Ewing floated "wilder ideas", including braces around keyword-only parameters, `def spam(a, b, c){d, e, f=x}`. — [python-3000/2006-April/001073](https://mail.python.org/pipermail/python-3000/2006-April/001073.html)
  - Sam Pointon proposed `[]` for positional-only parameters as well. — [001091](https://mail.python.org/pipermail/python-3000/2006-April/001091.html)
  - **Guido (BDFL), 20 Apr 2006**: "Forget it… Remember, we don't want to emulate Perl 6!" Message-ID `<ca471dc20604200840o27b8c680u48c5cfbebe8acfbf@mail.gmail.com>`. **[cautionary-tale]**. Nobody in the thread had mentioned Perl 6. Guido invoked it as shorthand for syntactic excess, not as a reference to Perl 6's named-only `+$x`/`:$x` parameters. — [001094](https://mail.python.org/pipermail/python-3000/2006-April/001094.html)
  - **Guido, 21 Apr 2006**: to prevent "more abominations like this", he pronounced the single-star syntax `def foo(a, b, *, x=1, y=2)`. — [001150](https://mail.python.org/pipermail/python-3000/2006-April/001150.html)
  - Talin (later the PEP 3102 author) floated `def foo(a, b; x=1, y=2)` and offered to write a PEP. — [001162](https://mail.python.org/pipermail/python-3000/2006-April/001162.html)
  - Guido replied that he had seen something like `;` "in another language. (Perl? VB?)" but disliked reversing `;`'s priority. **[passing]**. — [001173](https://mail.python.org/pipermail/python-3000/2006-April/001173.html)
  - Talin posted "PEP: Keyword-Only Arguments" on 23 Apr 2006. — [001255](https://mail.python.org/pipermail/python-3000/2006-April/001255.html)
- **Later PEP 3102 threads.** Grepping every message of the PEP 3102 threads found no mention of Perl, Lisp, Ruby, Smalltalk or Ada. The threads checked:
  - on python-3000: "PEP 3102 Open Issues" (May 2006), "PEP 3102 feedback/comments" (May 2006), "PEP3102 Keyword-Only Arguments" (Aug 2006), "PEP 3102" (Feb 2008), "PEP 3102 question" (Apr 2008);
  - on python-dev: "PEP 3102: Keyword-only arguments", 62 msgs from 29 Apr 2006. — [python-dev/2006-April/064598](https://mail.python.org/pipermail/python-dev/2006-April/064598.html)
  - This confirms that **PEP 3102 names no precedent, and its discussion names none either**.
- **PEP 3107 annotations.** The function-annotation threads were checked: "Draft pre-PEP: function annotations" (86 msgs), "Type annotations: annotating generators", "What's the point of annotations?" and "PEP 3107 - Function Annotations". They contain **no Perl 6 mention**.
  - The only Perl mention nearby is Guido asking, in "Type parameterization", whether an idiom came from Perl or Tcl (18 May 2006). **[passing]**.
  - The "Type Expressions" thread, where Guido's "emulate Perl 6" remark appears, began on 18 Apr 2006 (Bill Birch) as a discussion of type-expression syntax for annotations. So that one negative remark is the sole Perl 6 reference in the pre-history of both PEP 3102 and PEP 3107.
- **Roles (PEP 3133) and ABCs (PEP 3119).**
  - **29 Apr 2007, Collin Winter (Google; PEP 3107 co-author), "Traits/roles instead of ABCs".** He called Perl 6's roles the mechanism he knew best (with Squeak traits), quoted an article on Perl 6 roles, and explained `does`. **[cited-as-prior-art]** → proposal. — [2007-April/007026](https://mail.python.org/pipermail/python-3000/2007-April/007026.html)
  - On 30 Apr he described Perl 6's conflict rule as throwing up your hands and letting the programmer sort it out. — [007061](https://mail.python.org/pipermail/python-3000/2007-April/007061.html)
  - **14 May 2007, Collin Winter posted PEP 3133**, citing S12 and Curtis Poe's article. — [2007-May/007589](https://mail.python.org/pipermail/python-3000/2007-May/007589.html)
  - **Guido, 15 May 2007.** Even on a level playing field he would still prefer ABCs, because they can be subclassed and carry concrete or partial methods. He asked what Roles would add if they too overloaded `isinstance()`. **[considered-and-rejected]**, on technical grounds, without anti-Perl rhetoric. — [007666](https://mail.python.org/pipermail/python-3000/2007-May/007666.html)
  - **Guido, 24 May 2007, "Accepting PEP 3119, rejecting PEP 3133".** — [007913](https://mail.python.org/pipermail/python-3000/2007-May/007913.html)
- **Other python-3000 Perl 6 remarks.**
  - **PEP 3131 (Unicode identifiers), 14–16 May 2007.**
    - Collin Winter recalled the Perl 6 people debating Unicode identifiers for months and running out of ASCII punctuation. — [007603](https://mail.python.org/pipermail/python-3000/2007-May/007603.html)
    - Guido answered a "Unicode for the hell of it" argument with "Down that road lies Perl 6. We need to give the world a sane alternative." **[cautionary-tale]**. — [007689](https://mail.python.org/pipermail/python-3000/2007-May/007689.html)
  - **Ellipsis literal, 25 Jan 2008.**
    - Gisle Aas (ActiveState; author of Perl 5's LWP) suggested a lone `...` should raise NotImplemented, "like" Perl 6's yada operator. — [2008-January/011830](https://mail.python.org/pipermail/python-3000/2008-January/011830.html)
    - Guido: "Blah, bah. :-)". **[considered-and-rejected]**: `...` remained a plain Ellipsis expression. — [011835](https://mail.python.org/pipermail/python-3000/2008-January/011835.html)
  - **Parrot, 14 Aug 2006.** Guido defended watching Parrot, which by then ran Python, Tcl and "even Perl 6 :-)". **[passing]**. — [2006-August/002928](https://mail.python.org/pipermail/python-3000/2006-August/002928.html)
  - **Jokes and passing remarks.**
    - Georg Brandl (core developer) joked that Python 3000 PEPs 31xx would be "Apocalypses" and 32xx "Synopses" (28 Mar 2006). — [2006-March/000166](https://mail.python.org/pipermail/python-3000/2006-March/000166.html)
    - Georg Brandl noted that Perl 6 was changing `[]` versus `{}` (30 Apr 2006). — [2006-April/001555](https://mail.python.org/pipermail/python-3000/2006-April/001555.html)
    - Paul Prescod noted that Perl 6 might add character-set independence (1 Sep 2006). — [2006-September/003403](https://mail.python.org/pipermail/python-3000/2006-September/003403.html)

### Inferences
- Python's keyword-only parameters are **independent of Perl 6** on the evidence of the list.
  - The design moved from brace and bracket proposals to `*`. The only Perl 6 reference was a rejection of syntactic complexity.
  - Nobody pointed out that Perl 6's A6 (Mar 2003) already had named-only parameters.
- Roles were the one Perl 6 idea that python-3000 seriously weighed. Guido rejected it for ABCs on technical grounds (subclassing plus `isinstance` overloading), consistent with the PEP 3133 rejection note.

### Gaps
- Guido's private and IRC discussions, and the 2006–2007 PyCon/Google sprint conversations about ABCs, are not on the list.

## 3. python-ideas and python-dev (2000 – Nov 2026)

### Takeaway
Across the full archives there are 37 python-ideas and about 116 python-dev messages with an original-text Perl 6/Raku mention. python-dev's 2000 pipermail files duplicate messages, so the unique count is lower.

Most are **cautionary** remarks about Perl 6's timeline or compatibility break, or Parrot/Pie-thon chatter. A minority cite Perl 6 as **prior art**, and **none records Python adopting anything from Perl 6**. The prior-art citations:
- `assuming` for PEP 309's partial;
- `@@` for PEP 344 exception chaining;
- hyper operators for PEP 335;
- junctions for `any()`/`all()`;
- feed operators for function composition;
- meta-operators for PEP 465;
- Unicode operators;
- given/when for pattern matching;
- Perl 6 regex syntax for `re`;
- surveys for PEP 572 and PEP 671.

Guido's own remarks are uniformly negative or joking. Perl 6 matching is how far "one can stretch the insanity" (2006); "Let them contribute to Perl 6" (2009); don't change `re.VERBOSE` just "because Perl 6 does it" (2017).

### Cited Findings
**Prior-art citations, by feature** (none adopted from Perl 6):
- **Partial application (PEP 309).** 24 Feb 2004, Gisle Aas: Perl 6 plans `f.assuming()` for currying, see A06. **[cited-as-prior-art]**. Outcome: Python shipped `functools.partial`. — [python-dev/2004-February/042702](https://mail.python.org/pipermail/python-dev/2004-February/042702.html)
- Also 4 Aug 2018, Steven D'Aprano, survey for partial-function syntax: Perl 6 gives all functions an `assuming` method. **[cited-as-prior-art]**; no change. — [python-ideas/2018-August/052612](https://mail.python.org/pipermail/python-ideas/2018-August/052612.html)
- **Exception chaining (PEP 344, later PEP 3134).** 20 May 2005, Ka-Ping Yee (PEP author): the only support he could find was the proposed `@@` in Perl 6. **[cited-as-prior-art]**. This matches the PEP's RFC 88 citation. — [python-dev/2005-May/053943](https://mail.python.org/pipermail/python-dev/2005-May/053943.html)
- **Element-wise operators (PEP 335).** 15 Sep 2004, James Y Knight: Perl 6 has distinct element-wise "hyper" operators. **[cited-as-prior-art]**. Outcome: PEP 335 was rejected; Python has no hyper operators. — [python-dev/2004-September/048919](https://mail.python.org/pipermail/python-dev/2004-September/048919.html)
- **Junctions.**
  - 11 Mar 2005, Reinhold Birkenfeld (later Georg Brandl), on adding `any()`/`all()`: shouldn't we save them for junctions like Perl 6's, "<har, har…>". **[passing/joke]**. — [python-dev/2005-March/052046](https://mail.python.org/pipermail/python-dev/2005-March/052046.html)
  - 13 Apr 2006, Mikhail Glushenkov: a predicate argument to `any()`/`all()` resembles Perl 6 junctions. **[cited-as-prior-art]**; not adopted. — [python-dev/2006-April/063680](https://mail.python.org/pipermail/python-dev/2006-April/063680.html)
  - A grep for "junction" across python-dev, python-ideas and python-3000 otherwise finds only Windows NTFS junction points.
- **Feeds / function composition.** 17 Feb 2006, Georg Brandl pointed a `foo(x) -> bar()` proposal to S03's piping operators. **[cited-as-prior-art]**; not adopted. — [python-dev/2006-February/061285](https://mail.python.org/pipermail/python-dev/2006-February/061285.html)
- **Switch / pattern matching.**
  - 19 Jun 2006, Guido in the PEP 3103 "Switch statement" thread: Perl 6 is defining a very general matching syntax worth studying "just to see how far one can stretch the insanity". **[cautionary-tale]**. Outcome: PEP 3103 was rejected. — [python-dev/2006-June/066238](https://mail.python.org/pipermail/python-dev/2006-June/066238.html)
  - 28 May 2016, Manuel Cerón, "Match statement brainstorm", linked Perl 6's `given`. **[cited-as-prior-art]**. Python's 2021 `match` (PEP 634) follows Scala/Rust-style structural patterns. — [python-ideas/2016-May/040568](https://mail.python.org/pipermail/python-ideas/2016-May/040568.html)
- **Matrix multiplication (PEP 465).** 7 Apr 2014, Victor Stinner (core developer): one might mention Perl 6 meta-operators, but they are "not directly related". **[passing]**. — [python-dev/2014-April/133802](https://mail.python.org/pipermail/python-dev/2014-April/133802.html)
- **Unicode operators.** 31 Oct 2016, Steven D'Aprano, "Non-ASCII in Python syntax?": Perl 6 is "leading the way" with Unicode operators and does the right thing for superscripts. **[cited-as-prior-art]**; no change. — [python-ideas/2016-October/043483](https://mail.python.org/pipermail/python-ideas/2016-October/043483.html)
- **Regex syntax.** **[cited-as-prior-art]**; not adopted.
  - 26 Feb 2003, Brett Cannon floated an option to follow Perl 6's suggested regex syntax. — [python-dev/2003-February/033735](https://mail.python.org/pipermail/python-dev/2003-February/033735.html)
  - 6 Aug 2004, Mike Coleman's pre-PEP on structured group matching cites A05, noting that it "did not inspire this PEP". — [python-dev/2004-August/047042](https://mail.python.org/pipermail/python-dev/2004-August/047042.html)
  - 28 Mar 2017, Steven D'Aprano listed Perl 6's `m//` and `rx//` when regex literals were proposed. — [python-ideas/2017-March/045324](https://mail.python.org/pipermail/python-ideas/2017-March/045324.html)
  - 28 Oct 2017, Steven D'Aprano asked what people thought of Perl 6 regex syntax. — [python-dev/2017-October/150021](https://mail.python.org/pipermail/python-dev/2017-October/150021.html)
  - 16 Nov 2017, **Guido**: "Let's not change things just because we can, or because Perl 6 does it" (on `re.VERBOSE` whitespace). **[considered-and-rejected]**. — [python-ideas/2017-November/047924](https://mail.python.org/pipermail/python-ideas/2017-November/047924.html)
- **Language surveys by Steven D'Aprano.** **[cited-as-prior-art]**, one language among many.
  - Assignment expressions (PEP 572 era, 19 May 2018), listing "Perl 6 a.k.a. Rakudo". — [python-ideas/2018-May/050938](https://mail.python.org/pipermail/python-ideas/2018-May/050938.html)
  - PEP 671 late-bound defaults (4 Dec 2021), with a Raku entry. — [HyperKitty P5JAIGZE…](https://mail.python.org/archives/list/python-ideas@python.org/message/P5JAIGZEI4PXR2TYS5ELB6TN4JVLUNL7/)
  - Variable type hints (1 Sep 2015), noting "(Java, C, Perl6)" for prefix type declarations. — [python-ideas/2015-September/035626](https://mail.python.org/pipermail/python-ideas/2015-September/035626.html)
- **Loop phasers.** 28 Aug 2008, Chris Rebert: the proposed "interstitially" clause equals Perl 6 `NEXT` blocks. **[cited-as-prior-art]**; not adopted. — [python-ideas/2008-August/001867](https://mail.python.org/pipermail/python-ideas/2008-August/001867.html)
- **Multiple dispatch.** 15 Dec 2007, Neil Toronto linked Perl 6 RFC 256. **[cited-as-prior-art]**. — [python-ideas/2007-December/001272](https://mail.python.org/pipermail/python-ideas/2007-December/001272.html)
- **Print statement.** 9 Jun 2020, **Guido**, "Bringing the print statement back": other languages with paren-less calls "(Ruby? R? Raku?) but I haven't investigated". **[passing]**. — [HyperKitty NCQX6ZIB…](https://mail.python.org/archives/list/python-ideas@python.org/message/NCQX6ZIBREUTLS52VVG3DSZ43OEXJFTT/)

**Cautionary remarks**:
- **Guido.**
  - 22 Apr 2002: no "herculean effort like the Perl 6 folks are making". — [python-dev/2002-April/023435](https://mail.python.org/pipermail/python-dev/2002-April/023435.html)
  - 31 Jan 2005: Python 3000 shouldn't be "as pervasive as the Perl 6 design". — [python-dev/2005-January/051363](https://mail.python.org/pipermail/python-dev/2005-January/051363.html)
  - 19 Mar 2006: developers holding their breath, as "with Perl 6". — [python-dev/2006-March/062644](https://mail.python.org/pipermail/python-dev/2006-March/062644.html)
  - 22 Oct 2009, language moratorium: "Let them contribute to Perl 6. :-)" — [python-ideas/2009-October/006411](https://mail.python.org/pipermail/python-ideas/2009-October/006411.html)
  - 16 Aug 2014, PEP 4000 thread: who's to say someone won't design a new language and call it Python 4, "following Larry Wall's Perl 6 example?" — [python-dev/2014-August/135812](https://mail.python.org/pipermail/python-dev/2014-August/135812.html)
- **Others in the PEP 4000 thread.**
  - Łukasz Langa (later Python 3.8/3.9 release manager): don't follow the Perl 6 example. — [135814](https://mail.python.org/pipermail/python-dev/2014-August/135814.html)
  - Nick Coghlan cited the examples of both Python 3 and Perl 6. — [135815](https://mail.python.org/pipermail/python-dev/2014-August/135815.html)
- **Antoine Pitrou (core developer).**
  - PEP 505 None-aware operators "look like noise (or like Perl 6…)" (19 Jul 2018). — [python-ideas/2018-July/052059](https://mail.python.org/pipermail/python-ideas/2018-July/052059.html)
  - On PEP 584 dict `+`: "an argument for Perl 6, not for Python" (15 Mar 2019). — [python-ideas/2019-March/055867](https://mail.python.org/pipermail/python-ideas/2019-March/055867.html)

**Passing mentions** (2000–2011): python-dev's early-2000s volume is dominated by the April 2001 "Parrot" joke turning into a real VM and by the Pie-thon bet.
- Thomas Wouters, Fredrik Lundh and Barry Warsaw on the July 2000 Perl 6 announcement.
- Jeremy Hylton's 1 Apr 2001 "Perl and Python to begin joint development".
- The "Parrot -- should life imitate satire?" thread of Jul–Aug 2001, with Dan Sugalski, Nathan Torkington and Simon Cozens.
- Guido on 3 Aug 2008: the Perl 6 VM's name comes from the Python/Perl merger joke. — [python-dev/2008-August/081655](https://mail.python.org/pipermail/python-dev/2008-August/081655.html)

### Inferences
- Python's design lists treated Perl 6 as a **foil**: its chained comparisons, keyword-only parameters and Unicode stance predate or ignore it. When posters raised Perl 6 features (junctions, hypers, given/when, Unicode operators, regex reform), the response was silence or rejection.
- The most influential Perl 6-adjacent idea on these lists, the yada `...`, was also rejected by Guido in python-3000.
- Perl 6 never appears as the stated source of any accepted PEP feature on these lists.
- Steven D'Aprano's surveys (2015–2021) show Raku becoming a routine "prior art" entry after 2015, without influence.

### Gaps
- The archives cover the lists only. Since about 2022 the real design debate is on discuss.python.org (Discourse), which was not searched.
- python-dev's 2000 pipermail files contain duplicated messages, so the reported raw count overstates unique posts. Unique Message-IDs were used when reading.

## 4. es-discuss and es4-discuss

### Takeaway
Perl 6 appears about two dozen times, 2007–2017, almost always as either:
- (a) Brendan Eich's example of what a compatibility-breaking redesign looks like (admired but ruled out), or
- (b) prior art for regex reform, chained comparisons, graphemes, roles or operator-definition syntax, none of which JavaScript took.

The one near-adoption is the **January 2012 arrow-function discussion**. Eich floated a *prefix* `->` arrow after a suggestion "referencing Ruby 1.9 and Perl6". ES2015 shipped the infix `=>` instead.

### Cited Findings
- **Coverage.**
  - mail-archive.com's es4-discuss mirror (22 Jun 2007 – 15 Aug 2008) and es-discuss mirror (15 Aug 2008 – 30 Mar 2021) were searched for "perl 6", "perl6", "perl-6", "raku", "rakudo", "parrot", "pugs", "larry wall" and "apocalypse".
  - The original mail.mozilla.org pipermail monthly files for Jun 2006 – Jul 2007 were fetched from the Wayback Machine and grepped in full. They contain no Perl 6 mention; Perl appears only as Perl 5 (`use`/`require`, `||=` semantics, regex character classes).
  - mail.mozilla.org itself no longer resolves.
- **es4-discuss.**
  - 15 Nov 2007, liorean: a good diamond-problem resolution exists in Python and "was borrowed by Perl 6". **[passing]**. — [es4-discuss msg00768](https://www.mail-archive.com/es4-discuss@mozilla.org/msg00768.html)
  - 1 Feb 2008, **Brendan Eich** (JavaScript creator, Mozilla CTO): ES4 had "not pulled the Perl 6 lever" of an incompatible, more concise regexp syntax. **[considered-and-rejected]**. — [msg01004](https://www.mail-archive.com/es4-discuss@mozilla.org/msg01004.html)
  - 26 Mar 2008, Eich, "complexity tax": no one working on ES4 wants it "to be like Perl 6". **[cautionary-tale]**, already known. — [msg01666](https://www.mail-archive.com/es4-discuss@mozilla.org/msg01666.html)
- **Regex reform, 2010–2011.** **[considered-and-rejected]**: JavaScript has no `/n` flag and no Perl 6-style regex.
  - 13 Feb 2010, Eich: "Perl 6 of course breaks compatibility utterly, and good for it." He mused about a new RegEx class. — [es-discuss msg03884](https://www.mail-archive.com/es-discuss@mozilla.org/msg03884.html)
  - 2 Mar 2010, Andy Chu (later author of the Oil shell) cited A05's critique of regex culture, saying Perl 6 admitted Perl 5 regexes "got out of hand". **[cited-as-prior-art]**. — [msg03931](https://www.mail-archive.com/es-discuss@mozilla.org/msg03931.html)
  - 3 Mar 2010, Steven Levithan (XRegExp author): Perl 6's radical redesign is already ruled out. — [msg03933](https://www.mail-archive.com/es-discuss@mozilla.org/msg03933.html)
  - 19 Jan 2011, Jasvir Nagra on Perl 6's `^^`/`$$`. — [msg05590](https://www.mail-archive.com/es-discuss@mozilla.org/msg05590.html)
  - 2 Jun 2011, Kyle Simpson proposed a default non-capturing flag, citing Perl 6's `[ ]`. — [msg07927](https://www.mail-archive.com/es-discuss@mozilla.org/msg07927.html)
  - Eich replied: "it's great Perl 6 broken compatibility. Wish we could." He agreed "with lwall" that capturing-by-default is wrong. — [msg07928](https://www.mail-archive.com/es-discuss@mozilla.org/msg07928.html)
- **Compatibility as a principle.** **[cautionary-tale]**.
  - 10 Mar 2011, Eich: Harmony is not "Perl 6 vs. 5", though he likes Perl 6's lessons-learned changes. — [msg05993](https://www.mail-archive.com/es-discuss@mozilla.org/msg05993.html)
  - 11 Aug 2014, the "reddit" message, already known. — [msg31454](https://www.mail-archive.com/es-discuss@mozilla.org/msg31454.html)
  - 25 Jul 2017, Eich: opt-in versioning "a la Python3 or Perl6" is "a non-starter". — [msg41137](https://www.mail-archive.com/es-discuss@mozilla.org/msg41137.html)
- **Arrow functions, 18 Jan 2012.** Eich, "Block lambda is cool, its syntax isn't", sketched `->`-prefixed ArrowFunctionExpression grammar. He said someone with the nick "satyr" (author of the Coco/LiveScript dialects) had suggested it, referencing Ruby 1.9 and Perl6. **[considered-and-rejected]**. Outcome: ES2015 `(a) => …`, a CoffeeScript-style infix arrow. — [msg12716](https://www.mail-archive.com/es-discuss@mozilla.org/msg12716.html)
- **Other prior-art citations.** All **[cited-as-prior-art]**, none adopted.
  - **Roles.** 14 Apr 2013, Peter Seliger preferred Perl 6 "Roles" as the generic term in the Object.mixin debate. — [msg22677](https://www.mail-archive.com/es-discuss@mozilla.org/msg22677.html)
  - **Chained comparisons.** 18 Jul 2013, Andy Earnshaw, "Chained comparisons from Python and CoffeeScript", cited Python, Perl 6 and CoffeeScript. — [msg24857](https://www.mail-archive.com/es-discuss@mozilla.org/msg24857.html)
    - 22 Jul 2013, Claude Pache replied in the same thread. — [msg24923](https://www.mail-archive.com/es-discuss@mozilla.org/msg24923.html)
  - **Graphemes.**
    - 24 Oct 2013, Bjoern Hoehrmann: grapheme iteration is Perl 6's specified default. — [msg27126](https://www.mail-archive.com/es-discuss@mozilla.org/msg27126.html)
    - 2 Apr 2015, Mathias Bynens (then at Opera): Perl 6 handles grapheme clusters; ES leaves it to user-land. — [msg35050](https://www.mail-archive.com/es-discuss@mozilla.org/msg35050.html)
  - **Operator definition.** 13 Oct 2015, Steve Fink (Mozilla SpiderMonkey): Perl 6's `infix:<+>` and `circumfix:«( )»` beat C++'s `operator`. — [msg37097](https://www.mail-archive.com/es-discuss@mozilla.org/msg37097.html)

### Inferences
- es-discuss confirms the doc's picture. Eich consistently admired Perl 6's willingness to break compatibility while declaring it impossible for JavaScript.
- The arrow-function episode is the closest JavaScript came to Perl 6 syntax, and it went the other way.
- Chained comparisons were proposed to TC39's list in 2013 with Perl 6 among the precedents and went nowhere. C# approved them only in 2026.

### Gaps
- **Full-text cross-check (done).** 121 original pipermail month files were fetched from the Wayback Machine, covering Jun 2006 – Oct 2016. Four 2016 months (Apr, May, Jul, Aug) have no snapshot.
  - Grepping them in full reproduced exactly the hits found by mail-archive.com's search for the overlapping period, and found no others. Broader terms (`parrot`, `pugs`, `apocalypse`, `larry wall`) found nothing more either.
  - The Nov 2016 – Mar 2021 months rest on mail-archive.com's search alone, which could miss tokenizations such as "Perl6's". The fetch was stopped to avoid hammering the Wayback Machine, which returns 404 with long back-offs for several months.
- esdiscuss.org's own Algolia search was not used, because a direct query was refused by this session's permission classifier.
- TC39's GitHub proposal repos and meeting notes (tc39/notes) were not searched here.

## 5. ruby-dev / ruby-core around Matz's July 2005 `->` lambda commit

### Takeaway
Yes, there is more than the 2008 ruby-core messages. The design was argued in **Japanese on ruby-dev two days after the commit** (thread "Ruby2.0BlockParameterNotation", ruby-dev:26623–26654, 28–30 Jul 2005). There Matz said the current grammar is aligned with Perl 6 and gave a comparison table headed "Perl6, Ruby2 `-> (x,y) {...}`".

His diary of 22 Jul 2005 is more explicit still: the original `->` proposal "actually comes from Perl6". He weighed Perl 6's cultural closeness to Ruby against doubts about when, or whether, Perl 6 would ever be used.

### Cited Findings
- **Matz's diary, 1 Jul 2005** (www.rubyist.net/~matz). Listing candidate lambda syntaxes, he wrote up "Perl6風無名関数" (Perl6-style anonymous function) `-> (x,y) { ... }`, with Perl 6's `foreach ary -> (x) {...}` usage. He later said "the `->` in Perl6 would be fine" as a shorter symbol than `lambda`, while also considering Haskell's `\`. **[adopted-from]** (design stage). — [rubyist.net/~matz/20050701.html](http://www.rubyist.net/~matz/20050701.html)
- **Matz's diary, 21 Jul 2005**: the `->(a){...}` lambda-expression plan, also allowing `ary.each -> x {puts x}`. — [20050721.html](http://www.rubyist.net/~matz/20050721.html)
- **Matz's diary, 22 Jul 2005, "新lambdaの文法" (new lambda grammar).** Of `->` he wrote "もともとの案。実はPerl6から来ている" ("the original proposal; it actually comes from Perl6"). He explained Perl 6's `for list -> x { ... }` and `-> x { ... }` closures, and noted that Perl 6 is culturally close to Ruby. He added the worry: when will Perl 6 be practical, will it be used at all? **[adopted-from]**, with **[cautionary-tale]** reservations. — [20050722.html](http://www.rubyist.net/~matz/20050722.html)
- **Commit, 27 Jul 2005**: ChangeLog "Perl6 style -> lambda expression" (already known). — [ruby/ruby 759a63b6](https://github.com/ruby/ruby/commit/759a63b6a3355b85e1e52b78810d66fd9ee7b838)
- **ruby-dev:26623, 28 Jul 2005, SASADA Koichi** (YARV author). He summarized the diary discussion, linked an RWiki page "Ruby2.0LambdaWithLocalVariable", and said he personally resisted moving block parameters outside the braces. — [blade ruby-dev/26623](https://blade.ruby-lang.org/ruby-dev/26623)
- **ruby-dev:26625 and 26630, 28 Jul 2005, Matz.** The same `-> params { ... }` notation serves both lambdas and blocks; parentheses are optional, at the cost of optional arguments. `ary.map -> (x) {x*x}` and `ary.map -> x {x*x}` are both allowed, but `{ -> (x) ... }` is not. — [ruby-dev/26625](https://blade.ruby-lang.org/ruby-dev/26625); [ruby-dev/26630](https://blade.ruby-lang.org/ruby-dev/26630)
- **ruby-dev:26650, 29 Jul 2005, Matz**, answering Shin-ichiro HARA's suggestion to put parameters inside the braces. He said yacc would not object, and "今の文法は Perl6 に合わせているのですが" ("the current grammar is aligned with Perl6"). He would reconsider given a positive reason. His comparison: Perl6/Ruby2 `-> (x,y) {...}`, Groovy `{x, y -> ...}`, Haskell `\x y -> ...`. **[adopted-from]**. — [ruby-dev/26650](https://blade.ruby-lang.org/ruby-dev/26650)
- **ruby-dev:26653–26654, 30 Jul 2005.**
  - HARA found the "Perl6式" (Perl 6 style) feel like an assignment outside the block. — [ruby-dev/26653](https://blade.ruby-lang.org/ruby-dev/26653)
  - Matz rejected Groovy-style `{->…}` because parameterless blocks would then need `->`, and defended the outside position with JavaScript's `function(x, y) {...}`. — [ruby-dev/26654](https://blade.ruby-lang.org/ruby-dev/26654)
- **ruby-core.** Articles 5470–5560 (16 Jul – 10 Aug 2005) were scanned page by page: there was no English discussion of `->` or Perl 6 in that window. Five pages, 5504–5508, returned 403.

### Inferences
- The `->` borrowing is documented three times in July 2005: the diary (twice), the commit message, and ruby-dev:26650.
- What Ruby took was the *pointy-block notation* (parameters before the braces). It did not take the semantics, as the doc already says, nor the `for … -> x` loop form, which Matz said in 2008 he had tried and abandoned as too radical.
- The 2005 sources show Matz hedging on Perl 6's viability even while borrowing, which fits his 2008 ruby-core remark about mind share.

### Gaps
- ruby-dev was scanned only for 26540–26720 (15 Jul – 2 Aug 2005). Later ruby-dev threads during the 1.9 "->" debates (2006–2008), ruby-talk, and the RWiki page were not read.
- The diary is a personal blog, not a mailing list, but it is Matz's own primary record.
