# Research notes for PERL6-FEATURES-IN-OTHER-LANGUAGES.md

These are the source notes behind [../PERL6-FEATURES-IN-OTHER-LANGUAGES.md](../PERL6-FEATURES-IN-OTHER-LANGUAGES.md). Every claim in the notes carries its URL or message-ID. Every hit is classified as one of: adopted-from, cited-as-prior-art, considered-and-rejected, cautionary-tale or passing mention. Each note ends with the gaps its search left.

The notes are a historical record of two research rounds, 7–8 November 2026, and are not maintained. Where the report and a note disagree, the report wins: it reconciles the notes, and the second round corrected some first-round dates and verdicts.

## feature-timelines/ — first round

- **[perl6_design_chronology.md](feature-timelines/perl6_design_chronology.md)** — When each Perl 6 feature was proposed, specified and implemented, from the RFCs through the Apocalypses, Synopses, Pugs and Rakudo. Also what Perl 6 itself credited, and which commonly repeated attributions have no primary support.
- **[perl5_backports.md](feature-timelines/perl5_backports.md)** — Every Perl 5 core feature and CPAN module traced to Perl 6, 5.10 to 5.44, with the smartmatch history.
- **[types_and_oo.md](feature-timelines/types_and_oo.md)** — Gradual typing, roles and traits, multiple dispatch, definedness, subset types, signatures, mixins and the MOP.
- **[syntax_and_operators.md](feature-timelines/syntax_and_operators.md)** — Defined-or, ranges, implicit lambda parameters, the `->` arrow, interpolation, heredocs, pair shorthand, pipelines, hyper and Unicode operators.
- **[text_control_runtime.md](feature-timelines/text_control_runtime.md)** — Grammars and PEGs, graphemes, phasers and `defer`, gather/take, junctions, pattern matching, concurrency, `sub MAIN`, rationals and macros.
- **[influence_evidence.md](feature-timelines/influence_evidence.md)** — Explicit mentions of Perl 6 in other languages' design archives, an audit of the Wikipedia and Wikidata influence claims, and influence through Pugs, Parrot and Moose.
- **[proposal_archive_search.md](feature-timelines/proposal_archive_search.md)** — Full-text grep of swift-evolution, rust-lang/rfcs, Kotlin KEEP, csharplang, both Scala SIP repositories, dart-lang/language and the JEPs, plus their GitHub threads.

## beyond-proposals/ — second round

- **[design_forums.md](beyond-proposals/design_forums.md)** — The Discourse forums, csharplang Discussions, the Ruby tracker, PHP internals and the D forum. This search found the Swift SE-0168 heredoc borrowing.
- **[mailing_lists.md](beyond-proposals/mailing_lists.md)** — perl5-porters, python-3000, python-ideas, python-dev, es-discuss and ruby-dev. This search found the p5p credits for `//`, `state`, `say`, given/when, `\h`/`\v` and `<<~`, and Matz's 2005 `->` sources.
- **[other_language_repos.md](beyond-proposals/other_language_repos.md)** — Twenty more design and documentation repositories, 329 TC39 proposal repositories and 17 issue trackers (Go, Carbon, GHC, F#, Julia, TypeScript, Zig and others).
- **[raku_side_dates_and_claims.md](beyond-proposals/raku_side_dates_and_claims.md)** — First appearances dated from the Pugs svn mirror and perl6-language: `~~`, "junction", `»op«`, `X`, `Z`, `sub MAIN`, `"{ }"` and sequence deduction. Also an audit of the Raku community's own influence claims.
- **[lookalike_origins.md](beyond-proposals/lookalike_origins.md)** — Documented origins of the features the report calls independent: C# `??` and iterators; Swift `$0`, `\()`, `??`, `..<` and `defer`; Kotlin and Groovy `it`; Scala `_`; ES2015 shorthand and template literals; Go `defer`; D scope guards; Python `any`/`all`; and Moose's `has`.
