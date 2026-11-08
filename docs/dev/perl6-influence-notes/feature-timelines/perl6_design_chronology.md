# Perl 6 / Raku design chronology: proposed → specified → implemented, and what Perl 6 itself credited

Conventions used throughout:
- **P** = proposed (2000 RFC; date = the RFC's own `Date:` header, i.e. first submission; status = its final status: Frozen / Developing / Withdrawn / Retracted).
- **S** = specified (Apocalypse "A*" by Larry Wall, Exegesis "E*" by Damian Conway, Synopsis "S*"; Apocalypse/Exegesis date = perl.com byline date; Synopsis date = its own `Created:` header, or the commit date in the Raku/old-design-docs git history, which only begins 2006-04-06 for a read-only mirror and 2008-11-26 for the canonical files).
- **I** = implemented (Pugs ChangeLog release dates 2005–2006; Rakudo docs/ChangeLog, which begins with the 2009-02 release #14; Rakudo release announcements).
- "≤ date" means "already present in a snapshot of that date; the true first appearance is earlier and not recoverable from the sources I could reach."
- Methodological notes: raku.org/archive (RFCs, Apocalypses mirror) now returns 404, so RFCs were read via the Wayback Machine; design.raku.org now redirects to the GitHub repo Raku/old-design-docs. Copies of old blog posts in the raku.github.io "CCR Remaster" collection have had "Perl 6" mechanically rewritten to "Raku" (e.g. a 2008 post refers to the "Raku on Parrot compiler"), so quote them with care.

---

## Q1. What are the dated phases and documents of the Perl 6 design (2000–2026), and the key release milestones?

### Takeaway
The RFC phase ran 1 Aug – ~1 Oct 2000 (361 RFCs); Larry Wall's Apocalypses appeared Apr 2001 – Apr 2004 (A1–A6, A12 only), Damian Conway's Exegeses May 2001 – Feb 2004, and the Synopses were created 2002–2004 (S05 first, Jun 2002) and maintained until ~2015. Pugs (Haskell) began 1 Feb 2005 and was the first implementation of most signature features in 2005–2006; Rakudo was named 16 Jan 2008, released independently from Feb 2009, shipped Rakudo Star 29 Jul 2010, 6.c on 25 Dec 2015, 6.d in Nov 2018; as of the latest Rakudo release (2026.09, 28 Sep 2026) 6.e is still `v6.e.PREVIEW`, but RakuAST is now the default frontend.

### Cited Findings
**Announcement and RFCs**
- Design process announced by Larry Wall in "State of the Onion 2000", 19 July 2000 (Wikipedia, citing perl.com) — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))
- The archived RFC index lists 361 RFCs, numbered 1–361 — [RFC index (Wayback, 2019 snapshot)](https://web.archive.org/web/20191117000414/https://raku.org/archive/rfc/)
- RFC 1 ("Implementation of Threads in Perl", Bryan C. Warnock) is dated 1 Aug 2000; late RFCs are dated 28–29 Sep 2000 (e.g. RFC 348, Bart Lateur, 28 Sep 2000; RFC 352, Timothy Covell, 29 Sep 2000), and many show "Last Modified: 1 Oct 2000" — [RFC 1](https://web.archive.org/web/2019/https://raku.org/archive/rfc/1.html); [RFC 348](https://web.archive.org/web/2019/https://raku.org/archive/rfc/348.html); [RFC 352](https://web.archive.org/web/2019/https://raku.org/archive/rfc/352.html)

**Apocalypses / Exegeses (perl.com byline dates; all now carry "Editor's Note: this Apocalypse is out of date")**
- A1 "The Ugly, the Bad, and the Good" — 2 Apr 2001 — [A1](https://www.perl.com/pub/2001/04/02/wall.html/)
- A2 "Bits and Pieces" — 3 May 2001 — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- E2 — byline 15 May 2001 (URL path says 2001/05/08) — [E2](https://www.perl.com/pub/2001/05/08/exegesis2.html/)
- A3 "Operators" — 2 Oct 2001 — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A4 "Syntax" (control structures) — 15 Jan 2002 — [A4](https://www.perl.com/pub/2002/01/15/apo4.html/)
- A5 "Pattern Matching" — 4 Jun 2002 — [A5](https://www.perl.com/pub/2002/06/04/apo5.html/)
- E5 — 22 Aug 2002 — [E5](https://www.perl.com/pub/2002/08/22/exegesis5.html/)
- A6 "Subroutines" — 7 Mar 2003 — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- E6 — 29 Jul 2003 — [E6](https://www.perl.com/pub/2003/07/29/exegesis6.html/)
- E7 ("Formats") — 27 Feb 2004 — [E7](https://www.perl.com/pub/2004/02/27/exegesis7.html/)
- A12 "Objects" — 16 Apr 2004 — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)

**Synopses (`Created:` headers in the final files)**
- S01 10 Aug 2004; S02 10 Aug 2004; S03 8 Mar 2004; S04 19 Aug 2004; S05 24 Jun 2002; S06 21 Mar 2003; S07 7 Sep 2015; S08 20 Sep 2009; S09 13 Sep 2004; S10/S11/S12 27 Oct 2004; S13 2 Nov 2004; S14 24 Feb 2009 ("extracted from S12-objects.pod"); S15 2 Dec 2013; S17 3 Nov 2013; S19 12 Dec 2008; S29 12 Mar 2005 — [S02](https://github.com/Raku/old-design-docs/blob/master/S02-bits.pod), [S03](https://github.com/Raku/old-design-docs/blob/master/S03-operators.pod), [S04](https://github.com/Raku/old-design-docs/blob/master/S04-control.pod), [S05](https://github.com/Raku/old-design-docs/blob/master/S05-regex.pod), [S06](https://github.com/Raku/old-design-docs/blob/master/S06-routines.pod), [S07](https://github.com/Raku/old-design-docs/blob/master/S07-lists.pod), [S09](https://github.com/Raku/old-design-docs/blob/master/S09-data.pod), [S12](https://github.com/Raku/old-design-docs/blob/master/S12-objects.pod), [S14](https://github.com/Raku/old-design-docs/blob/master/S14-roles-and-parametric-types.pod), [S15](https://github.com/Raku/old-design-docs/blob/master/S15-unicode.pod), [S17](https://github.com/Raku/old-design-docs/blob/master/S17-concurrency.pod)
- Wikipedia dates "Synopses" to 10 August 2004 (= S01's Created date), which understates S03/S05/S06 — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))
- Git history of the spec starts with a 2006-04-06 read-only mirror of svn.perl.org Synopses (commit 0879bdc, audreyt) and the canonical move on 2008-11-26 (commit 68d062f, pmichaud) — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108); [68d062f](https://github.com/Raku/old-design-docs/commit/68d062fcaaa4e6e6a032e30775e3d7782e95eb28)

**Implementations**
- Pugs started 1 Feb 2005 by Audrey Tang, written in Haskell; paused 21 Apr 2010 — [Wikipedia: Pugs](https://en.wikipedia.org/wiki/Pugs_(compiler)); Tang described Pugs as her second Haskell program, begun as an exercise for *Types and Programming Languages* — [Interview with Audrey Tang (A. Shitov, 2015)](https://andrewshitov.com/2015/05/05/interview-with-audrey-tang/)
- Pugs 6.0.0 "Initial CPAN release", 7 Feb 2005; ChangeLog runs to 6.2.13 (17 Oct 2006) plus an unreleased 6.28.0 (June 2007) — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- "Rakudo Perl" name announced by Patrick Michaud 16 Jan 2008 for the Perl 6-on-Parrot compiler, name suggested by Damian Conway ("Rakuda-do", way of the camel) — [Michaud, "The compiler formerly known as…" (CCR copy)](https://raku.github.io/CCR/Remaster/Patrick%20Michaud/The-compiler-formerly-known-as-raku.html)
- Rakudo release #14 "Vienna" (Feb 2009) was "the first release independent from Parrot releases"; Vienna.pm had sponsored Jonathan Worthington's Rakudo work since April 2008 — [Rakudo announce 2009-02](https://github.com/rakudo/rakudo/blob/main/docs/announce/2009-02)
- Rakudo Star ("a useful, usable, 'early adopter' distribution"), 29 Jul 2010, bundling Rakudo compiler #31 and Parrot 2.6.0 — [TPF news](https://news.perlfoundation.org/post/rakudo_star_-_a_useful_usable)
- Rakudo 2015.12, release #94 "коледа": "This is the Christmas release … targets the v6.c 'Christmas' specification"; the 2015.12 tag is timestamped 2015-12-25 — [announce 2015.12](https://github.com/rakudo/rakudo/blob/main/docs/announce/2015.12.md); [Rakudo tags](https://github.com/rakudo/rakudo/tags?after=2016.01)
- 6.d "Diwali" spec, 5 Nov 2018 — [Wikipedia, citing Zoffix's announcement](https://en.wikipedia.org/wiki/Raku_(programming_language)); [announcement](http://blogs.perl.org/users/zoffix_znet/2018/11/announce-raku-perl-6-diwali-6d-language-specification-release.html). Rakudo 2018.11 (#128, tag 2018-11-29): "Default language version is now v6.d" — [announce 2018.11](https://github.com/rakudo/rakudo/blob/main/docs/announce/2018.11.md); [tags](https://github.com/rakudo/rakudo/tags?after=2018.12)
- Renamed to Raku, October 2019 — [LWN](https://lwn.net/Articles/802329/)
- Rakudo 2026.09 (#197, tag 2026-09-28) "makes the RakuAST implementation of the Raku grammar and actions the default … a key step in the roadmap to releasing Raku language level 6.e, which will occur in a future release. To test your code in advance, add `use v6.e.PREVIEW;`" — [announce 2026.09](https://github.com/rakudo/rakudo/blob/main/docs/announce/2026.09.md); 2026.09 is the newest tag as of this research — [Rakudo tags](https://github.com/rakudo/rakudo/tags)

### Inferences
- The "specified" layer is stratified: Apocalypse decisions (2001–2004) were frequently reversed by later Synopsis edits (e.g. `~~` was logical XOR in A3, smartmatch was spelled `=~` in A4 — see Q11). For a feature, the earliest Apocalypse mention is the right "first specified" date, but the final semantics usually date from a Synopsis edit.
- Only A1–A6 and A12 were ever written; features whose natural home would have been A7–A11/A13+ (formats, packages, modules, concurrency) got their first spec in Exegeses or Synopses directly.

### Gaps
- Exegeses E3 (Oct 2001) and E4 (Apr 2002) could not be retrieved (perl.com URLs return 404), so features first shown there (likely early junction/given usage in E4) may predate the dates below.
- Pre-2009 Rakudo (2007–2008, inside the Parrot repo as languages/perl6) has no ChangeLog in the rakudo repo; early Rakudo/PGE implementation dates for roles/junctions/grammars are therefore not given here.
- Spec evolution before 2006-04-06 is only recoverable from the Apocalypses/Exegeses; between 2006-04 and 2008-11 only "present in snapshot or not" can be stated.

---

## Q2. Master comparison table (one row per feature)

### Takeaway
Most signature features were first proposed in Aug–Sep 2000 RFCs (many by Damian Conway), specified in A2–A6/A12 (2001–2004), and first implemented in Pugs in Feb–Oct 2005; the late-arriving features — type smileys, `Mu`, `WhateverCode`, `X`/`Z` metaops in final form, `sub MAIN`, NFG, Promises/Supplies/react — were specified 2008–2013 and implemented in Rakudo 2009–2015.

### Cited Findings
| Feature | P: earliest RFC | S: first spec | I: first implementation | Credited source (by Perl 6 itself) |
|---|---|---|---|---|
| Optional/gradual typing | RFC 4 "type inference" (S. Fink, 1 Aug 2000) [RFC 4](https://web.archive.org/web/2019/https://raku.org/archive/rfc/4.html); RFC 89 "Controllable Data Typing" (10 Aug 2000) [RFC 89](https://web.archive.org/web/2019/https://raku.org/archive/rfc/89.html); RFC 218 "`my Dog $spot` is just an assertion" (P. Cawley, 13 Sep 2000) [RFC 218](https://web.archive.org/web/2019/https://raku.org/archive/rfc/218.html) | A2, 3 May 2001 (discusses RFC 218) [A2](https://www.perl.com/pub/2001/05/03/wall.html/); A6, 7 Mar 2003 (typed signatures, `returns Int\|Str`) [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/) | Pugs 6.0.3, 12 Feb 2005 (formal params & binding) [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog) | none explicit; A2 wants numeric types translatable to Java/C# [A2](https://www.perl.com/pub/2001/05/03/wall.html/) |
| `subset … where` | — | A12, 16 Apr 2004 "Types and Subtypes" [A12](https://www.perl.com/pub/2004/04/16/a12.html/); `subset` in spec ≤ Apr 2006 [mirror](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108) | not pinned (see Gaps) | **Ada** ("I've always really liked the Ada distinction between types and subtypes") [A12](https://www.perl.com/pub/2004/04/16/a12.html/) |
| `:D`/`:U` smileys | — | S12 commit 2009-06-26 ("Int:D Allow only defined (concrete) Int values") [04840a3](https://github.com/Raku/old-design-docs/commit/04840a3a6967d9ee641a9a76a1f9ae929b0d0a19) | Rakudo 2011.09 ":U and :D type modifiers" [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog); 6.d: `:D`/`:U` variables default to type object [6.d ChangeLog](https://github.com/Raku/6.d-prep/blob/master/ChangeLog.md) | none found |
| Coercion types `T(S)` | — | "coercive" types ≤ Nov 2008 [68d062f](https://github.com/Raku/old-design-docs/commit/68d062fcaaa4e6e6a032e30775e3d7782e95eb28) | Rakudo 2015.02: "Str(Any) as a term now parses as a coercion type"; "Foo() works as … shortcut for Foo(Any)" [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog) | none found |
| `Mu`/`Any` root | — | 2009-11-24 "much less tentatively go with the Mu suggestion from TheDamian++" [e7a6479](https://github.com/Raku/old-design-docs/commit/e7a6479d0f35c1b043ee7f978eab399cf0dc8dcd) | Rakudo 2010.02 "root of the object hierarchy is now Mu"; 2010.03 untyped vars default to Any [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog) | name suggested by Damian Conway (commit msg) |
| Roles / `does` / `but` | RFC 265 "Interface polymorphism considered lovely" (Cawley, 20 Sep 2000; Java interfaces) [RFC 265](https://web.archive.org/web/2019/https://raku.org/archive/rfc/265.html) | A12, 16 Apr 2004 [A12](https://www.perl.com/pub/2004/04/16/a12.html/); S12 27 Oct 2004; S14 24 Feb 2009 | Pugs 6.2.4, 24 May 2005 (`class Foo does Bar`); 6.2.10, 10 Oct 2005 ("monkey-but"); 6.2.13, 17 Oct 2006 role mixins [Pugs](https://github.com/audreyt/pugs/blob/master/ChangeLog); Rakudo 2009-04 parametric roles [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog) | **Schärli, Ducasse, Nierstrasz, Black, "Traits: Composable Units of Behavior", ECOOP 2003** (A12's sole reference) [A12](https://www.perl.com/pub/2004/04/16/a12.html/) |
| Multiple dispatch | RFC 256 "Native support for multimethods" (Conway, 18 Sep 2000) [RFC 256](https://web.archive.org/web/2019/https://raku.org/archive/rfc/256.html) | A6, 7 Mar 2003 (`multi`) [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/) | Pugs 6.0.2, 9 Feb 2005 "Multimethod dispatch" [Pugs](https://github.com/audreyt/pugs/blob/master/ChangeLog) | Class::Multimethods (Conway's Perl 5 module) and CLOS (RFC 256); S12: semicolon-per-param gives "dispatch semantics much like Common Lisp" [S12](https://github.com/Raku/old-design-docs/blob/master/S12-objects.pod) |
| Signatures (named, slurpy) | RFC 57 (A. Wardley, 7 Aug 2000) [RFC 57](https://web.archive.org/web/2019/https://raku.org/archive/rfc/57.html); RFC 128 (Conway, 17 Aug 2000) [RFC 128](https://web.archive.org/web/2019/https://raku.org/archive/rfc/128.html); RFC 160 (M. Maraist, 25 Aug 2000) [RFC 160](https://web.archive.org/web/2019/https://raku.org/archive/rfc/160.html) | A6, 7 Mar 2003; S06 21 Mar 2003 | Pugs 6.0.3, 12 Feb 2005 (optional `?`, named `+`/`++`, slurpy `*`) [Pugs](https://github.com/audreyt/pugs/blob/master/ChangeLog) | none explicit |
| `sub MAIN` CLI | (RFC 352 "default Main class", 29 Sep 2000 — tangential) [RFC 352](https://web.archive.org/web/2019/https://raku.org/archive/rfc/352.html) | absent Apr 2006, present Nov 2008 in S06 [68d062f](https://github.com/Raku/old-design-docs/commit/68d062fcaaa4e6e6a032e30775e3d7782e95eb28) | Rakudo 2010.06 "MAIN and USAGE subs" [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog) | none found |
| Grammars / new regex | RFC 112 (16 Aug 2000), RFC 145 (24 Aug 2000), RFC 150 (23 Aug 2000), RFC 314 (25 Sep 2000) | A5, 4 Jun 2002 [A5](https://www.perl.com/pub/2002/06/04/apo5.html/); S05 24 Jun 2002 | Pugs 6.2.3, 12 May 2005 "Perl 6 Rules support via Parrot/PGE" [Pugs](https://github.com/audreyt/pugs/blob/master/ChangeLog) | Prolog (`::`/`:::` "variants on Prolog's 'cut'"), Prolog/SNOBOL as languages with built-in matching [A5](https://www.perl.com/pub/2002/06/04/apo5.html/) |
| LTM / protoregexes | — | "longest-token" ≤ Apr 2006; "proto token" ≤ Nov 2008 | Rakudo 2011.11 "basic protoregex support with NFA-driven LTM" [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog) | none found |
| Slangs / braids | — | S02 2009-05-08 "documenting how interwoven languages work" [5cfa62e](https://github.com/Raku/old-design-docs/commit/5cfa62e1c79746f9b0151da987bc862695607e27) | Rakudo 2014.06 "allowing slangs transparently (such as 'v5')" [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog) | none found |
| Junctions | RFC 225 "Data: Superpositions" (Conway, 14 Sep 2000) [RFC 225](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/225.html) | A4, 15 Jan 2002 (`any(...)`) [A4](https://www.perl.com/pub/2002/01/15/apo4.html/); E6 29 Jul 2003; S09 13 Sep 2004 "Junctions" | **Pugs 6.0.0, 7 Feb 2005** ("Junctions and chained comparisons") [Pugs](https://github.com/audreyt/pugs/blob/master/ChangeLog) | **Quantum::Superpositions** (RFC 225: "See the Quantum::Superpositions module") |
| Hyper ops | RFC 82 (J. Howard, 10 Aug 2000) [RFC 82](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/82.html) | A3, 2 Oct 2001 (`^op`) [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/); `»op«` by E7 (27 Feb 2004) and A12 (16 Apr 2004) | Pugs 6.2.4, 24 May 2005 ("All operators now receive hyperised forms") [Pugs](https://github.com/audreyt/pugs/blob/master/ChangeLog) | **APL** ("APL, here we come… :-)") [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/); PDL, Numerical Python (RFC 82) |
| Reduction `[op]` | RFC 76 "Builtin: reduce" (Conway, 10 Aug 2000) [RFC 76](https://web.archive.org/web/2019/https://raku.org/archive/rfc/76.html) | `[+]` in spec ≤ Apr 2006 | Pugs 6.2.3 (12 May 2005, experimental `[+]`), 6.2.4 (24 May 2005, all infix ops) [Pugs](https://github.com/audreyt/pugs/blob/master/ChangeLog) | Graham Barr's List::Util `reduce` (RFC 76) |
| Cross `X` / zip `Z` | RFC 90 "merge() and unmerge()" (Howard, 10 Aug 2000) [RFC 90](https://web.archive.org/web/2019/https://raku.org/archive/rfc/90.html) | `X` metaop ≤ Nov 2008; `Z` metaop added 2010-03-15 [63679e1](https://github.com/Raku/old-design-docs/commit/63679e18654b6dfbc59599ca936e720749dde058) | Pugs 6.0.11 infix `Y` (zip), 14 Mar 2005; Rakudo 2010.03 lazy `Z` | none found |
| Whatever `*` / WhateverCode | RFC 23 `^_` placeholder (Conway, 4 Aug 2000) [RFC 23](https://web.archive.org/web/2019/https://raku.org/archive/rfc/23.html) | `Whatever` first in spec 2006-09-21 [b22582e](https://github.com/Raku/old-design-docs/commit/b22582e6bef46cbad0604401f051e74594053c4c); WhateverCode 2009-08-25 [357be11](https://github.com/Raku/old-design-docs/commit/357be1193fb2de11f8a178dcfb7a5cb2e3cdc8d3) | Rakudo 2009-04 "calling a method on a Whatever star generates a closure" [ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog) | Haskell (RFC 23 links Haskell tutorial on currying) |
| Placeholders `$^a` | RFC 23 (4 Aug 2000) | A3, 2 Oct 2001 (`{ $^a * $^b }`); A4 15 Jan 2002 ("curried function") | Pugs 6.0.3, 12 Feb 2005 | Haskell (via RFC 23) |
| Pointy blocks `->` | RFC 173 multiple loop vars (P. Scott, 29 Aug 2000) [RFC 173](https://web.archive.org/web/2019/https://raku.org/archive/rfc/173.html) | A4, 15 Jan 2002 (`for @foo -> $a, $b`) | Pugs 6.0.7, 18 Feb 2005 ("pointy" code literals) | none |
| Sequence `...` w/ deduction | RFC 24 (Conway, 4 Aug 2000, Withdrawn); RFC 81 (Howard, 10 Aug 2000) | arithmetic/geometric deduction ≤ Nov 2008; "series"→"sequence" 2010-09-23 [1779592](https://github.com/Raku/old-design-docs/commit/17795925bc48affda10275bd2768e1f2d9ef6345) | Rakudo 2009-10 (variants), 2010.09 (revamp, `...^`) | "to accord with math culture" (rename commit) |
| Lazy lists | RFC 24, RFC 81 (Haskell, Python cited), RFC 123 | A3, 2 Oct 2001 (`1 .. Inf`) | Pugs 6.2.0, 13 Apr 2005 "Infinite lists, constant time" | Haskell & Python (RFC 81) |
| gather/take | RFC 31 co-routines/`yield` (Conway, 4 Aug 2000) [RFC 31](https://web.archive.org/web/2019/https://raku.org/archive/rfc/31.html) | A12, 16 Apr 2004 ("conjectural gather/take construct") | Pugs 6.2.4, 24 May 2005 | **none found (no Icon credit located)** |
| Caret ranges `..^`, `^N` | RFC 117 (Withdrawn) | `..^` used in A12 (16 Apr 2004); in spec ≤ Apr 2006 | not pinned | none |
| Chained comparisons | RFC 25 (Conway, 4 Aug 2000) [RFC 25](https://web.archive.org/web/2019/https://raku.org/archive/rfc/25.html) | A3, 2 Oct 2001 | Pugs 6.0.0, 7 Feb 2005 | mathematical notation (no language credited) |
| `//` defined-or | none | A3, 2 Oct 2001 ("Non-RFC considerations") | Pugs 6.0.11, 14 Mar 2005 (`//=`) | — ; Perl 5.10.0 later added `//` |
| `say` | none (RFC 39 "print operator" Retracted) | by A12, 16 Apr 2004 | Pugs 6.0.7, 18 Feb 2005 | — ; Perl 5.10.0 later added `say` |
| `state` | none | E7, 27 Feb 2004; A12 16 Apr 2004 | Pugs 6.2.3, 12 May 2005 | C "static" (A12: "Perl's version of lexically scoped 'static' variables") |
| given/when + smartmatch | RFC 22 switch (Conway, 4 Aug 2000) [RFC 22](https://web.archive.org/web/2019/https://raku.org/archive/rfc/22.html) | A4, 15 Jan 2002 (given/when, `=~` table); `~~` as smartmatch by A6, 7 Mar 2003 | Pugs 6.0.13, 27 Mar 2005 (given/when); 6.0.12 (`~~` for regex) | none |
| Phasers PRE/POST/KEEP/UNDO/CATCH/NEXT | RFC 88 (T. Olekshy, 8 Aug 2000); RFC 194 (J. Hietaniemi, 5 Sep 2000); RFC 271 (Conway, 20 Sep 2000) | A4, 15 Jan 2002 | Pugs 6.0.9 (END, 28 Feb 2005); 6.2.13 (17 Oct 2006: FIRST/NEXT/LAST, START/PRE/POST/KEEP/UNDO/ENTER/LEAVE) | "design-by-contract" (A4); Class::Contract (RFC 271) — Eiffel not named |
| Heredoc indent stripping | RFC 111 (R. Proctor, 16 Aug 2000); RFC 162 (Proctor, 27 Aug 2000) | A2, 3 May 2001 ("remove whitespace equivalent to the terminator"); `q:to` ≤ Apr 2006 | Pugs 6.2.7, 13 Jun 2005 (`qq:to/END/`); Rakudo 2012.11 | none |
| Interpolation of expressions | RFC 222 (14 Sep 2000), RFC 252 (17 Sep 2000) | A2, 3 May 2001 (`"$(expr)"`, `"@(expr)"`) | not pinned | none |
| Pairs / `:foo()` adverbs | RFC 84 (Conway, 10 Aug 2000) [RFC 84](https://web.archive.org/web/2019/https://raku.org/archive/rfc/84.html) | A3 (2 Oct 2001); `:foo()` in A12 (16 Apr 2004) | Pugs 6.0.8 (21 Feb 2005 Pairs); 6.0.14 (4 Apr 2005 adverb pairs) | Unix command-line options and Perl 5 I/O layers (A12) |
| Feeds `==>`/`<==` | none | A6, 7 Mar 2003 ("pipe operators") | Rakudo 2009-08 only emits "not yet implemented" message (see Q9) | none explicit |
| Rat / bigints | RFC 43 "BigInts (and BigRats)" (J. Hietaniemi, 5 Aug 2000) [RFC 43](https://web.archive.org/web/2019/https://raku.org/archive/rfc/43.html); RFC 209 (11 Sep 2000) | A2, 3 May 2001 (tie to type system, not pragma) | Rakudo 2009-09 `Int/Int` → Rat | none |
| NFG / graphemes | RFC 294, 295, 312 (S. Cozens, 25 Sep 2000) | A5 (4 Jun 2002) Unicode "level 2: dot means grapheme"; "NFG" in S02 2009-01-30 [dc6f02a](https://github.com/Raku/old-design-docs/commit/dc6f02a49a82becee70b518927fd5a509fc70632) | Pugs 6.2.0 (Apr 2005) `chars`/`codes`/`graphs` builtins; MoarVM/Rakudo NFG on 24 Apr 2015 | Unicode normalization forms (NFC/NFD) |
| Concurrency (Promise/Channel/Supply/react) | RFC 1 (1 Aug 2000), RFC 178 (30 Aug 2000), RFC 185 (31 Aug 2000) | S17 rewrite 2013-11-03 [0b11765](https://github.com/Raku/old-design-docs/commit/0b11765d37acafd8f074386423c1cae2b3e4e83b) | Rakudo 2013.07 (promises, JVM); 2013.08 (Channel); 2014.05 (Supply); 2015.09 (`supply`/`whenever`/`react`) | Rx / reactive programming (Worthington article) |
| hyper/race | — | S07 rewrite "HyperSeq" 2015-09-12 [de53e4b](https://github.com/Raku/old-design-docs/commit/de53e4be1241a36451e7f90c3902bedcdaab6a73) | `hyper for`/`race for` statement prefixes in 6.d (2018) | none |
| Macros | none found | A6, 7 Mar 2003 | Pugs 6.2.7 (13 Jun 2005, rudimentary); Rakudo 2012.03 (macro + quasi); "Disable macros in 6.e" 2026.06 | none |
| Sigil invariance | RFC 9 "Highlander Variable Types" (A. Wardley, 1 Aug 2000) [RFC 9](https://web.archive.org/web/2019/https://raku.org/archive/rfc/9.html) | A2, 3 May 2001 ("write `@foo[1]` where they used to write `$foo[1]`") | Pugs 2005 | none |

### Inferences
- Damian Conway authored the founding RFC for an outsized share of the signature features (switch/smartmatch 22, placeholders 23, lazy lists 24, chained comparisons 25, co-routines 31, reduce 76, pairs 84, named params 128, NEXT 190, delegation 193, superpositions 225, multimethods 256, pre/post 271), and Larry also credited Damian for the `given/when` naming discussion, `Mu`, and the name "Rakudo".
- Pugs is the first implementation for nearly every 2001–2004-specified feature; its order of work (junctions and chained comparisons on day 1, multis/signatures within a week, rules via PGE in May 2005, reduce/hyper/gather in May 2005, roles in mid-2005) is a usable proxy for "first working implementation".

### Gaps
- See per-feature Gaps sections below; rows marked "not pinned" lacked a reliable dated source.

---

## Q3. Types: gradual typing, `:D`/`:U`, `subset … where`, coercion types, `Mu`/`Any`

### Takeaway
Optional typing was proposed in Aug–Sep 2000 RFCs and endorsed in A2/A6; `subset` came from A12's Ada-inspired "subtypes" (Apr 2004); `:D`/`:U`, `Mu`, and coercion types are 2008–2009 spec additions implemented in Rakudo 2010–2015.

### Cited Findings
- RFC 4 "type inference" — Steve Fink, 1 Aug 2000, status Developing — [RFC 4](https://web.archive.org/web/2019/https://raku.org/archive/rfc/4.html)
- RFC 89 "Controllable Data Typing" — Syloke Soong, 10 Aug 2000, Developing — [RFC 89](https://web.archive.org/web/2019/https://raku.org/archive/rfc/89.html)
- RFC 218 "`my Dog $spot` is just an assertion" — Piers Cawley, 13 Sep 2000, Frozen — [RFC 218](https://web.archive.org/web/2019/https://raku.org/archive/rfc/218.html); discussed in A2 (3 May 2001) — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- A6 (7 Mar 2003): "A <type> is actually allowed to be a junction of types: `sub foo returns Int|Str {...}`" — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- A12 (16 Apr 2004), section "Types and Subtypes": "I've always really liked the Ada distinction between types and subtypes. A type is something that adds capabilities, while a subtype is something that takes away…" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- The word `subset` already occurs 15 times in the April 2006 Synopses mirror — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108)
- `Int:D  Allow only defined (concrete) Int values` / `Int:D  Int where *.defined` were added to S12 in a commit dated 2009-06-26 — [04840a3](https://github.com/Raku/old-design-docs/commit/04840a3a6967d9ee641a9a76a1f9ae929b0d0a19)
- Rakudo 2011.09: "+ :U and :D type modifiers" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- 6.d: "variables with `:D`/`:U` type constraints default to type object of…" — [6.d ChangeLog](https://github.com/Raku/6.d-prep/blob/master/ChangeLog.md)
- "coercive" type language already in the Nov 2008 spec import — [68d062f](https://github.com/Raku/old-design-docs/commit/68d062fcaaa4e6e6a032e30775e3d7782e95eb28); Rakudo 2015.02: "Str(Any) as a term now parses as a coercion type", "Foo() works as a coercion type shortcut for Foo(Any)"; 2015.04: "an Int() coercion type in a multi now creates two candidates: Any and Int" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- `Mu`: Larry Wall commit 2009-11-24 "much less tentatively go with the Mu suggestion from TheDamian++" — [e7a6479](https://github.com/Raku/old-design-docs/commit/e7a6479d0f35c1b043ee7f978eab399cf0dc8dcd); Rakudo 2010.02 "The name of the root of the object hierarchy is now Mu" and "The term 'undef' is gone, replaced by Nil, Mu or *.notdef"; 2010.03 "Switch untyped variables to default to Any instead of Mu" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- Wikipedia states Raku "offers a gradual typing system" but this sentence is uncited; it cites S02 only for adding static types to Perl's dynamic system — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))

### Inferences
- "Gradual typing" is a later, external label; the Perl 6 design documents framed it as optional typing/type assertions (RFC 218 → A2/A6).
- The 2009-06-26 commit is a large squash from the svn era (its message is about header formatting), so `Int:D` may have entered the svn spec somewhat earlier; treat 2009-06-26 as "≤".

### Gaps
- First Pugs/Rakudo implementation date of `subset … where` not pinned (Pugs ChangeLog does not mention `subset`; Rakudo's ChangeLog begins after it was presumably already supported).
- No source found for when the term "type smiley" was coined.
- The newer coercion protocol (`COERCE` method) date not determined.

---

## Q4. Roles (composition, conflicts, punning, parametric roles) and runtime mixins (`does`/`but`)

### Takeaway
Interfaces were proposed in RFC 265 (Sep 2000, citing Java); roles were specified in A12 (16 Apr 2004), which explicitly credits the 2003 ECOOP Traits paper while deliberately diverging from it (roles may hold state; no rename/exclude conflict resolution; runtime mixins). Pugs implemented `does` mid-2005 and `but` mixins Oct 2005; Rakudo had parametric roles by Apr 2009.

### Cited Findings
- RFC 265 "Interface polymorphism considered lovely" — Piers Cawley, 20 Sep 2000, Frozen; abstract: "a mechanism for declaring class interfaces … runtime checking to ensure that a class actually *does* implement said interface"; body: "Java is one language that springs to mind that uses interface … from Java let's steal something good" — [RFC 265](https://web.archive.org/web/2019/https://raku.org/archive/rfc/265.html)
- A12 (16 Apr 2004): "we buy the argument of the Traits paper that classes should not be used both to manage objects and to manage code reuse"; "Unlike the Traits paper, we will allow state as part of our implementation"; "As in the Traits paper, roles are composed at class construction time"; "The Traits paper recommends providing ways of renaming or excluding one or the other of the conflicting methods. We don't recommend that" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- A12's only reference: "Nathanael Schärli, Stéphane Ducasse, Oscar Nierstrasz and Andrew Black. Traits: Composable Units of Behavior. European Conference on Object-Oriented Programming (ECOOP), July 2003. Springer LNCS 2743, Ed. Luca Cardelli." — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- A12 explains the naming clash: "Here we're talking about Perl's traits (as in compile-time properties), not Traits (as in the Traits paper)" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- A12 "Use of Roles at Runtime (mixins)": "they can also function as mixin classes at runtime. The 'does' binary operator performs the feat of deriving a new class and binding the object…"; `$a but= tainted` ≈ `$a does taint::tainted` — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- S12 created 27 Oct 2004; S14 "Roles and Parametric Types" created 24 Feb 2009, "extracted from S12-objects.pod" — [S14](https://github.com/Raku/old-design-docs/blob/master/S14-roles-and-parametric-types.pod)
- Pugs: 6.2.4 (24 May 2005) "Inheritance: `class Foo is Bar` and `class Foo does Bar`"; 6.2.6 (2 Jun 2005) "Stub implementation for `.does` as an alias of `.isa` (no Roles yet)"; 6.2.8 (13 Jul 2005) "Support for `does RoleName` and `is ClassName` within class declarations"; 6.2.10 (10 Oct 2005) "Support for 'monkey-but' (`$foo but {...}`)"; 6.2.11 (1 Feb 2006) "Role Meta-Model partially complete in PIL^N"; 6.2.13 (17 Oct 2006) "Role mixins: `role R {...} class C does R {...}`" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Rakudo 2009-04 (#16 "Bratislava"): "Associative, Positional and Callable are now parametric roles"; "parametric role subtyping (R[T1] ~~ R[T2] where T1 ~~ T2)"; 2011.09: "parametric roles reify types properly" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- Wikipedia says roles take the function of Java interfaces, Ruby mixins and PHP/Squeak traits, citing the SCG Traits page (2003), Worthington's 2009 advent post, chromatic (2009), and docs.raku.org for "automatic role punning" — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))

### Inferences
- The Smalltalk link in Raku's lineage runs through the Traits paper (a Squeak/Smalltalk research result) rather than through any Perl 6 document naming Smalltalk.
- Perl 5's `UNIVERSAL::DOES()` (5.10.0, credited to chromatic) is the Perl 5 echo of `does` — [perl5100delta](https://chromium.googlesource.com/chromium/deps/perl.git/+/ac0d98b5cee6c024b0cffeb4f8f45b6fc5ccdb78/perl/lib/pods/perl5100delta.pod)

### Gaps
- Date when role punning (using a role as a class) entered the spec not found.
- No Perl 6 design document found that names Smalltalk as a source, despite Wikipedia's "Influenced by: … Smalltalk".

---

## Q5. Multiple dispatch, signatures, captures/destructuring, `sub MAIN`

### Takeaway
Multimethods were proposed by Damian (RFC 256, 18 Sep 2000) explicitly after his Class::Multimethods module and with reference to CLOS, specified in A6 (Mar 2003), and implemented in Pugs within a week of its first release (Feb 2005). Signatures (named/slurpy/optional) were specified in A6 and implemented in Pugs 6.0.3 (12 Feb 2005). `sub MAIN` entered S06 between Apr 2006 and Nov 2008 and shipped in Rakudo 2010.06.

### Cited Findings
- RFC 256 "Objects : Native support for multimethods" — Damian Conway, 18 Sep 2000, Frozen; "Some languages (e.g. CLOS) take a different approach…"; "See the Class::Multimethods module" — [RFC 256](https://web.archive.org/web/2019/https://raku.org/archive/rfc/256.html)
- RFC 92 "Extensible Meta-Object Protocol" (Tony Olekshy, 11 Aug 2000) quotes "In CLOS terminology, an introspective protocol…" — [RFC 92](https://web.archive.org/web/2019/https://raku.org/archive/rfc/92.html)
- A3 (2 Oct 2001) already says operator semantics may be implemented "in the case of multimethods" elsewhere — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A6 (7 Mar 2003): "A multi is a multimethod, that is, a method that called like a subroutine or operator, but is dispatched based on the types of one or more of its arguments"; "Multimethods can have multiple invocants. A colon terminates the list of invocants" — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- S12: "putting a semicolon after every parameter produces dispatch semantics much like Common Lisp" — [S12](https://github.com/Raku/old-design-docs/blob/master/S12-objects.pod)
- Signature RFCs: RFC 57 "Subroutine prototypes and parameters" (Andy Wardley, 7 Aug 2000) — [RFC 57](https://web.archive.org/web/2019/https://raku.org/archive/rfc/57.html); RFC 128 "Extend subroutine contexts to include name parameters and lazy arguments" (Conway, 17 Aug 2000, Frozen) — [RFC 128](https://web.archive.org/web/2019/https://raku.org/archive/rfc/128.html); RFC 160 "Function-call named parameters" (Michael Maraist, 25 Aug 2000) — [RFC 160](https://web.archive.org/web/2019/https://raku.org/archive/rfc/160.html)
- Destructuring precursor: RFC 261 "Pattern matching on perl values" (Steve Fink, 19 Sep 2000) — [RFC 261](https://web.archive.org/web/2019/https://raku.org/archive/rfc/261.html); A5 (4 Jun 2002): "This reminds me a bit of unification in Prolog… I think this is probably too…" — [A5](https://www.perl.com/pub/2002/06/04/apo5.html/)
- A6 (7 Mar 2003): "'Slurpy' parameters are denoted by a unary * before the variable name"; "Parameters marked with a + zone marker are named-only parameters"; `*%named` slurpy hash; "Currying is done with the .assuming method" — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- S06 created 21 Mar 2003; S08 "Capture" created 20 Sep 2009 — [S06](https://github.com/Raku/old-design-docs/blob/master/S06-routines.pod); [S08](https://github.com/Raku/old-design-docs/blob/master/S08-capture.pod)
- Pugs 6.0.2 (9 Feb 2005) "Multimethod dispatch"; 6.0.3 (12 Feb 2005) "Formal parameters declaration and binding", "Invocant-based multimethod dispatch", "Optional(`?`), named(`+` and `++`), slurpy(`*`) parameters with defaulting"; 6.0.13 (27 Mar 2005) "Support for currying: `&foo.assuming(param => 'bar')`" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- `sub MAIN` is absent from the April 2006 Synopses mirror and present in the Nov 2008 S06 (`sub MAIN ($directory, :$verbose, *%other, *@filenames)`) — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108); [68d062f](https://github.com/Raku/old-design-docs/commit/68d062fcaaa4e6e6a032e30775e3d7782e95eb28)
- Rakudo 2010.06: "+ MAIN and USAGE subs" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- 6.d: `$*ARGFILES` inside `sub MAIN` fed by `$*IN`; new definable `&RUN-MAIN`, `&ARGS-TO-CAPTURE`, `&GENERATE-USAGE`; `is hidden-from-USAGE` — [6.d ChangeLog](https://github.com/Raku/6.d-prep/blob/master/ChangeLog.md)
- RFC 352 "Merge Perl and C#, but have default Main class for scripting" (Timothy Covell, 29 Sep 2000, Developing) is the only RFC mentioning "Main"; it is about OO-by-default, not CLI generation — [RFC 352](https://web.archive.org/web/2019/https://raku.org/archive/rfc/352.html)

### Inferences
- `proto` appears in the Nov 2008 spec (43 occurrences vs 14 in the 2006 mirror, where some are unrelated uses), so explicit `proto` declarations were specified by 2008; Rakudo's 2009-02 notes already mention it.
- The A6 `+name` (named-only) / `*@slurpy` markers were later replaced by `:$name` and `*@`, but Pugs implemented the A6 forms first.

### Gaps
- Exact date `sub MAIN` entered S06 (between 2006-04-06 and 2008-11-26) — the svn history is not in the git repo.
- No Perl 6 source credits any other language (e.g. Python's argparse/docopt) for `MAIN`.

---

## Q6. Grammars, new regex syntax, LTM, protoregexes, slangs/braids

### Takeaway
Named captures and brace matching were RFC'd in Aug 2000; A5 (4 Jun 2002) redesigned regexes as first-class grammar rules (whitespace-insensitive, `<rule>` calls, `:=` binding), crediting Prolog's cut for the backtracking controls. LTM is in the spec by Apr 2006 and protoregexes by Nov 2008; Pugs ran rules via Parrot's PGE from May 2005; Rakudo got NFA-driven LTM for protoregexes in 2011.11; slangs were spec'd 2009 and usable in Rakudo by 2014.

### Cited Findings
- RFC 112 "Assignment within a regex" (Richard Proctor, 16 Aug 2000, Frozen): "Provide a simple way of naming and picking out information from a regex without having to count the brackets" — [RFC 112](https://web.archive.org/web/2019/https://raku.org/archive/rfc/112.html)
- RFC 150 "Extend regex syntax to provide for return of a hash of matched subpatterns" (Kevin Walker, 23 Aug 2000, Frozen) — [RFC 150](https://web.archive.org/web/2019/https://raku.org/archive/rfc/150.html)
- RFC 145 "Brace-matching for Perl Regular Expressions" (Eric J. Roode, 24 Aug 2000) — [RFC 145](https://web.archive.org/web/2019/https://raku.org/archive/rfc/145.html)
- RFC 104 "Backtracking" (iVAN Georgiev, 14 Aug 2000) cites "The Prolog Language" — [RFC 104](https://web.archive.org/web/2019/https://raku.org/archive/rfc/104.html); RFC 348 "Regex assertions in plain Perl code" (Bart Lateur, 28 Sep 2000) — [RFC 348](https://web.archive.org/web/2019/https://raku.org/archive/rfc/348.html); RFC 314 "A parser is a many-layered thing" (Simon Cozens, 25 Sep 2000) — [RFC 314](https://web.archive.org/web/2019/https://raku.org/archive/rfc/314.html)
- A5 (4 Jun 2002): "When we have an organized system of parsing rules, we call it a grammar"; "Without support for grammar classes, there's no decent way to think of deriving one grammar from another"; "Whitespace around tokens should be the norm, not the exception"; RFC 112/150 are "covered by the `$foo:=(...)` notation"; backtracking controls: "You can think of all of these as variants on Prolog's 'cut' operator"; contrasts languages "like Prolog or Snobol where the pattern matching is built into the very control structure" — [A5](https://www.perl.com/pub/2002/06/04/apo5.html/)
- E5 (22 Aug 2002) worked the A5 design through an example — [E5](https://www.perl.com/pub/2002/08/22/exegesis5.html/); S05 created 24 Jun 2002 — [S05](https://github.com/Raku/old-design-docs/blob/master/S05-regex.pod)
- "longest-token" already in the April 2006 mirror; "proto token" in the Nov 2008 spec — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108); [68d062f](https://github.com/Raku/old-design-docs/commit/68d062fcaaa4e6e6a032e30775e3d7782e95eb28)
- Slangs/braids: S02 commit 2009-05-08 "take stab at documenting how interwoven languages work" (first appearance of both "slang" and "braid" in the repo) — [5cfa62e](https://github.com/Raku/old-design-docs/commit/5cfa62e1c79746f9b0151da987bc862695607e27)
- Pugs: 6.0.5 (15 Feb 2005) "New parser logic merged from Michaud and Palmer's `Perl6.grammar`"; 6.2.3 (12 May 2005) "Perl 6 Rules support via Parrot/PGE: Named rules and subrule support"; 6.2.4 "Perl6::Rules test suite incorporated"; 6.2.13 (17 Oct 2006) "Pugs::Compiler::Rule (PCR) replaces Parrot/PGE as the default engine" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Rakudo 2011.11 "basic protoregex support with NFA-driven LTM for some declarative constructs"; 2011.12 "improved protoregex support, including NFA caching"; 2014.06 "more work on allowing slangs transparently (such as 'v5')" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- Rakudo 6.c announcement claims Perl 6 "Dramatically reforms and sets a new standard in regex syntax, which scales up to full grammars - powerful enough to parse Perl 6 itself" — [announce 2015.12](https://github.com/rakudo/rakudo/blob/main/docs/announce/2015.12.md)
- Wikipedia relates rules to PEG syntactic predicates and ANTLR (its framing, citing S05) — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))

### Inferences
- The Rakudo 2011.11 entry belongs to the "nom" rewrite; earlier nqp-rx-based Rakudo (2009–2010) already had protoregexes in some form, so 2011.11 is the date of NFA-driven LTM, not of protoregexes per se.

### Gaps
- Date of Larry Wall's STD.pm (the reference grammar that drove LTM/protoregex spec work, ~2007) not sourced here.
- PGE (Parrot Grammar Engine) creation date not sourced.

---

## Q7. Junctions (any/all/one/none)

### Takeaway
Junctions are Damian Conway's "superpositions": proposed in RFC 225 (14 Sep 2000) explicitly from his Quantum::Superpositions module, used in A4 (Jan 2002) and E6 (Jul 2003), formalized in S09 (Sep 2004), and were the very first feature of the first Pugs release (7 Feb 2005).

### Cited Findings
- RFC 225 "Data: Superpositions" — Damian Conway, 14 Sep 2000, last modified 18 Sep 2000, Frozen; "This RFC (seriously) proposes Perl 6 provide any and all operators, and, thereby, conjunctive and disjunctive superpositional types"; proposes adding "the any, all, and eigenstates operators proposed in those talks"; "See the Quantum::Superpositions module"; references quantum-computing literature (Deutsch, Shor, Grover…) — [RFC 225](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/225.html)
- A4 (15 Jan 2002): "If we accept Damian's superposition RFC, it could be written this way: `when true any(@$!).isa(Foo)`"; the switch table defines list matching as `match if $a =~ any($b)` ("recursive disjunction") — [A4](https://www.perl.com/pub/2002/01/15/apo4.html/)
- A6 (7 Mar 2003) uses "junction" terminology: "A <type> is actually allowed to be a junction of types" — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- E6 (29 Jul 2003): `if any(@newvals) > any(@oldvals) {…}` "instead of resorting to the horror of nested greps" — [E6](https://www.perl.com/pub/2003/07/29/exegesis6.html/)
- S09 (created 13 Sep 2004): "A junction is a superposition of data values pretending to be a single data value"; `any()`/`|`, `all()`/`&`, `one()`/`^`, `none()` — [S09](https://github.com/Raku/old-design-docs/blob/master/S09-data.pod)
- Pugs 6.0.0 (7 Feb 2005): "Junctions and chained comparisons"; 6.0.2: "Autothreading over chained comparison and multiarg functions"; 6.0.3: "Junctions are now proper sets" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Wikipedia introduces junctions citing S09 (Wall, 2004) — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))

### Inferences
- The rename "superposition" → "junction" happened between A4 (Jan 2002, still "superposition RFC") and A6 (Mar 2003, "junction of types").

### Gaps
- Exact coinage of "junction": a web search surfaced a perl6-language post by Damian Conway dated 8 Dec 2002 using "junctions", but I could not open and verify the message (lead: [perl.perl6.language on Google Groups](https://groups.google.com/g/perl.perl6.language/c/QbGOWSCrLXg), thread unverified). Exegesis 4 (Apr 2002), which may contain the first public `any`-junction usage, could not be retrieved.
- Release date of Quantum::Superpositions on CPAN not checked (RFC 225 implies it existed with talks by Sep 2000).

---

## Q8. Metaoperators: hyper `»op«`, reduction `[op]`, cross `X`, zip `Z`, reverse/assign

### Takeaway
Element-wise operators were RFC 82 (Aug 2000, crediting PDL/Numerical Python); A3 (Oct 2001) accepted them as "hyper" operators with a `^` marker and the quip "APL, here we come"; the `»op«` spelling is in use by Feb 2004 and A12 records the renaming back from "vector" to "hyper". Reduce came from RFC 76 (List::Util), `[op]` is in the spec by Apr 2006 and in Pugs by May 2005; `X` is spec'd by Nov 2008 and `Z` as a metaop on 2010-03-15.

### Cited Findings
- RFC 82 "Arrays: Apply operators element-wise in a list context" — Jeremy Howard, 10 Aug 2000, last modified 21 Sep 2000, Frozen; references "Implementation in Perl Data Language" and "Array operations in Numerical Python" — [RFC 82](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/82.html)
- A3 (2 Oct 2001): "RFC 082 … APL, here we come… :-)"; "That special marker turns out to be an uparrow, with a tip o' the hat to higher-order functions. That is, the hyper-operator: `@a ^* @b` is equivalent to `parallel { $^a * $^b } @a, @b`"; "Unary ^ is now reserved for hyper operators … `@a ^+= 1`" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- E7 (27 Feb 2004) already uses `»`/`«` hyper syntax (24 occurrences) — [E7](https://www.perl.com/pub/2004/02/27/exegesis7.html/)
- A12 (16 Apr 2004): "Vector Operators Renamed Back to 'hyper' Operators: The mathematicians got confused when we started talking about 'vector' operators…"; "Unary Hyper Operators Now Use One Quote Rather Than Two": `@neg = -« @pos; @objects».bletch(1,2,3)` — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- RFC 76 "Builtin: reduce" — Damian Conway, 10 Aug 2000, Frozen: "modelled after Graham Barr's `reduce` subroutine from the List::Utils module" — [RFC 76](https://web.archive.org/web/2019/https://raku.org/archive/rfc/76.html)
- RFC 90 "Arrays: merge() and unmerge()" (Jeremy Howard, 10 Aug 2000) is the zip precursor — [RFC 90](https://web.archive.org/web/2019/https://raku.org/archive/rfc/90.html); RFC 207 "Efficient Array Loops" (Buddha Buck, 8 Sep 2000) cites "ufuncs in Numeric Python" — [RFC 207](https://web.archive.org/web/2019/https://raku.org/archive/rfc/207.html)
- `[+]` already in the April 2006 Synopses mirror — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108)
- `Z` metaop: commit 2010-03-15 "[S03] add Z to go with X metaop note that X and Z desugar to higher-order methods, crosswith and zipwith" — [63679e1](https://github.com/Raku/old-design-docs/commit/63679e18654b6dfbc59599ca936e720749dde058)
- Pugs: 6.0.11 (14 Mar 2005) "Infix Y (and its UTF8 form)" [zip]; 6.2.3 (12 May 2005) "Experimental support for prefix reduce metaoperator `[+]`", "Hyper operators now works on arrays too"; 6.2.4 (24 May 2005) "All infix operators now receive reduction forms, such as `[+]`", "All operators now receive hyperised forms, such as `>>+<<` and `~<<`"; 6.2.13 (17 Oct 2006) "Hyperised reduce operators: `[>>+<<]` and `[\>>+<<]`", "Nullary reduction … `[*]() === 1`" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Rakudo 2010.03 "Lazy implementation of the Z operator" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- A3 also says of Unicode operators: "Note that, unlike APL, we won't be hardware dependent" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)

### Inferences
- The only explicit APL credit in the design documents is the A3 joke about hyper operators (and the A3 contrast for Unicode operators); for reduce, the explicitly credited source is Perl 5's List::Util, not APL. Calling `[op]` "APL-derived" is a reasonable reading but not something the docs state.
- Assignment metaoperators (`op=`) generalize C/Perl 5 compound assignment; Pugs supported `//=`, `||=`, `.=` by 14 Mar 2005 — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)

### Gaps
- The exact date `^op` became `»op«` (somewhere between A3, Oct 2001, and E7, Feb 2004) was not found; A6 (Mar 2003) and E6 (Jul 2003) contain no `»`.
- The date the `X` (cross) metaoperator first entered S03 (≤ Nov 2008) and the reverse metaop `R` were not determined.

---

## Q9. Whatever-star, placeholders, pointy blocks; feeds

### Takeaway
Implicit-parameter currying began as RFC 23's `^_` placeholder (4 Aug 2000, which links Haskell for currying); A3/A4 (2001–2002) turned it into `$^a` and introduced pointy blocks; A6 (Mar 2003) severed placeholders from currying (`.assuming`) and introduced pipe (feed) operators. `Whatever` appears in the spec Sep 2006 and `WhateverCode` Aug 2009; Rakudo had Whatever-currying in Apr 2009.

### Cited Findings
- RFC 23 "Higher order functions" — Damian Conway, 4 Aug 2000, last modified 25 Sep 2000, Frozen: "It is proposed that Perl reserve the bareword `^_` (caret-underscore) as a 'placeholder' for generating higher order functions more cleanly"; abstract term "higher-order functions (a.k.a. 'currying')"; reference "Implementation in Haskell: http://www.haskell.org/tutorial/functions.html" — [RFC 23](https://web.archive.org/web/2019/https://raku.org/archive/rfc/23.html)
- A3 (2 Oct 2001) uses `parallel { $^a * $^b }` and `my sub operator:EQ { lc($^a) eq lc($^b) }` — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A4 (15 Jan 2002): `given $value -> $g {…}`, `for @foo -> $a, $b { # two at a time`, `grep { $^a eq 3 } @list` "a more explicit use of a curried function", `grep -> $x { $x eq 3 } @list` — [A4](https://www.perl.com/pub/2002/01/15/apo4.html/)
- RFC 173 "Allow multiple loop variables in foreach statements" — Peter Scott, 29 Aug 2000, Frozen — [RFC 173](https://web.archive.org/web/2019/https://raku.org/archive/rfc/173.html)
- A6 (7 Mar 2003), on RFC 23: "the rules are too complicated. There's really no need for special numbered placeholders"; "Placeholders are now just for argument declaration, and imply no currying. Currying on the other hand is done with an explicit…" `.assuming` — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- A6: "There will be two new operators, called pipe operators… `stuff @foo <== 1,2,3` / `1,2,3 ==> stuff @foo`"; "the ==> and <== operators are big, fat, and obnoxiously noticeable. I like them that way" — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- First `Whatever` in the spec repo: 2006-09-21 (`multi pick ( Whatever, *@values )`) — [b22582e](https://github.com/Raku/old-design-docs/commit/b22582e6bef46cbad0604401f051e74594053c4c); absent from the April 2006 mirror — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108)
- "[S02] document WhateverCode and HyperWhateverCode", 2009-08-25 — [357be11](https://github.com/Raku/old-design-docs/commit/357be1193fb2de11f8a178dcfb7a5cb2e3cdc8d3)
- Pugs 6.0.3 (12 Feb 2005) "Extraction of higher-order placeholder variables"; 6.0.7 (18 Feb 2005) "Code literals -- /sub/, /pointy/ and /bare/ variants all works" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Rakudo 2009-04: "calling a method on a Whatever star generates a closure"; 2009-08: "instead of throwing nasty parse errors, Rakudo now informs you that feed operators are not yet implemented" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)

### Inferences
- The `^` twigil in `$^a` is a direct survival of RFC 23's `^_` (the A3 hyper-operator text calls `^` "a tip o' the hat to higher-order functions").
- A6's "pipe operators" were later renamed "feeds"; the term change is not dated here.

### Gaps
- First working implementation of feeds in Pugs/Rakudo not pinned.
- No Perl 6 source credits Scala's `_` or any other language for Whatever-currying.

---

## Q10. Sequences `...`, lazy lists, gather/take, caret ranges

### Takeaway
Lazy lists were RFC'd in Aug 2000 (RFC 24 withdrawn; RFC 81 crediting Haskell and Python), accepted in A3 (`1 .. Inf`), implemented in Pugs Apr 2005 on top of Haskell laziness. gather/take was introduced in A12 (Apr 2004) as "conjectural" with no external credit; the sequence operator's arithmetic/geometric deduction is in S03 by Nov 2008, renamed from "series" in Sep 2010 "to accord with math culture", and revamped in Rakudo 2010.09.

### Cited Findings
- RFC 24 "Data types: Semi-finite (lazy) lists" — Damian Conway, 4 Aug 2000, **Withdrawn**: "the right operand of a `..` operator may be omitted in a list context, producing a lazily evaluated semi-finite list" — [RFC 24](https://web.archive.org/web/2019/https://raku.org/archive/rfc/24.html)
- RFC 81 "Lazily evaluated list generation functions" — Jeremy Howard, 10 Aug 2000, Frozen; cites list comprehensions in "Haskell and Python v2" and "List comprehension in Haskell"; numeric notation of "Matlab and IDL" — [RFC 81](https://web.archive.org/web/2019/https://raku.org/archive/rfc/81.html)
- RFC 123 "Builtin: lazy" (David Nicol, 17 Aug 2000) — [RFC 123](https://web.archive.org/web/2019/https://raku.org/archive/rfc/123.html); RFC 117 "Perl syntax support for ranges" (pdl-porters, 16 Aug 2000, Withdrawn) — [RFC 117](https://web.archive.org/web/2019/https://raku.org/archive/rfc/117.html)
- RFC 31 "Subroutines: Co-routines" (Conway, 4 Aug 2000, Frozen) proposes `yield` — [RFC 31](https://web.archive.org/web/2019/https://raku.org/archive/rfc/31.html); RFC 27 "Coroutines for Perl" (Tom Scola, 4 Aug 2000) cites Simula and Python ("Coroutines in Python") — [RFC 27](https://web.archive.org/web/2019/https://raku.org/archive/rfc/27.html)
- A3 (2 Oct 2001) on RFC 081: "It will be tricky to make this one work right: `($first, @rest) = 1 .. Inf;`" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A12 (16 Apr 2004): "We used one example of the conjectural gather/take construct. A gather executes a closure, returning a list of all the values returned by take within its lexical scope. In a lazy context it might run as a coroutine. There probably ought to be a dynamically scoped variant…" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- A12 also uses `..^`: `use Dog-(1.2.1..^1.3);` — [A12](https://www.perl.com/pub/2004/04/16/a12.html/); `..^` is in the April 2006 Synopses mirror — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108)
- Nov 2008 S03: "If the right operand is `*` (Whatever) and the sequence is obviously arithmetic or geometric, the appropriate function is deduced: `1, 3, 5 ... *` # odd numbers; `1, 2, 4 ... *` # powers of 2" — [68d062f (S03.pod)](https://github.com/Raku/old-design-docs/commit/68d062fcaaa4e6e6a032e30775e3d7782e95eb28)
- Rename: "s/series/sequence/ to accord with math culture", TimToady, 2010-09-23 — [1779592](https://github.com/Raku/old-design-docs/commit/17795925bc48affda10275bd2768e1f2d9ef6345)
- Pugs 6.0.7 (18 Feb 2005) "The `...` (dotdotdot) literal"; 6.2.0 (13 Apr 2005) "Lazy IArray structures: 'Infinite lists, constant time'", "Infinite slices: `@x[1...]`"; 6.2.4 (24 May 2005) "`gather {...}` and `take()` implemented", "Experimental support for `coro { ... }` … and `yield()`", "Experimental support for `lazy {...}`"; 6.2.10 (10 Oct 2005) "Infinite lazy lists" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Rakudo 2009-10 "some variants of the new series operator"; 2010.09 "revamp of series operator code, and adaption to new spec", "implement `...^` up-to-but-excluding-series operator" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- Great List Refactor: S07 re-created 7 Sep 2015 ("Initial work on an S07 re-write", jnthn, 2015-09-12, introduces HyperSeq) — [S07](https://github.com/Raku/old-design-docs/blob/master/S07-lists.pod); [de53e4b](https://github.com/Raku/old-design-docs/commit/de53e4be1241a36451e7f90c3902bedcdaab6a73)
- Wikipedia describes Raku's lazy lists as the method "found in some functional languages such as Haskell", citing S09 — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language)) — but S09 itself does not mention Haskell (grep of the final Synopses finds "Haskell" only in S04's remark that `do` works "much like the `$` operator in Haskell") — [S04](https://github.com/Raku/old-design-docs/blob/master/S04-control.pod)

### Inferences
- The Haskell→laziness credit in the primary record is RFC 81 (2000), not the Synopses; the "via Pugs" path is implementation lineage (Pugs's Haskell runtime), not a documented design attribution.
- gather/take has no documented external source; A12 frames it as a lexically scoped collector that "might run as a coroutine", connecting it to RFC 31's `yield` rather than to Icon.

### Gaps
- **No Perl 6 document found crediting Icon** (for generators, gather/take, or goal-directed evaluation); "Icon" does not occur in A1–A6, A12 or the final Synopses. Treat an Icon attribution as external commentary unless another researcher finds a primary quote.
- Exact pre-2008 date of the arithmetic/geometric deduction text and of `^N` ("upto") not found.

---

## Q11. Chained comparisons, `//`, `say`, `state`, given/when and smartmatch `~~` (incl. Perl 5 backports)

### Takeaway
Chained comparisons (RFC 25) and given/when (RFC 22) are Damian's Aug 2000 RFCs; `//` and smartmatch were Larry's own additions in A3/A4 (2001–2002); `state` and `say` appear by early 2004. All were implemented in Pugs in Feb–May 2005; `//`, `say`, `state`, given/when and `~~` were later added to Perl 5.10.0, whose perldelta does not itself mention Perl 6.

### Cited Findings
- RFC 25 "Operators: Multiway comparisons" — Damian Conway, 4 Aug 2000, Frozen: "multiway comparisons such as `if ( 0 <= $x < 10 )` … should do what the user means" — [RFC 25](https://web.archive.org/web/2019/https://raku.org/archive/rfc/25.html); accepted in A3 (2 Oct 2001), which summarizes it as "expressions involving multiple chained comparisons should act like mathematician would expect … `0 <= $x < 10` really means something like `0 <= $x && $x < 10`. The $x would only be evaluated once" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A3 (2 Oct 2001), "Non-RFC considerations": "A binary `//` operator is the defaulting operator. That is: `$a // $b` is short for: `defined($a) ?? $a :: $b` except that the left side is evaluated only once" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A3 (2 Oct 2001): "Binary `~~`: This is a logical XOR operator" (i.e. `~~` was not yet smartmatch) — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- RFC 22 "Control flow: Builtin switch statement" — Damian Conway, 4 Aug 2000, Frozen: keywords `switch`/`case`, "nearly 30 forms of matching" — [RFC 22](https://web.archive.org/web/2019/https://raku.org/archive/rfc/22.html)
- A4 (15 Jan 2002): "After thrashing over several options with Damian and other folks, we've settled on using `given` instead of `switch`, and `when` instead of `case`"; "The following table will also define how the Perl 6 `=~` operator works!"; "I think it's silly not to unify exception handlers with switch statements" — [A4](https://www.perl.com/pub/2002/01/15/apo4.html/)
- A6 (7 Mar 2003): "The want function can be used with smart matching: `if want ~~ List & 2 & Lvalue {…}`" (smartmatch now spelled `~~`) — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- E7 (27 Feb 2004) uses `state $linenum = 1;` — [E7](https://www.perl.com/pub/2004/02/27/exegesis7.html/); A12 (16 Apr 2004): "assignment to state variables (Perl's version of lexically scoped 'static' variables), is taken as pseudo-assignment representing a call to the 'first' trait" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- A12 (16 Apr 2004): "`for @strings {say}` … ('say' is just Perl 6's version of a printline function.)" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/); RFC 39 "Perl should have a print operator" (Jon Ericson, 5 Aug 2000) was Retracted — [RFC 39](https://web.archive.org/web/2019/https://raku.org/archive/rfc/39.html)
- Pugs: 6.0.0 (7 Feb 2005) chained comparisons; 6.0.7 (18 Feb 2005) "The `say`, `exit`, `die`… primitives"; 6.0.11 (14 Mar 2005) "Stub implementation for ~~ and !~", "Assignment operators like `.=`, `//=` and `||=`"; 6.0.12 (21 Mar 2005) "Smartmatch ~~ for regex"; 6.0.13 (27 Mar 2005) "Support for `given`, `when`"; 6.2.3 (12 May 2005) "`state $var` implemented"; 6.2.4 "`state $x = 42` now only assigns `$x` once" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- perl5100delta (Perl 5.10.0): `feature` pragma enables "`switch` (adds a switch statement), `say` (adds a `say` built-in function), and `state`"; "A new operator `//` (defined-or) has been implemented"; "Perl 5 now has a switch statement … introduces three new keywords, `given`, `when`, and `default`"; `say` credited to Robin Houston; `state` to Rafael Garcia-Suarez and Nicholas Clark; the document's only "Perl 6" mention is MAD, "work leading to a Perl 5 to Perl 6 converter" — [perl5100delta](https://chromium.googlesource.com/chromium/deps/perl.git/+/ac0d98b5cee6c024b0cffeb4f8f45b6fc5ccdb78/perl/lib/pods/perl5100delta.pod)
- Damian Conway's 2020 Raku Advent post on RFC 22 (as summarized by search) says Perl 5's version is "widely considered the biggest design failure of the past two decades" whereas in Raku it is non-controversial because `$left ~~ $right` translates to `$right.ACCEPTS($left)` — [Raku advent repo commit](https://github.com/Raku/advent/commit/cc578b3021de7aff347e060d41c977f91cf0af48) (not opened directly; from search summary)

### Inferences
- `//`, `say`, `state`, given/when and `~~` all existed in the Perl 6 design (2001–2004) and in Pugs (2005) before Perl 5.10.0, so they are Perl 6 → Perl 5 backports even though perl5100delta does not say so.
- `<=>`, `cmp`, `||`/`&&` returning values, `..`, `=>`, `**` (A3: "we used Fortran's `**` operator for exponentiation") and heredocs are inherited from Perl 5 or earlier, not Perl 6 inventions — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)

### Gaps
- The exact date `~~` replaced `=~` as smartmatch (between A5/E5, Jun–Aug 2002, which have no `~~`, and A6, Mar 2003) is not pinned.
- Earliest Perl 6 use of `say` before A12 (possibly S03, Mar 2004, or mailing-list posts) not found.
- Perl 5.10.0 release date (commonly given as 18 Dec 2007) not verified from a fetched source in this session.

---

## Q12. Phasers (BEGIN/END/ENTER/LEAVE/KEEP/UNDO/FIRST/NEXT/LAST/PRE/POST/CATCH/CONTROL)

### Takeaway
Structured exceptions (RFC 88) and pre/post handlers (RFC 194, RFC 271) were RFC'd in Aug–Sep 2000; A4 (Jan 2002) introduced in-block `CATCH`, `PRE`/`POST` "for design-by-contract programmers", `KEEP`/`UNDO`, and turned `continue` into `NEXT`. Pugs implemented the full set in Oct 2006. Perl 6 credits "design-by-contract" (Eiffel's term) and Damian's Class::Contract, but never names Eiffel.

### Cited Findings
- RFC 88 "Omnibus Structured Exception/Error Handling Mechanism" — Tony Olekshy, 8 Aug 2000, Frozen — [RFC 88](https://web.archive.org/web/2019/https://raku.org/archive/rfc/88.html)
- RFC 194 "Standardise Function Pre- and Post-Handling" — Jarkko Hietaniemi, 5 Sep 2000 — [RFC 194](https://web.archive.org/web/2019/https://raku.org/archive/rfc/194.html)
- RFC 271 "Subroutines : Pre- and post- handlers for subroutines" — Damian Conway, 20 Sep 2000, Frozen; generalizes his "Class::Contract module" into a generic "handler" mechanism — [RFC 271](https://web.archive.org/web/2019/https://raku.org/archive/rfc/271.html)
- A4 (15 Jan 2002): requirement "The cleanup mechanism should mesh nicely with the notions of post condition processing under design-by-contract"; "(The POST block has a corresponding PRE block for design-by-contract programmers.)"; "I'd like to call them KEEP and UNDO. KEEP blocks would only be executed if the block succeeded. UNDO blocks would only be executed if the block failed"; "The continue block changes its name to NEXT and moves inside the block it modifies, to work like POST blocks" — [A4](https://www.perl.com/pub/2002/01/15/apo4.html/)
- A12 (16 Apr 2004) ties `state` initialization to a "first" trait — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- S04 (created 19 Aug 2004) is the consolidated phaser spec — [S04](https://github.com/Raku/old-design-docs/blob/master/S04-control.pod)
- Pugs 6.0.9 (28 Feb 2005) "`END{}`"; 6.0.13 "`END {...}` now runs after `die`"; 6.2.4 (24 May 2005) "`FIRST {...}`"; 6.2.13 (17 Oct 2006) "Support for `FIRST`, `NEXT`, `LAST` loop control blocks", "Support for `START`, `PRE`, `POST`, `KEEP`, `UNDO`, `ENTER`, `LEAVE` blocks", "`[t/closure_traits/]` created" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- 6.d: "`CX::Warn` and `CX::Done` can be caught inside `CONTROL` phaser" — [6.d ChangeLog](https://github.com/Raku/6.d-prep/blob/master/ChangeLog.md)
- BEGIN/END/INIT/CHECK pre-date Perl 6 (Perl 5); Perl 5.10.0 added `UNITCHECK` — [perl5100delta](https://chromium.googlesource.com/chromium/deps/perl.git/+/ac0d98b5cee6c024b0cffeb4f8f45b6fc5ccdb78/perl/lib/pods/perl5100delta.pod)

### Inferences
- In 2006 these were still called "closure traits" (Pugs test dir name); the term "phaser" is later.

### Gaps
- Date the term "phaser" was adopted, and when ENTER/LEAVE/FIRST/LAST/CONTROL first entered the spec (A4 does not mention ENTER/LEAVE/FIRST; S04 Aug 2004 or later), not pinned.
- No Perl 6 document found that names Eiffel.

---

## Q13. Strings and quoting: code interpolation, heredoc indentation stripping, quoting adverbs; pairs and `:$var`

### Takeaway
Interpolation of methods/subs was RFC'd Sep 2000 and answered in A2 (May 2001) with `"$(expr)"`; RFC 162's indentation-stripping heredoc was accepted in A2 ("remove whitespace equivalent to the terminator") and later surfaced as `q:to`. Pairs came from RFC 84 (Aug 2000); the `:foo(...)` colon-pair/adverb syntax was introduced in A12 (Apr 2004) with explicit Unix-option and Perl 5 I/O-layer inspiration.

### Cited Findings
- RFC 222 "Interpolation of object method calls" (Michael G Schwern, 14 Sep 2000) — [RFC 222](https://web.archive.org/web/2019/https://raku.org/archive/rfc/222.html); RFC 252 "Interpolation of subroutines" (Schwern, 17 Sep 2000) — [RFC 252](https://web.archive.org/web/2019/https://raku.org/archive/rfc/252.html)
- A2 (3 May 2001): "Freeing up the various bracketing characters allows us to use them for other purposes, such as interpolation of expressions: `"$(expr)"` # interpolate a scalar expression, `"@(expr)"`"; "There are $(Dog.numdogs) dogs."; on RFC 222 "I've been hankering for methods to interpolate for a long time" — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- RFC 111 "Here Docs Terminators (Was Whitespace and Here Docs)" — Richard Proctor, 16 Aug 2000 — [RFC 111](https://web.archive.org/web/2019/https://raku.org/archive/rfc/111.html); RFC 162 "Heredoc contents" — Proctor, 27 Aug 2000, Frozen: "introduces the <<< Enhanced Heredoc that removes whitespace" — [RFC 162](https://web.archive.org/web/2019/https://raku.org/archive/rfc/162.html)
- A2 (3 May 2001): "RFC 111 … Fine." "RFC 162: Heredoc contents — I think I like option (e) the best: remove whitespace equivalent to the terminator." — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- `q:to` heredoc syntax is in the April 2006 Synopses mirror — [0879bdc](https://github.com/Raku/old-design-docs/commit/0879bdcdfe9d2718b544f487f88d180aaff50108)
- Pugs 6.2.7 (13 Jun 2005) "Experimental heredoc support via `qq:to/END/ ... END`"; 6.2.13 "Shell-like quoting rules implemented for `<< $x "qq" 'q' >>`"; 6.2.12 "Support for Unicode bracket characters for quotelike operators" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog); Rakudo 2012.11 "+ heredocs" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- RFC 84 "Replace => (stringifying comma) with => (pair constructor)" — Damian Conway, 10 Aug 2000, Frozen — [RFC 84](https://web.archive.org/web/2019/https://raku.org/archive/rfc/84.html); A3 (2 Oct 2001): "I like the basic idea of pairs because it generalizes to more than just hash values. Named parameters will almost certainly be implemented…" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A12 (16 Apr 2004), ":foo() Adverbs": "There's a new pair syntax that is more conducive to use as option arguments. This syntax is reminiscent of both the Unix command line syntax and the I/O layers syntax of Perl 5. But unlike Unix command-line options, we use colon…"; table `foo => $bar` ⇒ `:foo($bar)`, `foo => 1` ⇒ `:foo`; `1..100 :by(3)` — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- Pugs 6.0.14 (4 Apr 2005) "Adverb pairs: `:key(123)`, `:key<abc>`, `:key[1,2,3]` and `:key`" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)

### Inferences
- The final `"{ code }"` closure interpolation replaced A2's `$(…)`/`@(…)` form at some later point; I could not date that change.

### Gaps
- Dates for `"{...}"` closure interpolation and for the `:$var` shorthand (colon-pair from a variable name) not found.

---

## Q14. Numerics: Rat for decimal literals, big integers by default

### Takeaway
Big integers and big rationals were proposed by Jarkko Hietaniemi in RFC 43 (5 Aug 2000) as a pragma; Larry (A2, May 2001) preferred tying them to the run-time type system. Rakudo produced `Rat` from `Int/Int` by 2009-09; the date that decimal literals became `Rat` by default was not pinned.

### Cited Findings
- RFC 43 "Integrate BigInts (and BigRats) Support Tightly With The Basic Scalars" — Jarkko Hietaniemi, 5 Aug 2000, Developing: "sometimes when the application would prefer to stay accurate, the use of 'bigints' (and for division, 'bigrats') would be preferable" — [RFC 43](https://web.archive.org/web/2019/https://raku.org/archive/rfc/43.html)
- RFC 209 "Fuller integer support in Perl." — Shane Kerr, 11 Sep 2000 — [RFC 209](https://web.archive.org/web/2019/https://raku.org/archive/rfc/209.html)
- A2 (3 May 2001): "RFC 043 … This RFC suggests that a pragma enables the feature, but I think it should probably be tied to the run-time type system"; also "We should also try to avoid defining a type system that makes translation of numeric types to Java or C# types problematic" — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- Rakudo 2009-09: "updates to numeric operators: infix</>(Int, Int) creates a Rat" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- 6.d: "Rationals are always reduced on creation"; `Rational.norm` requirement; `-Inf`/`Inf`/`NaN` round-trip through Rational — [6.d ChangeLog](https://github.com/Raku/6.d-prep/blob/master/ChangeLog.md)
- Rakudo 2026.09: "Only use new Rat.raku rendering in 6.e+" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)

### Inferences
- `Rat` as a type name appears only once in the April 2006 Synopses mirror vs 8 times in Nov 2008, suggesting the Rat-literal design was fleshed out 2006–2008 (weak signal).

### Gaps
- Date when S02 made decimal literals `Rat` by default; whether Pugs (Haskell `Rational`/`Integer`) implemented exact decimal literals; first-class bigint-by-default date. No external credit (e.g. Scheme's exact numbers) found.

---

## Q15. Unicode: operators/identifiers, NFG, `.chars` vs `.codes`

### Takeaway
Unicode source and Unicode operators were decided in A2/A3 (2001); A5 (2002) defined Unicode "levels" in which `.` can mean a grapheme; the name "NFG" entered S02 on 30 Jan 2009; Jonathan Worthington's MoarVM implementation made all Rakudo strings grapheme-level from 24 Apr 2015 (Rakudo 2015.05).

### Cited Findings
- RFC 294 "Internally, data is stored as UTF8", RFC 295 "Normalisation and `unicode::exact`", RFC 312 "Unicode Combinatorix" — all Simon Cozens, 25 Sep 2000, Developing — [RFC 294](https://web.archive.org/web/2019/https://raku.org/archive/rfc/294.html); [RFC 295](https://web.archive.org/web/2019/https://raku.org/archive/rfc/295.html); [RFC 312](https://web.archive.org/web/2019/https://raku.org/archive/rfc/312.html)
- A2 (3 May 2001): "Perl 6 programs are notionally written in Unicode, and assume Unicode semantics by default"; "Even `chars($foo)` might not be sufficiently precise … you might want to know how long the string is in actual characters, not counting combining characters … But that's a topic for later"; "Property names should start with an identifier letter (which includes Unicode letters and ideographs)" — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- A3 (2 Oct 2001), "Unicode operators": "I see no reason … why Unicode operators should not be allowed. Note that, unlike APL, we won't be hardware dependent" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- A5 (4 Jun 2002): "`:u0` # use bytes, `:u1` # level 1 support (. is codepoint), `:u2` # … (. is grapheme), `:u3` # … language dependent"; "Under level 2 Unicode support, a character is assumed to mean a grapheme"; data assumed to be in Normalization Form C — [A5](https://www.perl.com/pub/2002/06/04/apo5.html/)
- S02 commit 2009-01-30 "[S02] random clarifications" adds "Perl presents Unicode in 'NFG' formation, where each grapheme counts as one character" (first "NFG" in the spec repo) — [dc6f02a](https://github.com/Raku/old-design-docs/commit/dc6f02a49a82becee70b518927fd5a509fc70632); final S02 suggests "all graphemes be translated on input to unique grapheme numbers and represented as integers" — [S02](https://github.com/Raku/old-design-docs/blob/master/S02-bits.pod)
- S15 Unicode created 2 Dec 2013 (commit "The addition of S15!", lue, 2013-12-11) — [S15](https://github.com/Raku/old-design-docs/blob/master/S15-unicode.pod)
- Pugs 6.2.0 (13 Apr 2005) builtins `bytes`, `chars`, `codes`, `graphs` — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Worthington, 30 Apr 2015: "Since the 24th April, all strings you work with in Rakudo on MoarVM are at grapheme level"; example where `.chars` is 1 but `.NFC.codes` is 2 — [Worthington, "This week: the big NFG switch on" (CCR copy)](https://raku.github.io/CCR/Remaster/Jonathan%20Worthington/This-week-the-big-NFG-switch-on-and-many-fixes.html)
- TPF grant report: from April 2015, under the Perl 6 Core Development Fund, he implemented NFG strings in MoarVM and updated Rakudo; late-May and late-June 2015 releases had grapheme-level strings — [TPF news](https://news.perlfoundation.org/post/perl_6_release_goals_initial_g)
- Rakudo 2015.04 "Uni.Str coercion produces an NFG string"; 2015.05 "+ NFG, NFC, NFD, Uni", ":m/:ignoremark implemented (now that we have NFG)" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- 6.c announcement: "Has outstanding Unicode support, with strings working at grapheme level" — [announce 2015.12](https://github.com/rakudo/rakudo/blob/main/docs/announce/2015.12.md)

### Inferences
- "NFG" is named by analogy with Unicode's NFC/NFD normalization forms; no Perl 6 source credits another language for grapheme-level strings (Swift's grapheme-based `Character`, 2014, is contemporaneous, not credited).

### Gaps
- Whether NFG (as named) predates the 2009 S02 commit in svn or Parrot design notes was not verified. The Worthington post does not say who designed NFG.

---

## Q16. Concurrency: Promises, Supplies, Channels, `start`, `await`, react/whenever, hyper/race

### Takeaway
Threads were among the very first RFCs (RFC 1, 1 Aug 2000), but the shipped model is Jonathan Worthington's "heretical" S17 rewrite (3 Nov 2013): Promises/Channels landed in Rakudo on the JVM in Jul–Aug 2013, Supplies by 2014, `supply`/`react`/`whenever` blocks in 2015.09, `hyper`/`race` via the 2015 Great List Refactor, and 6.d (2018) made `await` non-blocking and added `hyper for`/`race for`. The explicitly cited external idea is Rx (reactive extensions).

### Cited Findings
- RFC 1 "Implementation of Threads in Perl" (Bryan C. Warnock, 1 Aug 2000, Frozen): "Perl 6 should be built around threads from the beginning" — [RFC 1](https://web.archive.org/web/2019/https://raku.org/archive/rfc/1.html); RFC 178 "Lightweight Threads" (Steven McDougall, 30 Aug 2000, Frozen) — [RFC 178](https://web.archive.org/web/2019/https://raku.org/archive/rfc/178.html); RFC 185 "Thread Programming Model" (McDougall, 31 Aug 2000) — [RFC 185](https://web.archive.org/web/2019/https://raku.org/archive/rfc/185.html)
- An S17 draft existed by Feb 2006 ("move S17draft to Perl6::Spec::Concurrency", audreyt, 2006-02-24; the repo's first commit) — [1771847](https://github.com/Raku/old-design-docs/commit/177184733aa742388a2267e7133ce05945f747ce)
- "Add a heretical S17, written from scratch." — Jonathan Worthington, 2013-11-03 (first `await`/`Channel` in the spec repo); S17 header "Created: 3 Nov 2013" — [0b11765](https://github.com/Raku/old-design-docs/commit/0b11765d37acafd8f074386423c1cae2b3e4e83b); [S17](https://github.com/Raku/old-design-docs/blob/master/S17-concurrency.pod)
- Rakudo 2013.07 "basic support for threads and promises (JVM only)"; 2013.08 "Improvements to Threads, including Channel and KeyReducer (JVM only)"; 2014.05 "added or updated many Supply methods", "IO::Socket::Async.connect, returns a Promise", "signal() which returns a Supply"; 2015.09 "New 'supply', 'whenever' and 'react' blocks for easy reactive programming" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- S07 rewrite (jnthn, 2015-09-12) introduces `HyperSeq` — [de53e4b](https://github.com/Raku/old-design-docs/commit/de53e4be1241a36451e7f90c3902bedcdaab6a73)
- 6.d: "`&await` no longer blocks *a thread* while waiting"; "`whenever` not in lexical scope of `react` throws"; "`start` blocks in sink context attach exception handler"; "New `for` statement modifiers `hyper for`, `race for`, and `lazy for`" — [6.d ChangeLog](https://github.com/Raku/6.d-prep/blob/master/ChangeLog.md)
- Worthington's Supplies article links reactive programming to the Rx library "which has now been ported to many platforms. In Raku, we're providing support for this in core"; his 2014 talks describe Promise (single async result) and Supply (async streams) and the iterator/observer duality — [Worthington, "Raku Supplies: Reactive Programming" (CCR copy)](https://raku.github.io/CCR/Remaster/Jonathan%20Worthington/Raku-Supplies-Reactive-Programming.html); [YAPC::EU 2014 talk abstract](https://act.yapc.eu/ye2014/talk/5635) (both from search results, not opened in full)
- 6.c announcement: "Provides composable constructs for working with asynchronous data and parallel computations" — [announce 2015.12](https://github.com/rakudo/rakudo/blob/main/docs/announce/2015.12.md)

### Inferences
- The 2000 RFCs concern OS/lightweight threads; nothing in them anticipates Promises/Supplies, so for these features the meaningful "proposed" date is the 2013 S17 rewrite, not 2000.

### Gaps
- Exact Rakudo release where `.hyper`/`.race` methods first shipped (the ChangeLog's first mention is a 2018.04 performance note).
- Jonathan Worthington's own statement of Rx/C#-async influences was not read in full; treat the Rx credit as plausible but secondary-sourced.

---

## Q17. Macros/AST, sigil invariance, and the object root

### Takeaway
Macros were specified in A6 (Mar 2003) as parse-time subroutines optionally bound to grammar rules; Pugs had rudimentary macros by Jun 2005 and AST-returning `q:code` by Jun 2006; Rakudo got `macro`/`quasi` in 2012.03. RakuAST became Rakudo's default frontend in 2026.09, while the old macros were disabled under 6.e (2026.06). Sigil invariance (`@foo[1]`) was decided in A2 (May 2001) in response to RFC 9.

### Cited Findings
- A6 (7 Mar 2003): "A macro is a subroutine that is called immediately upon completion of parsing. It has a default means of parsing arguments, or it may be bound to an alternate grammar rule to parse its arguments however you l[ike]"; "If you declare one as a macro, the body is executed at compile time, and returns a string, a parse tree, or a closure"; `macro circumfix:(**) () is parsed(/.*?/ { "" }` — [A6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- Pugs 6.2.7 (13 Jun 2005) "Rudimentary, source-filter-like macro support added"; 6.2.11 (1 Feb 2006) "Macros can now be called and reinterpolated at runtime"; 6.2.12 (26 Jun 2006) "`q:code {...}` gives ASTs in macros"; 6.2.13 "Support for user-defined unary and optional-unary prefix macros" — [Pugs ChangeLog](https://github.com/audreyt/pugs/blob/master/ChangeLog)
- Rakudo 2012.03 "implemented macro declarations and quasi quotes (sans placeholders)"; 2012.09 "basic macro unquoting"; 2012.11 "macro arguments now carry their lexical environment properly"; 2023.04 "use v6.e.PREVIEW or v6.* implies `use experimental :rakuast`"; 2026.06 "Disable macros in 6.e"; 2026.09 "Make RakuAST default for core and programs" — [Rakudo ChangeLog](https://github.com/rakudo/rakudo/blob/main/docs/ChangeLog)
- RFC 9 "Highlander Variable Types" — Andy Wardley, 1 Aug 2000 — [RFC 9](https://web.archive.org/web/2019/https://raku.org/archive/rfc/9.html)
- A2 (3 May 2001) on RFC 009: "I basically agree with the problem this RFC is trying to solve, but I disagree with the proposed solution … I think people like to think of `$foo`, `@foo` and `%foo` as separate variables … all variables may be thought of as references … It means that Perl programmers must learn to write `@foo[1]` where they used to write `$foo[1]`" — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- Object root `Mu` (Damian's suggestion), see Q3 — [e7a6479](https://github.com/Raku/old-design-docs/commit/e7a6479d0f35c1b043ee7f978eab399cf0dc8dcd)

### Inferences
- "Disable macros in 6.e" alongside "RakuAST default" (both 2026) indicates the 2012-era `quasi`-based macros are being retired in favour of a RakuAST-based design; 6.e had not been released as of 2026.09.

### Gaps
- No RFC proposing macros was identified among the 361 titles (RFC 18 "Immediate subroutines" was not read).
- No external source (Lisp, Scheme hygienic macros) is credited for macros in A6.

---

## Q18. What external sources did Perl 6 itself openly credit, and which commonly repeated attributions lack a primary quote?

### Takeaway
Primary-source credits are: the ECOOP 2003 Traits paper (roles), Quantum::Superpositions (junctions), Class::Multimethods + CLOS/Common Lisp (multis), List::Util (reduce), PDL/Numerical Python and an "APL" quip (hyper ops), Haskell (currying in RFC 23; laziness/list comprehensions in RFC 81), Ada (subtypes → `subset`), Prolog (regex cut, `:=` binding as unification), Ruby (unary `*` flattening), Java (interfaces, RFC 265), Unix CLI and Perl 5 I/O layers (`:adverbs`), "design-by-contract"/Class::Contract (PRE/POST), Rx (Supplies). No primary quote was found for Icon (gather/take), Eiffel by name, or Smalltalk.

### Cited Findings
- Traits paper (Schärli et al., ECOOP July 2003, LNCS 2743) — sole reference in A12 — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- Quantum::Superpositions — [RFC 225](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/225.html)
- Class::Multimethods, CLOS — [RFC 256](https://web.archive.org/web/2019/https://raku.org/archive/rfc/256.html); "much like Common Lisp" — [S12](https://github.com/Raku/old-design-docs/blob/master/S12-objects.pod); CLOS terminology for the MOP — [RFC 92](https://web.archive.org/web/2019/https://raku.org/archive/rfc/92.html)
- List::Util reduce — [RFC 76](https://web.archive.org/web/2019/https://raku.org/archive/rfc/76.html)
- APL ("APL, here we come… :-)"), and A2: defaults depend on "whether you think more like an APL programmer or a mere mortal" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/); [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- PDL and Numerical Python — [RFC 82](https://web.archive.org/web/2012/http://dev.perl.org/perl6/rfc/82.html)
- Haskell — [RFC 23](https://web.archive.org/web/2019/https://raku.org/archive/rfc/23.html); [RFC 81](https://web.archive.org/web/2019/https://raku.org/archive/rfc/81.html); S04 `do` "much like the `$` operator in Haskell" — [S04](https://github.com/Raku/old-design-docs/blob/master/S04-control.pod); Wikipedia's infobox "Influenced by: Perl, Ruby, Smalltalk, Haskell, JavaScript" (only Haskell cited there) — [Wikipedia: Raku](https://en.wikipedia.org/wiki/Raku_(programming_language))
- Ada: "the Ada distinction between types and subtypes" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/); A2 also says the `$pkg'var` syntax was "unduly influenced by Ada" and multiline-comment refusal by "the rationale for the design of Ada" — [A2](https://www.perl.com/pub/2001/05/03/wall.html/)
- Prolog: `:=` "a sort of unification operator (though without the implicit backtracking semantics)"; Ruby: "Unary * is the list flattening operator. (See Ruby for prior art.)" — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/); regex cut — [A5](https://www.perl.com/pub/2002/06/04/apo5.html/)
- Java interfaces — [RFC 265](https://web.archive.org/web/2019/https://raku.org/archive/rfc/265.html); "final" class "(to use the Java term)" — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- Python: A12 "Python's attributes suffer from the same misdesign as Perl 5's attributes. (My fault for copying Python's object model. :-)" (i.e., Perl 5 OO was modeled on Python's) — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- Unix CLI / Perl 5 I/O layers for `:foo()` — [A12](https://www.perl.com/pub/2004/04/16/a12.html/)
- Design-by-contract / Class::Contract — [A4](https://www.perl.com/pub/2002/01/15/apo4.html/); [RFC 271](https://web.archive.org/web/2019/https://raku.org/archive/rfc/271.html)
- Fortran `**` (inherited via Perl 5) — [A3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- Rx — [Worthington (CCR copy)](https://raku.github.io/CCR/Remaster/Jonathan%20Worthington/Raku-Supplies-Reactive-Programming.html)

### Inferences
- Many "inspired by" claims circulating about Raku (Icon generators, Eiffel contracts, Smalltalk) are reasonable but are third-party attributions; the primary record credits Perl 5 CPAN modules (often Damian's) at least as often as other languages — i.e. many Perl 6 features were prototyped as Perl 5 modules first (Quantum::Superpositions, Class::Multimethods, Class::Contract, List::Util, Perl6::Rules).

### Gaps
- Larry Wall's "State of the Onion" talks and the full text of Damian's 2020 RFC-anniversary advent posts were not read; they may contain additional explicit attributions.
- E3/E4 and the perl6-language mailing-list archives (2000–2004) were not searched systematically; first coinages ("junction", "phaser", "Whatever", "smiley") likely live there.
