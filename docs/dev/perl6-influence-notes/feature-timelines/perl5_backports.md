# Perl 6 / Raku features back-ported into Perl 5 (core and CPAN), 2000 – Nov 2026

Method note for the report writer: core-doc claims were checked against the POD sources themselves, fetched from the Perl/perl5 GitHub repo. That covers every perlNNNdelta from 5.10.0 to 5.44.0, the 5.45.x dev deltas, and historical tags of feature.pm, perlsyn, perlre and perlfunc. I grepped all of them for "Perl 6", "Perl6" and "Raku". MetaCPAN first-release dates come from the MetaCPAN API (`https://fastapi.metacpan.org/v1/release/_search?q=distribution:<Dist>&sort=date:asc`). Module quotes come from the current POD on MetaCPAN. perldoc.perl.org serves the same POD (for example https://perldoc.perl.org/perl5100delta).

Bottom line: **no perldelta from 5.10 to 5.44 says that a language feature "comes from Perl 6/Raku".** Five places in the core docs do credit Perl 6/Raku:
- feature.pm, for `say` and `given/when`.
- perlsyn, for given/when and smartmatch.
- perlre, for `(*THEN)` and `(*COMMIT)`.
- perlfunc, for `no VERSION` / `no 6`.
- perl5240delta, for hashbang redirection.

Most of the other resemblances (`//`, chained comparisons, `state`, signatures, `...`, `class`) have no attribution in the Perl 5 docs. For some of them the Perl 6 design documents show Perl 6 had the feature first.

---

## Q1. Perl 5.10.0 (2007-12-18): what the docs credit to Perl 6, and what became of smartmatch and given/when

### Takeaway
perl5100delta itself credits nothing in the language to Perl 6. It mentions Perl 6 only for internals: MAD, a Perl 5→6 converter, and `SVt_BIND`, for "Perl 6 on 5". The credit appears in the feature and syntax docs that shipped with 5.10.0:
- feature.pm called `say` and `given/when` "the Perl 6 say function" and "the Perl 6 given/when construct".
- perlsyn said switch was "closely based on the Perl 6 proposal" and had a "Differences from Perl 6" section.
- perlre called `(*THEN)` and `(*COMMIT)` Perl 6 constructs.

Smartmatch and given/when had a turbulent life. Smartmatch was redesigned in 5.10.1. Both were made experimental in 5.18, and a 2017 redesign was reverted. `switch` was dropped from the :5.36 bundle, both were deprecated in 5.38 with removal set for 5.42, and they were removed from blead in July 2024. That removal was reverted in February 2025. **In 5.42.0 they were kept behind the feature flags `switch` and `smartmatch`, with no deprecation warning, and the removal was "indefinitely postponed".** perlexperiment now files them under "Features abandoned in place".

### Cited Findings

**Release date**
- Perl 5.10.0 was released 2007-Dec-18 and 5.10.1 on 2009-Aug-22 (perlhist table). — [perlhist.pod](https://github.com/Perl/perl5/blob/blead/pod/perlhist.pod)
- The Perl Foundation's announcement, as reported on 2007-12-21, said only that the porters had "taken features and inspiration from the ambitious Perl 6 project". It named no features. — [Linux.com, 21 Dec 2007](https://www.linux.com/?p=512004)

**What perl5100delta itself says (no Perl 6 credit for any language feature)**
- The `feature` pragma enables "new syntax that would break Perl's backwards-compatibility". It lists `switch`, `say` and `state` with no mention of Perl 6. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- `say()` is "similar to print(), but that implicitly appends a newline" (Robin Houston). No Perl 6 credit. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- Defined-or `//` and `//=`: "A new operator // (defined-or) has been implemented". No Perl 6 credit. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- Switch and smart match: "Perl 5 now has a switch statement… given, when, and default… This kind of match is called smart match… via the new ~~ operator… This feature was contributed by Robin Houston." No Perl 6 credit in the delta. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- `state` variables (Rafael Garcia-Suarez, Nicholas Clark) are described as "static" variables. No Perl 6 credit. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- Lexical `my $_` (Rafael Garcia-Suarez), new in 5.10. No Perl 6 credit, although Perl 6's `$_` is lexical. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- Regex features, all by Yves Orton: recursive patterns `(?PARNO)`, named captures `(?<NAME>…)` / `\k<NAME>` / `%+` `%-`, possessive quantifiers, backtracking-control verbs, `\g{N}` and `\K`. The delta compares them to **PCRE** and **.NET**, not to Perl 6. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- `UNIVERSAL::DOES()` (chromatic) was added because "DOES() has been designed to be overridden when module authors use other types of relations between classes". In practice that means roles, but the delta does not mention Perl 6. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- The `mro` pragma with C3 (Brandon Black). No Perl 6 credit in the delta. — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)

**Perl 6 mentions that are in perl5100delta (internals only)**
- MAD: "a still-in-development work leading to a Perl 5 to Perl 6 converter" (Larry Wall, Nicholas Clark). — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- `SVt_BIND`: "A new type SVt_BIND has been added, in readiness for the project to implement Perl 6 on 5. There deliberately is no implementation yet". — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)
- MAD was removed in 5.22. It had been "an unmaintained attempt… so that automatic conversion of Perl 5 to Perl 6 would have been easier", and it "had probably seriously diverged on both Perl 5 and Perl 6 sides". — [perl5220delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5220delta.pod)
- `no VERSION`, new in 5.10.0. perlfunc today says it "was added during early designs of the Raku language (formerly "Perl 6"), so that a Perl 5 program could begin `no 6;`". — [perl5100delta.pod](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod); [perlfunc.pod](https://github.com/Perl/perl5/blob/blead/pod/perlfunc.pod)

**Where the 5.10.0 docs do credit Perl 6**
- feature.pm at the 5.10.0 tag says `use feature 'switch'` enables "the Perl 6 given/when construct" and `use feature 'say'` enables "the Perl 6 say function". — [lib/feature.pm @ perl-5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/lib/feature.pm)
- perlsyn at 5.10.0 says the switch feature "is closely based on the Perl 6 proposal". Its section "Differences from Perl 6" adds that the Perl 5 constructs "are not absolutely identical to their Perl 6 analogues", and that "the table of smart matches is not identical to that proposed by the Perl 6 specification". — [pod/perlsyn.pod @ perl-5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/pod/perlsyn.pod)
- perlre at 5.10.0 says `(*THEN)` "is similar to the "cut group" operator :: from Perl 6" and `(*COMMIT)` "is the Perl 6 "commit pattern" <commit> or :::". — [pod/perlre.pod @ perl-5.10.0](https://github.com/Perl/perl5/blob/perl-5.10.0/pod/perlre.pod)

**How the wording changed later**
- By 5.20, feature.pm read "the Perl 6 style say function", and perlsyn said switch "is loosely based on an old version of a Perl 6 proposal, but it no longer resembles the Perl 6 construct". — [lib/feature.pm @ v5.20.0](https://github.com/Perl/perl5/blob/v5.20.0/lib/feature.pm); [pod/perlsyn.pod @ v5.20.0](https://github.com/Perl/perl5/blob/v5.20.0/pod/perlsyn.pod)
- Commits on 2020-05-30 changed "Perl 6 -> Raku where appropriate" (H.Merijn Brand). Sawyer X then temporarily put "Perl 6" back in mro.pm and feature.pm, because changing them would force a version bump. "Last bits of Perl 6 -> Raku" followed on 2020-09-26. — [commit 17b3504](https://github.com/Perl/perl5/commit/17b35041bdff13ad8301eae5df5f75578f910ce1); [commit 6cd75cd](https://github.com/Perl/perl5/commit/6cd75cd17c4f67caa33d73b42d0d0e2535818367); [commit 5d6cc14](https://github.com/Perl/perl5/commit/5d6cc1466e254bcfa41c95c42fad1818091cae64)
- feature.pm in 5.32.0 still says "Perl 6 style". From 5.34.0 onward it says "the Raku-inspired say function" and "the Raku given/when construct". — [lib/feature.pm @ v5.32.0](https://github.com/Perl/perl5/blob/v5.32.0/lib/feature.pm); [lib/feature.pm @ v5.34.0](https://github.com/Perl/perl5/blob/v5.34.0/lib/feature.pm); [lib/feature.pm (blead)](https://github.com/Perl/perl5/blob/blead/lib/feature.pm)
- Current perlsyn still has "Differences from Raku". It says the smartmatch table "is not identical to that proposed by the Raku specification… also because the Raku spec has changed since Perl 5 rushed into early adoption". It recommends Raku's "(parallelizable) any operator" in place of `@array ~~ "red"`. — [perlsyn.pod (blead)](https://github.com/Perl/perl5/blob/blead/pod/perlsyn.pod)
- Current perlre still credits Raku for `(*THEN)` (`::`) and `(*COMMIT)` (`<commit>` / `:::`). — [perlre.pod (blead)](https://github.com/Perl/perl5/blob/blead/pod/perlre.pod)

**Fate of smartmatch and given/when, dated**

| When | What happened | Source |
|---|---|---|
| 5.10.0, 2007-12-18 | `given`/`when`/`default` and `~~` introduced | [perl5100delta](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod) |
| 5.10.1, 2009-08-22 | Smartmatch "significantly revised" | [perl5180delta](https://github.com/Perl/perl5/blob/blead/pod/perl5180delta.pod); [perlexperiment](https://github.com/Perl/perl5/blob/blead/pod/perlexperiment.pod) ("Modified in Perl 5.10.1, 5.12.0") |
| 5.18.0, 2013-05-18 | "The smartmatch family of features are now experimental": warnings on `~~`, `given` and `when`. `given` now aliases the global `$_`. Lexical `$_` also became experimental. | [perl5180delta](https://github.com/Perl/perl5/blob/blead/pod/perl5180delta.pod) |
| 5.24.0, 2016-05-09 | Lexical `$_` removed. perlsyn: "lexicalized $_ itself was removed in Perl 5.24" | [perlsyn (blead)](https://github.com/Perl/perl5/blob/blead/pod/perlsyn.pod) |
| Dev 5.27.x, Nov–Dec 2017 | Zefram's "regularise when" redesign landed (2017-11-21), then was reverted on 2017-12-29: "The pumpking has determined that the CPAN breakage caused by changing smartmatch [perl #132594] is too great for the smartmatch changes to stay in for 5.28." | [commit b69ef01](https://github.com/Perl/perl5/commit/b69ef0135bfebf0c32023bcb970dbb95957b32e7); [commit 7896dde](https://github.com/Perl/perl5/commit/7896dde7482a2851e73f0ac2c32d1c71f6e97dca) |
| 5.36.0, 2022-05-27 | `switch` removed from the `use v5.36` bundle ("If you want to use it (against our advice), you'll have to enable it explicitly") | [perl5360delta](https://github.com/Perl/perl5/blob/blead/pod/perl5360delta.pod) |
| 5.38.0, 2023-07-02 | "we are declaring the experiment a failure… These features will be entirely removed from perl in v5.42.0." Moved from experimental to deprecated (`deprecated::smartmatch`). | [perl5380delta](https://github.com/Perl/perl5/blob/blead/pod/perl5380delta.pod) |
| blead, 2024-07-18 | Removal committed: "switch removal: remove smartmatch" (Tony Cook) | [commit cb2167d](https://github.com/Perl/perl5/commit/cb2167d3785e61e23819ca2c58ac1e89d5e4bf3b) |
| blead, 2025-02-16 | Removal reverted: "re-enable (feature guarded) switch and re-enable smartmatch with a new feature guard. Also remove any deprecation warnings", and "add the smartmatch feature" | [commit a215a77](https://github.com/Perl/perl5/commit/a215a77dd95c2472a6395310fd80580c750260c2); [commit 15cbd20](https://github.com/Perl/perl5/commit/15cbd20848a892cb2f52f280d0dacd2df4efc953); [commit ed1ae14](https://github.com/Perl/perl5/commit/ed1ae14707cc8efc10ac8beaff8b62152be1f65f) |
| 5.42.0, 2025-07-03 | "Switch and Smart Match operator kept, behind a feature… After extensive discussion their removal has been indefinitely postponed. Using them no longer produces a deprecation warning." `switch` is in the bundles for 5.9.5–5.34. `smartmatch` is a new feature, on by default and in all bundles up to 5.40, and off in the 5.41+ bundle (so `use v5.42` disables `~~`). | [perl5420delta](https://github.com/Perl/perl5/blob/blead/pod/perl5420delta.pod); [feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm) |
| Now (blead 5.45.x, Nov 2026) | perlexperiment, "Features abandoned in place": "experiments that are considered failures, but due to mass use have been left in place… The switch and smartmatch features are likely to be the only ones". Both entries say "Scheduled for removal in 5.42.0, but this was reversed." perlsyn: "The switch feature is discouraged for new code and is retained for backward compatibility." | [perlexperiment.pod](https://github.com/Perl/perl5/blob/blead/pod/perlexperiment.pod); [perlsyn.pod](https://github.com/Perl/perl5/blob/blead/pod/perlsyn.pod) |

**Perl 6 design dates for the 5.10 features, showing Perl 6 had them first**
- Apocalypse 3 (Larry Wall, 2001-10-02), under "Binary //": "A binary // operator is the defaulting operator… $a // $b is short for defined($a) ?? $a :: $b… $pi //= 3". It also lists "Binary ~~". — [Apocalypse 3, perl.com](https://www.perl.com/pub/2001/10/02/apocalypse3.html)
- Apocalypse 4 (Larry Wall, 2002-01-15), on RFC 022 "Control flow: Builtin switch statement": "After thrashing over several options with Damian and other folks, we've settled on using given instead of switch, and when instead of case". — [Apocalypse 4, perl.com](https://www.perl.com/pub/2002/01/15/apo4.html)

### Inferences
- In 5.10, `say` and `given/when/~~` are the only core features the core docs themselves call Perl 6 imports, through feature.pm and perlsyn. Over time the docs went from crediting Perl 6 ("closely based") to distancing from it ("loosely based on an old version… no longer resembles"). `//` and `~~`-as-spelling appear in Apocalypse 3 six years before 5.10, so their Perl 6 lineage is very likely. Perl 5 never wrote it down for `//`, though, and a Perl 5 "dor" patch circulated independently (not verified here).
- Named captures, recursion and possessive quantifiers were framed against PCRE/.NET, not Perl 6. Treat them as "looks similar".
- For a Raku implementer, given/when is the cautionary tale. Perl 5 adopted the Perl 6 design early ("rushed into early adoption", per perlsyn). The Perl 6 spec then moved on, and the Perl 5 version became an "abandoned in place" experiment that could not be removed because of CPAN use.

### Gaps
- I found no p5p mail or interview in which Robin Houston or Rafael Garcia-Suarez states the Perl 6 origin of `say`, `state` or `//`. The Linux.com/TPF credit is generic. Rafael's own release announcement (use.perl.org, Dec 2007) was not retrieved.
- I did not verify which dev release first shipped the 2024 removal, or the first after the 2025 revert. By perlhist dates they are probably 5.41.2 (2024-07-20) and 5.41.9 (2025-02-24), but that is inference only.
- I could not confirm from the deltas what happened to `SVt_BIND` after 5.10. No later perldelta mentions it by name.

---

## Q2. Later core features (5.12 → 5.44, plus 5.45.x dev): which are credited to Perl 6/Raku and which only look similar

### Takeaway
Two places in the 5.12–5.44 perldeltas mention Perl 6/Raku: 5.24.0's "Hashbang redirection to Perl 6", reverted in 5.24.1, and the 5.32 obituary of Jeff Goff. All the big modern features have no Raku attribution in their perldeltas: signatures, postfix deref, `<<~`, chained comparisons, `isa`, try/catch, defer, builtin, n-at-a-time `for`, `class`, `any`/`all`, and named signature parameters. Raku shows up in the design records instead:
- PPC0004 renamed `defer` away from `LEAVE` because its semantics differ from Raku's LEAVE.
- PPC0027 explicitly rejects making `any`/`all` junctions.
- PPC0005 ("everything slices", `@a[*]`, "cribbed from Raku") was rejected.
- PPC0021 (`?->`, citing Raku among others) is still a draft.
- Ovid named Raku as one of the "biggest influences" on Corinna, the design behind the `class` feature.
- 5.44's named parameters (`:$alpha`) cite CPAN prior art whose own docs credit Perl 6.

### Cited Findings

**Dated table of later core features**

| Feature | Version (date) | Raku/Perl 6 credit in Perl docs? | Later fate | Source |
|---|---|---|---|---|
| `package NAME VERSION` | 5.12.0 (2010-04-12) | None. Rationale is `$VERSION` parsing. | Kept | [perl5120delta](https://github.com/Perl/perl5/blob/blead/pod/perl5120delta.pod) |
| Yada yada `...` | 5.12.0 | None. "intended to mark placeholder code". The 5.12 perlop "Yada Yada Operator" section has no Perl 6 mention. | Kept | [perl5120delta](https://github.com/Perl/perl5/blob/blead/pod/perl5120delta.pod); [perlop @ v5.12.0](https://github.com/Perl/perl5/blob/v5.12.0/pod/perlop.pod) |
| `when` as statement modifier; `//` in `when` | 5.12.0 | None (switch family) | As in Q1 | [perl5120delta](https://github.com/Perl/perl5/blob/blead/pod/perl5120delta.pod) |
| `Switch.pm` (CPAN/core module) | Deprecated 5.12, removed from core 5.14.0 (2011-05-14) | Not applicable (see Q5) | Removed from core | [perl5120delta](https://github.com/Perl/perl5/blob/blead/pod/perl5120delta.pod); [perl5140delta](https://github.com/Perl/perl5/blob/blead/pod/perl5140delta.pod) |
| `package NAME BLOCK` | 5.14.0 | None | Kept | [perl5140delta](https://github.com/Perl/perl5/blob/blead/pod/perl5140delta.pod) |
| `__SUB__` (cf. Raku `&?ROUTINE`) | 5.16.0 (2012-05-20) | None | Kept | [perl5160delta](https://github.com/Perl/perl5/blob/blead/pod/perl5160delta.pod) |
| Lexical subs `my sub` | 5.18.0 experimental; stable 5.26.0 | None | Stable | [perl5180delta](https://github.com/Perl/perl5/blob/blead/pod/perl5180delta.pod); [feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm) |
| Subroutine signatures | 5.20.0 (2014-05-27) experimental; stable 5.36.0 (2022-05-27) | None in perldelta, perlsub or feature.pm | Stable. `//=`/`\|\|=` defaults in 5.38. Named params in 5.44 (below). | [perl5200delta](https://github.com/Perl/perl5/blob/blead/pod/perl5200delta.pod); [feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm); [perl5380delta](https://github.com/Perl/perl5/blob/blead/pod/perl5380delta.pod) |
| Postfix deref `$r->@*` | 5.20.0 experimental (committed 2013-10-05 by Father Chrysostomos, docs by Ricardo Signes); stable 5.24.0 | None | Stable | [perl5240delta](https://github.com/Perl/perl5/blob/blead/pod/perl5240delta.pod); [commit 89f3591](https://github.com/Perl/perl5/commit/89f3591148a8f1ca21a2faaed697cfa194632816) |
| Hashbang redirection to Perl 6 | 5.24.0 (2016-05-09) | **Yes.** "To improve compatibility with Perl 6 this behavior has been extended to also redirect if "perl" is followed by "6"." | **Reverted in 5.24.1 (2017-01-14)** "because it broke in cases such as #!/opt/perl64/bin/perl" | [perl5240delta](https://github.com/Perl/perl5/blob/blead/pod/perl5240delta.pod); [perl5241delta](https://github.com/Perl/perl5/blob/blead/pod/perl5241delta.pod) |
| Indented heredoc `<<~` | 5.26.0 (2017-05-30) | None | Kept | [perl5260delta](https://github.com/Perl/perl5/blob/blead/pod/perl5260delta.pod) |
| Chained comparisons `$x < $y <= $z` | 5.32.0 (2020-06-20). Commit by Zefram 2020-03-12, message just "chained comparisons". | None | Kept | [perl5320delta](https://github.com/Perl/perl5/blob/blead/pod/perl5320delta.pod); [commit 02b85d3](https://github.com/Perl/perl5/commit/02b85d3dab092d678cfc958a2dc252405333ed25) |
| `isa` operator | 5.32.0 experimental; stable 5.36 | None | Stable | [perl5320delta](https://github.com/Perl/perl5/blob/blead/pod/perl5320delta.pod); [feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm) |
| try/catch | 5.34.0 (2021-05-20) experimental; `finally` 5.36; try/catch stable 5.40.0 (2024-06-09), `finally` still experimental | None | Stable (without `finally`) | [perl5340delta](https://github.com/Perl/perl5/blob/blead/pod/perl5340delta.pod); [perl5400delta](https://github.com/Perl/perl5/blob/blead/pod/perl5400delta.pod); [feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm) |
| `defer {}` | 5.36.0 experimental | None in delta. **PPC0004:** "originally called LEAVE {}, which was rejected because its semantics don't match the Raku language feature of the same name. It was then renamed to FINALLY {}…" | Still experimental | [perl5360delta](https://github.com/Perl/perl5/blob/blead/pod/perl5360delta.pod); [PPC0004](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0004-defer-block.md) |
| `builtin::` (true/false, is_bool, trim, indexed, weaken…) | 5.36.0 experimental; stable 5.40.0; `use v5.40` imports them | None. PPC0009 and PPC0016 cite List::Util / Scalar::Util. | Stable | [perl5360delta](https://github.com/Perl/perl5/blob/blead/pod/perl5360delta.pod); [perl5400delta](https://github.com/Perl/perl5/blob/blead/pod/perl5400delta.pod); [PPC0009](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0009-builtin-namespace.md) |
| `for my ($k,$v) (%h)` (cf. Raku `for %h.kv -> $k, $v`) | 5.36.0 experimental; stable 5.40.0 | None (PPC0001) | Stable. 5.44 combines it with refaliasing. | [perl5360delta](https://github.com/Perl/perl5/blob/blead/pod/perl5360delta.pod); [perl5400delta](https://github.com/Perl/perl5/blob/blead/pod/perl5400delta.pod); [PPC0001](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0001-n-at-a-time-for.md) |
| `class` / `field` / `method` / `ADJUST` / `:isa` / `:param` | 5.38.0 (2023-07-02) experimental | None in delta or perlclass. feature.pm: implements "the "new" object system, previously codenamed "Corinna"". See Q4 for Ovid's Raku credit. | Still experimental (see Q4 follow-ups) | [perl5380delta](https://github.com/Perl/perl5/blob/blead/pod/perl5380delta.pod); [perlclass.pod](https://github.com/Perl/perl5/blob/blead/pod/perlclass.pod); [feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm) |
| `__CLASS__`, `field :reader` | 5.40.0 | None | Experimental | [perl5400delta](https://github.com/Perl/perl5/blob/blead/pod/perl5400delta.pod) |
| `^^` logical xor (Raku also has infix `^^`) | 5.40.0. `^^=` added in 5.42.0. | None. Rationale is "completing the set" of `&&`/`\|\|`. | Kept | [perl5400delta](https://github.com/Perl/perl5/blob/blead/pod/perl5400delta.pod); [perl5420delta](https://github.com/Perl/perl5/blob/blead/pod/perl5420delta.pod) |
| `any { } @list` / `all { } @list` (features `keyword_any`, `keyword_all`) | 5.42.0 (2025-07-03) experimental | **Credited to List::Util, not Raku.** "These are inspired by the same-named functions in the List::Util module". PPC0027 lists "Keywords as Junctions" under **Rejected Ideas**: "as junctions behave like values, they do not require special syntax… can be provided by regular function-call syntax from regular modules". | Experimental | [perl5420delta](https://github.com/Perl/perl5/blob/blead/pod/perl5420delta.pod); [PPC0027](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0027-any-and-all.md) |
| `field :writer`, `my method`, `->&` lexical method call | 5.42.0 | None. PPC0033 cites Object::Pad::LexicalMethods. | Experimental | [perl5420delta](https://github.com/Perl/perl5/blob/blead/pod/perl5420delta.pod); [PPC0033](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0033-ampersand-method-calls.md) |
| Named parameters in signatures `sub f ($x, :$alpha, :$beta = undef)` (Raku's `:$name` syntax) | Introduced in dev 5.43.5 (2025-11-20). **5.44.0 (2026-07-15)**, experimental (`experimental::signature_named_parameters`). | None to Raku. PPC0024 (Paul Evans) cites Function::Parameters, Kavorka and Method::Signatures, which "use the leading-colon syntax… It would appear we are in good company here." Kavorka and Method::Signatures credit Perl 6 (see Q3). | Experimental | [perl5440delta](https://github.com/Perl/perl5/blob/blead/pod/perl5440delta.pod); [perlexperiment](https://github.com/Perl/perl5/blob/blead/pod/perlexperiment.pod); [PPC0024](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0024-signature-named-parameters.md); [perlhist](https://github.com/Perl/perl5/blob/blead/pod/perlhist.pod) |
| Class fields `$_bar :param :reader` drop the leading underscore in derived names | Dev 5.45.2 (2026-08-20) | "This is how Object::Pad works, too." | Dev only | [perl5452delta](https://github.com/Perl/perl5/blob/blead/pod/perl5452delta.pod) |
| Undef-aware equality `equ` / `===` (four operators) | Dev 5.45.3 (2026-09-24) | None. PPC0030 cites Syntax::Keyword::Match. Raku's `===` (value identity) has different semantics. | Dev only (PPC0030 status in repo: "Exploratory") | [perl5453delta](https://github.com/Perl/perl5/blob/blead/pod/perl5453delta.pod); [PPC0030](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0030-undef-aware-equality.md) |

**Other PPCs (Perl's proposal process since 2021) that mention Raku**
- PPC0005 "Everything Slices" (Ricardo Signes): "The use of `*` is cribbed from Raku, where it is used a a multipurpose placeholder". **Status: Rejected.** — [PPC0005](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0005-everything-slices.md)
- PPC0021 "Optional Chaining" `?->` (Breno G. de Oliveira) cites "JavaScript, Kotlin, C#, Swift, TypeScript, Groovy, PHP, Raku, Ruby and Rust". **Status: Draft.** — [PPC0021](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0021-optional-chaining-operator.md)
- PPC0031 "Meta-operator Flags" (Paul Evans, Exploratory) proposes `eq:u`, `==:u`. It speaks of an "`in:OP` hyper-operator" and "adverb-like" flags, all Raku vocabulary, but it does not name Raku. — [PPC0031](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0031-metaoperator-flags.md)
- PPC0025 "Perl 5 is Perl" (Aristotle Pagaltzis, Draft) argues that "Perl and Raku can and should stay in touch and take inspiration from each other". — [PPC0025](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0025-perl-version.md)
- A clone of the PPCs repo at its 2026-08-26 HEAD contains no other "Raku" or "Perl 6" mentions in PPC texts, apart from the PPC docs' warning about "The Perl 6 RFC failure". — [PPCs repo](https://github.com/Perl/PPCs); [docs/motivation.md](https://github.com/Perl/PPCs/blob/main/docs/motivation.md)

**Other Perl 6/Raku mentions in 5.12–5.44 deltas**
- perl5320delta mentions Raku only in the obituary of Jeff Goff, "an integral part of the Perl and Raku" communities. — [perl5320delta](https://github.com/Perl/perl5/blob/blead/pod/perl5320delta.pod)

**Perl 6 had these first (looks similar; Perl 5 does not credit it)**
- Chained comparisons: Apocalypse 3 (2001-10-02) accepted RFC 025 "Operators: Multiway comparisons", so that `0 <= $x < 10` means `0 <= $x && $x < 10` with `$x` evaluated once. — [Apocalypse 3](https://www.perl.com/pub/2001/10/02/apocalypse3.html)
- C3 MRO: Perl's `mro` docs say C3 came from Dylan, was adopted by Python 2.3, and "Most recently it has been adopted as the "canonical" MRO for Raku classes". So Raku is a fellow adopter, not the source. — [ext/mro/mro.pm](https://github.com/Perl/perl5/blob/blead/ext/mro/mro.pm)

### Inferences
- From 2010 to 2026, the design culture of p5p and the PPCs consistently routes features through CPAN prototypes rather than through Raku. Examples are List::Util → `any`/`all`, Syntax::Keyword::Try → try/catch, Syntax::Keyword::Defer → `defer`, Object::Pad → `class`, and Function::Parameters/Kavorka/Method::Signatures → named params. Where Raku is mentioned, it is often to diverge from it: `defer` vs `LEAVE`, `any`/`all` vs junctions.
- Several Raku-looking features reached Perl 5 one or two decades after their Perl 6 design, with no stated credit:
  - chained comparisons (A03 2001 → 5.32 2020)
  - `:$name` named params (S06 → 5.44 2026)
  - `^^`
  - lexical subs
  - `for` with multiple loop variables
  - `__SUB__`

  The report should label all of these "looks similar", not "from Raku".

### Gaps
- I found no p5p or author statement linking these to Perl 6: signatures (Peter Martini and Zefram, 5.20), postfix deref (Ricardo Signes), yada `...`, indented heredocs (Ruby's `<<~` is the more likely model, but that is unverified), `isa`, or `__SUB__`. The mailing-list archives were not searched exhaustively.
- I found no statement on why Perl 5.40's `^^` uses the same token as Raku's `^^`.

---

## Q3. CPAN: Perl6::* modules and other Perl 6-derived modules, with dates and attributions

### Takeaway
2001 to 2009 was the heyday of "Perl 6 in Perl 5" CPAN modules. Damian Conway's family included Perl6::Variables, Placeholders, Currying, Form, Export, Slurp, Gather, Say, Rules, Builtins, Export::Attrs and Perl6::Perldoc. Others came from Brent Dax, Luke Palmer, Chip Salzenberg, Steve Walters (SWALTERS), Ovid, Gaal Yahas, Moritz Lenz and the Pugs team. Then came the signature and grammar modules that state a Perl 6 inspiration:
- Method::Signatures (2007)
- MooseX::Method::Signatures (2008)
- Regexp::Grammars (2009)
- Kavorka (2013)

Only Perl6::Say's feature went into core (5.10 `say`), and its own docs say so. Junction modules (Perl6::Junction, Syntax::Keyword::Junction) remain CPAN-only. Core explicitly declined junction semantics in 2025 (PPC0027).

### Cited Findings

**First-release dates from MetaCPAN** (API: `fastapi.metacpan.org/v1/release/_search?q=distribution:<Dist>&sort=date:asc`, see [MetaCPAN](https://metacpan.org)). Each row also cites the module's own doc on what it implements or credits.

| Dist (author) | First release | Last release | What it says |
|---|---|---|---|
| [Quantum-Superpositions](https://metacpan.org/pod/Quantum::Superpositions) (DCONWAY) | 2000-08-09 | 2.03, 2018-07-08 (LEMBARK) | "adds two new operators to Perl: any and all". **No Perl 6 mention, because it predates Perl 6** (see Q5). |
| [Switch](https://metacpan.org/pod/Switch) (DCONWAY) | 2000-08-07 | 2.17, 2014-03-18 | Later versions: "Perl 6 will provide a built-in switch statement… In Perl 6 switch will be spelled given, and case will be pronounced when… available via… `use Switch 'Perl6';`" |
| Perl6-Variables (DCONWAY) | 2001-05-17 | 0.02_002, 2004 (SWALTERS, backpan) | (doc not fetched) |
| Perl6-Interpolators (BRENTDAX) | 2001-08-02 | 2001-08-10 | (doc not fetched) |
| [Perl6-Parameters](https://metacpan.org/pod/Perl6::Parameters) (BRENTDAX) | 2001-08-07 | 2002-08-17 | "simulates Perl 6's named" parameters / "Perl 6-style prototypes with named parameters" |
| [Perl6-Currying](https://metacpan.org/pod/Perl6::Currying) (DCONWAY) | 2002-05-14 | 2002-05-29 | "lets you try out the new Perl 6 explicit higher-order function syntax… prebind" |
| [Perl6-Placeholders](https://metacpan.org/pod/Perl6::Placeholders) (DCONWAY) | 2002-05-29 | 0.07, 2005-10-08 (LPALMER) | "Perl 6 implicitly declared parameters for Perl 5": `$^name` placeholders |
| [Perl6-Classes](https://metacpan.org/pod/Perl6::Classes) (LPALMER) | 2003-04-06 | same | "(somewhat) Perl 6-style classes… has keyword… $.this… submethods" |
| [Perl6-Binding](https://metacpan.org/pod/Perl6::Binding) (KVAIL) | 2003-04-28 | 2004-05-23 | "implement Perl6 aliasing features": the `:=` operator |
| [autobox](https://metacpan.org/pod/autobox) (CHOCOLATE) | 2003-08-04 | 2024-09-03 | Methods on native values. **No Perl 6 mention in its doc** (looks similar only). |
| [Class-Roles](https://metacpan.org/pod/Class::Roles) (CHROMATIC) | 2003-12-12 | 2003-12-14 | "provides a Perl 5 implementation of Perl 6 roles" |
| [Perl6-Form](https://metacpan.org/pod/Perl6::Form) (DCONWAY) | 2004-02-27 | 0.090, 2019-01-09 | "Implements the Perl 6 'form' built-in" ("Perl 6 doesn't have a format keyword… Instead it has a Form.pm module") |
| [Perl6-Export](https://metacpan.org/pod/Perl6::Export) (DCONWAY) | 2004-02-27 | 2015-11-23 | "Implements the Perl 6 'is export(...)' trait" |
| [Perl6-Slurp](https://metacpan.org/pod/Perl6::Slurp) (DCONWAY) | 2004-03-01 | 2014-02-22 | "Implements the Perl 6 'slurp' built-in" |
| [Perl6-Gather](https://metacpan.org/pod/Perl6::Gather) (DCONWAY) | 2004-03-09 | 2009-12-23 | "Implements the Perl 6 'gather/take' control structure" |
| [Perl6-Say](https://metacpan.org/pod/Perl6::Say) (DCONWAY) | 2004-03-27 | 0.16, 2010-07-14 (CHORNY) | "You don't need this module. The Perl 6 "say" function is available in Perl 5.10 by saying use feature 'say'" |
| [Perl6-Rules](https://metacpan.org/pod/Perl6::Rules) (DCONWAY) | 2004-04-12 | 0.03 the same day | "Implements (most of) the Perl 6 regex syntax… translating them back to Perl 5 regexes via a source filter" |
| Perl6-Contexts (SWALTERS) | 2004-05-20 | 2005-06-19 (backpan) | (doc not fetched) |
| [Perl6-Pugs](https://metacpan.org/pod/Perl6::Pugs) (AUTRIJUS/AUDREYT) | 2005-02-06 | 6.2.13, 2006-10-17 | "Pugs is an implementation of Perl 6… written in Haskell" |
| [Perl6-Attributes](https://metacpan.org/pod/Perl6::Attributes) (LPALMER) | 2005-03-13 | 2005-09-17 | "Perl 6-like member variable syntax… takes the Perl 6 secondary sigil "." and translates it" |
| Perl6-Subs (CHIPS) | 2005-03-25 | 2005-04-13 (backpan) | (doc not fetched) |
| [Perl6-Junction](https://metacpan.org/pod/Perl6::Junction) (CFRANKS) | 2005-05-03 | 1.60000, 2013-08-21 | "Perl6 style Junction operators in Perl5… Inspired by the Perl6 design docs" (Exegesis 6). "a limited subset of the functionality of Quantum::Superpositions" |
| [Contextual-Return](https://metacpan.org/pod/Contextual::Return) (DCONWAY) | 2005-05-21 | 2017-04-14 | No Perl 6 mention in doc |
| [Perl6-Builtins](https://metacpan.org/pod/Perl6::Builtins) (DCONWAY) | 2005-05-30 | 2005-05-30 | "Provide Perl 5 versions of the new Perl 6 builtins", e.g. `system` returning true on success |
| [Perl6-Export-Attrs](https://metacpan.org/pod/Perl6::Export::Attrs) (DCONWAY) | 2005-08-05 | 2018-02-17 | "The Perl 6 'is export(...)' trait as a Perl 5 attribute" |
| [Class-C3](https://metacpan.org/pod/Class::C3) (STEVAN) | 2005-08-07 | 2020-11-09 | C3 "adopted as the 'canonical' MRO for Perl 6 classes, and the default MRO for Parrot objects". Became core `mro` in 5.10. |
| [Perl6-Roles](https://metacpan.org/pod/Perl6::Roles) (RKINYON) | 2005-10-28 | same | "an implementation of current state of Perl6 roles in Perl5… draws very heavily from both the Class::Role and Class::Roles modules" |
| v6-alpha (AUDREYT → FGLOCK) | 2006-07-06 | 2007-05-08 (backpan) | Pugs-derived Perl 6-on-Perl 5 |
| [v6](https://metacpan.org/pod/v6) (FGLOCK) | 2007-05-09 | 0.047, 2016-07-28 | "A Perl 6 implementation… front-end to the "Perlito" Perl6 compiler" |
| [Perl6-Perl](https://metacpan.org/pod/Perl6::Perl) (DANKOGAI) | 2006-12-23 | same | (Perl 6-related; not analysed) |
| [Perl6-Take](https://metacpan.org/pod/Perl6::Take) (GAAL) | 2007-01-30 | 2007-01-30 | "gather/take in Perl 5" |
| [Perl6-Perldoc](https://metacpan.org/pod/Perl6::Perldoc) (DCONWAY) | 2007-04-12 | 2014-10-28 | "Use Perl 6 documentation in a Perl 5 program": strips Perl 6 Pod "as specified in Synopsis 26" |
| [Perl6-Caller](https://metacpan.org/pod/Perl6::Caller) (OVID) | 2007-04-21 | 2009-10-18 | "OO caller() interface". Perl 6 only in the name. |
| [Method-Signatures](https://metacpan.org/pod/Method::Signatures) (MSCHWERN) | 2007-12-26 | 20170211 (BAREFOOT) | "signatures, very similar to Perl 6 signatures… mostly a straight subset of Perl 6 signatures". Has a section "Differences from Perl 6". |
| [Perl6-Signature](https://metacpan.org/pod/Perl6::Signature) (GAAL) | 2008-01-10 | 2013-11-23 | "models routine signatures as specified in Synopsis 6" |
| [Perl6-GatherTake](https://metacpan.org/pod/Perl6::GatherTake) (MORITZ) | 2008-04-12 | 2008-07-17 | "Perl 6 like gather { take() }… lazy list" |
| [Perl6-Str](https://metacpan.org/pod/Perl6::Str) (MORITZ) | 2008-07-21 | 2012-08-18 | "Grapheme level string implementation… API similar to that of Perl 6" |
| [MooseX-Method-Signatures](https://metacpan.org/pod/MooseX::Method::Signatures) (FLORA) | 2008-09-24 | 2015-10-07 | "The signature syntax is heavily based on Perl 6" |
| [MooseX-Declare](https://metacpan.org/pod/MooseX::Declare) (FLORA) | 2008-10-19 | 2015-08-16 | No Perl 6 mention in its main doc |
| [Perl6-Pod](https://metacpan.org/pod/Perl6::Pod) (ZAG) | 2009-05-25 | 0.75, 2025-11-09 | Perl 6 Pod tooling (not analysed) |
| [Regexp-Grammars](https://metacpan.org/pod/Regexp::Grammars) (DCONWAY) | 2009-07-24 | 1.058, 2022-09-15 | Subrule calls in angle brackets "(like in Perl 6)". `<.SUBRULE>` "is stolen directly from Perl 6". |
| [Perl6-Feeds](https://metacpan.org/pod/Perl6::Feeds) (ASG) | 2009-10-11 | 2009-11-01 | "implements perl6 feed operators in perl5 via source filter" |
| [Function-Parameters](https://metacpan.org/pod/Function::Parameters) (MAUKE) | 2009-12-14 | 2.002006, 2025-08-16 | **No Perl 6 mention in its doc** |
| [Syntax-Keyword-Junction](https://metacpan.org/pod/Syntax::Keyword::Junction) (FREW) | 2010-11-07 | 2024-08-20 | "Inspired by the Perl 6 design docs… a fork of Perl6::Junction… to avoid the incendiary name containing Perl 6" |
| [Kavorka](https://metacpan.org/pod/Kavorka) (TOBYINK) | 2013-10-03 | 0.039, 2018-07-27 | "The syntax provided by Kavorka is largely inspired by Perl 6". "Inspirations: http://perlcabal.org/syn/S06.html, Function::Parameters, Method::Signatures" |
| [Inline-Perl6](https://metacpan.org/pod/Inline::Perl6) (NINE) | 2015-02-09 | 2022-02-21 | Embeds Rakudo in Perl 5. A bridge, not a backport. |

### Inferences
- Perl6::Say was the clearest case of CPAN to core: its own docs send 5.10 users to `use feature 'say'`. Conway's Switch.pm 'Perl6' mode was the CPAN dress rehearsal for 5.10's given/when.
- Grammars never reached the core regex engine as Perl 6 rules. Perl6::Rules (2004, source filter, only three days of releases) gave way to Regexp::Grammars (2009). Regexp::Grammars builds on 5.10's recursion and named captures, and borrows Perl 6 surface syntax: `<rule>` and `<.rule>`.
- Signatures went from Perl 6 S06 → Method::Signatures (2007), MooseX::Method::Signatures (2008) and Kavorka (2013), all three crediting Perl 6, → core named params in 5.44 (2026). PPC0024 cites those modules as prior art. It is an indirect chain: the PPC itself does not name Raku.
- The renaming of Perl6::Junction to Syntax::Keyword::Junction (2010) reflects community friction around the "Perl 6" brand in Perl 5 space, a decade before the Raku rename.

### Gaps
- Docs for Perl6::Variables, Perl6::Interpolators, Perl6::Contexts and Perl6::Subs (backpan) were not fetched, so their exact stated scope is unverified.
- MetaCPAN dates the Role::Tiny dist from 2012-03-29 (first stand-alone release), but it shipped inside Moo earlier. Moo-0.009001 dates from 2010-11-16. The exact first appearance of Role::Tiny inside Moo was not checked.
- Stevan Little's talks and blog posts on Perl 6 inspiration were not fetched. The Moose docs below serve instead.

---

## Q4. Moose / roles / MOP lineage, and the Corinna → `class` feature (5.38 – 5.44)

### Takeaway
Moose (2006) is the most consequential Perl 6 → Perl 5 transfer. Its manual says it is "based in large part on the Perl 6 object system" and "based on the prototypes and experiments Stevan did for the Perl 6 meta-model". Class::MOP lists the Pugs Perl 6 MetaModel as prior art. Moose roles credit the Smalltalk/Squeak Traits paper. Perl 6 roles came to Perl 5 earlier via chromatic's Class::Roles (2003), and core got `DOES()` in 5.10.

The core `class` feature (5.38, 2023-07-02) came from Ovid's Corinna. Ovid (2023-07-04): "the biggest influences were Raku, Moo/se, and yes, Java." Paul Evans's Object::Pad (2019) was the test bed. Object::Pad started with Raku/Moose's `has` keyword, switched to Corinna's `field` in 2022, and deprecated `has` in 2023. Core `class` still has no roles, which are only a TODO in perlclass.

### Cited Findings
- Class::MOP first release 2006-02-02; Moose-0.01 2006-03-15 (STEVAN). Latest Moose-2.4001 2026-10-01 (ETHER). — [MetaCPAN Moose](https://metacpan.org/dist/Moose); [MetaCPAN Class-MOP](https://metacpan.org/dist/Class-MOP)
- Moose::Manual: "Moose is based in large part on the Perl 6 object system, as well as drawing on the best ideas from CLOS, Smalltalk, and many other languages." — [Moose::Manual](https://metacpan.org/pod/Moose::Manual)
- Moose::Manual: "Moose is *based* on the prototypes and experiments Stevan did for the Perl 6 meta-model." — [Moose::Manual](https://metacpan.org/pod/Moose::Manual)
- Moose::Manual, under "Is Moose just Perl 6 in Perl 5?": "No. While Moose is very much inspired by Perl 6, it is not itself Perl 6… Stevan built Moose because he was tired of writing the same old boring Perl 5 OO code, and drooling over Perl 6 OO." It also says "we have "borrowed" features from Perl 6, CLOS (LISP), Smalltalk, Java, BETA, OCaml, Ruby and more". — [Moose::Manual](https://metacpan.org/pod/Moose::Manual)
- Class::MOP "Prior Art": "The Perl 6 MetaModel work in the Pugs project" (Perl6-ObjectSpace). — [Class::MOP](https://metacpan.org/pod/Class::MOP)
- Moose::Manual::Roles says roles "are based on the original concept of Traits for the Smalltalk-80 dialect Squeak". It does not credit Perl 6 directly. — [Moose::Manual::Roles](https://metacpan.org/pod/Moose::Manual::Roles)
- Class::Roles (chromatic, 2003-12-12): "provides a Perl 5 implementation of Perl 6 roles". — [Class::Roles](https://metacpan.org/pod/Class::Roles)
- Other first releases: Mouse 2008-06-10 (SARTAK; "plenty of code borrowed from Class::MOP and Moose"); Moo 2010-11-16 (MSTROUT); Role::Tiny dist 2012-03-29; Mo 2011-09-08 (INGY); Moxie 2017-06-21 (STEVAN). Mouse, Moo and Role::Tiny docs do not mention Perl 6, so their lineage runs through Moose. — [Mouse](https://metacpan.org/pod/Mouse); [Moo](https://metacpan.org/pod/Moo); [Role::Tiny](https://metacpan.org/pod/Role::Tiny); [Moxie](https://metacpan.org/pod/Moxie)
- Object::Pad (PEVANS): first release 2019-10-17. It has a section on its relation to `use feature 'class'`, describing itself as a "preview of what Perl's "use feature 'class'" syntax will likely become". Its ADJUST named-param notation is "borrowed from a plan to add named parameter support to perl's subroutine signature syntax". **No Raku mention in its doc.** — [Object::Pad](https://metacpan.org/pod/Object::Pad)
- Object::Pad Changes:
  - 0.66 (2022-07-07): "Support `field` keyword as a synonym of `has`".
  - 0.73 (2022-12-03): "Generally prefer `field` over `has`".
  - 0.800 (2023-08-09): "Emit deprecation warnings at compiletime from `has` keyword".
  - "Long-deprecated `extends` and `implements` keywords" became compile-time failures in 0.73.
  — [Object-Pad Changes](https://metacpan.org/dist/Object-Pad/changes)
- Curtis "Ovid" Poe, "Corinna in the Perl Core", dated 2023-07-04: "I also looked at Smalltalk, Java, Raku, Python, Eiffel, and Ruby… the biggest influences were Raku, Moo/se, and yes, Java." He also says "Paul Evans is doing the actual implementation work", and reports companies using the Object::Pad prototype "a 10% reduction in CPU usage". — [curtispoe.org (formerly ovid.github.io)](https://curtispoe.org/articles/corinna-in-the-perl-core.html)
- Heise article on Corinna's background: Moose by Stevan Little, then Ovid's "Cor", later renamed Corinna. Ovid credits Stevan Little's work as the main inspiration. — [heise.de](https://heise.de/-9759590) (via search summary; not fetched in full)
- The Corinna repo (Ovid/Cor) is dedicated to "Jeff Goff and David Adler, two Perl developers who also loved Raku". Its RFC's method-modifiers document is "translated almost verbatim from the Moose documentation". — [Ovid/Cor README](https://github.com/Ovid/Cor); [rfc/method-modifiers.md](https://github.com/Ovid/Cor/blob/master/rfc/method-modifiers.md)
- Core: 5.38.0 added `class`, `field`, `method` and `ADJUST`. feature.pm: "the "new" object system, previously codenamed "Corinna"". — [perl5380delta](https://github.com/Perl/perl5/blob/blead/pod/perl5380delta.pod); [feature.pm](https://github.com/Perl/perl5/blob/blead/lib/feature.pm)
- Follow-ups to `class`:
  - 5.40.0: `__CLASS__` and `:reader`.
  - 5.42.0: `:writer` (`set_x`), `my method` and `->&`.
  - Dev 5.45.2: leading-underscore stripping, "how Object::Pad works, too".
  — [perl5400delta](https://github.com/Perl/perl5/blob/blead/pod/perl5400delta.pod); [perl5420delta](https://github.com/Perl/perl5/blob/blead/pod/perl5420delta.pod); [perl5452delta](https://github.com/Perl/perl5/blob/blead/pod/perl5452delta.pod)
- perlclass TODO (blead) still lists:
  - "Roles: Some syntax for declaring a role (likely a role keyword), and for consuming a role into a class (likely a :does() attribute)"
  - parameters to ADJUST blocks `ADJUST (:$alpha, :$beta = 123)`
  - a metaprogramming API (PPC0022)

  perlclass's History section mentions no Raku. — [perlclass.pod](https://github.com/Perl/perl5/blob/blead/pod/perlclass.pod)

### Inferences
- For a Raku implementer, the shapes line up as follows (correspondence only; Perl docs do not draw it):

  | Perl 5 core `class` | Raku |
  |---|---|
  | `field $x :param :reader` | `has $.x` |
  | `:writer` | `is rw` |
  | `:isa(Parent)` | `is Parent` |
  | `ADJUST` | `TWEAK` |
  | `__CLASS__` | `::?CLASS` |
  | `my method` + `->&m` | private `method !m` / `self!m` |
  | planned `role` + `:does` | `role` / `does` |

  The keyword choice `field` rather than `has` was a deliberate move away from both Moose and Raku spelling.
- Moose sits in a different category from feature-by-feature backports. It transplanted the Perl 6 object model (metaclass, attributes with traits, roles, method modifiers) into Perl 5 a decade before core acted. Core's eventual design (Corinna) still names Raku as a top influence.

### Gaps
- I did not retrieve a primary Stevan Little talk or blog post, so the Moose::Manual text is the primary author statement used.
- The Corinna RFC text (Ovid/Cor/rfc/*.md) contains no "Raku" or "Perl 6" mentions except the README dedication, per my grep of the cloned repo. So the Raku credit rests on Ovid's 2023 article.

---

## Q5. Features that went the other way (Perl 5/CPAN first, then Perl 6 adopted them)

### Takeaway
At least three "Perl 6 features" started in Perl 5 or CPAN before Perl 6 adopted them:
- **Junctions:** Quantum::Superpositions, 2000-08-09, then RFC 225 in Sept 2000.
- **given/when:** Switch.pm, 2000-08-07, then RFC 022, Apocalypse 4 in Jan 2002, and Perl 5.10's given/when in 2007.
- **C3 MRO:** Dylan and Python first, then Perl 6/Parrot, Class::C3 in 2005 and the core `mro` pragma.

Junctions and switch then round-tripped back into Perl 5: Perl6::Junction in 2005, and the core given/when in 5.10.

### Cited Findings
- Quantum::Superpositions-1.00 (Damian Conway) was released 2000-08-09. Its doc says it "adds two new operators to Perl: any and all… superimposes them into a single scalar value (a superposition)". It does not mention Perl 6. — [MetaCPAN Quantum::Superpositions](https://metacpan.org/pod/Quantum::Superpositions)
- RFC 225 (v2) "Data: Superpositions" by Damian Conway was created 14 Sep 2000, last modified 18 Sep 2000, status Frozen. It points to the existing module: "See the Quantum::Superpositions module", and "Inclusion in the core would also allow the current module-based pure Perl implementation to be greatly optimized". — [RFC 225 on perl6-all](https://www.mail-archive.com/perl6-all@perl.org/msg04454.html)
- Apocalypse 4 (2002-01-15) refers to "Damian's superposition RFC" and sketches `any(@$!)`. — [Apocalypse 4](https://www.perl.com/pub/2002/01/15/apo4.html)
- Perl6::Junction (2005-05-03) re-imported junctions into Perl 5: "Inspired by the Perl6 design docs… a limited subset of the functionality of Quantum::Superpositions". — [Perl6::Junction](https://metacpan.org/pod/Perl6::Junction)
- Switch-1.00 (Conway) was released 2000-08-07. Apocalypse 4 then accepted RFC 022 "Control flow: Builtin switch statement" and renamed switch/case to given/when "after thrashing over several options with Damian". Switch.pm later added a `use Switch 'Perl6'` mode with given/when. Perl 5.10 shipped native given/when. Switch was deprecated in 5.12 and removed from core in 5.14 ("Switch has been removed from the Perl core. Prior version was 2.16."). — [Switch on MetaCPAN](https://metacpan.org/pod/Switch); [Apocalypse 4](https://www.perl.com/pub/2002/01/15/apo4.html); [perl5120delta](https://github.com/Perl/perl5/blob/blead/pod/perl5120delta.pod); [perl5140delta](https://github.com/Perl/perl5/blob/blead/pod/perl5140delta.pod)
- C3: Class::C3 (Stevan Little, 2005-08-07) says C3 came from Dylan, then Python 2.3, then "the 'canonical' MRO for Perl 6 classes, and the default MRO for Parrot objects". The core `mro` pragma (5.10, Brandon Black) carries the same text, now with "Raku". — [Class::C3](https://metacpan.org/pod/Class::C3); [ext/mro/mro.pm](https://github.com/Perl/perl5/blob/blead/ext/mro/mro.pm); [perl5100delta](https://github.com/Perl/perl5/blob/blead/pod/perl5100delta.pod)

### Inferences
- Junctions are the purest "Perl 5 first" case: a CPAN module (Aug 2000) → Perl 6 RFC (Sept 2000) → core Perl 6 junctions → back to Perl 5 CPAN as Perl6::Junction (2005). Perl 5 core never took junctions. In 2025 PPC0027 explicitly rejected `any`/`all` as junctions, leaving junction values to modules like Data::Checks (2024).
- given/when made a full loop: CPAN (Switch.pm, 2000) → Perl 6 design (A04, 2002) → Perl 5 core (5.10, 2007) → deprecated (5.38) → kept but discouraged (5.42).

### Gaps
- I did not check which Apocalypse or Synopsis finally named "junctions", or when the word replaced "superpositions".

---

## Q6. Did Perl 6 ideas reach third languages through Perl 5?

### Takeaway
Yes, mainly through Moose. Joose, a JavaScript meta-object system by Malte Ubl (stable 2.1 in Aug 2009), is described as "heavily inspired by Moose… which was itself inspired by the Perl 6 object system". That makes it a documented Perl 6 → Moose → JavaScript chain. There were also Moose ports to Ruby (the MooseX gem) and Python (Bullwinkle, Elk). I found no other third-language transfers via Perl 5 core features.

### Cited Findings
- Joose: an "open-source metaobject system for JavaScript… heavily inspired by Moose, the object system for Perl 5 which was itself inspired by the Perl 6 object system". It supports roles/traits and method modifiers (`after`) with `has`/`isa` syntax. Developer Malte Ubl; stable release 2.1, 2009-08-02. — [Wikipedia: Joose (framework)](https://en.wikipedia.org/wiki/Joose_(framework))
- Ruby: the MooseX gem describes itself as "a port of Moose/Moo from Perl to Ruby world". Version 0.0.21 is from Feb 2014. — [RubyGems moosex](https://rubygems.org/gems/moosex/versions/0.0.4?locale=fr) (via search summary)
- Python and others: a 2014 Hacker News comment names "two attempts… to bring (a subset of) Moose to Python", Bullwinkle and Elk. It mentions clones for Ruby, JavaScript and Lua. — [Hacker News item 7323523](https://news.ycombinator.com/item?id=7323523) (via search summary)

### Inferences
- The Moose chain (Perl 6 metamodel → Moose → Joose/MooseX-Ruby/Elk) is the only well-documented three-hop transfer. Core-language backports (say, smartmatch, `//`) have no documented onward path through Perl 5. Where other languages share them (`//` in PHP/JS as `??`, chained comparisons), the evidence points to independent invention or other sources.

### Gaps
- I did not fetch Joose's own documentation or the Bullwinkle/Elk pages. Claims rest on Wikipedia and the search summary of HN/RubyGems.
- I found no evidence for or against Moose influencing mainstream language features such as PHP 5.4 traits. Those cite Smalltalk traits research, which was not verified here.

---

## Q7. Context: Perl 7 (2020) and the post-Raku-rename framing

### Takeaway
Sawyer X announced Perl 7 on 24 June 2020 (Conference in the Cloud). It was meant to be Perl 5.32 with modern defaults. After governance turmoil it was shelved: Sawyer X resigned in April 2021. In May 2022 the PSC said Perl would continue with 5.XX releases until a feature set justified `use v7`. Meanwhile Perl 5 kept adopting features that resemble Raku's (signatures stable 2022, `class` 2023, `any`/`all` 2025, named params 2026), but routed them through CPAN and PPCs without crediting Raku. PPC0025 (draft) proposes dropping the "5" ("Perl 5 is Perl"), since "There is no Perl 6 looming any more".

### Cited Findings
- The Perl 7 plan was announced by Sawyer X at the Conference in the Cloud on 24 June 2020. — [Habr news](https://habr.com/en/news/t/508130) (via search summary); critiques at the time: [Dan Book, "Perl 7: A Risk-Benefit Analysis"](https://blogs.perl.org/users/grinnz/2020/07/perl-7-a-risk-benefit-analysis.html), [Leon Timmermans](https://blogs.perl.org/users/leon_timmermans/2020/06/not-quite-getting-better-yet.html)
- Sawyer X's departure was reported in April 2021. — [The Register, 2021-04-13](https://www.theregister.com/2021/04/13/perl_dev_quits/) (via search summary)
- PSC, "What happened to Perl 7?" (2022-05-25): "we'll continue with 5.XX releases… At some point in the future, the PSC may decide that the set of features, taken together, represent a big enough step forward to justify a new baseline for Perl… the version will be bumped to 7.0", with "use v7;" required for the new features. The post does not mention Raku. — [blogs.perl.org PSC](https://blogs.perl.org/users/psc/2022/05/what-happened-to-perl-7.html)
- PPC0025 "Perl 5 is Perl" (draft): "There is no Perl 6 looming any more… We should just drop the 5". It also says "Perl and Raku can and should stay in touch and take inspiration from each other". — [PPC0025](https://github.com/Perl/PPCs/blob/main/ppcs/ppc0025-perl-version.md)
- Perl 5.44.0 was released 2026-07-15 (Leon Timmermans). Blead is at 5.45.x (5.45.4 on 2026-10-22). — [perlhist.pod](https://github.com/Perl/perl5/blob/blead/pod/perlhist.pod)

### Inferences
- After the 2019 rename to Raku and the 2020–22 Perl 7 episode, Perl 5's docs moved from "Perl 6" credit lines to softer "Raku-inspired" wording (feature.pm, 5.34+). New proposals barely mention Raku. Influence continues mostly in practice: syntax shapes like `:$named`, `class`/`method`/`field`, and `^^`. Attribution has largely stopped.

### Gaps
- The Perl 7 announcement article on perl.com (brian d foy, June 2020) was not fetched directly. The date comes from secondary reporting.
