# Mentions — Raku++ written about by other people

Where somebody outside this project wrote about Raku++: newsletters, posts,
threads, remarks in passing. Each entry is a link, a date, a name, and a plain
account of what was actually said.

Three neighbouring files hold the things this one is not:

- **[BLOG.md](BLOG.md)** — our own posts, on andrewshitov.com and dev.to.
- **[TALKS.md](TALKS.md)** — our own talks.
- **[live/ADOPTIONS.md](live/ADOPTIONS.md)** — other people's *software* that
  reached for the engine, which is adoption rather than a mention.

The bar: **somebody else wrote it, published where a third party can read it
without going through us, and it can be linked and dated.** A mention that is
lukewarm or critical belongs here on the same terms as a warm one — the point
of the file is the record, not the applause. Our own announcement is the thing
being reacted to, not a mention of it.

A link post is not writing. Our own posts get submitted to /r/rakulang and
relayed by the newsletter as a matter of course, and neither the submission
nor the relay is somebody choosing to write about Raku++ — so a thread only
earns a row here when a third party started it, or when the comments under it
say something worth quoting.

| Date | Where | Who | What |
|---|---|---|---|
| 27 July 2026 | [Rakudo Weekly 2026.28/29/30](https://rakudoweekly.blog/2026/07/27/2026-28-29-30-thank-you/) | Elizabeth Mattijsen | a section of its own, introducing Raku++ to the Raku community |
| 10 August 2026 | [Rakudo Weekly 2026.32](https://rakudoweekly.blog/2026/08/10/2026-32-dan-floating-hex/) | Elizabeth Mattijsen | the live editor-and-playground on raku.online |
| 18 August 2026 | [Rakudo Weekly 2026.33](https://rakudoweekly.blog/2026/08/18/2026-33-all-the-way-to-infinity/) | Elizabeth Mattijsen | the v3.14 release post |
| August 2026 | [on X](https://x.com/perlancar/status/2090616597169967282) | perlancar | start-up time, and native-compiled Raku++ against Perl 5 |
| 25 August 2026 | [Rakudo Weekly 2026.34](https://rakudoweekly.blog/2026/08/25/2026-34-yar-by-coke/) | Elizabeth Mattijsen | the [live/](live/) registry, and JSON::Native |
| 31 August 2026 | [Rakudo Weekly 2026.35](https://rakudoweekly.blog/2026/08/31/2026-35-infinity-revisited/) | Elizabeth Mattijsen | the early-adopters post, the benchmark board, perlancar's remark |
| 14 September 2026 | [Rakudo Weekly 2026.36/7](https://rakudoweekly.blog/2026/09/14/2026-36-7-multiplicity/) | Elizabeth Mattijsen | CSV::Native |

## Rakudo Weekly News

[Rakudo Weekly News](https://rakudoweekly.blog/), written by Elizabeth
Mattijsen, is the Raku community's weekly newsletter, and it has carried
Raku++ from the week of the announcement on.

The first one is the one that matters:
[**2026.28/29/30 Thank you!**](https://rakudoweekly.blog/2026/07/27/2026-28-29-30-thank-you/)
gave the project a section headed *Andrew's Playground* — the compiler,
[raku.online](https://raku.online), [the Long Read](LONGREAD.md), the finished
[Complete Raku Course](https://course.raku.org) and the books, in one place.
It described Raku++ as a Raku compiler written in C++, linked the announcement
and the Long Read, and pointed at the /r/rakulang comments. That is the
introduction to the community, written by somebody else.

After that it has been steady, roughly a mention per release or per post:

- [**2026.32 Dan Floating Hex**](https://rakudoweekly.blog/2026/08/10/2026-32-dan-floating-hex/)
  (10 August 2026) — an *Andrew's Corner* on joining an editor to a playground
  in a live manner, linking the [live-coding post](https://andrewshitov.com/2026/08/05/live-coding-in-raku/)
  and the /r/rakulang thread.
- [**2026.33 All The Way To Infinity**](https://rakudoweekly.blog/2026/08/18/2026-33-all-the-way-to-infinity/)
  (18 August 2026) — the v3.14 post, as an overview of the most interesting
  parts.
- [**2026.34 YAR by Coke**](https://rakudoweekly.blog/2026/08/25/2026-34-yar-by-coke/)
  (25 August 2026) — the [live/](live/) registry of other people's programs
  running unaltered, and the start of modules that use what Raku++ can do,
  [JSON::Native](https://raku.land/zef:ash/JSON::Native) among them, with a
  note on its performance.
- [**2026.35 Infinity Revisited**](https://rakudoweekly.blog/2026/08/31/2026-35-infinity-revisited/)
  (31 August 2026) — the [early adopters](https://andrewshitov.com/2026/08/29/early-raku-adopters/)
  post, the [benchmark board](docs/status/BENCHMARKS.md), and perlancar's
  remark, linked under the heading *First time in ~18 years*. App::Rakus in
  the module list.
- [**2026.36/7 Multiplicity**](https://rakudoweekly.blog/2026/09/14/2026-36-7-multiplicity/)
  (14 September 2026) — [CSV::Native](https://raku.land/zef:ash/CSV::Native)
  in the module list.

## perlancar, August 2026

perlancar — a prolific CPAN author, and the author of Bencher — starred the
repository and wrote [on X](https://x.com/perlancar/status/2090616597169967282)
that he was "genuinely excited to use Perl 6 (Raku) again" for the first time
in something like eighteen years, naming the millisecond start-up and calling
native-compiled Raku++ only twice slower than Perl 5. Rakudo Weekly 2026.35
linked it as *First time in ~18 years*.

It is the most useful thing anyone has written about the project so far,
because the second half of it is a bug report with the location field left
blank. No program was named, so one was built to carry the claim:
[`tools/bench/hashfill.raku`](tools/bench/hashfill.raku) and
[`hashfill.pl`](tools/bench/hashfill.pl), the same work line for line in both
languages under a byte-identical-output gate. perl won it in every mode we
had — the native binary at 113 ms of wall clock against perl's 82. The factor
of two turned out to be three removable constants and none of them the
hashing; after they were removed the compiled mode measured 82.1 ms against
perl's 81.8 in the same sitting. `perl` is now a column in
`run-bench.raku`, and the whole account is in
[docs/dev/findings/HASHFILL-AND-BIGINT.md](docs/dev/findings/HASHFILL-AND-BIGINT.md).

An outside remark became a kernel, and the kernel reached perl. That is what
this file is for.

## Adding an entry

Keep it dated, linked and attributed, keep the note to what the source
actually says, and put it in the table in date order with a section below if
it deserves more than a line. If a mention sends us somewhere — a bug, a
benchmark, a feature — say where it went, the way the perlancar entry does.
A thread we started ourselves, or a relay of our own writing, is not an entry.
Mentions of somebody's *software* using Raku++ go to
[live/ADOPTIONS.md](live/ADOPTIONS.md) instead.
