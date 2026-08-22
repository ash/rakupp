# Reading Other Engines

The last chapter followed one findings document — the Perl 5 study — through
four applied batches. This one is about the shelf that document started. Over
2026-08-21 and -22 the project read nine implementations of dynamic languages
at the design level and wrote one findings doc per engine, each pairing the
engine's primary sources with the measured state of our own structures. The
full ledger lives in the repository under `docs/dev/findings/engines/` and
stays maintained there; this chapter records the method, the shape of
what came back, and the honest accounting of which ideas were imports and
which were convergences.

## The method

A study here is not a survey paragraph. The rules that made the shelf worth
keeping:

- **Primary sources, fetched and dated.** Nikita Popov's zval and hashtable
  write-ups for PHP; the PEPs and release notes for CPython; the object-shapes
  proposal and the basic-block-versioning paper for Ruby; MoarVM's own repo
  docs; the Lua 5.0 implementation paper read in full; Filip Pizlo's
  speculation post for JavaScriptCore; the V8 team's parser posts; Rakudo's
  optimizer source and dispatcher inventory. Where a source was unreachable
  and memory had to serve — some of the MoarVM material — the doc says so
  rather than laundering recollection as citation.
- **Ground every comparison in a measured local number.** The day the PHP
  study was written, a scratch program printed `sizeof` for `Value`,
  `CowStr`, `ValueHash` and its entries, and `ObjectData` on this machine —
  so "their bucket is 32 bytes, our entry is 168" is arithmetic, not
  impression.
- **Rank by transfer, and say what does not transfer.** Every doc carries an
  honest frame — how much of the engine's speed is a smaller semantic
  surface rather than technique — and a section of things deliberately not
  taken, with reasons. LuaJIT's ceiling is not our ceiling; PHP's
  request-scoped allocator does not fit a long-running process; Python's
  everything-boxed layout is the base cost our `Value` avoided.
- **Sort every finding into one of three buckets**: already implemented
  here independently; imported and applied, with measurements; or a design
  input parked in a plan document. The buckets are the point — a pile of
  interesting facts is not a findings doc.

## Nine engines, one line of inheritance each

**Perl 5** contributed the applied playbook of Chapter 40 — pads, result
slots, the stored-hash table, the head/body diagnosis — and the framing
sentence the whole shelf keeps confirming: pay per compile, not per use;
don't carry per value what only some values need.

**PHP 7** is the same family's playbook executed as one deliberate rewrite —
roughly 2× on real applications with no JIT — and the best-documented proof
that the order is layout first, dispatch second, compilation machinery a
distant third. Its new levers for us: per-callsite dispatch caches,
compile-time attribute slots, and `zend_reference` — the production
precedent for "a container cell should exist only where binding demands
one", which is the shape of our container refactor.

**CPython** supplied the control layer: adaptive specialisation with
counters and cheap de-optimisation (PEP 659), the type version tag as the
invalidation half of any cache design, the twelve-year migration of
attribute storage toward slots, and lazy frames that survive introspection
as demanding as `CALLER::`. Its free-threading design (PEP 703) reads as
corroboration from the other direction: the harden-the-runtime road our
parallel plan had already chosen is the road it takes, and its worked-out
mechanisms — biased refcounts, immortal objects, per-container critical
sections — now feed that plan.

**Ruby** is the closest language sibling, and its two contributions are a
key and a warning: object shapes show the attribute-cache key can work
*across* classes (which roles make more relevant here than it is in Ruby),
and the years its method cache spent behind one global serial — any
definition anywhere flushing everything — are the cautionary tale our
`ClassInfo` serial is designed against. YJIT's lazy basic-block versioning
also gave a name to a habit this codebase already had: specialise on first
observation, not on a counter.

**MoarVM** — the reference VM for this same language — is less a bag of
techniques than a tax map: the guards its specialiser inserts are a ranked
list of what Raku semantics cost, and containers sit at the top, which is
independent confirmation of our refactor's aim. Its one directly actionable
export is the interned callsite: the argument shape of a call is static, so
the binder should compute a binding plan once per callsite-and-callee, not
rediscover named arguments by scanning every argument list on every call.

