# Module wishlist — what real-life Raku is missing

Survey date: 2026-08-02.

Goal: identify the modules worth writing so that Raku (and specifically Raku++)
is usable for ordinary day-to-day programming — the things people reach for in
Perl, Python, Node and Ruby without thinking about it. Each candidate is
annotated with what already exists in the Raku ecosystem (raku.land).

## Method

Demand was taken from four ecosystems, by the metric each one publishes:

| Ecosystem | Metric | Source |
|---|---|---|
| CPAN | top 300 distributions by *immediate* reverse dependencies | MetaCPAN `distribution/_search`, sorted `river.immediate` |
| PyPI | top 15 000 projects by download count (top 200 used) | `hugovk.dev/top-pypi-packages` |
| npm | most-depended-upon packages | published summaries (see Sources) |
| RubyGems | most-downloaded gems | `rubygems.org/stats` |

Download counts overstate build-time and transitive plumbing (`certifi`,
`six`, `packaging`, `ExtUtils-MakeMaker`). Candidates below were selected on
*what a developer deliberately reaches for*, and cross-checked so that each one
appears near the top of at least two of the four ecosystems.

Supply was taken from the actual Raku ecosystem, not from memory:

- `https://360.zef.pm/` — the fez/zef index, 1 584 distinct module names. This
  is what `zef install` resolves against today.
- `https://raw.githubusercontent.com/Raku/REA/main/META.json` — the Raku
  Ecosystem Archive, adding 928 further names that exist only in the legacy
  p6c/CPAN ecosystems.
- 2 512 unique module names total. raku.land indexes both, so "it is on
  raku.land" is not the same as "it is maintained" — the tables below
  distinguish **fez** (current, zef-installable) from **REA-only** (legacy,
  typically last touched years ago).

## The constraint that should drive the choice

Raku++'s NativeCall resolves symbols with `dlsym` and calls them through a
fixed 8-integer/8-float prototype, relying on the platform ABI to place
arguments in the right registers. There is no `libffi`. Measured against the
binary on 2026-08-02 (`docs/guide/FEATURES.md` understates this — mixed args and
callbacks do work):

- **Works:** integer/pointer/float args, mixed int+float, `Str`→`char*`,
  `CArray`, `CStruct`/`CPointer` handles, `is rw` out-params with copy-back,
  synchronous callbacks (a 64-slot trampoline pool), and struct-by-value
  returns up to 8 bytes if you declare them as `int64` and unpack by hand.
- **Clean error:** more than 8 integer or 8 float register arguments.
- **Silently wrong:** variadic C functions. `snprintf(buf, 16, "%d", 42)`
  yields `"0"`, not `"42"` — variadic arguments go on the stack on the Apple
  ARM64 ABI, and the fixed prototype puts them in registers.
- **Unsupported:** by-value struct *arguments*, struct returns larger than a
  register pair, and callbacks that C stores and fires later from another
  thread.

So the ABI ceiling is lower than the *deployment* ceiling, and it is the
latter that should drive this list — see below.

At least 10% of the current fez ecosystem is C bindings by self-description,
and that 10% is concentrated precisely in the infrastructure categories:

| Category | The Raku module people use | Bound to |
|---|---|---|
| XML | `LibXML` | libxml2 |
| Compression / archives | `Archive::Libarchive`, `Compress::Bzip2` | libarchive, libbzip2 |
| Password hashing | `Crypt::Argon2`, `Crypt::SodiumPasswordHash`, `Crypt::LibGcrypt` | libargon2, libsodium, libgcrypt |
| Fast digests | `Digest::SHA256::Native` | C |
| TUI | `Notcurses::Native`, `Selkie` | notcurses |
| Images | `GD`, `MagickWand`, `Imlib2`, `Image::Resize` | libgd, ImageMagick, imlib2 |
| Numerics | `Math::Libgsl::*` | libgsl |
| SSH | `SSH::LibSSH` | libssh |
| HTTP (alt) | `LibCurl`, `Net::Curl` | libcurl |
| Spreadsheets | `Spreadsheet::Libxlsxio` | libxlsxio |
| YAML (full) | `YAML::Parser::LibYAML`, `LibYAML` | libyaml |
| CSV (fast) | `Text::CSV::LibCSV`, `Duck::CSV` | libcsv, DuckDB |

