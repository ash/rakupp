# The measured ecosystem top-50 (working set for v2.0.0)

Reverse-dependency ranking over the **Raku Ecosystem Archive** (Raku/REA
META.json, fetched 2026-07-22; 14,764 dist-versions, ~2,505 distinct dists,
latest version per dist by release-date). `run` = distinct dists that
runtime-`depends` on it; `all` adds build-/test-depends. Pins are the
newest release **≥60 days old** (≤ 2026-05-23). The `Rakudo` pseudo-entry
is excluded. This is the *working set*: the final N and tier boundaries
get fixed after the Tier-3 baseline (see V2-MODULES-PLAN.md).

| # | dist | run | all | pin | pinned release | auth | notes |
|--:|---|--:|--:|---|---|---|---|
| 1 | JSON::Fast | 170 | 187 | 0.19 | 2023-04-30 | zef:timo |  (latest 0.20.1 too fresh) |
| 2 | Cro::HTTP | 54 | 56 | 0.8.12 | 2026-04-23 | zef:cro |  (latest 0.8.13 too fresh) |
| 3 | File::Temp | 52 | 84 | 0.0.12 | 2025-08-03 | zef:raku-community-modules |  |
| 4 | Terminal::ANSIColor | 47 | 47 | 0.14 | 2025-11-21 | zef:raku-community-modules |  |
| 5 | HTTP::UserAgent | 40 | 42 | 1.2.0 | 2025-05-03 | zef:raku-community-modules |  |
| 6 | JSON::Tiny | 38 | 46 | 1.0 | 2017-10-24 | cpan:MORITZ |  |
| 7 | YAMLish | 36 | 37 | 0.1.2 | 2025-01-05 | zef:leont |  (latest 0.1.3 too fresh) |
| 8 | File::Find | 35 | 39 | 0.2.5 | 2025-12-11 | zef:raku-community-modules |  |
| 9 | Test::META | 34 | 231 | 0.0.20 | 2023-07-04 | zef:jonathanstowe |  |
| 10 | XML | 33 | 33 | 0.3.6 | 2025-02-21 | zef:raku-community-modules |  |
| 11 | URI | 32 | 34 | 0.3.8 | 2024-11-17 | zef:raku-community-modules |  |
| 12 | MIME::Base64 | 26 | 27 | 1.2.5 | 2025-09-01 | zef:raku-community-modules |  |
| 13 | DBIish | 25 | 27 | 0.6.8 | 2026-04-12 | zef:raku-community-modules | NativeCall |
| 14 | Digest | 25 | 27 | 1.1.0 | 2023-12-27 | zef:grondilu |  |
| 15 | URI::Encode | 24 | 24 | 1.0 | 2025-06-03 | zef:raku-community-modules |  |
| 16 | IO::Socket::SSL | 23 | 25 | 0.0.4 | 2025-04-28 | zef:raku-community-modules | NativeCall |
| 17 | Digest::HMAC | 22 | 22 | 1.0.7 | 2023-11-16 | zef:jjmerelo |  |
| 18 | Sparrowdo | 22 | 22 | 0.1.52 | 2026-04-09 | zef:sp1983 |  (latest 0.1.54 too fresh) |
| 19 | LibraryMake | 21 | 47 | 1.0.5 | 2024-02-19 | zef:jjmerelo | NativeCall |
| 20 | Method::Also | 21 | 21 | 0.0.10 | 2025-01-12 | zef:lizmat |  |
| 21 | File::Directory::Tree | 20 | 26 | 0.2 | 2025-08-02 | zef:raku-community-modules |  |
| 22 | Math::Libgsl::Constants | 19 | 19 | 0.0.13 | 2022-12-27 | zef:FRITH | NativeCall |
| 23 | Distribution::Builder::MakeFromJSON | 17 | 30 | 0.6 | 2020-02-01 | cpan:NINE |  |
| 24 | File::Which | 17 | 18 | 1.0.1 | 2018-04-09 | ? |  |
| 25 | OpenSSL | 17 | 18 | 0.2.7 | 2026-04-20 | zef:raku-community-modules | NativeCall |
| 26 | UUID | 17 | 17 | 1.0.0 | 2018-04-30 | ? | NativeCall |
| 27 | Terminal::ANSI | 16 | 16 | 0.0.25 | 2024-12-08 | cpan:BDUGGAN |  |
| 28 | Text::Utils | 15 | 18 | 4.0.2 | 2025-10-09 | zef:tbrowder |  |
| 29 | NativeHelpers::Blob | 15 | 15 | 0.1.12 | 2019-03-04 | zef:raku-community-modules | NativeCall (latest 0.1.13 too fresh) |
| 30 | LWP::Simple | 14 | 16 | 0.109 | 2022-09-15 | zef:dwarring |  |
| 31 | Base64 | 14 | 15 | 0.0.2 | 2019-10-30 | github:ugexe |  |
| 32 | Data::Dump | 14 | 14 | 0.0.18 | 2026-04-10 | zef:tony-o |  |
| 33 | Log::Async | 14 | 14 | 0.0.17 | 2026-03-09 | zef:bduggan |  |
| 34 | Color | 13 | 14 | 1.004001 | 2022-02-20 | zef:raku-community-modules |  |
| 35 | DateTime::Format | 13 | 13 | 0.1.5 | 2023-10-07 | zef:raku-community-modules |  |
| 36 | HTTP::Status | 13 | 13 | 0.0.5 | 2025-01-10 | zef:lizmat |  |
| 37 | HTTP::Tiny | 13 | 13 | 0.2.6 | 2025-03-09 | zef:jjatria |  |
| 38 | NativeHelpers::Array | 13 | 13 | 0.0.6 | 2026-01-06 | zef:jonathanstowe | NativeCall |
| 39 | Shell::Command | 12 | 23 | 1.2 | 2025-08-05 | zef:raku-community-modules |  |
| 40 | Cro::Core | 12 | 14 | 0.8.10 | 2025-01-15 | zef:cro |  |
| 41 | Test::Output | 11 | 27 | 1.3 | 2025-08-16 | zef:lizmat |  |
| 42 | JSON::Class | 11 | 11 | 0.0.6 | 2024-04-11 | zef:vrurg |  |
| 43 | OO::Monitors | 11 | 11 | 1.1.7 | 2025-11-05 | zef:raku-community-modules |  |
| 44 | Test::When | 10 | 38 | 2.0 | 2025-01-16 | zef:raku-community-modules |  (latest 2.1 too fresh) |
| 45 | Config | 10 | 10 | 3.0.4 | 2021-03-18 | cpan:TYIL |  |
| 46 | Date::Calendar::Strftime | 10 | 10 | 0.1.1 | 2025-03-28 | zef:jforget |  |
| 47 | Digest::SHA256::Native | 10 | 10 | 1.0.1 | 2026-04-25 | zef:bduggan | NativeCall |
| 48 | PDF::Lite | 10 | 10 | 0.0.15 | 2025-03-25 | zef:dwarring |  |
| 49 | Sparrow6 | 10 | 10 | 0.0.91 | 2026-05-20 | zef:sp1983 |  (latest 0.0.93 too fresh) |
| 50 | Term::termios | 10 | 10 | 0.2.8 | 2024-06-12 | zef:krunen | NativeCall |

## Observations

- **JSON::Fast is the ecosystem keystone** (170 runtime dependents — 3× the
  runner-up). It is pure Raku but performance-tuned with nqp:: ops in hot
  paths — an early compatibility probe is mandatory.
- **The NativeCall block starts at rank 13** (DBIish) and includes ~14 of
  the top 50 — phase 4 is not optional for a credible top-50, but the
  pure-Raku majority is reachable first.
- **Test::META (231 total deps) and Test::When are test-time keystones**:
  passing them under rakupp unlocks *running other dists test suites*.
- YAMLish (r7) and MIME::Base64 (r12) already load on rakupp today;
  JSON::Tiny (r6) is pure Raku and frozen since 2017 — likely an easy
  early Tier-1 candidate.
- Sparrowdo/Sparrow6 are large frameworks with system side effects —
  battery-review candidates for *deferral* on safety grounds despite rank.