**Lua** contributed the sharpest single design in the shelf: upvalues —
per-variable capture cells, pointing into the frame while it lives and
closing over the value when it dies — which is the closure half of the
container refactor, reached by a third independent road. Its register VM
paper is filed as the tightest starting spec for a future threaded
execution loop.

**JavaScriptCore** published the constants everyone else implies: a wrong
speculation costs three to four orders of magnitude more than a right one
saves, so speculate only when the probability of success is
indistinguishable from one — the quantified form of a rule our
decided-once flags and MoarVM's statistics both already practise. Its
watchpoints completed the invalidation menu (check per use, subscribe and
jettison, or let the key miss), and Bun — a runtime shell around JSC, not
a faster engine — is market evidence for this project's founding bet:
startup plus a native runtime surface plus drop-in compatibility wins
users before peak throughput does.

**V8** earned a scoped doc for one idea: don't compile what you don't run.
Preparse everything, parse a body on first call, and save the skipped
body's summary so nothing is ever parsed twice — with a documented trap
(superlinear reparsing) and a sequencing insight we would not have found
alone: lazy bodies are trivial under whole-frame capture and hard after
per-variable capture, so those two designs must be written together. At a
2–3 ms startup none of it is today's bottleneck; the doc is explicitly a
when-the-time-comes study, and the time is module-scale programs.

**Rakudo**, ninth and last, required a decision recorded in its opening:
until 2026-08-22 this project deliberately never read Rakudo's code.
Reading it at the design level produced the only catalogue of its kind —
eighteen *Raku-legal* static optimisations, each one a shortcut the
language's own designers pre-litigated — and a dispatcher inventory that
prices the semantic sites (in the reference implementation, even
assignment and boolification are guarded dispatches). The stance that
emerged is stated in the study and in the project's founding documents
alike: read designs, never port code; Roast remains the only definition of
correct.

## Already ours, before any study

The shelf could leave the impression that every fast mechanism here traces
to someone else. The record says otherwise, and the series index states it
plainly: several of the findings were implemented in this codebase before
the corresponding engine was read. Copy-on-write strings with cached scan
state predate the perl study that expected to teach them (Chapter 9's
`CowStr`, out of the string-scanning work); in-place `~=` append predates
the two engines whose rope designs it answers; the decide-once-at-first-
execution habit predates the literature that names it; conditions-as-bool
landed from our own profiles days before the PHP and Lua studies found the
same fusion called "smart branch" in both; the lazily materialised
rare-case block (`EnvExtras`) predates the engines that institutionalise
the pattern; and the Array/Hash split means three engines' packed-array
machinery solves a problem this design never had.

The distinction matters beyond credit. An idea reached independently here
and by an engine under different constraints is *stronger* evidence than
an idea copied — most of the convergence list below is agreement this
project is one of the witnesses to, not a syllabus it received.

## What the engines agree on

Nine engines, read against each other, agree more than they differ. The
short form of the convergences the index records:

- The value head is small; the body is behind a pointer. Four independent
  data points bracket the endgame at 8–24 bytes; with containers and the
  numeric tower, ours lands at 16–24.
- Arguments belong in the callee's frame, not in an argument object — Lua
  register windows, PHP's SEND, MoarVM's callsite buffers.
- Strings intern once and carry their hash — five engines.
- Specialisation runs under one of two policies — counted warmup or first
  observation — and always counts its failures and retires its losers.
- Invalidation has exactly three modes, and mutation frequency picks one.
- Ropes are a last resort; both engines that rope grew flattening
  heuristics when reads suffered.
- Resumable control flow forbids a C-stack-recursive runloop — two VMs,
  independently, which is why first-class `gather` resumption waits for
  the threaded loop rather than arriving before it.
- Layout before dispatch before compilation machinery. The JITs bolted on
  late moved real workloads least; the interpreters' layout years moved
  them most.

## Where it stops, for now

The campaign and the reading programme end at the same line: everything
cheap and local is banked, and what remains is architecture — the
head/body endgame coupled to the container refactor, the callsite cache
programme, the threaded loop, lazy bodies. Each has its design inputs
parked in a plan document with the relevant studies cited, so the work can
start from evidence instead of from recollection.

The next campaign is not speed at all — it is modules. The shelf will
still be there when the profiles point back.