So the selection rule for this project writes itself: **prefer pure-Raku
implementations**. Note the reason carefully — it is *not* mainly that Raku++
cannot call these libraries (mostly it can; most C APIs are pointer-based, and
those work today). It is that a native binding turns "install Raku++ and run"
into "install Raku++, then find libxml2 / libsodium / notcurses / libgsl for
your platform, in a compatible version". Raku++'s value proposition is a
single portable binary, and every native dependency spends it. Adding `libffi`
would raise the ABI ceiling but would not change this at all.

Every Tier A candidate below is pure-Raku-feasible.

Two further rules follow from the same constraint:

1. Anything that *must* be native (TLS, fast SHA, zlib inflate) is better
   provided as a Raku++ **built-in** than as a module — the interpreter can
   expose it directly, and the module then becomes pure Raku on top.
2. A pure-Raku module whose hot loop is slow is a Raku++ optimisation target,
   not a reason to bind C. That feedback loop is the actual advantage of
   writing these ourselves.

---

## Tier A — high cross-language demand, real gap in Raku

Twelve modules. These are the ones where every other ecosystem has an obvious
answer and Raku does not.

### A1. `HTTP::Simple` — a batteries-included HTTP client

The single most-demanded capability in the survey. PyPI's top 10 is
effectively an HTTP client and its dependencies (`requests`, `urllib3`,
`certifi`, `idna`, `charset-normalizer`); CPAN has `libwww-perl` at #14 and
`HTTP-Message` at #18; npm has axios; Ruby has faraday.

**Raku today:** `Cro::HTTP::Client` (fez) is capable but drags in the whole Cro
stack; `HTTP::UserAgent` (fez) is synchronous and long unmaintained;
`HTTP::Tiny` and `WWW` (both fez) are deliberately minimal; `LibCurl` and
`Net::Curl` are REA-only *and* native.

**Gap:** no light, current, pure-Raku client that does redirects, cookies,
TLS, JSON request/response, timeouts, retries, proxies and streaming bodies
behind one call. **Status: partially served, badly.**

### A2. `Data::Schema` — declarative validation and coercion

