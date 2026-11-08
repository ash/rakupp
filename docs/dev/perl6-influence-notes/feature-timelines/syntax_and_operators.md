# Perl 6 / Raku syntax and operator features in other languages (2000–Nov 2026)

Method note: for each design document listed below I fetched the primary text (PEP, RFC, KEEP, JEP, SE proposal, TC39 README, Ruby tracker JSON with all journals, GitHub issue threads via API, perldelta pods, commit messages) and grepped it for `perl` and `raku` (case-insensitive) plus the other languages it names. "No mention" therefore means "no occurrence of the string in the text fetched", not a judgement from a summary. Where a document lists prior art, the list is quoted.

## Overall: where is Perl 6/Raku documented as the source, and where is only Perl 5 (or nobody) credited?

### Takeaway
Only one borrowing into another mainstream language is documented in a primary source: Ruby's `->` lambda (2005 commit "Perl6 style -> lambda expression"; Matz in 2008: "Yes. It's the origin."). Perl 5 credits Raku for its `say` and given/when backports. Elsewhere design documents cite **Perl 5** as prior art (spaceship, digit separators, `$` interpolation, heredocs/`qq{}`) or cite no Perl at all. Perl 6/Raku appears in one other place: a community comment on the C# ranges thread, which the C# team did not adopt.