Python's `pydantic` is #14 on PyPI with `pydantic-core` at #18,
`annotated-types` at #32 and `pydantic-settings` at #88; `jsonschema` and
`attrs` are also top-30. CPAN has `Type-Tiny` (#28), `Data-Sah` (#78),
`Params-Validate` (#88).

**Raku today:** `JSON::Schema` and `LIVR` are **REA-only**;
`OpenAPI::Schema::Validate` is REA-only; `Function::Validation` is narrow.

**Gap:** parse untrusted JSON/YAML into typed Raku objects with coercion and a
structured error report. Raku's subsets, `where` clauses and `TWEAK` make this
a better fit than in any of the source languages. **Status: missing (stale
only).** Highest value-per-line on the list.

### A3. `JSON::Schema` (maintained) — Draft 2020-12 validator

Distinct from A2: needed to consume other people's schemas (OpenAPI, config
validation, LLM structured output). The existing `JSON::Schema` is REA-only.
**Status: stale.**

### A4. `Log` — one structured logging standard

`logging` ships with Python and `loguru`/`structlog` are ubiquitous; CPAN has
`Log-ger` (#61), `Log-Log4perl` (#82), `Log-Any` (#87), `Log-Dispatch`; npm has
winston/pino.

**Raku today:** `Log::Async`, `Log::Dispatch`, `Log::Timeline`, `Lumberjack`
(fez) and `LogP6` (REA-only) — five competing designs, none dominant, none with
JSON/structured output plus per-module level filtering plus pluggable sinks.

**Gap:** fragmentation. **Status: partially served.**

### A5. `CLI` — subcommands, config and completion on top of `MAIN`

`click` is #12 on PyPI, `typer` at #110, `rich` at #53; CPAN has `Getopt-Long`
at #17 and `App-Cmd`; npm has commander and yargs.

**Raku today:** `MAIN` + `USAGE` is genuinely good for a single command.
`Getopt::Long` is **REA-only**.

**Gap:** git-style subcommands, `--flag` bound to env vars and config files,
shell completion generation, coloured help, `--dry-run`/`--verbose`
conventions. **Status: partially served (core only).**

### A6. `Terminal::Rich` — colour, tables, progress, tracebacks

`rich` (#53), `colorama` (#95), `tqdm` (#48), `tabulate`, `wcwidth` (#131) on
PyPI; `chalk` on npm; `Term-ANSIColor` (#100) on CPAN.

**Raku today:** `Terminal::ANSIColor` (fez, colour only), `Terminal::Gauge`,
`Progress::Bar`, `Term::ProgressBar` (REA-only), `Terminal::Spinners`
(REA-only), `Text::Table::Simple` (**REA-only**), `Gzz::Text::Utils`. Scattered
across seven half-modules; the table module — the most-used piece — is legacy.

**Gap:** one coherent, pure-Raku, Unicode-width-correct output toolkit.
**Status: fragmented.** Cheap to build, visible in every script.

### A7. `HTML::DOM` — HTML parsing with CSS selectors

`beautifulsoup4` (#92), `lxml` (#100), `soupsieve` (#98) on PyPI; `HTML-Parser`
(#68) and `HTML-Tree` on CPAN; cheerio on npm; nokogiri on Ruby.

**Raku today:** `DOM::Tiny` — the natural choice — is **REA-only**. `Gumbo` and
`HTML::MyHTML` are REA-only *and* native. `HTML::Parser` (fez) is a role, not a
tree with selectors. `Web::Scraper` (fez) sits on top of the above.

**Gap:** a maintained pure-Raku parser producing a tree with
`.querySelectorAll`. **Status: stale.**

### A8. `YAML` — complete, not a subset

`pyyaml` is #15 on PyPI. Config files are YAML nearly everywhere.

**Raku today:** `YAMLish` (fez) describes itself as "a useful subset of YAML".
Full YAML means `LibYAML`/`YAML::Parser::LibYAML` — native, therefore off-limits
under Raku++.

**Gap:** anchors, aliases, merge keys, multi-document streams, block scalars,
tags, round-trip emit. **Status: partially served.**

### A9. `AWS::S3` (and SigV4 core)

`boto3` is **#1 on PyPI**, `botocore` #10, `s3fs` #43, `s3transfer` #29,
`awscli` #121; `aws-sdk-core` is **#2 on RubyGems**.

**Raku today:** `WebService::AWS::Auth::V4` (fez) signs requests and stops
there. `Amazon::DynamoDB` and `AWS::Session` are REA-only. `Kivuli` and `Ikoko`
cover EC2 role credentials and Secrets Manager only.

**Gap:** the largest single absence in the Raku ecosystem relative to every
other language. Scope realistically: SigV4 + credential chain (env, profile,
IMDS) + S3 + SQS + SNS. Pure Raku on top of A1. **Status: missing.**

### A10. `Cache` — LRU/TTL with pluggable backends

`cachetools` (#125 PyPI), `CHI` on CPAN, `redis`/`memcached` everywhere.

**Raku today:** `Cache::LRU`, `Propius`, `Cache::Memcached`, `Web::Cache` are
**all REA-only**. `Cache::Async` (fez) is async-specific; `ObjectCache` and
`Ecosystem::Cache` are narrow. **Status: stale.**

### A11. `Resilience` — retry, backoff, timeout, rate limit, circuit breaker

`tenacity` is #97 on PyPI and this pattern is in every production HTTP path.

**Raku today:** `Retry` (fez) exists but is minimal; nothing else.
**Status: partially served.**

### A12. `Prometheus` / metrics endpoint

Every long-running service in the survey exposes metrics; the OpenTelemetry
packages occupy ~12 slots in PyPI's top 200.

**Raku today:** `Prometheus::Client` and `Template::Prometheus` are **REA-only**.
Nothing current. **Status: stale.** Pure Raku, small, and it makes Cro/
Humming-Bird services deployable.

---

## Tier B — demand is real, Raku has something that needs consolidating

Eighteen. Lower priority than Tier A, but each is a genuine friction point.

| # | Capability | Cross-language anchors | Raku today | Verdict |
|---|---|---|---|---|
| B1 | Date parsing/formatting/humanising | `python-dateutil` #16, `arrow`, `DateTime` #22 CPAN, `pytz` #64 | `DateTime::Format`, `DateTime::strftime`, `Date::Calendar::Strftime`, `Timezones::ZoneInfo`, `DateTime::Timezones`, `LocalTime` — six overlapping (all fez) | consolidate: parse-anything + tz + durations |
| B2 | Path & file utilities | `Path-Tiny` #33 CPAN, `pathlib`, `File-Slurp` #47, `filelock` #38 | core `IO::Path` is good; `File::Find`, `paths`, `File::Directory::Tree` (fez) | add atomic write, lazy slurp, tree copy, locking |
| B3 | Running subprocesses | `subprocess`, `IPC-Run`, `IPC-System-Simple`, `psutil` #109 | `Proc::Async` (core, low-level), `Proc::Easy`/`Proc::Easier` (fez, thin) | ergonomic capture/timeout/pipeline wrapper |
| B4 | Sending mail | `Email-MIME`, `smtplib`, nodemailer | `Net::SMTP` + `Email::MIME`/`Email::Simple` (fez) — works, no single entry point | one `send-mail` façade with TLS/auth/attachments |
| B5 | Compression & archives | `Archive-Tar`, `Archive-Zip` CPAN, `zipfile`/`gzip` stdlib | `Compress::Zlib` **REA-only**; `Archive::Libarchive`, `Compress::Bzip2` native; `Archive::SimpleZip`, `Archive::Tar::PP` (fez) | pure-Raku gzip/zip/tar, or a Raku++ built-in inflate |
| B6 | Digests, HMAC, symmetric crypto | `cryptography` #11, `Digest-MD5` #52, `Digest-SHA` #74, `bcrypt` #196 | `Digest` (pure, slow); everything fast is native (`Crypt::Argon2`, `Crypt::Sodium*`, `Digest::SHA256::Native`) | best solved as Raku++ built-ins |
| B7 | UUID / ULID | `Data-UUID` CPAN, `uuid` stdlib/npm | `UUID::V4` (fez) only; `UUID`, `LibUUID`, `ULID` REA-only | add v1/v5/**v7** + ULID; trivial, pure Raku |
| B8 | Standalone template engine | `jinja2` #42, `Template-Toolkit` #60, handlebars | `Template6`, `Template::Mustache`, `Template::HAML`, `ERK`, `HTML::Template`, `Cro::WebApp::Template` (all fez) | fragmented; pick/build one usable outside Cro |
| B9 | Layered configuration | `pydantic-settings` #88, `python-dotenv` #41, `Config-Any` | `Config::INI`, `Config::TOML`, `TOML`, `Env::Dotenv`, `Trait::Env` (fez) — no layering | file + env + CLI merge, on top of A2 |
| B10 | Redis client | `redis` #147 PyPI, ubiquitous | `Redis` (fez) is the only current option; `Redis::Async` **REA-only** | async/pipelined client |
| B11 | Message queues | `Net-AMQP`, `kafka-python`, `pika` | `Net::AMQP` **REA-only**, `PKafka` **REA-only**; `MQTT::Client`/`Protocol::MQTT` (fez) are fine | AMQP 0.9.1 in pure Raku |
| B12 | OAuth2 / OIDC client | `requests-oauthlib` #135, `oauthlib` #124, `msal` #172 | `OAuth2::Client::Google` REA-only; `JSON::JWT` (fez) is good | genuine gap for any API integration |
| B13 | Test tooling beyond `Test` | `pytest` #22, `pytest-cov`, `coverage` #122, `Test2-Suite` #35, `Test-Deep` #34 | core `Test`, `Test::Mock`, `Test::Selector`, `Test::Scheduler`, `Test::ContainerizedService` (fez) | missing: coverage, fixtures, parametrised cases, HTTP mocking, snapshots |
| B14 | Async DNS | `dnspython` #128, `Net-DNS` CPAN | `Net::DNS` (fez, "Simple Resolver") | resolver with SRV/TXT/MX + async |
| B15 | Feeds (RSS/Atom) | `feedparser`, `XML-Feed` | `Syndication` **REA-only** | small, self-contained |
| B16 | Text tables & wrapping | `tabulate`, `Text-Tabs+Wrap`, `wcwidth` #131 | `Text::Table::Simple` **REA-only**; `Text::Wrap`, `Gzz::Text::Utils` (fez) | fold into A6 |
| B17 | Dataframes & stats | `pandas` #38, `numpy` #19, `scipy` #90 | `Dan`, `Statistics`, `Statistics::Distributions`, `Math::Matrix` (fez); `Dan::Pandas` bridges to Python; `Math::Libgsl::*` native | real but large; native-free subset only |
| B18 | Charts | `matplotlib` #180, `plotly` | `Chart::Gnuplot` (external binary), `JavaScript::D3`, `CLI::Graphing::BarChart` (fez); `SVG::Plot` **REA-only** | pure-Raku SVG chart output |

---

## Tier C — already well served; do not rebuild

Confirmed present and current (fez) — worth knowing so effort is not wasted:

- **PDF** — `PDF`, `PDF::API6`, `PDF::Class`, `PDF::Content`, `PDF::Font::Loader`,
  `PDF::Document`, `PDF::Extract`. The strongest corner of the ecosystem;
  ahead of what most languages offer.
- **i18n / Unicode** — `Intl::CLDR`, `CLDR::List`, `Locale::Codes::Country`,
  `POFile`, `Geo::Region`. Excellent.
- **Web frameworks** — `Cro::HTTP` (incl. HTTP/2), `Cro::WebApp`,
  `Cro::WebSocket`, `Humming-Bird::Core`, `Air`, `Hiker`.
- **ORM** — `Red`, plus `DB::Migration::Declare`, `DBIish`, `DBIish::Pool`.
- **LLM / AI** — unusually strong: `LLM::Functions`, `WWW::OpenAI`, `LLM::DWIM`,
  `LLM::Chat`, `LLM::Graph`, `Jupyter::Chatbook`, and a full **`MCP`** SDK
  (`MCP::Client`, `MCP::Server`). Raku is not behind here.
- **Serialisation** — `JSON::Fast`, `JSON::Class`, `TOML`, `Text::CSV`,
  `CBOR::Simple`, `BSON`, `XML`, `XML::Class`.
- **Markdown** — `Text::Markdown`, `Markdown::Grammar`, `Pod::To::Markdown`.
- **Spreadsheets** — `Spreadsheet::XLSX`, `Spreadsheet::ODS`.
- **Browser automation** — `WWW::Playwright`, `WebDriver`, `WebDriver2`.
- **Diff / string distance** — `Text::Diff`, `Algorithm::Diff`,
  `Text::Levenshtein`, `Math::DistanceFunctions::Edit`, `Text::Sorensen`.
- **Misc** — `Version::Semver`, `URI`, `Email::Valid`, `Net::IP::Parse`,
  `SSH::LibSSH` (native), `Kubernetes`, `Docker::File`, `Data::Reshapers`,
  `Data::Generators`.

---

## Suggested shape of the work

A single namespace, one repository, one release, all pure Raku, all with a
Raku++ conformance test suite — so that "these work under Raku++" is a
checkable claim rather than a hope.

Ordered by (demand × gap × cheapness):

1. **A6** `Terminal::Rich` — cheapest, most visible, no dependencies.
2. **A1** `HTTP::Simple` — unblocks A9, A12, B12 and most real programs.
3. **A2** `Data::Schema` — the piece Raku's type system should be best at.
4. **A4** `Log` — needed by anything long-running.
5. **A5** `CLI` — turns scripts into tools.
6. **A11** `Resilience` + **A10** `Cache` — small, pair naturally with A1.
7. **A7** `HTML::DOM` and **A8** `YAML` — both are "a stale module blocks a
   whole category".
8. **A3** `JSON::Schema`, **A12** `Prometheus`, then **A9** `AWS::S3`.

Two things belong in the interpreter rather than in a module, because pure
Raku cannot do them well and Raku++'s NativeCall cannot reach the C libraries:
**fast digests/HMAC/AES** (B6) and **zlib inflate/deflate** (B5).

## Sources

- [MetaCPAN API — distributions by river](https://fastapi.metacpan.org/v1/distribution/_search)
- [Top PyPI packages](https://hugovk.dev/top-pypi-packages/)
- [RubyGems statistics](https://rubygems.org/stats)
- [Most depended-on npm packages 2026 — PkgPulse](https://www.pkgpulse.com/guides/most-depended-on-npm-packages-2026)
- [Top 200 depended-on npm packages](https://gist.github.com/Lennervald/5277e3d7804c42866184d5f8008b81c8)
- [fez/zef ecosystem index](https://360.zef.pm/)
- [Raku Ecosystem Archive META.json](https://raw.githubusercontent.com/Raku/REA/main/META.json)
- [raku.land](https://raku.land/)