### Cited Findings
- Ruby ChangeLog, 27 Jul 2005: "parse.y (lambda): Perl6 style -> lambda expression. [NEW] [VERY EXPERIMENTAL]" — [ruby/ruby commit 759a63b6](https://github.com/ruby/ruby/commit/759a63b6a3355b85e1e52b78810d66fd9ee7b838); quoted again on ruby-core by David Flanagan — [ruby-core:16920](https://blade.ruby-lang.org/ruby-core/16920)
- Matz, 12 May 2008, answering "Does perl 6 have the -> syntax?": "Yes. It's the origin." — [ruby-core:16734](https://blade.ruby-lang.org/ruby-core/16734)
- Perl 5 `feature.pm`: "`use feature 'say'` tells the compiler to enable the Raku-inspired `say` function" and "enable the Raku given/when construct" — [lib/feature.pm](https://raw.githubusercontent.com/Perl/perl5/blead/lib/feature.pm)
- Perl 5 is credited, Perl 6 is not, in these: PHP spaceship RFC ("It exists in other languages: Perl, Ruby, and Groovy") — [PHP RFC](https://wiki.php.net/rfc/combined-comparison-operator); C++ P0515R0's existing-practice table ("Perl <=>") — [P0515R0 PDF](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0515r0.pdf); PEP 515 prior art ("Perl 5: multiple, basically anywhere") — [PEP 515](https://peps.python.org/pep-0515/); TC39 numeric separators prior art ("Perl: multiple, anywhere") — [proposal README](https://raw.githubusercontent.com/tc39/proposal-numeric-separator/master/README.md); PEP 215 ("variable interpolation found in Unix shells, awk, Perl, or Tcl") — [PEP 215](https://peps.python.org/pep-0215/); JEP 326 ("qq{...} Perl"; heredocs "have found their way into programming languages such as Perl") — [JEP 326](https://openjdk.org/jeps/326); SE-0200 (Perl `'...'`/`q(...)` in its delimiter table; "We considered a Perl/Ruby-like approach to arbitrary delimiters, but quickly rejected it") — [SE-0200](https://raw.githubusercontent.com/swiftlang/swift-evolution/main/proposals/0200-raw-string-escaping.md)
- Perl 6 mentioned but not adopted: a commenter on the C# ranges champion issue, 27 Feb 2017: "I like how Perl 6 specifies these: a..b / a^..b / a..^b / a^..^b" — [csharplang#185 comment](https://github.com/dotnet/csharplang/issues/185#issuecomment-282744126). C# 8 shipped `..` (exclusive end) plus `^n` index-from-end instead — [C# 8 ranges spec](https://raw.githubusercontent.com/dotnet/csharplang/main/proposals/csharp-8.0/ranges.md)
- On the Ruby tracker, "Perl" shows up in feature debates only as a warning: "we are currently quite reluctant towards introducing 'Perlisms'" (#18980) — [Feature #18980](https://bugs.ruby-lang.org/issues/18980); "I don't want Ruby to become Perl in terms of readability" (#15723) — [Feature #15723](https://bugs.ruby-lang.org/issues/15723)

### Inferences
- In the design documents that list prior art, "Perl" means Perl 5. No PEP, KEEP, JEP, Swift Evolution proposal, Rust RFC, PHP RFC or TC39 proposal I fetched mentions Perl 6 or Raku, even where Raku has the nearest analogue (`:$x` vs PEP 736 / Rust RFC 1682; `..^` vs the Kotlin KEEP; `«+»` vs Julia dot-broadcast; `<=>` returning an `Order` enum vs P0515's claim that only Haskell returns a type).
- Ruby is the only case where the influence is both documented and in shipped syntax. Elsewhere, the cited sources point to C# (null coalescing, lambda arrows), F#/Isabelle-ML/Unix (pipes), ES6/Haskell (field punning), Swift/Groovy (`..<`), Matlab/NumPy/APL (broadcast), and Perl 5/shell/Ruby (interpolation, heredocs, `<=>`, `_` separators).

### Gaps
- The Perl 6 RFC archive (raku.org/archive/rfc, perl6.org/archive/rfc) returned 404/525, and web.archive.org was unreachable from this session. RFC texts (e.g. RFC 023 placeholders, RFC 025, RFC 111/162) are cited only through the Apocalypses that rule on them.
- No proposal text exists for several features that shipped before their language had a public process: Swift 1.0 `$0`, `\()`, `??`; Kotlin `it`, `${}`, `?:`; C# 2.0 `??`; Groovy `it`, `..<`, `?:`. Their origins remain undocumented here.

## 1. Null/undefined coalescing: `//` vs `??` / `?:`

### Takeaway
Perl 6 specified `//` in Apocalypse 3 (Oct 2001), and Perl 5 got it from a 2002 patch, released in 5.10. No later language cites Perl for `??`. PHP cites C# for precedence, TC39 links only Wikipedia, and Dart's changelog gives no source.

### Cited Findings
- Apocalypse 3 (2 Oct 2001): "A binary // operator is the defaulting operator… `$a // $b` is short for `defined($a) ?? $a :: $b`… It also has a corresponding assignment operator… `$pi //= 3;`" — [Apocalypse 3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- Perl 5: first commit "Defined-or patch (cleaned up)", author Brent Dax, 26 Jul 2002 (p4 change 17682) — [Perl/perl5 c963b15115](https://github.com/Perl/perl5/commit/c963b15115) (via the [GitHub commits API for t/op/dor.t](https://api.github.com/repos/Perl/perl5/commits?path=t/op/dor.t)). Shipped in 5.10: "A new operator `//` (defined-or) has been implemented… same precedence and associativity as `||`." No Perl 6 credit in perl5100delta — [perl5100delta](https://raw.githubusercontent.com/Perl/perl5/blead/pod/perl5100delta.pod)
- PHP 7 `??`: RFC dated 2014-09-06 by Andrea Faulds, original patch by Nikita Popov. The only other language named is C#: "the precedence… is the same as C#", with a link to C# operator precedence. It has no prior-art section and no Perl mention — [PHP RFC: Null Coalesce Operator](https://wiki.php.net/rfc/isset_ternary)
- JavaScript `??`: the TC39 README's "Prior Art" is a single link to Wikipedia's "Null coalescing operator". No Perl/Raku. It names optional chaining as the companion feature — [proposal-nullish-coalescing](https://raw.githubusercontent.com/tc39/proposal-nullish-coalescing/master/README.md)
- Dart 1.12.0 (2015-08-31): "Null-aware operators — `??`: if null operator…" — [Dart SDK CHANGELOG](https://raw.githubusercontent.com/dart-lang/sdk/main/CHANGELOG.md)
- Swift: Lattner said Swift drew on "Objective-C, Rust, Haskell, Ruby, Python, C#, CLU, and far too many others to list". Perl is not named — [Daring Fireball quoting Lattner, 2014-06-06](https://daringfireball.net/linked/2014/06/06/lattner)

### Inferences
- Verdicts:
  - **Perl 5 `//`**: borrowed from the Perl 6 design. The timing (A3 in Oct 2001, then a Perl 5 patch by a Perl 6/Parrot contributor in Jul 2002) is strong but circumstantial, because perl5100delta and the commit don't say so. The Perl 5 backports notes (sibling file perl5_backports.md) may have more.
  - **C# `??`**: independent invention. No source links it to Perl.
  - **PHP `??`**: common ancestor C# (the RFC cites C#).
  - **Swift `??`**: plausibly C# (circumstantial; Lattner lists C#).
  - **JS `??`**: common ancestor, generic "null coalescing" lineage (links only Wikipedia).
  - **Dart `??`**: undocumented; probably C#-lineage (circumstantial).
  - **Kotlin `?:`**: common ancestor Groovy's Elvis (unverified, see Gaps).
- Semantics differ: Raku and Perl `//` test *definedness*, which differs from null/undefined testing. In Raku a type object is undefined, so `Int // 5` gives 5. That is closer to C#/JS null tests than to `||`, but it is not the same thing.

### Gaps
- No primary source found for where C# 2.0 `??` came from, or for the C# 2.0 spec draft date. I could not tell whether `??` was public before or after Perl 5's 2002 `//` patch.
- Kotlin `?:` and Groovy Elvis dates and origins were not verified. Swift 1.0 (2014), JS ES2020 and C# 2.0 (2005) release dates are from the brief, not checked.

## 2. Chained comparisons (`a < b < c`)

### Takeaway
Python had chained comparisons before 2000. Perl 6 accepted them through RFC 025 in Apocalypse 3 (2001). Perl 5.32 added them in 2020, and neither the commit nor perldelta mentions Raku. Julia documents them without naming a source.

### Cited Findings
- Apocalypse 3, RFC 025 "Operators: Multiway comparisons": "`0 <= $x < 10` really means something like `0 <= $x && $x < 10`… The $x would only be evaluated once… this RFC is accepted." — [Apocalypse 3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- Perl 5: commit "chained comparisons" by Zefram, 2020-02-05. The message is just the title; it touches perly.y, op.c, pp.c and t/op/cmpchain.t — [Perl/perl5 02b85d3](https://github.com/Perl/perl5/commit/02b85d3dab092d678cfc958a2dc252405333ed25). perl5320delta, "Chained comparisons capability": `if ( $x < $y <= $z ) {...}` "behaves exactly like `$x < $y && $y <= $z`". No mention of Raku or Python — [perl5320delta](https://raw.githubusercontent.com/Perl/perl5/blead/pod/perl5320delta.pod)
- Julia manual: "Chaining comparisons is often quite convenient in numerical code… use the && operator for scalar comparisons, and the & operator for elementwise comparisons… `0 .< A .< 1`". The middle expression is evaluated once, and the evaluation order is undefined — [Julia manual: Mathematical Operations](https://docs.julialang.org/en/v1/manual/mathematical-operations/)

### Inferences
- Verdicts:
  - **Perl 6**: common ancestor Python and mathematical notation. A3 frames it as "like mathematician would expect".
  - **Perl 5.32**: plausibly influenced by Raku (circumstantial: same family, Raku had it for years), but nothing documents it.
  - **Julia**: common ancestor Python/mathematics. Julia's elementwise `.<` chaining is its own extension.

### Gaps
- Could not read RFC 025 directly (archive offline), so I can't check whether Conway cited Python. Did not find the 2020 p5p thread for Zefram's patch.
- No other-language proposals for chained comparisons were researched (e.g. a JS or Rust discussion).

## 3. Exclusive-end ranges and index-from-end (`..^`, `^..`, `^10`, `*-1`)

### Takeaway
No range proposal I found cites Perl 6. Kotlin cites Swift, Ruby, Groovy and Rust, Rust cites Ruby, and C# rejected a community suggestion to copy Perl 6. Swift renamed `..` to `..<` in beta 3 (July 2014) for clarity, with no source given.

### Cited Findings
- Kotlin `..<`: the KEEP's comparison names "Swift: `...` end-inclusive range, `..<` end-exclusive", "Ruby: `..` …`...` end-exclusive", "Groovy: `..` end-inclusive range, `..<` end-exclusive, `<..` start-exclusive, `<..<` both", and "Rust: `..=` end-inclusive range, `..` end-exclusive". No Perl/Raku — [KEEP: open-ended ranges](https://raw.githubusercontent.com/Kotlin/KEEP/master/proposals/open-ended-ranges.md). The original YouTrack request (created 2017-01-10): "My personal top preference comes from Swift that has `..<` operator" — [KT-15613](https://youtrack.jetbrains.com/issue/KT-15613). Shipped experimental in Kotlin 1.7.20 (released 29 Sep 2022) — [What's new in 1.7.20](https://kotlinlang.org/docs/whatsnew1720.html). Stable in 1.9.0 (6 Jul 2023): "The ..< operator and time API are Stable" — [What's new in 1.9.0](https://kotlinlang.org/docs/whatsnew19.html)
- Rust RFC 1192 (start 2015-07-07) proposed `a...b` inclusive, noting "The `...` vs. `..` distinction is the exact inversion of Ruby's syntax." No Perl — [RFC 1192](https://raw.githubusercontent.com/rust-lang/rfcs/master/text/1192-inclusive-ranges.md). Neither the RFC 1192 nor the RFC 1980 (`..=`) PR comment threads contain "Perl 6", "raku", `..^` or `^..` — [rust-lang/rfcs#1192](https://github.com/rust-lang/rfcs/pull/1192), [#1980](https://github.com/rust-lang/rfcs/pull/1980)
- Swift: in Xcode 6 beta 3 (July 2014) the half-open operator changed from `..` to `..<`, reportedly "to make it more clear alongside the ... operator" (secondary reports of Apple's release notes) — [AppDeveloperMagazine](https://appdevelopermagazine.com/apple-opens-new-swift-blog,-announces-xcode-6-beta-3-for-ios-development/); [MacTrast](https://www.mactrast.com/2014/07/apples-new-swift-programming-language-gets-significant-beta-3-updates/)
- C# 8 (2019): `^n` means index from end and `..` is end-exclusive (`array[^1]`, `array[2..^3]`, `Range.All` ≡ `0..^0`). The spec cites no other languages — [C# 8 ranges proposal](https://raw.githubusercontent.com/dotnet/csharplang/main/proposals/csharp-8.0/ranges.md). In the champion thread a commenter proposed Perl 6's `^..`/`..^` (2017-02-27), and Roslyn team member Cyrus Najmabadi later wrote (2018-06-28) that "how ^ is used in perl, regex, fortran, etc. isn't really relevant" — [csharplang#185](https://github.com/dotnet/csharplang/issues/185#issuecomment-282744126); [comment 400886623](https://github.com/dotnet/csharplang/issues/185#issuecomment-400886623). Another commenter defended end-relative indexing: "Perl, PHP, Ruby, PowerShell, Bash and Groovy also work like this" — [comment 489682413](https://github.com/dotnet/csharplang/issues/185#issuecomment-489682413)

### Inferences
- Verdicts:
  - **Swift `..<`**: independent invention, or Groovy ancestor (circumstantial; no source).
  - **Kotlin `..<`**: borrowed from Swift (documented in KT-15613), with Groovy and Ruby named in the KEEP.
  - **Rust `..`/`..=`**: independent, defined against Ruby (documented).
  - **Ruby `...`**: pre-2000 ancestor.
  - **Groovy `..<`**: date and origin not verified.
  - **C# `^1`**: independent invention. Perl 6 was raised in public discussion and explicitly not used.
  - **Nim `..<`**: not researched.
- Raku's `*-1` is a WhateverCode called with the length. C#'s `^1` is a typed `Index` value. Both avoid Python's negative-index ambiguity, but they are different mechanisms, and the C# record shows no link.
- Groovy's `<..<` (start- and end-exclusive) is the closest functional match to Raku's `^..^`. The KEEP mentions it only as Groovy and Guava prior art.

### Gaps
- Groovy `..<` introduction date and origin; Nim `..<`; Swift beta-3 rationale from Apple's own notes (only secondary reports were reachable); C# LDM notes explaining why `^` was chosen.

## 4. Implicit lambda parameters / placeholder currying (`$^a`, `* + 1`)

### Takeaway
`$^a` placeholders and `->` pointy blocks are in Apocalypse 4 (Jan 2002). Scala's `_ + 1` came in Scala 2.5 (May 2007), not 2003/2004. Ruby's `_1` (2.7) and `it` (3.4) threads cite Elixir, Clojure and Kotlin, and mention Perl only to warn against "Perlisms". No source names Perl 6 placeholders.

### Cited Findings
- Apocalypse 4 (15 Jan 2002): "Bare closures topicalize their first argument, implicitly aliasing it to $_ unless $^a or some such is used… `grep { $^a eq 3 } @list`" — [Apocalypse 4](https://www.perl.com/pub/2002/01/15/apo4.html/)
- Scala: the changelog under "Changes in Version 2.5 (02-May-2007)", "Implicit anonymous functions": "It is now possible to define anonymous functions using underscores in parameter position… `_ + 1` → `x => x + 1`, `_ * _` → `(x1, x2) => x1 * x2`". No sources cited — [Scala 2.12 spec changelog](https://scala-lang.org/files/archive/spec/2.12/15-changelog.html)
- Ruby numbered parameters: Feature #4475 opened 2011-03-06, closed 2019-03-17. Other languages named in the thread: Elixir `&(&1 + 2)` (janfri, 2019-02-08) and Clojure `%1` (bbatsov, 2019-03-18). No Perl/Raku — [Feature #4475](https://bugs.ruby-lang.org/issues/4475). Ruby 2.7.0 release (2019-12-25): "Numbered parameters as default block parameters are introduced." — [Ruby 2.7.0 released](https://www.ruby-lang.org/en/news/2019/12/25/ruby-2-7-0-released/)
- Ruby #15723 "Reconsider numbered parameters" (2019): "We already discourage Perl style `$`-references" and "I don't want Ruby to become Perl in terms of readability" — [Feature #15723](https://bugs.ruby-lang.org/issues/15723)
- Ruby `it`: Feature #18980 (k0kubun, 2022-08-26) discusses Kotlin's `it`. zverok wrote that Ruby is "quite reluctant towards introducing 'Perlisms'". Matz accepted it on 2023-12-07 ("accept `it` on Ruby 3.4") — [Feature #18980](https://bugs.ruby-lang.org/issues/18980). Ruby 3.4.0 (2024-12-25): "`it` is added to reference a block parameter with no variable name. [Feature #18980]" — [Ruby 3.4.0 released](https://www.ruby-lang.org/en/news/2024/12/25/ruby-3-4-0-released/)
- The TC39 pipeline README compares its `%` topic token with Clojure's `#(%1 %2)` function literals — [proposal-pipeline-operator README](https://raw.githubusercontent.com/tc39/proposal-pipeline-operator/main/README.md)

### Inferences
- Verdicts:
  - **Scala `_`**: independent invention. No source; its 2007 date is after Perl 6 `$^a` (2002) and around when Whatever-star was being designed, but nothing links them.
  - **Clojure `#(%)`**: independent; ancestor is Lisp reader macros (unverified).
  - **Groovy `it`**: independent (unverified).
  - **Kotlin `it`**: plausibly Groovy (circumstantial; no Breslav statement found).
  - **Swift `$0`**: independent / undocumented. `$0` resembles shell positional parameters, and Lattner's influence list doesn't name Perl.
  - **Ruby `_1`**: independent. The thread cites Elixir and Clojure, and critics of the earlier `@1` spelling called it Perl-like (#15723). `@1` was later replaced by `_1`, but the tickets don't give that as the reason.
  - **Ruby `it`**: plausibly influenced by Kotlin's `it`. Kotlin's `it` is discussed in #18980, but no comment says Ruby copies it.
  - **Elixir `&1`**: independent.
  - **Mathematica `#`/`&`**: pre-2000 ancestor.
- Raku's two mechanisms (`$^a`, sorted lexically, and WhateverCode currying `* + 1`) map onto two separate families elsewhere: numbered or named positional (`$0`, `_1`, `&1`, `%1`, `#1`) and a single anonymous hole (`_` in Scala, `it`). No source connects either family to Raku.

### Gaps
- When Raku's Whatever-star currying (`* + 1`) entered the design (S02/S03, mid-2000s) was not verified this session. Neither were the dates of Groovy `it`, Kotlin `it` (2011 announcement / 1.0 2016), Swift `$0` (2014), Clojure `#()` (2007) and Elixir `&1`. Perl 6 RFC 023 (placeholders) could not be fetched.

## 5. Pointy-block lambda arrow (`-> $x { }`)

### Takeaway
This is the one clearly documented borrowing. Ruby's `->(x) { }` (in development from July 2005, shipped in 1.9.0, Dec 2007) is explicitly "Perl6 style". Java 8 copied C# and Scala, and PHP 7.4's `fn` cites JavaScript, Hack, Rust and Haskell.

### Cited Findings
- Perl 6 pointy blocks, Apocalypse 4 (Jan 2002): `given $value -> $g {`, `for @foo -> $a, $b { # two at a time`, `grep -> $x { $x eq 3 } @list` — [Apocalypse 4](https://www.perl.com/pub/2002/01/15/apo4.html/). Apocalypse 6 (Mar 2003) names them "Pointy subs… declare a closure with an unparenthesized signature" — [Apocalypse 6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- Ruby: commit by matz, 2005-07-27, "parse.y (lambda): Perl6 style -> lambda expression. [NEW] [VERY EXPERIMENTAL]" — [ruby/ruby 759a63b6](https://github.com/ruby/ruby/commit/759a63b6a3355b85e1e52b78810d66fd9ee7b838). Ruby 1.9.0 released 2007-12-25 — [ruby-lang.org](https://www.ruby-lang.org/en/news/2007/12/25/ruby-1-9-0-released/); [ruby-core:16920 ChangeLog quote](https://blade.ruby-lang.org/ruby-core/16920)
- Matz (ruby-core, May 2008): "I am pretty well satisfied with current ->. In addition, if both Ruby and Perl6 have it (in same syntax), it will soon have large mind share in the dynamic language 'market'." Asked "Does perl 6 have the -> syntax?", he answered "Yes. It's the origin. Perl6 is even more radical to change 'for' statement to `for obj -> var { }`… I once tried to make block syntax to `method(arg) -> x { … }` but felt it was too radical and abandoned." — [ruby-core:16733](https://blade.ruby-lang.org/ruby-core/16733), [ruby-core:16734](https://blade.ruby-lang.org/ruby-core/16734). Also: "Not everything in Perl(6) is bad or ugly. If it's good, it's good." — [ruby-core:16766](https://blade.ruby-lang.org/ruby-core/16766)
- Java 8: Brian Goetz, 8 Sep 2011: "we decided to essentially adopt the C# syntax… better to choose something that has already been shown to work well in the two languages that are most like Java -- C# and Scala -- rather than to invent something new." Thin versus fat arrow was still open then; Java later chose `->` — [lambda-dev "Syntax decision"](https://mail.openjdk.org/pipermail/lambda-dev/2011-September/003936.html)
- PHP 7.4 `fn($x) => …` (RFC dated 2019-03-12): names JavaScript `($x) => …`, Hack `==>`, Haskell `\x ->`, Rust closures, and Ruby/Swift block syntax among alternatives. No Perl — [PHP RFC: Arrow Functions 2.0](https://wiki.php.net/rfc/arrow_functions_v2)

### Inferences
- Verdicts:
  - **Ruby `->`**: borrowed from Perl 6 (documented, by commit and by the language designer).
  - **Java `->`**: common ancestor C#/Scala (documented). The thin arrow is ML/Haskell-style; no Perl link.
  - **C# `=>`**: independent / undocumented.
  - **PHP `fn =>`**: common ancestor JS/Hack (documented).
- Ruby took only the syntax. `->(x) {}` builds a lambda object, while Raku's `->` is a Block with a signature that control structures like `for` and `given` consume. Matz says he tried and abandoned the block-argument form.

### Gaps
- No primary design note found for C# 3.0's choice of `=>`.

## 6. Interpolating arbitrary expressions in strings (`"{ expr }"`)

### Takeaway
Nobody credits Perl 6's `{ }` interpolation. Python's lineage cites Perl 5, shell, awk and Tcl (PEP 215, 2000) and the shell (PEP 292). PEP 498 (f-strings) points only to Wikipedia. Scala's SIP-11 has no prior art. JS template literals grew out of a 2009 "quasi-literal" strawman, where Perl appears only in discussion of heredocs and backticks.

### Cited Findings
- PEP 215 (Ka-Ping Yee, created 24-Jul-2000, superseded): a `$` prefix "in a manner reminiscent to the variable interpolation found in Unix shells, awk, Perl, or Tcl" — [PEP 215](https://peps.python.org/pep-0215/)
- PEP 292 (string.Template), quoting the BDFL: "The $ means 'substitution' in so many languages besides Perl that I wonder where you've been. […] We're copying this from the shell." — [PEP 292](https://peps.python.org/pep-0292/)
- PEP 498 (created 01-Aug-2015, Python 3.6), "Similar support in other languages": "Wikipedia has a good discussion of string interpolation in other programming languages… implemented in many languages, with a variety of syntaxes". No language is named and there is no Perl/Raku — [PEP 498](https://peps.python.org/pep-0498/)
- Scala SIP-11 (Odersky, 15 Jan 2012, revising 15 Oct 2011): `${expr}` and `$id` with user-definable string processors. No other language is cited — [SIP-11 page](https://docs.scala-lang.org/sips/11.html), [SIP-11 Google Doc (txt export)](https://docs.google.com/document/d/1NdxNxZYodPA-c4MLr33KzwzKFkzm9iW9POexT9PkJsU/edit)
- JavaScript: Mike Samuel's "quasi-literal strawman" on es-discuss (14 Dec 2009). In replies, Perl comes up only for backticks ("In perl, ruby and php is all the system execute command") and for "heredocs in perl and ruby" — [es-discuss thread](https://esdiscuss.org/topic/quasi-literal-strawman); the champion is Mike Samuel and the feature was formerly "Quasi Literals" — [TC39 wiki: Template Strings](https://tc39wiki.calculist.org/es6/template-strings)
- Swift: Lattner's influence list omits Perl — [Daring Fireball](https://daringfireball.net/linked/2014/06/06/lattner)

### Inferences
- Verdicts:
  - **Python f-strings**: common ancestor shell/Perl 5. PEP 215 documents Perl 5; PEP 498 credits nobody.
  - **JS template literals**: common ancestor E-language quasi-literals (the name; see Gaps) and shell/Perl 5/Ruby.
  - **Scala**: independent / undocumented.
  - **Swift `\()`**: independent / undocumented.
  - **Kotlin `${}`**: common ancestor Groovy/shell (unverified).
  - **C# 6 `$"{}"`**: undocumented here.
  - **Ruby `#{}`**: pre-2000 ancestor.
  - **Dart `${}`**: undocumented.
- The block-like `{ expr }` form is generic. Ruby's `#{}` (1990s) and shell's `${}` predate Perl 6, so an expert should treat Raku as a sibling, not an ancestor.

### Gaps
- Could not reach the original `strawman:quasis` page (wiki.ecmascript.org is offline, web.archive.org unreachable), so the E-language lineage of "quasi-literal" is unverified. No design notes found for C# 6 interpolation, Kotlin `${}`, Swift `\()` or Dart.

## 7. Indented heredocs that strip the terminator's indentation (`q:to/END/`)

### Takeaway
Perl 6 settled the rule in Apocalypse 2 (May 2001, RFC 162): "remove whitespace equivalent to the terminator". Perl 5.26, PHP 7.3, Swift 4, Java 15 text blocks and C# 11 raw strings all use the closing-delimiter rule, and none cites Perl 6. Ruby 2.3's `<<~` uses a least-indented-line rule instead and came from Rails' `strip_heredoc`. Java's JEP 326 names Perl 5 heredocs as precedent and calls heredocs "an anachronism".

### Cited Findings
- Apocalypse 2 (3 May 2001): "RFC 111: Here Docs Terminators… Fine. RFC 162: Heredoc contents — I think I like option (e) the best: remove whitespace equivalent to the terminator. By default, if it has to dwim, it should dwim assuming that hard tabs are 8 spaces wide." — [Apocalypse 2](https://www.perl.com/pub/2001/05/03/wall.html/)
- Ruby: Feature #9098 (Prem Sichanugrist, 2013-11-10): "I would like to port `String#strip_heredoc` from Rails". Avdi Grimm proposed the "squiggly-heredoc" `<<~EOF` (2013-11-14). Matz: "Accepted. … As a UNIX user, I'd like to take 8 spaces for tab stops." (2015-11-09). Closed 2015-12-07. There is no "perl" or "raku" anywhere in the 24 journals — [Feature #9098](https://bugs.ruby-lang.org/issues/9098)
- Perl 5: commit "Add indented here-docs" by Matthew Horsfall, 2016-11-11: "look for /^\s*$DELIM\n/ as the closing delimiter… strip, from each line in the here-doc, the same whitespace that appears before the delimiter… lines that don't include the proper beginning whitespace will cause perl to croak." No Raku credit — [Perl/perl5 cdd6375](https://github.com/Perl/perl5/commit/cdd6375d6dbc0eea3676faa4e615af1ae28bb103). perl5260delta repeats this text, also with no Raku credit — [perl5260delta](https://raw.githubusercontent.com/Perl/perl5/blead/pod/perl5260delta.pod)
- A CPAN source filter predating this, Filter::Indent::HereDoc, says its terminator style is "specified in Perl6 RFC111" (secondary, via search result) — [Filter::Indent::HereDoc](https://metacpan.org/release/JONALLEN/Filter-Indent-HereDoc-1.01/view/HereDoc.pm)
- Swift SE-0168 (Swift 4.0): indentation stripping is driven by the closing `"""`. A considered "Heredoc" alternative was described only as "Taking a precedent from other languages", with no Perl/Raku named — [SE-0168](https://raw.githubusercontent.com/swiftlang/swift-evolution/main/proposals/0168-multi-line-string-literals.md)
- PHP 7.3 (RFC v0.9, 2017-09-16, Thomas Punt): "The indentation of the closing marker dictates the amount of whitespace to strip from each line". No other language cited — [PHP RFC: Flexible Heredoc and Nowdoc Syntaxes](https://wiki.php.net/rfc/flexible_heredoc_nowdoc_syntaxes)
- Java: JEP 326 surveyed "C, C++, C#, Dart, Go, Groovy, Haskell, JavaScript, Kotlin, Perl, PHP, Python, R, Ruby, Scala and Swift", listed `qq{...}` for Perl, and said "Heredocs were first used in Unix shells and have found their way into programming languages such as Perl… thought by many to be an anachronism" — [JEP 326](https://openjdk.org/jeps/326). JEP 355/378 text blocks remove "incidental white space" with a re-indentation algorithm in which the closing-delimiter line counts. Brian Goetz is quoted: "Many people have suggested that Java should adopt multi-line string literals from Swift or Rust…" No Perl/Raku — [JEP 378](https://openjdk.org/jeps/378); [JEP 355](https://openjdk.org/jeps/355)
- C# 11 raw string literals: the proposal text contains no references to Perl, Raku, Swift, Python or Java — [csharplang raw-string-literal.md](https://raw.githubusercontent.com/dotnet/csharplang/main/proposals/csharp-11.0/raw-string-literal.md)

### Inferences
- Verdicts:
  - **Ruby `<<~`**: independent, from Rails `strip_heredoc` (documented). Its rule is the minimum indentation, not the terminator's.
  - **Perl 5.26 `<<~`**: plausibly influenced by Perl 6 (circumstantial). It adopts exactly A2's "whitespace equivalent to the terminator" rule, but borrows Ruby's `~` spelling. No credit in commit or perldelta.
  - **Swift 4**: independent (closing-delimiter rule, no citation).
  - **PHP 7.3**: independent (no citation).
  - **Java 15**: independent; it cites Perl 5 heredocs only as rejected prior art.
  - **C# 11**: independent (no citation).
  - **Kotlin `trimIndent()`**: library function, not researched.
- The "closing delimiter sets the indentation" rule that Perl 6 picked in 2001 became the industry consensus (Swift, PHP, Java, C#, Perl 5). The record supports convergent design, not borrowing.

### Gaps
- The p5p discussion behind Horsfall's 2016 patch was not found; that is where a Raku mention would most likely be. Python textwrap.dedent and the related proposals were not researched. Ruby 2.3.0's release announcement doesn't mention `<<~`, and NEWS-2.3.0 couldn't be fetched, so the 2.3.0 release date (2015-12-25, [release post](https://www.ruby-lang.org/en/news/2015/12/25/ruby-2-3-0-released/)) is the evidence that it shipped there.

## 8. Pair / colon-pair shorthand (`:$x` → `x => $x`)

### Takeaway
The punning shorthands that came later (ES2015 `{x}`, Rust `S { x }`, Ruby 3.1 `{x:, y:}`, Python `f"{x=}"`, rejected PEP 736 `f(x=)`) cite ES6, Haskell NamedFieldPuns, OCaml, ReasonML and each other. None cites Raku. Rust considered and rejected a prefix-colon form `S { :x }` (very close to Raku's `:$x`), citing MoonScript.

### Cited Findings
- Rust RFC 1682 (start 2016-07-18): "This RFC takes inspiration from the Haskell NamedFieldPuns extension, and from ES6 shorthand property names." Alternative: "if the sigil were `:`, the existing syntax `S { x: x }` would be expressed as `S { :x }`. This is used in MoonScript… in languages such as Ruby and Elixir, `:x` denotes a symbol or atom, which may be confusing" — [RFC 1682](https://raw.githubusercontent.com/rust-lang/rfcs/master/text/1682-field-init-shorthand.md)
- Ruby 3.1 hash/keyword value omission: Feature #14579 (Shugo Maeda, 2018-03-06). Matz (2018-03-15): "this introduces a Ruby-specific syntax different from ES6 syntax"; the proposal was withdrawn, then accepted (2021-09-11): "Our mindset has been updated (mostly due to mandatory keyword arguments). Accepted." The only Perl mention is against the ES6-style `{x, y}`: "too close to perl's weird handling of lists/hashes" (Matthew Kerwin) — [Feature #14579](https://bugs.ruby-lang.org/issues/14579). Ruby 3.1.0 (2021-12-25): "Values in Hash literals and keyword arguments can be omitted. [Feature #14579]" — [Ruby 3.1.0 released](https://www.ruby-lang.org/en/news/2021/12/25/ruby-3-1-0-released/)
- Python `f"{x=}"` (3.8): CPython issue #80998 / bpo-36817, "Add = to f-strings for easier debugging" (2019-05-06), developed from a `!d` proposal (PR #13059). The only other language cited is Rust's `dbg!` — [python/cpython#80998](https://github.com/python/cpython/issues/80998)
- PEP 736 (created 28-Nov-2023, Rejected): its Prior Art names f'{x=}', Ruby `f(x:, y:)`, ReasonML `f(~x, ~y)`, SystemVerilog `.name` port connections, Jakt, OCaml let-punning, JavaScript `{ x, y }`, and Rust `User { x, y }`. No Raku — [PEP 736](https://peps.python.org/pep-0736/)

### Inferences
- Verdicts:
  - **JS ES2015 shorthand**: independent / undocumented (see Gaps).
  - **Rust**: common ancestor Haskell NamedFieldPuns plus ES6 (documented).
  - **Ruby 3.1**: common ancestor ES6, with Ruby keyword syntax (documented).
  - **Python `f"{x=}"`**: independent; Rust `dbg!` cited.
  - **PEP 736**: common ancestor Ruby/OCaml/JS/Rust (documented).
- Raku's `:$x` is arguably the closest precedent for PEP 736 (named-argument punning at call sites, since 2000s design) and for Rust's rejected `:x` form, yet neither survey lists it. That supports "independent invention" rather than any Raku influence.

### Gaps
- Origin of ES2015 shorthand properties (harmony "object initialiser shorthand") was not found; wiki.ecmascript.org is offline. The date of Raku's `:$x` in the design docs (S02/S06) was not verified this session.

## 9. Pipelines / feeds (`==>`, `<==`)

### Takeaway
Perl 6's `==>`/`<==` (Apocalypse 6, Mar 2003) explicitly model Unix pipes. The `|>` family descends from Isabelle/ML (Nipkow, 1994, "with obvious semiotic inspiration from UNIX pipes"), then F# (2003), OCaml 4.01 (2013), Elixir and the TC39 F#/Elixir/Hack debate (2015–). No pipeline proposal mentions Perl 6.

### Cited Findings
- Apocalypse 6 (7 Mar 2003): "List operators are like commands in Unix… streams of data coming in and going out via pipes… There will be two new operators, called pipe operators… `1,2,3 ==> stuff @foo`… the ==> and <== operators are big, fat, and obnoxiously noticeable. I like them that way." — [Apocalypse 6](https://www.perl.com/pub/2003/03/07/apocalypse6.html/)
- Isabelle/ML 1994: Don Syme reproduces the April/May 1994 emails among Larry Paulson, Tobias Nipkow and Makarius Wenzel in which Nipkow "came up with the actual name |>" — [Syme, "Archeological Semiotics: The Birth of the Pipeline Symbol, 1994"](https://learn.microsoft.com/en-us/archive/blogs/dsyme/archeological-semiotics-the-birth-of-the-pipeline-symbol-1994)
- F#: "the 'pipe-forward' operator, added to the F# standard library in 2003… the use of the pipeline symbol in ML dialects actually originates from Tobias Nipkow, in May 1994 (with obvious semiotic inspiration from UNIX pipes)" (excerpt of Syme's HOPL 2020 paper) — [Batsov quoting "The Early History of F#"](https://batsov.com/articles/2025/05/22/the-origin-of-the-pipeline-operator/); [HOPL paper PDF](https://fsharp.org/history/hopl-final/hopl-fsharp.pdf)
- OCaml 4.01.0 (12 Sep 2013): "infix application operators |> and @@ in Pervasives" — [OCaml Changes](https://raw.githubusercontent.com/ocaml/ocaml/trunk/Changes)
- TC39: the history goes 2015–2017 with "comparing F# pipes with Elixir pipes" and "placeholder pipes (i.e., Hack pipes)", then F# vs Hack. The README compares Hack-language pipes and Clojure threading macros. "perl" and "raku" do not appear in README.md or HISTORY.md — [HISTORY.md](https://raw.githubusercontent.com/tc39/proposal-pipeline-operator/main/HISTORY.md); [README.md](https://raw.githubusercontent.com/tc39/proposal-pipeline-operator/main/README.md)
- Elixir's CHANGELOG (v0.10.0) records `|>` precedence changes and deprecation of an older `/>` operator in favour of `|>` — [Elixir v0.10.0 CHANGELOG](https://raw.githubusercontent.com/elixir-lang/elixir/v0.10.0/CHANGELOG.md)

### Inferences
- Verdicts:
  - **F# `|>`**: common ancestor Isabelle/ML 1994, itself from Unix pipes (documented).
  - **OCaml `|>`**: common ancestor ML/F#.
  - **Elixir `|>`**: common ancestor F# (circumstantial; not verified).
  - **Hack `|>` with `$$`**: independent topic-variable design (date not verified).
  - **R 4.1 `|>`**: common ancestor magrittr/F# (not verified).
  - **TC39**: common ancestor F#/Elixir/Hack (documented).
  - **Julia `|>`**: F#-lineage (not verified).
  - **Perl 6 `==>`**: Unix shell ancestor (documented in A6).
- Raku's feeds and Hack's `$$` topic share the "data into an argument slot" idea, but Raku feeds append to the list (slurpy) arguments rather than filling a placeholder.

### Gaps
- Not verified: R 4.1.0 native pipe date (May 2021; R NEWS pages failed to load), Hack pipe introduction date, the first Elixir release with `|>`, Julia `|>` origin.

## 10. Hyper / reduction / cross / zip metaoperators vs broadcasting

### Takeaway
Perl 6's own design cites APL for hyper operators ("APL, here we come… :-)", A3 2001). Julia's dot-broadcast (0.5 in 2016, fusion in 0.6 in 2017) cites Matlab, NumPy, R and Chapel, not Perl 6. The shared ancestor is APL and array languages.

### Cited Findings
- Apocalypse 3, RFC 082 "Arrays: Apply operators element-wise in a list context": "APL, here we come… :-)"; hyper ops "will also intuit where a dimension is missing from one of its arguments, and replicate a scalar value to a list value in that dimension". The original marker was `^` ("Unary ^ is now reserved for hyper operators") — [Apocalypse 3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/)
- Julia, "More Dots: Syntactic Loop Fusion" (Jan 2017): "After a lengthy design process (#8450) and preliminary foundations in Julia 0.5, Julia 0.6 includes new facilities for writing code in the 'vectorized' style (familiar from Matlab, Numpy, R, etcetera)". It contrasts Chapel's implicit vectorization of every function (Chamberlain et al. 2011). There is no Perl mention — [Julia blog](https://julialang.org/blog/2017/01/moredots/). The design issues #8450 ("Alternative syntax for `map(func, x)`", 2014-11-16) and #15032 (`f.(args...)`, 2016-02-11) contain no "perl"/"raku"/"hyper" in the issue bodies and comments fetched — [JuliaLang/julia#8450](https://github.com/JuliaLang/julia/issues/8450), [#15032](https://github.com/JuliaLang/julia/pull/15032)

### Inferences
- Verdicts:
  - **Julia `.+` / `f.(x)`**: common ancestor Matlab/NumPy/APL (documented: Matlab/NumPy/R).
  - **NumPy broadcasting**: common ancestor APL/Numeric (pre-2006; not verified).
  - **Python `sum`/`any`/`all`/`functools.reduce`**: pre-2000 / Lisp ancestors (not researched).
  - **APL/J**: ancestors of Raku's `[+]` (reduce `+/`), `X` (outer product `∘.`), and hyper.
- Julia's `.op` and Raku's `»op«` both mark element-wise application syntactically, so behaviour doesn't depend on value types. A2/A3 made that choice in 2001 against implicit vectorization. Julia argued the same against Chapel 15 years later, with no citation, so this is convergent design.

### Gaps
- NumPy broadcasting history and date; cross/zip metaoperator analogues (Python `itertools.product`/`zip`, Haskell `zipWith`) were not traced.

## 11. Unicode operators and identifiers

### Takeaway
Apocalypse 3 (2001) already planned "the (judicious) use of Unicode operators". Fortress, Julia, Swift and Scala adopted Unicode operators on their own. I found no citation of Perl 6, and Scala deprecated its Unicode arrows in 2.13 (PR merged Jan 2019).

### Cited Findings
- Apocalypse 3: "Rather than using longer and longer strings of ASCII characters to represent user-defined operators, it will be much more readable to allow the (judicious) use of Unicode operators." Apocalypse 2: "Perl 6 programs are notionally written in Unicode" — [Apocalypse 3](https://www.perl.com/pub/2001/10/02/apocalypse3.html/); [Apocalypse 2](https://www.perl.com/pub/2001/05/03/wall.html/)
- Scala PR "Deprecate unicode arrows `⇒`, `←` and `→`" (opened 2018-12-15, merged 2019-01-18, fixes scala-dev#585 / scala/bug#11210) — [scala/scala#7540](https://github.com/scala/scala/pull/7540)
- A GitHub issue search for "Perl 6" in JuliaLang/julia gives 19 incidental hits (build issues, version strings) and none about operator design. A search for "raku" gives 2 incidental hits — [search: repo:JuliaLang/julia "Perl 6"](https://github.com/search?q=repo%3AJuliaLang%2Fjulia+%22Perl+6%22&type=issues)

### Inferences
- Verdicts:
  - **Julia**: common ancestor mathematical notation / Fortress / Mathematica (not documented).
  - **Fortress**: independent (math-notation goal).
  - **Swift**: independent.
  - **Agda**: Haskell/math ancestry.
  - **Scala `⇒`**: independent, later deprecated.
- No researched language attributes its Unicode-operator support to Perl 6/Raku.

### Gaps
- Not researched in primary sources: Fortress specification dates, Julia's first Unicode operator commits, Swift's operator-character rules.

## 12. Spaceship `<=>`, `say`, string `++`, `x`/`xx`, numeric separators

### Takeaway
These are Perl 5 inheritances, and the later proposals that cite anyone cite Perl 5, never Raku. PHP's spaceship RFC credits Perl, Ruby and Groovy, and C++'s P0515 lists Perl `<=>` as existing practice. For digit separators PEP 515 and TC39 list Perl 5, and C# cites "Java and others". `say` went from Perl 6 to Perl 5, and Perl's own docs call it "Raku-inspired".

### Cited Findings
- PHP 7 `<=>` (RFC 2014-02-12, v0.2 2015-01-19): "also known as the 'spaceship operator' (a common name in other languages)… It exists in other languages: Perl, Ruby, and Groovy. For consistency with Perl, it has the same precedence as == and !=." — [PHP RFC: Combined Comparison (Spaceship) Operator](https://wiki.php.net/rfc/combined-comparison-operator)
- C++20, P0515R0 (Herb Sutter, 2017), existing-practice table: "C strcmp… C# IComparable.CompareTo… Java Comparable.compareTo… Groovy <=>… OCaml compare… Perl <=>… PHP <=> (since PHP 7)… Python cmp… Ruby <=>. Of the major languages, only Haskell returns a type (an enumeration)." — [P0515R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0515r0.pdf)
- Perl 5 `say`: "`use feature 'say'` tells the compiler to enable the Raku-inspired `say` function… available starting with Perl 5.10" — [feature.pm](https://raw.githubusercontent.com/Perl/perl5/blead/lib/feature.pm); perl5100delta credits the implementation to Robin Houston — [perl5100delta](https://raw.githubusercontent.com/Perl/perl5/blead/pod/perl5100delta.pod)
- PEP 515 (created 10-Feb-2016, Python 3.6), Prior Art: Ada, C# (open proposal for 7.0), C++14 (different separator), D, Java, Julia, "Perl 5: multiple, basically anywhere, although docs say it's restricted to one underscore between digits", Ruby, Rust, Swift. No Raku — [PEP 515](https://peps.python.org/pep-0515/)
- TC39 numeric separators: "`_` (Java, Python, Perl, Ruby, Rust, Julia, Ada, C#)". Prior art: Java 7, C++ `'`, Swift, "Perl: multiple, anywhere", Ruby, Rust, Julia, Ada, PEP 515, C# proposal — [proposal-numeric-separator](https://raw.githubusercontent.com/tc39/proposal-numeric-separator/master/README.md)
- C# 7 digit separators (roslyn#216): "We would follow Java and others, and use an underscore `_` as a digit separator." — [dotnet/roslyn#216](https://github.com/dotnet/roslyn/issues/216)

### Inferences
- Verdicts:
  - **PHP `<=>`**: borrowed from Perl 5 (documented).
  - **C++20 `<=>`**: Perl 5 named as existing practice for the spelling (documented as prior art, not as a borrowing statement). Its typed result (`strong_ordering` etc.) is credited to Haskell.
  - **Ruby `<=>`**: pre-2000 Perl 5 ancestor.
  - **Perl 5 `say`**: borrowed from Perl 6 (documented).
  - **Python `_` separators**: common ancestor Ada / Perl 5 and others (documented).
  - **JS separators**: same, with Perl listed (documented).
  - **C# 7 separators**: borrowed from Java (documented).
  - **Java 7 separators**: not verified.
- P0515 says only Haskell returns a type from three-way comparison. That overlooks Raku's `<=>`/`cmp`, which return the `Order` enum (`Less`/`Same`/`More`) — a precise point for an expert audience. It shows the C++ survey didn't consider Raku, consistent with the pattern in the other sections.

### Gaps
- Not checked: the Java 7 Project Coin "underscores in literals" proposal and its citations; string `++` (Ruby `String#succ`) and `x`/`xx` (Python `*`) origins; whether any language besides Perl 5 adopted `say`. REXX `SAY` is a likely ancestor, but I found no source for it this session.
