# `rakupp test` over the 100 most recently released distributions

1900 dists.

## Verdicts

| verdict | dists |
|---|---:|
| self-fail | 1303 |
| dep-fail | 421 |
| other | 67 |
| build-fail | 51 |
| timeout | 26 |
| dep-build-fail | 16 |
| pass | 13 |
| fetch-fail | 2 |
| unresolved | 1 |

**Green:** App::FIT2GPX, Crypt::Bcrypt, Date::Calendar::Julian, Digest::SHA1::Native, Digest::SHA256::Native, FastCGI::NativeCall, Linenoise, Log::Async, Math::DistanceFunctions::Edit, Term::termios, Test::Assertion, Text::Caesar, XML

## What blocks the 463 dists that never reached their own suite

A dependency that fails its own suite, or takes longer than the budget,
stops the dist above it before a single line of its tests runs.

| blocking dist | blocks |
|---|---:|
| Getopt::Long | 55 |
| DB | 14 |
| IO::Blob | 14 |
| has-word | 10 |
| DOM::Tiny | 9 |
| HTTP::Supply | 9 |
| Cro::HTTP::Test | 8 |
| Pretty::Table | 8 |
| Terminal::Table | 8 |
| Implementation::Loader | 7 |
| Test::Deeply::Relaxed | 7 |
| Git::Files | 6 |
| Inline::Perl5 | 5 |
| Timezones::ZoneInfo | 5 |
| Version::Repology | 5 |
| BitEnum | 4 |
| Cro::HTTP | 4 |
| EventSource::Server | 4 |
| JSON::Pointer | 4 |
| LibUUID | 4 |
| MCP::Server | 4 |
| Module::Pod | 4 |
| SDL2::Raw | 4 |
| Time::Crontab | 4 |
| Auth::SCRAM::Async | 3 |
| FindBin | 3 |
| Gnome::Gdk3 | 3 |
| IO::Socket::Async::SSL | 3 |
| InterceptAllMethods | 3 |
| Physics::Unit | 3 |
| TAP | 3 |
| Terminal::Spinners | 3 |
| shorten-sub-commands | 3 |
| Audio::Taglib::Simple | 2 |
| Concurrent::Progress | 2 |
| Cro::WebSocket | 2 |
| Datetime::Math | 2 |
| Dev::ContainerizedService | 2 |
| Digest | 2 |
| FiniteFields | 2 |
| Functional::LinkedList | 2 |
| Gnome::Pango | 2 |
| HarfBuzz::Shaper::Cairo | 2 |
| Hash2Class | 2 |
| IO::Dir | 2 |
| Inline::Python | 2 |
| LibGit2 | 2 |
| LogP6 | 2 |
| MCP | 2 |
| MPD::Client | 2 |
| Math::Vector | 2 |
| Matrix::Client | 2 |
| Menu::Simple | 2 |
| Net::Netmask | 2 |
| Net::SMTP | 2 |
| Node::Ethereum::KZG | 2 |
| P5localtime | 2 |
| Pakku | 2 |
| Retry | 2 |
| System::Passwd | 2 |
| TestML | 2 |
| Text::CSV | 2 |
| Text::Emoji | 2 |
| Timer::Breakable | 2 |
| ULID | 2 |
| Unix::errno | 2 |
| WebService::AWS::Auth::V4 | 2 |
| XML::Canonical | 2 |
| XML::XPath | 2 |
| mv2d | 2 |
| ASTQuery | 1 |
| AccessorFacade | 1 |
| Algorithm::MinMaxHeap | 1 |
| App::Mi6 | 1 |
| Async::Workers | 1 |
| Auth::SASL | 1 |
| Backtrace::AsHTML | 1 |
| CCLog | 1 |
| Cache::Dir | 1 |
| Chart::Gnuplot | 1 |
| CheckSocket | 1 |
| Chronic | 1 |
| Colorizable | 1 |
| CommandLine::Usage | 1 |
| Commands | 1 |
| Concurrent::Iterator | 1 |
| Concurrent::Queue | 1 |
| Contact | 1 |
| Cro::Core | 1 |
| Cro::TLS | 1 |
| Cro::WebApp | 1 |
| CroX::HTTP::FallbackPassthru | 1 |
| Crypt::LibScrypt | 1 |
| Crypt::Libcrypt | 1 |
| Crypt::RSA | 1 |
| DBIish | 1 |
| DSL::Entity::Geographics | 1 |
| Digest::MurmurHash3 | 1 |
| DispatchMap | 1 |
| Distribution::Common::Remote | 1 |
| Email::MIME | 1 |
| Event::Emitter | 1 |
| Fcntl | 1 |
| File-Path-Copy | 1 |
| File::Compare | 1 |
| File::Copy | 1 |
| File::file | 1 |
| FileSystem::Capacity | 1 |
| FunctionalParsers | 1 |
| GD::Raw | 1 |
| GUI::Editors | 1 |
| Game::Bayes | 1 |
| Git::Config | 1 |
| Git::Simple | 1 |
| Grammar::Extractor | 1 |
| Grammar::PrettyErrors | 1 |
| Gumbo | 1 |
| HTTP::Request::FormData | 1 |
| HTTP::Server::Middleware::JSON | 1 |
| Holidays::US::Federal | 1 |
| I18N::LangTags | 1 |
| IO::CatHandle::AutoLines | 1 |
| IP::Addr | 1 |
| IRC::Log | 1 |
| Image::RGBA | 1 |
| Intl::LanguageTag | 1 |
| JSON::Collector | 1 |
| JSON::Fast | 1 |
| JSON::Path | 1 |
| JSON::RepositoryEvent | 1 |
| JsonC | 1 |
| L10N::ZH | 1 |
| LLM::Chat | 1 |
| LLM::Graph | 1 |
| LWP::Simple | 1 |
| LaTeX::Grammar | 1 |
| LibUSB | 1 |
| License::Software | 1 |
| Lingua::StopwordsISO | 1 |
| List::MoreUtils | 1 |
| List::UtilsBy | 1 |
| LocalTime | 1 |
| Log::Async | 1 |
| Log::Dispatch | 1 |
| Log::ZMQ | 1 |
| Math::FFT::Libfftw3 | 1 |
| Math::Libgsl::MovingWindow | 1 |
| Math::Matrix | 1 |
| Math::Nearest | 1 |
| Math::RungeKutta | 1 |
| Module2Rpm | 1 |
| MongoDB::Fast | 1 |
| Mortgage | 1 |
| Native::FindVersion | 1 |
| NativeHelpers::CBuffer | 1 |
| NativeHelpers::iovec | 1 |
| Net::NetRC | 1 |
| Net::Osc | 1 |
| Net::ZMQ | 1 |
| Number::More | 1 |
| OO::Monitors | 1 |
| OrderedHash | 1 |
| Oyatul | 1 |
| P5getgrnam | 1 |
| P5getnetbyname | 1 |
| P5getprotobyname | 1 |
| P5getpwnam | 1 |
| P5getservbyname | 1 |
| P5times | 1 |
| PKCS5 | 1 |
| Pastebin::Shadowcat | 1 |
| Path::Util | 1 |
| Perl6::Parser | 1 |
| Pod::To::HTML | 1 |
| Pod::Utils | 1 |
| PostCocoon::Url | 1 |
| Raylib::Bindings | 1 |
| ReverseIterables | 1 |
| SAT | 1 |
| SQL::Builder | 1 |
| ScaleVec | 1 |
| Slang::Otherwise | 1 |
| Stache | 1 |
| StrictClass | 1 |
| String::Utils | 1 |
| SupplyTimeWindow | 1 |
| TOML::Thumb | 1 |
| TXN::Parser | 1 |
| TXN::Remarshal | 1 |
| Template::Classic | 1 |
| Termbox | 1 |
| Terminal::ReadKey | 1 |
| Terminal::Size | 1 |
| Terminal::Widgets | 1 |
| Test::Mock | 1 |
| Test::Run | 1 |
| Text::BorderedBlock | 1 |
| Text::CodeProcessing | 1 |
| Text::Markov | 1 |
| Text::Plot | 1 |
| Text::ShellWords | 1 |
| TimeUnit | 1 |
| Timer | 1 |
| TreeSitter::Native | 1 |
| TweetNacl | 1 |
| Type::EnumHOW | 1 |
| Typesafe::HTML | 1 |
| UNIX::Privileges | 1 |
| Unicode::PRECIS | 1 |
| Unix::Groups | 1 |
| User::Language | 1 |
| ValuePair | 1 |
| Version::Semantic | 1 |
| WWW::GCloud::API::Storage | 1 |
| WWW::Playwright | 1 |
| WebService::FootballData | 1 |
| WhereList | 1 |
| WriteOnceHash | 1 |
| ake | 1 |
| annotations | 1 |
| dosh | 1 |
| eigenstates | 1 |
| jmp | 1 |
| note: LibCurl::Easy — not in the zef index, resolved from the REA archive | 1 |
| snip | 1 |
| sortuk | 1 |

## Error signatures — the 1421 dists that fail on their own account

| dists | signature | which |
|---:|---|---|
| 77 | assertions: 1 of 1 tests failed | 6pm, Acme::Anguish, App::Cal, App::Gitstatus, Archive::Libarchive, Archive::SimpleZip, Audio::Playlist::JSPF, BSON, BTree, C::Parser, CWT-Repository-Hash, Color::Named, CompUnit::Repository::Tar, CompUnit::Search, Cooklang, CoreHackers::Q, CoreHackers::Sourcery, Curry, DataStar, Digest::xxHash, Gauge, Geo::Polyline, GitHub::Actions, Github::PublicKeys, GraphQL, HTTP::Signature, IO-Archive, Inline::Brainfuck, JS::Minify, Jupyter::Kernel, LLM::DWIM, LMDB, LendingClub, MQ::Posix, Manifesto, Math::Matrix, Music::Helpers, Net::Whois, OLE::Storage_Lite, Operator::defined-alternation, Ops::SI, PatternMatching, Perl6::Ecosystem, PerlMongers::Hannover, Pg::Notify, Pod::Contents, Printer::ESCPOS, Progress::Bar, RakuConfig, Rakudo::Perl6::Format, Redis, Repository::Precomp::Cleanup, SDL2::Raw, SHAI, Services::PortMapping, String::CamelCase, T, Template::Protone, Test::Run, Test::Script, Text::Markdown::Discount, Tinky::Declare, Tinky::JSON, Tree::Binary, TweetNacl, UNIX::Daemonize, URL::Find, Ujumla, WAT, WAT--CLI, WWW::CloudHosting::Hetzner, WebService::Slack::Webhook, delete-old-until-size, dosh, panda, raku-mailgun, vCard::Parser |
| 68 | (nothing captured) | Acme::Addslashes, Acme::DSON, Acme::Flutterby, Acme::Mangle, Adventure::Engine, Algorithm::Viterbi, App::P6Dx, App::jsonv, Archive-Zip-SimpleZip, BreakDancer, Bundle-Compress-Zlib, Bundle-IO-Compress-Bzip2, Cache::Memcached, Crypt::Game, Data::Pretty, Druid, Editsrc::Uggedit, Farabi6, GGE, Game::Crypt, HTML::Entity, HTTP::Server, HTTP::Server::Router, HTTP::Server::Router::YAML, Hiker, IO-Compress, IO-Compress-Lzf, IO-Compress-Lzma, IO-Compress-Lzop, IO::Prompter, IRC::Art, IRC::Client::Plugin::UrlTitle, JSON5::Tiny, JavaScript::SpiderMonkey, Kains, Math::ChebyshevPolynomial, Math::ContinuedFractions, Math::OddFunctions, Math::Polynomial, Modular, Net::FTP, Net::Ftp, Net::Packet, Net::Pcap, Net::XMPP, P6SGI, POSIX, Perl6::Literate, Perl6::Tracer, Phaser::ATEXIT, Pod::Strip, Pofig, RPi-native, Serialize-Naive, Serialize::Naive, Sort-Fast, Term::ProgressBar, Test::Junkie, Testing, Text::Indented, Time-Duration, Time::Duration, WebService::TelegramBot, Webservice::Lastfm, Yapsi, kazmath, ufo, wiringPI |
| 39 | native library missing (environment) | AI::FANN, Archive::Libarchive::Raw, Audio::Sndfile, Audio::Taglib::Simple, Cache::Dir, Cmark, CommonMark, Compress::Snappy, Cro::ZeroMQ, Crypt::LibGcrypt, Crypt::LibScrypt, Crypt::Libcrypt, Crypt::SodiumScrypt, Desktop::Notify::Progress, Duck::CSV, Duckie, File::LibMagic, File::Metadata::Libextractor, Filetype::Magic, GD::Raw, GEOS, Gcrypt, IConv, LLM::Chat, LibUUID, MagickWand, Math::FFT::Libfftw3, Native::Exec, Net::LibIDN, Net::LibIDN2, Net::ZMQ, Proc::ZMQed, Readline, Spreadsheet::Libxlsxio, TCC, TagLibC, Terminal::Size, Text::FriBidi, X11::Xdo |
| 33 | assertions: 1 of 2 tests failed | App::CPAN, ClassX::StrictConstructor, DateTime::Extended, DateTime::Grammar, DateTime::Parse, Dawa, Distribution::Common::Remote, FindBin, FindBin-libs, Game::Quest, Git::Simple, Git::Wrapper, HTML::MyHTML, HTTP::Roles, IRC::Client::Plugin::Factoid, Manifest::StopWar, Math::Libgsl::Constants, Math::Libgsl::Function, Math::Libgsl::Series, MergeOrderedSeqs, Mmap::Native, PDF::ISO_32000, PDF::ISO_32000_2, Proc::Async::Timeout, Prompt, Rat::Power, ReadWriteLock, StrictClass, TOML, TimeBomb, UEncoding, taurus, zef |
| 24 | The spawned command 'make' exited unsuccessfully (exit code: 2, signal: 0) | GDBM, Inline::Scheme::Guile, Linux::NFTables, Math::Libgsl::BLAS, Math::Libgsl::Combination, Math::Libgsl::Complex, Math::Libgsl::Histogram, Math::Libgsl::Interpolation, Math::Libgsl::LinearAlgebra, Math::Libgsl::Matrix, Math::Libgsl::Multiset, Math::Libgsl::Permutation, Math::Libgsl::Polynomial, Math::Libgsl::QuasiRandom, Math::Libgsl::Random, Math::Libgsl::Wavelet, Net::FTPlib, Node::Ethereum::KZG, RPi::Device::DHT11, Raylib::Bindings, Sys::Utmp, Termbox, Text::CSV::LibCSV, YAML::Parser::LibYAML |
| 22 | assertions: 2 of 2 tests failed | DBIx::NamedQueries, DSL::Entity::AddressBook, DSL::Entity::WeatherData, Data::Importers, Distribution::Builder::Cmake, Grammar::ErrorReporting, GtkLayerShell, Hash::Timeout, IO::Capture::Simple, IO::Path::Dirstack, LIVR, Mac::Battery::Alerter, Math::Libgsl::Eigensystem, Math::Libgsl::Elementary, Math::Libgsl::MovingWindow, Math::Libgsl::RandomDistribution, Math::Libgsl::RunningStatistics, OrderedHash, Pluggable, SQL::NamedPlaceholder, Sanity, WebService::SOP |
| 21 | parse: Confused (got '}') | Compress::Zstd, IP::Addr, Inline::Ruby, Kind, Kind::Subset::Parametric, Math::Factorial::Operator, Net::NNG, P5getgrnam, P5getnetbyname, P5getprotobyname, P5getpwnam, P5getservbyname, P5localtime, POSIX::PWDENT, Perl6::Parser, Pod::To::HTML, Pod::To::Markdown, Polyglot::Brainfuck, SDL, Sequence::Generator, Template::Mustache |
| 18 | assertions: 1 of 3 tests failed | App::ecogen, Coro::Simple, Fcntl, Geo::Hash, Int::polydiv, Map::DeckGL, Math::Quaternion, Module::Pod, P5sleep, Perl6-Math-Quaternion, Pod::Utilities, Pod::Utils, SAT, Supply::Timeout, Test::Builder, Win32::DrivesAndTypes, Wkhtmltox, fornax |
| 15 | assertions: 4 of 4 tests failed | Algorithm::HierarchicalPAM, Algorithm::LDA, Algorithm::SpiralMatrix, Audio::Convert::Samplerate, Automata::Cellular, CUID, DSL::Entity::Foods, DateTime::Monotonic, Glob::Grammar, GlotIO, Grammar::HTTP, JSON::WebToken, Moonphase, Rainbow, _ |
| 15 | parse: expected ) (got ':') | Async::Workers, BinaryHeap, Clifford, Concurrent::PChannel, Grammar::Extractor, Graph, Graphviz::DOT::Chessboard, IO::CatHandle::AutoLines, LLM::Graph, Operator-grandpa, Operator::grandpa, Selkie::UI, Sum, WWW::GCloud, WWW::GCloud::API::Storage |
| 15 | parse: expected } (got '') | CLI::Help, CLI::Version, CSS::Module::CSS3::Selectors, CSS::Specification, Cro::WebApp, DNS::Zone, Intl::CLDR, Intl::Format::Unit, Intl::Regex::CharClass, Intl::Token::Number, Iter::Able, RSV, Slang::Forgiven, TOML::Thumb, YakShave |
| 13 | assertions: 2 of 4 tests failed | App::Ariza, Chemistry::Elements, Config::Parser::json, Grammar-Common, Grammar::Common, LibraryCheck, Method::Protected, Object::Permission, P5index, Pod::EOD, System::Stats::MEMUsage, Timer::Breakable, URI::FetchFile |
| 12 | no such method: .AST on Str | L10N::AF, L10N::CY, L10N::DE, L10N::EN, L10N::FR, L10N::HU, L10N::IT, L10N::JA, L10N::NL, L10N::PT, L10N::TLH, L10N::ZH |
| 12 | no such method: .legacy on Raku | Auth::SCRAM::Async, Cro::APIToken, Cro::CBOR, Cro::HTTP, Crypt::Random, Date::Event, Holidays::US::Federal, JobQueue, LocalTime, MVC::Keayl::Admin, UUID::V4, if |
| 11 | Target is not assignable | AI::Agent, Cookie::Baker, Fasta, LCS::BV, Operator::feq, P5substr, Path::Map, Text::Levenshtein::Damerau, XHTML::Writer, hide-methods, sortuk |
| 11 | module not found: Sparrowdo::Core::DSL::Template | Sparrowdo::Cordova::OSx::Build, Sparrowdo::Cordova::OSx::Fortify, Sparrowdo::VSTS::YAML::Angular::Build, Sparrowdo::VSTS::YAML::Artifact, Sparrowdo::VSTS::YAML::Build, Sparrowdo::VSTS::YAML::Build::Assembly::Patch, Sparrowdo::VSTS::YAML::DotNet, Sparrowdo::VSTS::YAML::MsBuild, Sparrowdo::VSTS::YAML::Nuget, Sparrowdo::VSTS::YAML::Solution, Sparrowdo::VSTS::YAML::Update::Azure::SSL |
| 11 | too many levels of recursion | Class::Utils, Hash::Agnostic, Hash::Ordered, IO::Path::AutoDecompress, Map::Agnostic, Map::Ordered, P5chdir, P5unlink, Type::EnumHOW, ValueMap, ValuePair |
| 10 | assertions: 1 of 5 tests failed | CSV-Autoclass, Dist::Helper, Grammar::Message, Hinges, OEIS, ONNX::Native, Text::Diff, Text::Fortune, Timer, Version::Semantic |
| 10 | assertions: 3 of 3 tests failed | AWS::Session, Array::Sorted::Util, DSL::English::QuantileRegressionWorkflows, FastCGI::NativeCall::PSGI, FileSystem::Capacity, SION, Seq::Bounded, Sprockets, Supply::Folds, Text::BorderedBlock |
| 10 | parse: Confused (got ')') | Bench, Crypt::SodiumPasswordHash, Intl::Format::List, Intl::LanguageTag, Intl::Number::Plural, IntlFormatNumber, IntlPromptYesNo, SQL::Builder, TestML, User::Language |
| 9 | Can't determine actual Offset | DB::Migration::Simple, DBIish::Pool, DBIish::Transaction, LibZip, Lingua::Unihan, NativeHelpers::Blob, Reminders, Touch, X11::Xlib::Raw |
| 9 | undefined routine: void | Gnome::Cairo, Gnome::GObject, Gnome::Gdk4, Gnome::GdkPixbuf, Gnome::Glib, Gnome::Graphene, Gnome::N, Gnome::Pango, OpenMPT::Bindings |
| 8 | assertions: 1 of 4 tests failed | Algorithm::KdTree, Binary::Structured, Config::Simple, Deepgrep, Dice::Roller, Grok, P5study, Punnable |
| 8 | assertions: 6 of 6 tests failed | AttrX::Lazy, AttrX::Mooish, Bin::Utils, DSL::English::LatentSemanticAnalysisWorkflows, DSL::English::RecommenderWorkflows, DSL::Entity::MachineLearning, Router::Boost, String::Koremutake |
| 8 | parse: expected { (got ';') | CSS::Module, CSS::Writer, Config::DataLang::Refine, Config::Parser::toml, Crane, Format::Lisp, Router::Right, Sustenance |
| 8 | undefined routine: nqp::objprimspec | AWS::SNS::Notification, Cro::HTTP::BodySerializerJSONClass, Kivuli, Lumberjack::Config::JSON, Lumberjack::Message::JSON, Pastebin::Pasteee, SixPM, WebService::Soundcloud |
| 7 | assertions: 1 of 6 tests failed | App::termie, App::tmeta, Game::Bayes, Getopt::ForClass, Matrix::Client, Terminal::UI, Text::Abbrev |
| 7 | assertions: 2 of 3 tests failed | App::Miroku, Audio::OggVorbis, Digest::BubbleBabble, IP::Random, Image::Libexif, Mathematica::Serializer, Test::Base |
| 6 | assertions: 7 of 7 tests failed | Algorithm::GooglePolylineEncoding, DSL::English::EpidemiologyModelingWorkflows, Dev::ContainerizedService, Parameterizable, XML::XPath, unprint |
| 6 | no such method: .slang_grammar on Any | P5__DATA__, Slang::Date, Slang::Mosdef, Slang::Otherwise, Slang::Subscripts, Slangify |
| 6 | parse: expected ) (got '<<') | Air::Plugin::Hilite, FanFou, FeiShuBot, Hilite, Net::POP3, Script::Hash |
| 5 | Cannot invoke non-Callable value of type Any | Concurrent::File::Find, Digest::PSHA1, JSON::Path, Prometheus::Client, Text::MathematicalCase |
| 5 | Variable $.dwSize used where no 'self' is available | App::ByWord, Terminal-API, Terminal::LineEditor, Terminal::Print, Terminal::Widgets |
| 5 | assertions: 1 of 7 tests failed | Concurrent::Iterator, DSL::Shared, Noise::Simplex, Noise::Simplex::Native, Text::Table::Simple |
| 5 | assertions: 4 of 5 tests failed | Acme::Polyglot::Levenshtein::Damerau, Digest::MurmurHash3, Jupyter::Converter, Log::Minimal, Text::LTSV |
| 5 | assertions: 5 of 6 tests failed | Attribute::Lazy, HTTP::Client, Scalar::History, Test::Time, Tinky::Hash |
| 5 | no such method: .new on Backtrace | Backtrace::AsHTML, File::file, Log::Any, P5caller, Proc::Easier |
| 5 | parse: Confused (got ';') | Apache::LogFormat, Debug::Transput, Deps, Lingua::Stopwords, Plosurin |
| 5 | undefined routine: where | Build::Simple, Business::CreditCard, DateTime::TimeZone, Git::PurePerl, Text::VimColour |
| 4 | Array does not support associative indexing | Config::Netrc, Email::MIME, Email::Simple, Lingua::Number |
| 4 | Needs to have an X11 widowing system available. | App::pixel-pick, App::pixel::pick, App::pixelpick, X11::libxdo |
| 4 | assertions: 12 of 12 tests failed | HTTP::MultiPartParser, Math::Libgsl::Statistics, Recipe::Parser, shorten-sub-commands |
| 4 | assertions: 3 of 4 tests failed | Class::Loader::Dynamic, Implementation::Loader, ML::SparseMatrixRecommender, XML::Actions |
| 4 | assertions: 3 of 6 tests failed | Algorithm::LibSVM, Algorithm::MinMaxHeap, Math::RungeKutta, Shell::Capture |
| 4 | assertions: 4 of 6 tests failed | Audio::PortMIDI, Future, List::Allmax, RegexUtils |
| 4 | assertions: 5 of 5 tests failed | PKCS5, Prettier::Table, Pretty::Table, Subsets::Common |
| 4 | assertions: 8 of 8 tests failed | DSL::Entity::Jobs, DSL::Entity::Metadata, Geo::Region, P5__FILE__ |
| 4 | callsame is not in the dynamic scope of a dispatcher | Curlie, Grammar::PrettyErrors, Module::Does, RDF::Turtle |
| 4 | module not found: Pod::To::Text | Crypt::TweetNacl, Getopt::Tiny, Pod::Coverage, Text::Template |
| 4 | no such method: .hyper on Iterable | Ecosystem, Ecosystem::Cache, JSON::Fast::Hyper, hyperize |
| 4 | no such method: .new on Any | Algorithm::AhoCorasick, Crypt::RC4, Net::Telnet, Test::Declare |
| 4 | parse: You cannot declare an attribute here; maybe you'd like a class or a role? | Functional::LinkedList, HTTP::Server::Simple, Inline, annotations |
| 4 | parse: expected ) (got '{') | Java::Generate, LLM::RetrievalAugmentedGeneration, MessagePack, Slang::Lambda |
| 4 | parse: expected ] (got ';') | Air::Plugin::Asciinema, Air::Plugin::Donate, Air::Plugin::Wordcloud, Data::Selector |
| 4 | parse: expected } (got ';') | Git::File::History, MQTT::Client, Node::Ethereum::RLP, Selenium::WebDriver |
| 4 | parse: unexpected operator in term position (got ' ') | Carp, Function::Validation, Intl::Fluent, Regex::FuzzyToken |
| 3 | No matching multi candidate for method BUILD | Algorithm::NaiveBayes, Game::Sudoku, JSON::RPC |
| 3 | Stub code executed | Benchy, ake, sake |
| 3 | Unsupported prefix 'dimslip' | Data::Translators, Rakudo::Slippy::Semilist, Slippy::Semilist |
| 3 | assertions: 1 of 13 tests failed | Data::Geographics, LaTeX::Grammar, XML::Canonical |
| 3 | assertions: 1 of 16 tests failed | Bencode, Math::Polynomial::Chebyshev, Math::Roman |
| 3 | assertions: 1 of 8 tests failed | FastCGI, LibUSB, XML::Writer |
| 3 | assertions: 2 of 5 tests failed | Algorithm::Treap, Config::Parser::yaml, Cro::H |
| 3 | assertions: 2 of 8 tests failed | Date::YearDay, Util::Uuencode, octans |
| 3 | assertions: 3 of 10 tests failed | Method::Modifiers, Net::HTTP, Test::Lab |
| 3 | assertions: 3 of 15 tests failed | File::Tudo, Template::Jinja2, Unicode::Security |
| 3 | assertions: 3 of 8 tests failed | Acme::Skynet, Lingua::StopwordsISO, MermaidJS::Grammar |
| 3 | assertions: 4 of 7 tests failed | Data::TextOrBinary, P5reset, Test::Script::Output |
| 3 | assertions: 5 of 7 tests failed | DSL::Entity::Geographics, Semantic::Versioning, ULID |
| 3 | assertions: 6 of 8 tests failed | Algorithm::Heap::Binary, Crypt::CAST5, Net::NetRC |
| 3 | assertions: 9 of 9 tests failed | Flower, Pod::TreeWalker, Termbox2 |
| 3 | module not found: Zef::Utils::FileSystem | Algorithm::XGBoost, Chart::Gnuplot, MeCab |
| 3 | no such method: .!SET-SELF on Array | Point, ValueList, snip |
| 3 | no such method: .add_parent on <anon|1> | Email::SendGrid, Terminal::ReadKey, Test::Mock |
| 3 | no such method: .recv on Any | Pastebin::Shadowcat, Test::HTTP::Server, Tika |
| 3 | no such method: .value on Any | Facter, HTTP::UserAgent, PriorityQueue |
| 3 | parse: Confused (got ',') | Date::WorkdayCalendar, SelectiveImporting, Slang::SQL |
| 3 | parse: expected ) (got '(') | CSS::Stylesheet, SQL::Abstract, Text::CSV |
| 3 | parse: expected ] (got 'for') | ENIGMA::Machine, Iec104Parser, Terminal::Table |
| 3 | parse: expected variable after declarator (got ':') | CSS::Font::Resources, CSS::Selector::To::XPath, FixedInt |
| 3 | parse: expected variable in declaration (got 'Any') | Debugging::Tool, Statistics::OutlierIdentifiers, Stats |
| 3 | parse: expected { (got '(') | App::Mi6, TAP, python::itertools |
| 3 | parse: expected { (got 'coercions') | Code::Coverable, Code::Coverage, Test::Coverage |
| 3 | parse: expected { (got 'int') | Map::Match, Text::Emoji, uniname-words |
| 3 | parse: unexpected operator in term position (got '-->') | File::Copy, File::Utils, Terminal::Gauge |
| 3 | parse: unexpected operator in term position (got '>') | GD, Subset::Helper, mv2d |
| 3 | parse: unexpected operator in term position (got '>>') | CoreHackers::NfaChainsaw, Getopt::Long::Grammar, Graphviz::DOT::Grammar |
| 3 | parse: unexpected operator in term position (got '»') | GUI::Editors, Gzz::Text::Utils, Parse::Paths |
| 3 | undefined routine: nqp::unipropcode | Terminal::Tests, Text::MiscUtils, Unicode::GCB |
| 2 | ===WARNING=== Module MacOS::NativeLib EXPORT failed: Failed to create symlink called '/Use | HarfBuzz, HarfBuzz::Shaper::Cairo |
| 2 | Attempt to return outside of any Routine | ASCII::To::Uni, Texas::To::Uni |
| 2 | Cannot invoke non-Callable value of type Nil | ADT, ASN::BER |
| 2 | Class 'StreamClosure' cannot inherit from 'rw' because it is unknown | Cairo, Gnome::Gsk4 |
| 2 | JSON::simd: its build hook fails under rakupp — not installing | JSON-Simd, JSON::simd |
| 2 | Testo::Test::Result | IO::Dir, Testo |
| 2 | The given vectors are expected to have the same number of elements. | Algorithm::KDimensionalTree, Math::Nearest |
| 2 | Undeclared name 'Error' | Lumberjack, Lumberjack::Dispatcher::Syslog |
| 2 | Undeclared name 'File::Temp::tempdir' | File-TreeBuilder, File::TreeBuilder |
| 2 | Undeclared name 'RandomColor' | p6-RandomColor, raku-RandomColor |
| 2 | Undeclared name 'SeekFromBeginning' | File::Compare, Geo::IP2Location::Lite |
| 2 | Usage: | App::APOTD, envy |
| 2 | Variable @.hash used where no 'self' is available | Net::Ethereum, Node::Ethereum::Keccak256::Native |
| 2 | assertions: 1 of 12 tests failed | MIME::Types, Math::SparseMatrix |
| 2 | assertions: 1 of 14 tests failed | Scheduler::DelayBetween, Template::HAML |
| 2 | assertions: 1 of 17 tests failed | Date::Discordian, Datetime::Math |
| 2 | assertions: 1 of 19 tests failed | ECMA262Regex, Form |
| 2 | assertions: 1 of 42 tests failed | Concurrent::Trie, RPG::Base |
| 2 | assertions: 1 of 9 tests failed | Calculator, Map::Leaflet |
| 2 | assertions: 18 of 18 tests failed | Ask, Hash::MultiValue |
| 2 | assertions: 2 of 74 tests failed | Lingua::EN::Numbers, cro |
| 2 | assertions: 2 of 9 tests failed | LLM::Prompts, Memoize |
| 2 | assertions: 20 of 20 tests failed | Doublephone, Graphics::TinyTIFF |
| 2 | assertions: 3 of 14 tests failed | Hash::int, Hash::str |
| 2 | assertions: 3 of 5 tests failed | Unicode::PRECIS, eigenstates |
| 2 | assertions: 3 of 9 tests failed | META6::To::Man, subs |
| 2 | assertions: 4 of 10 tests failed | Cro::FCGI, JSON::Pointer |
| 2 | assertions: 4 of 8 tests failed | Distribution::Common, Sub::Util |
| 2 | assertions: 5 of 9 tests failed | GeoIP2, Test::Scheduler |
| 2 | assertions: 6 of 12 tests failed | Gherkin::Grammar, Text::SubParsers |
| 2 | assertions: 6 of 14 tests failed | AccountableBagHash, IO::Blob |
| 2 | assertions: 6 of 7 tests failed | Colorizable, Tinky |
| 2 | assertions: 7 of 10 tests failed | RakupodObject, from |
| 2 | cannot resolve caller: MAIN(); no matching multi candidate | App::Ebread, App::Stouch |
| 2 | cannot resolve caller: chars(Any:U); the invocant is a type object, not an instance | Hashids, Terminal::Spinners |
| 2 | cannot resolve caller: contains(Any:U); the invocant is a type object, not an insta | DAWG, Pod::To::Man |
| 2 | module not found: Gnome::N::N-GObject | Gnome::Gdk3, Gnome::Gio |
| 2 | module not found: NQPHLL | Slang::Tuxic, Test::Async |
| 2 | module not found: QAST | Asserter, Control::Bail |
| 2 | module not found: Sparrowdo::Core::DSL::Directory | Sparrowdo::Prometheus, Sparrowdo::VSTS::YAML::Nuget::Build |
| 2 | no such method: .ACCEPTS on Any | Version::Nginx, Version::Raku |
| 2 | no such method: .base on Num | Num::HexFloat, Text::Table::List |
| 2 | no such method: .evaluator on Any | LLM::Functions, ML::FindTextualAnswer |
| 2 | no such method: .max_threads on Scheduler | Mux, Uxmal |
| 2 | no such method: .new on CompUnit::PrecompilationStore::FileSystem | Pod::Load, RakuDoc::Load |
| 2 | no such method: .new on Compiler | META::constants, Subsets::IO |
| 2 | no such method: .new on ValueObjAt | ValueType, ValueTypeCache |
| 2 | no such method: .next-iterator on Array | ForwardIterables, ReverseIterables |
| 2 | no such method: .set_name on Method | Attribute::Predicate, JSON::RepositoryEvent |
| 2 | parse: Malformed numeric literal | Slang::Kazu, Slang::NumberBase |
| 2 | parse: expected '!!' in ternary (got '(') | Chemistry::Stoichiometry, Data::ExampleDatasets |
| 2 | parse: expected ) (got 'plus') | French, German |
| 2 | parse: expected ) (got 'return') | MoarVM::Bytecode, paths |
| 2 | parse: expected ) (got '加') | Chinese, ClassicalChinese |
| 2 | parse: indirect method call requires parentheses: $obj.'name'() (got '}') | Identity::Utils, String::Utils |
| 2 | parse: unexpected operator in term position (got '&') | ArrayHash, Test::Describe |
| 2 | parse: unexpected operator in term position (got '/') | P5ref, Uni63 |
| 2 | parse: unexpected operator in term position (got '==>') | P6Repl::Helper, Proc::Feed |
| 2 | parse: unexpected operator in term position (got '>>=') | Monad, Monad-Result |
| 2 | parse: unexpected operator in term position (got '﻿') | Math::Constants, Wikidata::API |
| 2 | samewith is not in the dynamic scope of a dispatcher | Games::Wordle, HomoGlypher |
| 2 | type check (binding to parameter '$incoming'): expected Supply, got List | Cro::HTTP::Test, EyeoftheBeholder3 |
| 2 | undefined routine: EVALFILE | Device::HIDAPI, Notcurses::Native |
| 2 | undefined routine: String::CRC32::crc32 | Image::PNG::Portable, String::CRC32 |
| 2 | undefined routine: base-query | Badger, CheckedSQL |
| 2 | undefined routine: enum | HTML::Parser::XML, HTML::Restrict |
| 2 | undefined routine: h1 | Air, HTML::Functional |
| 2 | undefined routine: n | Grammar::Profiler::Simple, Inline::Go |
| 2 | undefined routine: nqp::const::BINARY_ENDIAN_LITTLE | Git::Files, path-utils |
| 2 | undefined routine: nqp::div_i | Array::Sorted::Map, String::Color |
| 2 | undefined routine: nqp::eqaddr | Tuple, are |
| 2 | undefined routine: nqp::getcomp | CodeUnit, Text::CodeProcessing |
| 2 | undefined routine: nqp::lc | HTML::Entity::Fast, HTML::Strip |
| 2 | undefined routine: nqp::stat | P5readlink, Path::Finder |
| 2 | undefined routine: override-user-timezone | User::Timezone, UserTimezone |
| 1 | "test default with args" does not start with a recognised command | Command::Despatch |
| 1 | '"\$one"' cannot be used as a source | Tee |
| 1 | (no log) | Acme::Don't |
| 1 | ./t/data/debian12.os-release line 1: Invalid os-release line | Sys::OsRelease |
| 1 | 2 | Compress::Bzip2 |
| 1 | 3rdparty/NotoSans-Bold.ttf Didn't get any queries | Font::QueryInfo |
| 1 | ===WARNING=== Module Native::Overflow EXPORT failed: No such symbol 'bit' | Native::Overflow |
| 1 | ?SYNTAX ERROR IN 10: Unknown statement '' | Inline::BASIC |
| 1 | A protocol name is required | X::Protocol |
| 1 | Acme::WTF: its own test suite fails under rakupp — not installing (--no-test to override) | Acme::WTF |
| 1 | Actions must be provided as a tuple of format: (Regex, Callable), recieved (Regex)! | Net::Osc |
| 1 | Array::Sparse: its own test suite fails under rakupp — not installing (--no-test to overri | Array::Sparse |
| 1 | Attempt to divide 0 by zero using infix:<div> | Number::Denominate |
| 1 | Bad client-send: MAIL FROM: | Net::SMTP |
| 1 | CRoaring: its own test suite fails under rakupp — not installing (--no-test to override) | CRoaring |
| 1 | Cannot assign to a readonly variable or a value | Number::More |
| 1 | Cannot connect to 127.0.0.1:27017 | MongoDB::Fast |
| 1 | Cannot convert string to number: base-10 number must begin with valid digits or '.' in 'An | WWW::HorizonsEphemerisSystem |
| 1 | Cannot convert string to number: base-10 number must begin with valid digits or '.' in 'he | FStrings |
| 1 | Cannot convert string to number: base-10 number must begin with valid digits or '.' in 'up | Version::Repology |
| 1 | Cannot deflate stream: stream error | Compress::Zlib |
| 1 | Cannot find native symbol 'QRinput_new' | Image::QRCode |
| 1 | Cannot find native symbol 'SHA' | SSL |
| 1 | Cannot find native symbol '_getpriority' | P5getpriority |
| 1 | Cannot find native symbol 'alpha_map_new' | Algorithm::Trie::libdatrie |
| 1 | Cannot find native symbol 'auth' | Auth::PAM::Simple |
| 1 | Cannot find native symbol 'epoll_create1' | epoll |
| 1 | Cannot find native symbol 'is_dst' | DateTime::DST |
| 1 | Cannot find native symbol 'is_utf8' | Math::DistanceFunctions::Native |
| 1 | Cannot find native symbol 'valhalla_reader_create' | Geo::Valhalla |
| 1 | Cannot listen on localhost:54341 | IO::Socket::Async::SSL |
| 1 | Cannot look up attributes in a Bar type object | Staticish |
| 1 | Cannot look up attributes in a Inner type object | MessagePack::Class |
| 1 | Cannot look up attributes in a Pod::From::Cache type object | raku-pod-from-cache |
| 1 | Cannot parse contact: | Contact |
| 1 | Class 'CLDR::List' cannot inherit from 'rw' because it is unknown | CLDR::List |
| 1 | Class 'DateTime' cannot inherit from / compose itself | DateTime::strftime |
| 1 | Class 'Exception' cannot inherit from 'CORE::Exception' because it is unknown | Getopt::Long |
| 1 | Class 'LeavePhaser' cannot inherit from 'RakuAST::StatementPrefix::Phaser::Leave' because  | FINALIZER |
| 1 | Class 'Proxy' cannot inherit from / compose itself | Hash::MutableKeys |
| 1 | Class 'iovec' cannot inherit from 'rw' because it is unknown | NativeHelpers::iovec |
| 1 | Constraint type check failed in binding to parameter '$region' | AWS::Pricing |
| 1 | Constraint type check failed in binding to parameter '@address' | Net::Netmask |
| 1 | Could not determine terminal width | Terminal::Width |
| 1 | Could not use native implementation. | Math::DistanceFunctions |
| 1 | Default constructor for 'InvalidChr' only takes named arguments | P5chr |
| 1 | Default constructor for 'Link' only takes named arguments | Random::Names |
| 1 | Died | Time::Repeat |
| 1 | Don't know what to do with: (:quit(sub { ... }), :exit("quit"), "release all" => sub { ... | Commands |
| 1 | Ellipsoid Bessel 1841 Nambia unknown. | Geo::Coordinates::UTM |
| 1 | Error opening terminal: unknown. | NCurses |
| 1 | Event::Emitter: its own test suite fails under rakupp — not installing (--no-test to overr | Event::Emitter |
| 1 | Extra attributes while building Inner | Hydrate |
| 1 | Failed to move '/privatelibargon2.dylib.1' to '/privatelibargon2.dylib': No such file or d | Crypt::Argon2 |
| 1 | Failed to open file /./resources/dist.male.first: no such file or directory | Text::Names |
| 1 | Failed to open file /privateMakefile.in: No such file or directory | SOD |
| 1 | Failed to open file /privatestd in: No such file or directory | rakudoc2man |
| 1 | Failed to open file file.txt: No such file or directory | FileSystem::Helpers |
| 1 | Failed to open file p6-log-timeline-cbor-test: No such file or directory | Log::Timeline |
| 1 | Found the following issues: | JSON::Collector |
| 1 | HexDump::Tiny: its own test suite fails under rakupp — not installing (--no-test to overri | HexDump::Tiny |
| 1 | Index out of range. Is: 1, should be in 0..0 | Date::Calendar::FrenchRevolutionary |
| 1 | Index out of range. Is: 3, should be in 0..0 | Pythonic::Str |
| 1 | Input (383 characters) is not a valid JSON string | JSON::Hjson |
| 1 | Invalid arguments to Mu.new. Use any valid DateTime.new arguments, a GCT time (e.g. `198.1 | Games::TauStation::DateTime |
| 1 | Malformed TAP output | Test::Harness |
| 1 | Method '!encode-body' must be resolved by class Protocol::MQTT::Packet::PubAck because it  | Protocol::MQTT |
| 1 | Method 'appendChild' must be implemented by Dtd because it is required by roles: W3C::DOM: | W3C::DOM |
| 1 | Method 'to-node' must be resolved by class Pod::To::Tree because it exists in multiple rol | Pod::To::HTMLBody |
| 1 | No compiler available for language 'Perl5' | Inline::Perl5 |
| 1 | No matching multi candidate for method attempt-mechanisms | Auth::SASL |
| 1 | No matching multi candidate for method editors | Proc::InvokeEditor |
| 1 | No matching multi candidate for method header | HTTP::Headers |
| 1 | No matching multi candidate for method subseq | BioPerl6 |
| 1 | P5each: its own test suite fails under rakupp — not installing (--no-test to override) | P5each |
| 1 | PDF::Native: its own test suite fails under rakupp — not installing (--no-test to override | PDF::Native |
| 1 | PYENV_ROOT not found. Please install pyenv and set PYENV_ROOT environment variable. | Inline::Python3 |
| 1 | Please set AWS_SECRET_ACCESS_KEY | WebService::AWS::S3 |
| 1 | Private method call to 'parse' outside the defining class | Data::Section::Simple |
| 1 | Promise broken | Module2Rpm |
| 1 | Rakudo::Version is supposed to run on Rakudo, not 'Raku++' | Rakudo::Version |
| 1 | Resource::Wrangler: its own test suite fails under rakupp — not installing (--no-test to o | Resource::Wrangler |
| 1 | Roaring::Tags: its own test suite fails under rakupp — not installing (--no-test to overri | Roaring::Tags |
| 1 | Selection of us / imperial depends on your locale | Physics::Unit |
| 1 | Shared library file 'libyamlstar.dylib' not found | YAMLStar |
| 1 | Shared library file 'libys.dylib.0.2.30' not found | YAMLScript |
| 1 | System::Stats::DISKUsage: its own test suite fails under rakupp — not installing (--no-tes | System::Stats::DISKUsage |
| 1 | The agument $ncol is expected to be a non-negative integer or Whatever. | Math::SparseMatrix::Native |
| 1 | The argument $highlight is expected to be a list of Date-string pairs, Date objects, month | Markup::Calendar |
| 1 | The attribute '$!s' is required, but you did not provide a value for it. | Definitely |
| 1 | The attribute '$!v33' is required, but you did not provide a value for it. | RPi::Device::ST7036 |
| 1 | The spawned command 'cd /privatesrc && make clean && make' exited unsuccessfully (exit cod | GTK::Scintilla |
| 1 | The spawned command 'cd /privatesrc && qmake RakuQtWidgets.pro && make' exited unsuccessfu | Qt::QtWidgets |
| 1 | The spawned command 'cd stub; make' exited unsuccessfully (exit code: 2, signal: 0) | Compress::Brotli |
| 1 | The spawned command 'echo '3433' | xclip' exited unsuccessfully (exit code: 127, signal: 0 | Clipboard |
| 1 | The spawned command 'g++ -Wall -shared -fPIC -o /privatelibopencv-perl6.dylib src/libopenc | OpenCV |
| 1 | The spawned command 'gcc -Wall -shared -fPIC -o /privatelibmsgpack-perl6.dylib src/libmsgp | MsgPack |
| 1 | The spawned command 'git' exited unsuccessfully (exit code: 128, signal: 0) | TXN::Parser |
| 1 | The spawned command 'make lib/libradamsa.so' exited unsuccessfully (exit code: 2, signal:  | Radamsa |
| 1 | The spawned command 'perl6 -Ilib ./bin/make-wp-input test.html' exited unsuccessfully (exi | RakuAdvent::WordPress |
| 1 | The spawned command 'sh install.sh' exited unsuccessfully (exit code: 1, signal: 0) | CCLog |
| 1 | The spawned command 'tree /private.test_88398' exited unsuccessfully (exit code: 127, sign | XDG::GuaranteedResources |
| 1 | This module is not compatible with the operating system macos | System::Passwd |
| 1 | This thread has no receiver | Actor |
| 1 | Trait::IO: its own test suite fails under rakupp — not installing (--no-test to override) | Trait::IO |
| 1 | Type 'ByteOrder' is not declared | Numeric::Pack |
| 1 | Type 'FeedFormat' is not declared | Syndicate |
| 1 | Type 'Style' is not declared | Hyperscript |
| 1 | Unable to detect clang config | Libclang |
| 1 | Undeclared name 'AR_COMMON' | Archive::Ar |
| 1 | Undeclared name 'Build' | Timezones::ZoneInfo |
| 1 | Undeclared name 'Event::Status::Started' | Terminal::MultiProgress |
| 1 | Undeclared name 'FORTUNE_UNORDER' | Fortune |
| 1 | Undeclared name 'LBFGS_LINESEARCH_MORETHUENTE' | Algorithm::LBFGS |
| 1 | Undeclared name 'LOCALE_CODE_NUMERIC' | Locale::Codes::Country |
| 1 | Undeclared name 'Level::trace' | LogP6 |
| 1 | Undeclared name 'MJD0' | DateTime::Julian |
| 1 | Undeclared name 'NotifyUrgencyLow' | Desktop::Notify |
| 1 | Undeclared name 'Rabble::Verbs::Shufflers' | Rabble |
| 1 | Undeclared name 'SeekType' | P5seek |
| 1 | Undeclared name 'Smooth' | Smooth::Numbers |
| 1 | Undeclared name 'T' | Slang::Predicate |
| 1 | Undeclared name 'URI::Template::Grammar' | URI::Template |
| 1 | Undeclared name 'Unknown' | Logic::Ternary |
| 1 | Undeclared name 'Writer' | Protocol |
| 1 | Unexpected adverbs passed to subscript: eject | Adverb::Eject |
| 1 | Unknown distro macos | RPi::GpioDirect |
| 1 | Unrecognized character name [ | Text::Flags |
| 1 | Unrecognized regex adverb: :foo1 | Symbol |
| 1 | Unsupported operator '=' | Array::Shaped::Console |
| 1 | Useless use of () in sink context (line 22) | HarfBuzz::Font::FreeType |
| 1 | Useless use of () in sink context (line 33) | HarfBuzz::Subset |
| 1 | Useless use of constant integer 1 in sink context (line 14) | List::Operator::DoublePlus |
| 1 | Variable $.original_name used where no 'self' is available | Gumbo |
| 1 | Variable $.stream-start used where no 'self' is available | LibYAML |
| 1 | Variable @.data used where no 'self' is available | Bitcoin::Core::Secp256k1 |
| 1 | [3 4] is not subgrid of element | Grid |
| 1 | assertions: 1 of 10 tests failed | Net::Whois::Async |
| 1 | assertions: 1 of 15 tests failed | DBI::Async |
| 1 | assertions: 1 of 20 tests failed | TimeUnit |
| 1 | assertions: 1 of 34 tests failed | Xmav::JSON |
| 1 | assertions: 1 of 58 tests failed | Listicles |
| 1 | assertions: 10 of 11 tests failed | Syslog::Parse |
| 1 | assertions: 10 of 12 tests failed | Array::Circular |
| 1 | assertions: 10 of 120 tests failed | Modf |
| 1 | assertions: 10 of 23 tests failed | P5chomp |
| 1 | assertions: 11 of 13 tests failed | Math::PascalTriangle |
| 1 | assertions: 11 of 16 tests failed | Math::Interval |
| 1 | assertions: 12 of 13 tests failed | Color::Names |
| 1 | assertions: 12 of 52 tests failed | DateTime::Format::W3CDTF |
| 1 | assertions: 13 of 13 tests failed | Locale::Codes |
| 1 | assertions: 13 of 23 tests failed | Hash-with |
| 1 | assertions: 13 of 42 tests failed | Getopt::Type |
| 1 | assertions: 14 of 19 tests failed | Cookie::Jar |
| 1 | assertions: 14 of 28 tests failed | Scalar::Util |
| 1 | assertions: 14 of 32 tests failed | Hash::Restricted |
| 1 | assertions: 14 of 35 tests failed | Array::Rounded |
| 1 | assertions: 15 of 150 tests failed | List::MoreUtils |
| 1 | assertions: 16 of 16 tests failed | DSL::English::ClassificationWorkflows |
| 1 | assertions: 16 of 27 tests failed | Array::Agnostic |
| 1 | assertions: 163 of 253 tests failed | Term::Choose |
| 1 | assertions: 17 of 19 tests failed | Pod::PerlTricks |
| 1 | assertions: 17 of 20 tests failed | JSONL |
| 1 | assertions: 17 of 22 tests failed | NativeLibs |
| 1 | assertions: 17 of 29 tests failed | Markdown::Lex |
| 1 | assertions: 18 of 32 tests failed | Magento |
| 1 | assertions: 18 of 38 tests failed | XML::Parser::Tiny |
| 1 | assertions: 2 of 14 tests failed | mod-div-specs |
| 1 | assertions: 2 of 17 tests failed | Template::Mojo |
| 1 | assertions: 2 of 23 tests failed | fez |
| 1 | assertions: 2 of 24 tests failed | HTTP::UserAgent::Strict |
| 1 | assertions: 2 of 29 tests failed | Concurrent::Queue |
| 1 | assertions: 2 of 30 tests failed | HTTP::Request::FormData |
| 1 | assertions: 2 of 34 tests failed | TreeSitter::Native |
| 1 | assertions: 2 of 40 tests failed | Time::Crontab |
| 1 | assertions: 2 of 42 tests failed | Date::Calendar::CopticEthiopic |
| 1 | assertions: 2 of 56 tests failed | Env |
| 1 | assertions: 2 of 6 tests failed | Retry |
| 1 | assertions: 2 of 67 tests failed | Lingua::EN::Numbers::Cardinal |
| 1 | assertions: 20 of 28 tests failed | WriteOnceHash |
| 1 | assertions: 20 of 29 tests failed | AccessorFacade |
| 1 | assertions: 22 of 24 tests failed | Text::Tabs |
| 1 | assertions: 22 of 38 tests failed | FiniteFields |
| 1 | assertions: 24 of 1110 tests failed | Math::FractionalPart |
| 1 | assertions: 24 of 1629 tests failed | Set::Equality |
| 1 | assertions: 25 of 27 tests failed | IO::Path::Mode |
| 1 | assertions: 25 of 39 tests failed | Version::RubyGems |
| 1 | assertions: 26 of 26 tests failed | Concurrent::PriorityQueue |
| 1 | assertions: 28 of 30 tests failed | Acme::Rautavistic::Sort |
| 1 | assertions: 28 of 70 tests failed | Hash::Util |
| 1 | assertions: 2883 of 5756 tests failed | P5quotemeta |
| 1 | assertions: 29 of 43 tests failed | Version::Conan |
| 1 | assertions: 29 of 44 tests failed | Version::Semverish |
| 1 | assertions: 3 of 11 tests failed | UpRooted |
| 1 | assertions: 3 of 20 tests failed | WebService::AWS::Auth::V4 |
| 1 | assertions: 3 of 21 tests failed | Prime::Factor |
| 1 | assertions: 3 of 23 tests failed | List::Agnostic |
| 1 | assertions: 3 of 25 tests failed | Date::Calendar::Hebrew |
| 1 | assertions: 3 of 54 tests failed | ANTLR4::Grammar |
| 1 | assertions: 3 of 56 tests failed | ANTLR4 |
| 1 | assertions: 3 of 7 tests failed | File::Spec::Case |
| 1 | assertions: 30 of 85 tests failed | Sub::Memoized |
| 1 | assertions: 4 of 14 tests failed | Test::Deeply::Relaxed |
| 1 | assertions: 4 of 16 tests failed | Hash::Consistent |
| 1 | assertions: 4 of 17 tests failed | DB |
| 1 | assertions: 4 of 20 tests failed | Math::Libgsl::Sort |
| 1 | assertions: 4 of 21 tests failed | Text::Sorensen |
| 1 | assertions: 4 of 22 tests failed | List::Utils |
| 1 | assertions: 4 of 29 tests failed | Path::Util |
| 1 | assertions: 4 of 41 tests failed | Finance::GDAX::API |
| 1 | assertions: 4 of 89 tests failed | MCP |
| 1 | assertions: 40 of 360 tests failed | Range::SetOps |
| 1 | assertions: 41 of 42 tests failed | Fortran::Grammar |
| 1 | assertions: 43 of 104 tests failed | PublicSuffix |
| 1 | assertions: 44 of 69 tests failed | Pod::Parser |
| 1 | assertions: 45 of 56 tests failed | US-ASCII |
| 1 | assertions: 47 of 107 tests failed | Pakku |
| 1 | assertions: 5 of 13 tests failed | P5print |
| 1 | assertions: 5 of 20 tests failed | Acme::Cow |
| 1 | assertions: 5 of 21 tests failed | Dist::META |
| 1 | assertions: 5 of 26 tests failed | List::Util |
| 1 | assertions: 5 of 38 tests failed | Cache::LRU |
| 1 | assertions: 5 of 63 tests failed | Terminal::QuickCharts |
| 1 | assertions: 5056 of 30428 tests failed | Lingua::EN::Stem::Porter |
| 1 | assertions: 6 of 10 tests failed | IO::Path::ChildSecure |
| 1 | assertions: 6 of 11 tests failed | DOM::Tiny |
| 1 | assertions: 6 of 15 tests failed | Serialize::Tiny |
| 1 | assertions: 6 of 16 tests failed | Lang::Transliterate |
| 1 | assertions: 6 of 22 tests failed | Math::Vector3D |
| 1 | assertions: 6 of 28 tests failed | String::Splice |
| 1 | assertions: 6 of 30 tests failed | PSGI |
| 1 | assertions: 6 of 37 tests failed | Date::Easter |
| 1 | assertions: 6 of 54 tests failed | Email::Valid |
| 1 | assertions: 6 of 74 tests failed | CBOR::Simple |
| 1 | assertions: 64 of 129 tests failed | Proc::More |
| 1 | assertions: 65 of 131 tests failed | GNU::Time |
| 1 | assertions: 7 of 13 tests failed | BioInfo |
| 1 | assertions: 7 of 14 tests failed | LLM::Data::Pipeline |
| 1 | assertions: 7 of 153 tests failed | Lang::JA::Kana |
| 1 | assertions: 7 of 16 tests failed | String::Fields |
| 1 | assertions: 7 of 19 tests failed | Selkie |
| 1 | assertions: 7 of 21 tests failed | PostCocoon::Url |
| 1 | assertions: 7 of 8 tests failed | Proxee |
| 1 | assertions: 8 of 10 tests failed | HTTP::Parser |
| 1 | assertions: 8 of 18 tests failed | File::Ignore |
| 1 | assertions: 8 of 33 tests failed | HTML::Template |
| 1 | assertions: 8 of 9 tests failed | Tokenizers |
| 1 | assertions: 89 of 121 tests failed | Grammar::Modelica |
| 1 | assertions: 92 of 204 tests failed | Test::Fuzz |
| 1 | cannot resolve caller: EXPORT(); no matching multi candidate | META::verauthapi |
| 1 | cannot resolve caller: ToWorkflowCode(); no matching multi candidate | DSL::English::DataQueryWorkflows |
| 1 | cannot resolve caller: comb(Any:U); the invocant is a type object, not an instance | Acme::BaseCJK |
| 1 | cannot resolve caller: descend(); no matching multi candidate | SQL::Lexer |
| 1 | cannot resolve caller: euclidean-distance(); no matching multi candidate | ML::Clustering |
| 1 | cannot resolve caller: match(Any:U); the invocant is a type object, not an instance | HTTP::Server::Async::Plugins::Router::Simple |
| 1 | cannot resolve caller: open(); no matching multi candidate | IO::Handle::Rollover |
| 1 | cannot resolve caller: pop(); no matching multi candidate | Path::Through |
| 1 | cannot resolve caller: roundrobin(); no matching multi candidate | roundrobin-slip |
| 1 | cannot resolve caller: to-base(); no matching multi candidate | Base::Any |
| 1 | cannot resolve caller: uc(Any:U); the invocant is a type object, not an instance | CSS::Minifier |
| 1 | cannot resolve caller: watch-var(); no matching multi candidate | Proxy::Watched |
| 1 | cannot resolve caller: weeks-in-month(); no matching multi candidate | Date::Utils |
| 1 | eval error: use CSS::Units :pt; | CSS::Properties |
| 1 | eval error: use v6; | Native::Packing |
| 1 | fatal: could not get user info: no such user | UNIX::Privileges |
| 1 | flow: its own test suite fails under rakupp — not installing (--no-test to override) | flow |
| 1 | make exited with signal 2 | Imlib2 |
| 1 | module not found: , | English |
| 1 | module not found: /private/var/folders/x1/pjhp8m3n0ks9wlph53lzfw400000gs/T/rakupp-install-5645-Math--ThreeD/Math--ThreeD-master/tools/gen-libs.p6 | Math::ThreeD |
| 1 | module not found: Configuration | App::RakuCron |
| 1 | module not found: Crypt::Argon2 | Crypt::Passphrase |
| 1 | module not found: Lingua::Stem::Bulgarian | Lingua::Stem::Bulgarian |
| 1 | module not found: Lingua::Stem::Portuguese | Lingua::Stem::Portuguese |
| 1 | module not found: Lingua::Stem::Russian | Lingua::Stem::Russian |
| 1 | module not found: META6::Query | Stache |
| 1 | module not found: NativeCall::Types | Font::FreeType |
| 1 | module not found: Net::ZMQ::Context | Logger::ZMQ |
| 1 | module not found: Path::Finder | Test::Selector |
| 1 | module not found: Perl6::Grammar | Rakudo::Perl6::Parsing |
| 1 | module not found: PlaywrightSupport | WWW::Playwright |
| 1 | module not found: Sparrowdo::Core::DSL::Bash | Sparrowdo::VSTS::YAML::Cordova |
| 1 | module not found: Zef::Distribution | App::Zef-Deps |
| 1 | module not found: binpath. | Inline::J |
| 1 | no such method: .!diary on Teenager | AttrX::PrivateAccessor |
| 1 | no such method: .ACCEPTS on Callable | Edit::Files |
| 1 | no such method: .Date on Instant | Toi |
| 1 | no such method: .FILETEST-E on Rakudo::Internals | inode-dev-devtype |
| 1 | no such method: .Failure on X::Concurrent::Stack::Empty | Concurrent::Stack |
| 1 | no such method: .INCLUDE on Rakudo::Internals | RakuDoc::Test::Files |
| 1 | no such method: .actions on Match | Perl6::TypeGraph |
| 1 | no such method: .add_fallback on Grammar::Highlighter | Grammar::Highlighter |
| 1 | no such method: .add_fallback on Mu | Inline::Python |
| 1 | no such method: .add_to_keys on SVG::Plot::Data::Series | SVG::Plot |
| 1 | no such method: .anti-pairs on Hash | Array::Unsorted::Map |
| 1 | no such method: .append on Str | Text::Plot |
| 1 | no such method: .base on Instant | IO::Maildir |
| 1 | no such method: .base on Rat | Data::MessagePack |
| 1 | no such method: .bind-udp on IO::Socket::Async | Reaper::Control |
| 1 | no such method: .bytes on <anon|2> | HTTP::Server::Logger |
| 1 | no such method: .catdir on Any | IO::Path::More |
| 1 | no such method: .chars on Any | Grammar::BNF |
| 1 | no such method: .child-by-name on IO::Path | Oyatul |
| 1 | no such method: .close on Any | Cro::SSL |
| 1 | no such method: .columns on Failure | Color::Palette |
| 1 | no such method: .constraint_list on Parameter | CommandLine::Usage |
| 1 | no such method: .cpu-cores-but-one on Kernel | ParaSeq |
| 1 | no such method: .data-sources on Any | DBDish::ODBC |
| 1 | no such method: .decode on Block | Image::RGBA |
| 1 | no such method: .elems on Data::TypeSystem::Assoc | Data::TypeSystem |
| 1 | no such method: .enum_value_list on SymbolSet | Terminal::Capabilities |
| 1 | no such method: .fail on Failure | JsonC |
| 1 | no such method: .fetch on Any | IoC |
| 1 | no such method: .file on Method | sourcery |
| 1 | no such method: .flatmap on Any | ABC |
| 1 | no such method: .foo on Array | Hash2Class |
| 1 | no such method: .foo on Foo | InterceptAllMethods |
| 1 | no such method: .foo-type on Foo | Trait::Enum |
| 1 | no such method: .frames on Any | Audio::Fingerprint::Chromaprint |
| 1 | no such method: .get on Any | MPD::Client |
| 1 | no such method: .get_attribute_for_usage on A | WebService::FootballData |
| 1 | no such method: .has-accessor on Attribute | Masquerade |
| 1 | no such method: .host on IO::Socket::INET | HTTP::Server::Async |
| 1 | no such method: .instruments on Any | Audio::Hydrogen |
| 1 | no such method: .interval on ScaleVec | ScaleVec |
| 1 | no such method: .invocation_handler on Metamodel::ClassHOW | Failable |
| 1 | no such method: .is-win on Any | Libui |
| 1 | no such method: .lang_prompt_yn on IO::Prompt::Testable | IO::Prompt |
| 1 | no such method: .list on XML::Text | Map::Mapnik |
| 1 | no such method: .loaded on CompUnit::Repository::Installation | CompUnit::Util |
| 1 | no such method: .made on List | Markdown::Grammar |
| 1 | no such method: .merged on Num | CRDT |
| 1 | no such method: .native-descriptor on FileHandle | Terminal-MakeRaw |
| 1 | no such method: .new on CompUnit::PrecompilationStore::File | Pod::To::Cached |
| 1 | no such method: .new on Digest::MD5 | Gravatar::URL |
| 1 | no such method: .new on IO::Handle | IO::MiddleMan |
| 1 | no such method: .new on ObjAt | ObjectCache |
| 1 | no such method: .new on Rakudo::Perl6::Tracer | Rakudo::Perl6::Tracer |
| 1 | no such method: .new on X::NYI | NYI |
| 1 | no such method: .nick on IRC::Client::Message::Join | IRC::Client::Plugin::Logger |
| 1 | no such method: .nsPrefix on Any | SOAP::Client |
| 1 | no such method: .opened on FileHandle | P5fileno |
| 1 | no such method: .osname on VM | Mac::Applications::List |
| 1 | no such method: .parse on Any | Log::Reader |
| 1 | no such method: .parse-base on IntStr | Geo::Basic |
| 1 | no such method: .perlseen on Hash::Sorted | Hash::Sorted |
| 1 | no such method: .platform on Any | Monitor::Monit |
| 1 | no such method: .precedence on Sub | Understitch |
| 1 | no such method: .precise on Rat | Rat::Precise |
| 1 | no such method: .push on Str | Archive::Tar::PP |
| 1 | no such method: .reference on Any | POFile |
| 1 | no such method: .refine_slang on Any | overload::constant |
| 1 | no such method: .roles_to_compose on SecretEnvoy | AttrX::InitArg |
| 1 | no such method: .root on Any | XML::Class |
| 1 | no such method: .rotate on Str | String::Rotate |
| 1 | no such method: .scenarios on Any | CucumisSextus |
| 1 | no such method: .schema on Any | ASN::Grammar |
| 1 | no such method: .sentences on Str | Lingua::EN::Fathom |
| 1 | no such method: .set_error_func on TCCState | TinyCC |
| 1 | no such method: .set_name on Block | Sub::Name |
| 1 | no such method: .share on Supply | Concurrent::Progress |
| 1 | no such method: .signal on Lock | Concurrent::BoundedChannel |
| 1 | no such method: .source on Any | Audio::Icecast |
| 1 | no such method: .stem on Any | IO::Stem |
| 1 | no such method: .tell on FileHandle | silently |
| 1 | no such method: .trieRootLabel on ML::TriesWithFrequencies::Trie | ML::TriesWithFrequencies |
| 1 | no such method: .use-repository on CompUnit::RepositoryRegistry | CompUnit::DynamicLib |
| 1 | no such method: .value on MyPackageClass | UML::Translators |
| 1 | no such method: .value on Stash | Inline::Lua |
| 1 | no such method: .ver on Any | DBIish |
| 1 | no such method: .version on Any | Audio::PortAudio |
| 1 | no such method: .version on Unicode | App::Uni |
| 1 | no such method: .watch on IO::Path | IO::Notification::Recursive |
| 1 | no such method: .wrap on Any | Repl::Tools |
| 1 | no such method: .write-uint32 on Buf | MIDI::Make |
| 1 | no such method: .y on Num | Operator::dB |
| 1 | only Windows and Linux are supported at the moment | Hash::File |
| 1 | parse: ===SORRY!=== Error while compiling 00raw.t | FontConfig |
| 1 | parse: ===SORRY!=== Error while compiling all.t | Dependency::Sort |
| 1 | parse: ===SORRY!=== Error while compiling sam.t | Bio |
| 1 | parse: Cannot add tokens of category 'str' | ASTQuery |
| 1 | parse: Cannot add tokens of category 'variable' | App::MoarVM::ConfprogCompiler |
| 1 | parse: Confused (got ']') | Data::StaticTable |
| 1 | parse: Couldn't find final ')' (corresponding ( was at line 25) | CSS::TagSet |
| 1 | parse: Couldn't find final ')' (corresponding ( was at line 7) | CompUnit::Repository::Mask |
| 1 | parse: Couldn't find terminator ' (corresponding ' was at line 44) | Polyglot::Regexen |
| 1 | parse: Couldn't find terminator ' (corresponding ' was at line 488) | Math::Angle |
| 1 | parse: Couldn't find terminator } (corresponding { was at line 92) | Text::ShellWords |
| 1 | parse: Ending delimiter CODE not found for heredoc | L10N::EO |
| 1 | parse: Ending delimiter SQRT2 not found for heredoc | BigRoot |
| 1 | parse: Ending delimiter TEXT not found for heredoc | Text::Markdown |
| 1 | parse: Placeholder variable $^a may not be used here because the surrounding block does not take a signature | FunctionalParsers |
| 1 | parse: Semicolon form of 'sub' without unit scope is illegal. You probably want 'unit sub'. (X::UnitScope::Invalid) | Template::Nest |
| 1 | parse: Unable to parse expression in double quotes; couldn't find final '"' (corresponding starter was at line 7) | Typesafe::HTML |
| 1 | parse: Unable to parse expression in single quotes; couldn't find final "'" (corresponding starter was at line 181) | License::Software |
| 1 | parse: Unable to parse expression in single quotes; couldn't find final "'" (corresponding starter was at line 27) | CLI::Wordpress |
| 1 | parse: Unable to parse expression in single quotes; couldn't find final "'" (corresponding starter was at line 64) | CLI::AWS::EC2-Simple |
| 1 | parse: Unsupported use of <>; in Raku please use lines() to read input, ('') for a null string or () for an empty list | Game::Entities |
| 1 | parse: Unsupported use of undef as a value; in Raku please use something more specific: an undefined type object such as Any, or Nil as the absence of a value | P5defined |
| 1 | parse: expected '!!' in ternary (got ')') | LibGit2 |
| 1 | parse: expected ) (got '!') | Math::Handy |
| 1 | parse: expected ) (got '$file') | P5-X |
| 1 | parse: expected ) (got '$obj') | MetamodelX::Dataclass |
| 1 | parse: expected ) (got '&') | Benchmark |
| 1 | parse: expected ) (got '::') | Zef::CPANReporter |
| 1 | parse: expected ) (got '<->') | Intl::Format::Number |
| 1 | parse: expected ) (got '?') | Slang::Piersing |
| 1 | parse: expected ) (got 'LEVEL') | Rakudo-Type-Introspection |
| 1 | parse: expected ) (got 'USD') | Moneys |
| 1 | parse: expected ) (got 'ip') | Net::IP::Parse |
| 1 | parse: expected ) (got 'is') | CustomImporting |
| 1 | parse: expected ) (got 'last') | Math::Combinatorics |
| 1 | parse: expected ) (got 'mais') | Portuguese |
| 1 | parse: expected ) (got 'más') | Spanish |
| 1 | parse: expected ) (got 'rIV') | Slang::Roman |
| 1 | parse: expected ) (got 'role') | RakuAST::Utils |
| 1 | parse: expected ) (got 'where') | WhereList |
| 1 | parse: expected ) (got '«') | File-Path-Copy |
| 1 | parse: expected ) (got '足す') | Japanese |
| 1 | parse: expected ) (got '더하기') | Korean |
| 1 | parse: expected ] (got ')') | November |
| 1 | parse: expected ] (got 'with') | Air::Plugin::Rakudoc |
| 1 | parse: expected method name after '.' (got '²') | Math::Sequences |
| 1 | parse: expected method name after '.' (got '³') | List::Divvy |
| 1 | parse: expected method name after '.' (got '»') | Display::Listings |
| 1 | parse: expected parameter variable (got '(') | PrettyDump |
| 1 | parse: expected variable after declarator (got 'a') | Slang::Nogil |
| 1 | parse: expected variable after declarator (got 'proto') | DispatchMap |
| 1 | parse: expected variable after declarator (got 'value-class') | ValueClass |
| 1 | parse: expected variable after declarator (got '👍') | Slang::Emoji |
| 1 | parse: expected variable in declaration (got 'Int') | Backtrace::Files |
| 1 | parse: expected { (got '(!==)') | Template::Nest::Fast |
| 1 | parse: expected { (got 'IO') | Config::JSON |
| 1 | parse: expected { (got '«') | Syntax::Highlighters |
| 1 | parse: expected { (got '☈') | Arithmetic::PaperAndPencil |
| 1 | parse: expected } (got '/#6E') | PDF::Grammar |
| 1 | parse: indirect method call requires parentheses: $obj.'name'() (got '.') | Email::Address |
| 1 | parse: indirect method call requires parentheses: $obj.'name'() (got 'if') | Version::Semver |
| 1 | parse: unexpected operator in term position (got '(.)') | Data::Dump::Tree |
| 1 | parse: unexpected operator in term position (got '*=') | Math::NumberTheory |
| 1 | parse: unexpected operator in term position (got '<=') | Path::Router |
| 1 | parse: unexpected operator in term position (got '=') | Dan |
| 1 | parse: unexpected operator in term position (got '?&') | LLM::Data::Inference |
| 1 | parse: unexpected operator in term position (got '`') | PDF::Extract |
| 1 | parse: unexpected operator in term position (got '⁈') | Slang::AltTernary |
| 1 | parse: unexpected operator in term position (got '↑') | Math::Arrow |
| 1 | parse: unexpected operator in term position (got '∈') | Math::Symbolic |
| 1 | taglib-config failed: 127 | Audio::TagLib |
| 1 | type check (assignment to $glyph-name): expected Str, got Any | Font::AFM |
| 1 | type check (binding to parameter '$attr'): expected Attribute, got JSON::Infer::Attribute | JSON::Infer |
| 1 | type check (binding to parameter '$b'): expected Math::Vector, got Num | Math::Vector |
| 1 | type check (binding to parameter '$batch'): expected UInt, got Bool | Proc::Q |
| 1 | type check (binding to parameter '$buf'): expected Buf, got Str | HTTP::Supply |
| 1 | type check (binding to parameter '$cfstring'): expected Pointer, got CArray | MacOS::AudioDevices |
| 1 | type check (binding to parameter '$eval'): expected Bool, got Int | Green |
| 1 | type check (binding to parameter '$integer'): expected Int, got Num | Math::Root |
| 1 | type check (binding to parameter '$mask'): expected Str, got Match | JSON::Mask |
| 1 | type check (binding to parameter '$orig-seq'): expected Sequence, got Seq | SeqSplitter |
| 1 | type check (binding to parameter '$s'): expected Seq, got List | Seq::PreFetch |
| 1 | type check (binding to parameter '$type'): expected Str, got CSSValue | CSS::Grammar |
| 1 | type check (binding to parameter '$v'): expected Version, got Str | Parse::STDF |
| 1 | type check (binding to parameter '$width'): expected Int, got Pair | Text::Center |
| 1 | type check failedin assignment; expected Iterable but got Hash | Avro |
| 1 | type check failedin assignment; expected KeyType but got Int | Duo |
| 1 | type check failedin assignment; expected MinG::Grammar but got Any | MinG |
| 1 | type check failedin assignment; expected Node but got Any | Algorithm::SkewHeap |
| 1 | type check failedin assignment; expected Str but got Any | Unix::Groups |
| 1 | type check failedin assignment; expected Str but got Int | Menu::Simple |
| 1 | type check failedin binding to parameter '%env' | Trait::Env |
| 1 | type check failedon attribute '$!backend-priority'; expected Keyrin | Keyring |
| 1 | type check failedon attribute '$!graph'; expected Build::Graph:D bu | Build::Graph |
| 1 | unable to determine gsc version | Inline::Scheme::Gambit |
| 1 | undeclared variable: $END | Method::Misspelt |
| 1 | undeclared variable: $block-size | Search::Dict |
| 1 | undeclared variable: $class_pointer | Template::Nest::XS |
| 1 | undeclared variable: $coerced | IdClass |
| 1 | undeclared variable: $cow | Acme::Cow6 |
| 1 | undeclared variable: $frob | vars |
| 1 | undeclared variable: $hash | URI::Query::FromHash |
| 1 | undeclared variable: $heap_cmp | Heap |
| 1 | undeclared variable: $ins | Docker::File |
| 1 | undeclared variable: $jmp | jmp |
| 1 | undeclared variable: $max | Hash::LRU |
| 1 | undeclared variable: $none-exclude | Pod::To::BigPage |
| 1 | undeclared variable: $prefix | BitEnum |
| 1 | undeclared variable: $raw-words | Template6 |
| 1 | undeclared variable: $s-mutex | Semaphore::ReadersWriters |
| 1 | undeclared variable: $sequence | Terminal::ANSIParser |
| 1 | undeclared variable: $sign | FatRatStr |
| 1 | undeclared variable: $val | Env::File |
| 1 | undeclared variable: $x | App::nm2perl6 |
| 1 | undeclared variable: %bans | Log::D |
| 1 | undeclared variable: %dictionary | Text::Emotion |
| 1 | undeclared variable: @set | ProblemSolver |
| 1 | undeclared variable: @types | Rake |
| 1 | undeclared variable: EXPERIMENTAL-PACK | P5pack |
| 1 | undefined routine: Algorithm::Diff::_longestCommonSubsequence | Algorithm::Diff |
| 1 | undefined routine: DEPRECATED | Typed::Subroutines |
| 1 | undefined routine: Printing::Jdf::mm | Printing::Jdf |
| 1 | undefined routine: Q | Acme::Text::UpsideDown |
| 1 | undefined routine: Qc | SP6 |
| 1 | undefined routine: actions | actions |
| 1 | undefined routine: actor | OO::Actors |
| 1 | undefined routine: bind | Injector |
| 1 | undefined routine: but | Lingua::NumericWordForms |
| 1 | undefined routine: exists | P5math |
| 1 | undefined routine: explicitly-manage | Log::Syslog::Native |
| 1 | undefined routine: export | PDF |
| 1 | undefined routine: guess_library_name | Parser::FreeXL::Native |
| 1 | undefined routine: headers | App::Squashathons |
| 1 | undefined routine: import | Math::Trig |
| 1 | undefined routine: indices | String::FuzzyIndex |
| 1 | undefined routine: latest-version | Native::FindVersion |
| 1 | undefined routine: model | ModelDB |
| 1 | undefined routine: nmax_by | List::UtilsBy |
| 1 | undefined routine: nqp::box_i | NativeHelpers::CBuffer |
| 1 | undefined routine: nqp::coerce_is | Net::Netmask::Fast |
| 1 | undefined routine: nqp::div_n | Random::Choice |
| 1 | undefined routine: nqp::gethllsym | Rakudo::CORE::META |
| 1 | undefined routine: nqp::getlexdyn | Rakudo::Options |
| 1 | undefined routine: nqp::getrusage | P5times |
| 1 | undefined routine: nqp::hllbool | has-word |
| 1 | undefined routine: nqp::isne_I | Bits |
| 1 | undefined routine: nqp::p6argvmarray | Pyrint |
| 1 | undefined routine: nqp::time | nano |
| 1 | undefined routine: nqp::time_n | Test::Stream |
| 1 | undefined routine: process | Web::Scraper |
| 1 | undefined routine: proto | uniprop |
| 1 | undefined routine: qw | Template::Classic |
| 1 | undefined routine: schema | OO::Schema |
| 1 | undefined routine: seconds | Interval |
| 1 | undefined routine: with | Configuration |
| 1 | undefined routine: without | Vips::Native |
| 1 | undefined routine: xxx | PSpec |
| 1 | undefined routine: þ | Algorithm::Evolutionary::Simple |
| 1 | undefined routine: нуль | Russian |

## Every dist, newest release first

| released | dist | version | verdict | blocked by / first error |
|---|---|---|---|---|
| 2026-08-22 | Env | 0.99 | self-fail | FAILED: 01-all.rakutest \| # Failed test 'did we change inside %*ENV also' at 01-all.rakutest line 14 |
| 2026-08-22 | Image::Resize | 0.2 | dep-fail | GD::Raw |
| 2026-08-22 | MergeOrderedSeqs | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test at 01-basic.rakutest line 6 |
| 2026-08-22 | P5chomp | 0.0.11 | self-fail | FAILED: 02-chomp.rakutest \| # Failed test 'did we actually chomp' at 02-chomp.rakutest line 14 |
| 2026-08-21 | App::Ariza | 0.2.4 | self-fail | FAILED: 01-resources.rakutest \| # Failed test 'found the distribution root' at 01-resources.rakutest line 12 |
| 2026-08-21 | App::Moneymoor | 0.4.2 | dep-fail | DBIish |
| 2026-08-21 | GD::Raw | 0.8 | self-fail | FAILED: 01-create-and-load.rakutest \| Cannot load native library 'gd 3': dlopen(gd 3, 0x0009): tried: 'gd 3'  … |
| 2026-08-21 | IRC::Log::Colabti | 0.0.53 | dep-fail | has-word |
| 2026-08-21 | IRC::Log::Textual | 0.0.18 | dep-fail | has-word |
| 2026-08-21 | Identity::Utils | 0.0.29 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 166: Error while compiling module String::Utils  … |
| 2026-08-21 | JSONL | 0.1.6 | self-fail | FAILED: 03-writer.rakutest \| # Failed test 'write-all writes array of Hashes' at 03-writer.rakutest line 37 |
| 2026-08-21 | JobQueue | 0.1.1 | self-fail | FAILED: 01-queue.rakutest \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of ty … |
| 2026-08-21 | LLM::Agent | 0.6.1 | dep-fail | MCP::Server |
| 2026-08-21 | LLM::Character | 0.2.3 | dep-fail | Getopt::Long |
| 2026-08-21 | LLM::Chat | 0.10.0 | self-fail | FAILED: 01_message.rakutest \| Cannot load native library '/Users/ash/eco-sweep/store3/resources/DA39A3EE5E6B4 … |
| 2026-08-21 | LLM::Classifiers::Emotions | 0.1.7 | dep-fail | Cro::HTTP |
| 2026-08-21 | LLM::Data::Inference | 0.10.0 | self-fail | FAILED: 02-task.rakutest \| ===SORRY!=== Parse error at line 562: Error while compiling module LLM::Data::Infe … |
| 2026-08-21 | LLM::Data::Pipeline | 0.7.1 | self-fail | FAILED: 04-runner.rakutest \| # Failed test 'Checkpoint writing — file exists after run' at 04-runner.rakutest … |
| 2026-08-21 | MCP::Client | 0.5.0 | dep-fail | MCP |
| 2026-08-21 | MCP::Server | 0.6.0 | dep-fail | MCP |
| 2026-08-21 | MCP::Server::Tool::Ask | 0.1.2 | dep-fail | MCP::Server |
| 2026-08-21 | MCP::Server::Tool::FileSystem | 0.2.2 | dep-fail | TreeSitter::Native |
| 2026-08-21 | MCP::Server::Tool::Shell | 0.4.1 | dep-fail | MCP::Server |
| 2026-08-21 | MCP::Server::Tool::Web | 0.1.1 | dep-fail | MCP::Server |
| 2026-08-21 | Markdown::Lex | 0.2.0 | self-fail | FAILED: 01-blocks.rakutest \| # Failed test 'every block is a Block' at 01-blocks.rakutest line 1430 |
| 2026-08-21 | Noise::Simplex::Native | 0.1.1 | self-fail | FAILED: 04-noise2d.t \| # Failed test 'Different seed → different output (1)' at 04-noise2d.t line 79 |
| 2026-08-21 | Rakudo-Type-Introspection | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 130: Error while compiling module Rakudo-Type-In … |
| 2026-08-21 | Roaring::Tags | 0.2.3 | self-fail | FAILED: 01-boolean-tags.rakutest \| |
| 2026-08-21 | Selkie | 0.16.0 | self-fail | FAILED: 01-widget.rakutest \| # Failed test 'theme - default when no theme set' at 01-widget.rakutest line 235 |
| 2026-08-21 | Template::Jinja2 | 0.3.0 | self-fail | FAILED: 01-lexer.rakutest \| # Failed test 'Equals operator' at 01-lexer.rakutest line 95 |
| 2026-08-21 | Tokenizers | 0.3.0 | self-fail | FAILED: 01_wrapper.rakutest \| # Failed test 'JSON tokenizer successfully created' at 01_wrapper.rakutest line … |
| 2026-08-21 | Vips::Native | 0.6.4 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'without' |
| 2026-08-20 | NotoFonts-OT | 0.2.1 | dep-fail | Getopt::Long |
| 2026-08-20 | TreeSitter::Native | 0.1.0 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'the core's Cursor is not shadowed' at 00-load.rakutest line 71 |
| 2026-08-20 | YAMLScript | 0.2.30 | self-fail | FAILED: 01-load.rakutest \| Shared library file 'libys.dylib.0.2.30' not found |
| 2026-08-19 | Gnome::Gtk4 | 0.2.10 | dep-fail | Gnome::Pango |
| 2026-08-19 | Gnome::N | 0.1.54 | self-fail | FAILED: GlibToRakuTypes.rakutest \| Undefined routine 'void' |
| 2026-08-18 | Elucid8::Build | 0.12.11 | dep-fail | Test::Deeply::Relaxed |
| 2026-08-17 | Notcurses::Native | 0.6.5 | self-fail | FAILED: 16-build-detect-platform.rakutest \| Undefined routine 'EVALFILE' |
| 2026-08-17 | Test::Selector | 0.4.2 | self-fail | FAILED: all.rakutest \| # Failed test 't_ignore-test Out okay.' at all.rakutest line 74 |
| 2026-08-16 | Draku | 0.0.3 | dep-fail | Cache::Dir |
| 2026-08-16 | Syndicate | 0.0.3 | self-fail | FAILED: 02-parse.rakutest \| Type 'FeedFormat' is not declared |
| 2026-08-16 | Terminal::MultiProgress | 0.0.3 | self-fail | FAILED: 02-event.rakutest \| Undeclared name 'Event::Status::Started' |
| 2026-08-16 | Terminal::UI | 0.1.14 | self-fail | FAILED: 00-basic.rakutest \| # Failed test 'The module can be use-d ok: Terminal::UI' at 00-basic.rakutest lin … |
| 2026-08-15 | Net::IP::Parse | 0.0.7 | self-fail | FAILED: basic.rakutest \| ===SORRY!=== Parse error at line 11: expected ) (got 'ip') |
| 2026-08-13 | Mi6::Helper | 1.2.1 | dep-fail | App::Mi6 |
| 2026-08-12 | MacOS::AudioDevices | 0.1.1 | self-fail | FAILED: 01-public-api.t \| Type check failed in binding to parameter '$cfstring'; expected Pointer but got CAr … |
| 2026-08-11 | ONNX::Native | 0.2.0 | self-fail | FAILED: 01-ffi.rakutest \| # Failed test 'no error set for cpu provider' at 01-ffi.rakutest line 53 |
| 2026-08-10 | CodeUnit | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::getcomp' |
| 2026-08-10 | ComfyUI::API | 0.2.2 | dep-fail | Cro::HTTP |
| 2026-08-10 | CompUnit::Util | 0.7.0 | self-fail | FAILED: 01-utils.rakutest \| # Failed test 'load again returns same thing' at 01-utils.rakutest line 123 |
| 2026-08-10 | HuggingFace::API | 0.2.2 | dep-fail | Cro::HTTP |
| 2026-08-10 | MagickWand | 0.2.0 | self-fail | FAILED: 02-magickwand.t \| Cannot load native library 'libMagickWand-7.Q16HDRI.dylib': dlopen(libMagickWand-7. … |
| 2026-08-10 | PDF::Native | 0.1.14 | self-fail | FAILED: 00-load.t \| # Failed test 'libpdf.dylib library has been built' at 00- … |
| 2026-08-10 | REPL | 0.0.26 | dep-fail | Text::Emoji |
| 2026-08-09 | Benchy | 1.2 | self-fail | FAILED: 01-operation.rakutest \| Stub code executed |
| 2026-08-09 | FINALIZER | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| Class 'LeavePhaser' cannot inherit from 'RakuAST::StatementPrefix::Phaser::Leave' … |
| 2026-08-09 | Testo | 1.9 | self-fail | FAILED: 01-is.t \| Testo::Test::Result |
| 2026-08-09 | overload::constant | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| ===WARNING=== Module overload::constant EXPORT failed: No such method 'refine_sla … |
| 2026-08-08 | Bitcoin::Core::Secp256k1 | 0.0.28 | self-fail | FAILED: 01.t \| Variable @.data used where no 'self' is available |
| 2026-08-08 | Qwiratry | 0.10.0 | dep-fail | Implementation::Loader |
| 2026-08-07 | App::samaki | 0.0.34 | dep-build-fail | Timezones::ZoneInfo |
| 2026-08-07 | SION | 0.1.0 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'SION loads' at 00-load.rakutest line 6 |
| 2026-08-05 | DateTime::strftime | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| Class 'DateTime' cannot inherit from / compose itself |
| 2026-08-04 | GUI::Editors | 0.1.18 | self-fail | FAILED: 000-GUI::Editors.rakutest \| ===SORRY!=== Parse error at line 119: unexpected operator in term positio … |
| 2026-08-04 | Node::Ethereum::KZG | 0.0.32 | build-fail |  |
| 2026-08-04 | Node::Ethereum::KeyStore::V3 | 0.0.27 | dep-fail | Crypt::LibScrypt |
| 2026-08-02 | Email::Simple | 2.1.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'We can get a header' at 01-basic.rakutest line 84 |
| 2026-08-02 | Graph | 0.1.2 | self-fail | FAILED: 01-basic-usage.rakutest \| ===SORRY!=== Parse error at line 104: Error while compiling module BinaryHe … |
| 2026-08-02 | Template::HAML | 0.9.6 | self-fail | FAILED: 0200-multiline-attrs.rakutest \| # Failed test 'child after multi-line hash records the actual source  … |
| 2026-07-31 | Geo::Valhalla | 0.0.4 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'module compiles and locates the native shim' at 00-load.rakutest li … |
| 2026-07-31 | if | 0.1.5 | self-fail | FAILED: if.rakutest \| # Failed test 'a bare use still works' at if.rakutest line 16 |
| 2026-07-30 | Jupyter::Chatbook | 0.3.9 | dep-fail | Text::Plot |
| 2026-07-29 | DSL::Entity::Foods | 0.1.1 | self-fail | FAILED: Food-names-parsing.t \| Cannot parse the command; target 'chocolate chip cookie ice creams' position 0 … |
| 2026-07-29 | DSL::Entity::Geographics | 0.1.6 | self-fail | FAILED: 02-Country-names-parsing.rakutest \| Cannot parse the command; target 'Argentina' position 0; parsed ' … |
| 2026-07-29 | DSL::Entity::Jobs | 0.1.1 | self-fail | FAILED: 01-Job-skills-parsing.rakutest \| Cannot parse the command; target 'java' position 0; parsed '', un-pa … |
| 2026-07-29 | Rakuast::RakuDoc::Render | 1.0.18 | dep-fail | Test::Deeply::Relaxed |
| 2026-07-29 | YAMLStar | 0.1.18 | self-fail | FAILED: 01-basic.rakutest \| Shared library file 'libyamlstar.dylib' not found |
| 2026-07-28 | Implementation::Loader | 0.0.10 | self-fail | FAILED: 01-basic-tests.rakutest \| # Failed test 'Passing class loaded OK' at 01-basic-tests.rakutest line 20 |
| 2026-07-28 | LLM::Functions | 0.5.10 | self-fail | FAILED: 02-LLM-evaluators.rakutest \| No such method 'evaluator' for invocant of type 'Any' |
| 2026-07-26 | JSON::RepositoryEvent | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| No such method 'set_name' for invocant of type 'Method' |
| 2026-07-26 | PDF::Font::Loader | 0.8.15 | dep-fail | Getopt::Long |
| 2026-07-26 | RepositoryEvent | 0.0.2 | dep-fail | JSON::RepositoryEvent |
| 2026-07-23 | CortexJS | 0.0.4 | dep-fail | LaTeX::Grammar |
| 2026-07-21 | Qwiratry::Format::XML | 0.0.1 | dep-fail | Implementation::Loader |
| 2026-07-21 | Qwiratry::Location::HTTP | 0.0.3 | dep-fail | Implementation::Loader |
| 2026-07-21 | Qwiratry::Test | 0.0.1 | dep-fail | Implementation::Loader |
| 2026-07-20 | Net::Ethereum | 0.0.185 | self-fail | FAILED: 01.t \| Variable @.hash used where no 'self' is available |
| 2026-07-20 | Qwiratry::Format::JSON | 0.0.1 | dep-fail | Implementation::Loader |
| 2026-07-20 | Qwiratry::Format::YAML | 0.0.1 | dep-fail | Implementation::Loader |
| 2026-07-18 | Markup::Calendar | 0.2.3 | self-fail | FAILED: 00-sanity.rakutest \| The argument $highlight is expected to be a list of Date-string pairs, Date obje … |
| 2026-07-16 | NativeHelpers::Blob | 0.1.13 | self-fail | FAILED: 01-basic.rakutest \| Can't determine actual Offset |
| 2026-07-15 | CSS::Properties | 0.10.9 | self-fail | FAILED: 00-readme.t \| # Failed test 'code sample' at 00-readme.t line 16 |
| 2026-07-13 | Termbox2 | 0.0.5 | self-fail | FAILED: 03-exports.t \| # Failed test 'use Termbox2 (bare) imports exactly the full symbol set' at 03-exports. … |
| 2026-07-11 | BDD::Behave::Playwright | 0.9.1 | dep-fail | WWW::Playwright |
| 2026-07-11 | Humming-Bird | 4.1.0 | dep-fail | ULID |
| 2026-07-11 | MVC::Keayl::Admin | 0.9.0 | self-fail | FAILED: load.rakutest \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of type ' … |
| 2026-07-11 | WWW::Playwright | 0.9.1 | self-fail | FAILED: diagnostics.rakutest \| Could not find PlaywrightSupport in: |
| 2026-07-09 | Air-Plugin-RakuDoc | 0.2.1 | dep-fail | Test::Deeply::Relaxed |
| 2026-07-09 | Random::Names | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| Default constructor for 'Link' only takes named arguments |
| 2026-07-06 | Sys::HostAddr | 0.2.1 | dep-fail | Net::Netmask |
| 2026-07-05 | Benchmark | 2.1 | self-fail | FAILED: statistics.rakutest \| ===SORRY!=== Parse error at line 13: expected ) (got '&') |
| 2026-07-04 | Air::Examples | 0.0.26 | dep-fail | Net::SMTP |
| 2026-07-04 | Air::Plugin::Donate | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 33: Error while compiling module Air::Plugin::Do … |
| 2026-07-04 | App::Mi6 | 3.0.9 | self-fail | FAILED: 01-basic.rakutest \| Potential difficulties: |
| 2026-07-03 | Air | 0.1.32 | self-fail | FAILED: 01-basic.rakutest \| Useless use of constant string " |
| 2026-07-02 | Qwiratry--Location--HTTP | 0.0.2 | dep-fail | Implementation::Loader |
| 2026-07-01 | Rainbow | 0.4.4 | self-fail | FAILED: sub-langs.rakutest \| # Failed test 'rakudoc-lang' at sub-langs.rakutest line 24 |
| 2026-06-30 | Ethelia | 0.0.154 | dep-build-fail | Node::Ethereum::KZG |
| 2026-06-29 | BusyIndicator | 0.6.2 | dep-fail | LibUSB |
| 2026-06-29 | FFmpegProgressBar | 0.0.7 | dep-fail | Terminal::Size |
| 2026-06-29 | GD | 0.0.5 | self-fail | FAILED: 010-load.rakutest \| ===SORRY!=== Parse error at line 12: Error while compiling module GD (line 12): u … |
| 2026-06-29 | Geo::Basic | 0.0.5 | self-fail | FAILED: 04-quadkey.rakutest \| No such method 'parse-base' for invocant of type 'IntStr' |
| 2026-06-29 | JSON::Collector | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| Found the following issues: |
| 2026-06-29 | OpenMPT::Bindings | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'void' |
| 2026-06-28 | JSON::Webhook | 0.0.2 | dep-fail | JSON::Collector |
| 2026-06-28 | Kubernetes | 0.0.3 | dep-fail | Text::CodeProcessing |
| 2026-06-27 | Chart::Gnuplot | 0.0.23 | build-fail |  |
| 2026-06-26 | hide-methods | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'can we call B.bar' at 01-basic.rakutest line 33 |
| 2026-06-26 | unprint | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'did 'print q/hello/' give 'hello'' at 01-basic.rakutest line 18 |
| 2026-06-25 | from | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'was &skip NOT imported' at 01-basic.rakutest line 7 |
| 2026-06-25 | uniname-words | 10.0.16 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 42: Error while compiling module uniname-words ( … |
| 2026-06-24 | Air::Plugin::MailForm | 0.0.1 | dep-fail | Net::SMTP |
| 2026-06-24 | Commands | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| Don't know what to do with: (:quit(sub { ... }), :exit("quit"), "release all" =>  … |
| 2026-06-24 | Edit::Files | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| No such method 'ACCEPTS' for invocant of type 'Callable' |
| 2026-06-24 | JSON::Path | 1.10 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'garbage input should die' at 01-basic.rakutest line 64 |
| 2026-06-24 | Needle::Compile | 0.0.12 | dep-fail | has-word |
| 2026-06-24 | Prompt | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| # Failed test ''prompt' not exported by default' at 01-basic.rakutest line 6 |
| 2026-06-24 | Prompt::Expand | 0.0.5 | dep-fail | Text::Emoji |
| 2026-06-24 | Text::Emoji | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 90: Error while compiling module Map::Match (lin … |
| 2026-06-23 | Cro::WebApp | 0.10.1 | self-fail | FAILED: compile-all.rakutest \| ===SORRY!=== Parse error at line 522: Error while compiling module Cro::WebApp … |
| 2026-06-23 | GOTO | 0.1.7 | dep-fail | GUI::Editors |
| 2026-06-23 | Sparky | 0.2.32 | dep-fail | Time::Crontab |
| 2026-06-22 | GnomeTools | 0.8.2 | dep-fail | Gnome::Pango |
| 2026-06-22 | MoarVM::Bytecode | 0.0.27 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 100: Error while compiling module paths (line 10 … |
| 2026-06-22 | Slang::Roman | 0.6.3 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 83: expected ) (got 'rIV') |
| 2026-06-17 | NativeLibs | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| # Failed test ''&trait_mod:<is>' loaded too' at 01-basic.rakutest line 22 |
| 2026-06-16 | Contact | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| Cannot parse contact: |
| 2026-06-15 | ASTQuery | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 12: Error while compiling module ASTQuery::Actio … |
| 2026-06-15 | vars | 0.0.8 | self-fail | FAILED: 01-basic.t \| # Failed test 'did we get an export for $frob at BEGIN' at 01-basic.t line 9 |
| 2026-06-14 | CSS::Minifier | 0.0.13 | self-fail | FAILED: 01-plugins.rakutest \| Cannot resolve caller uc(Any:U); the invocant is a type object, not an instance |
| 2026-06-14 | CSS::Writer | 0.3.3 | self-fail | FAILED: 00basic.t \| ===SORRY!=== Parse error at line 496: Error while compiling module CSS::Writer (line 496) … |
| 2026-06-14 | Ecosystem::Cache | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| No such method 'hyper' for invocant of type 'Iterable' |
| 2026-06-13 | CSS::Grammar | 0.4.3 | self-fail | FAILED: compat.t \| # Failed test 'css1 ws parse: <!-- unterminated comment' at compat.t line 56 |
| 2026-06-13 | CSS::Module | 0.7.7 | self-fail | FAILED: 00basic.t \| ===SORRY!=== Parse error at line 496: Error while compiling module CSS::Writer (line 496) … |
| 2026-06-13 | CSS::TagSet | 0.1.4 | self-fail | FAILED: tag-set-pango.t \| ===SORRY!=== Parse error at line 32: Couldn't find final ')' (corresponding ( was a … |
| 2026-06-13 | Ecosystem | 0.0.33 | self-fail | FAILED: 01-basic.rakutest \| No such method 'hyper' for invocant of type 'Iterable' |
| 2026-06-13 | Ecosystem::Archive::Update | 0.0.29 | dep-fail | Version::Repology |
| 2026-06-13 | Hash::Agnostic | 0.0.20 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does .Str work ok' at 01-basic.rakutest line 32 |
| 2026-06-13 | Map::Agnostic | 0.0.14 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does .Str work ok' at 01-basic.rakutest line 29 |
| 2026-06-13 | Map::Match | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 90: Error while compiling module Map::Match (lin … |
| 2026-06-13 | nano | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::time' |
| 2026-06-12 | Base64::Native | 0.0.10 | dep-fail | Getopt::Long |
| 2026-06-11 | FontConfig | 0.1.9 | self-fail | FAILED: 00raw.t \| ===WARNING=== Module MacOS::NativeLib EXPORT failed: Failed to create symlink called '/User … |
| 2026-06-11 | WebDriver2 | 0.0.11 | dep-fail | PostCocoon::Url |
| 2026-06-10 | App::IRC::Log | 0.0.54 | dep-fail | IRC::Log |
| 2026-06-10 | IRC::Channel::Log | 0.0.43 | dep-fail | has-word |
| 2026-06-10 | LibXML | 0.11.3 | dep-fail | Getopt::Long |
| 2026-06-10 | MongoDB::Fast | 0.2.2 | self-fail | FAILED: 01-basic.rakutest \| Cannot connect to 127.0.0.1:27017 |
| 2026-06-09 | JSON::Fast::Hyper | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| No such method 'hyper' for invocant of type 'Iterable' |
| 2026-06-09 | MoarVM::Profile | 0.0.8 | dep-fail | DB |
| 2026-06-09 | PURL | 0.0.15 | dep-fail | Version::Repology |
| 2026-06-09 | SBOM::CycloneDX | 0.0.16 | dep-fail | Version::Repology |
| 2026-06-09 | SBOM::Raku | 0.0.13 | dep-fail | Version::Repology |
| 2026-06-09 | Zef::Configuration | 0.0.13 | dep-fail | shorten-sub-commands |
| 2026-06-08 | Font::FreeType | 0.5.16 | self-fail | FAILED: 00-basic.t \| Could not find NativeCall::Types in: |
| 2026-06-07 | GtkLayerShell | 0.2.8 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: GtkLayerShell::Native:auth<zef:CIAvash> … |
| 2026-06-07 | Math::DistanceFunctions::Native | 0.1.2 | self-fail | FAILED: 02-edit-distance.rakutest \| Cannot find native symbol 'is_utf8' |
| 2026-06-06 | Terminal::Print | 0.978 | self-fail | FAILED: 00-basics.t \| Variable $.dwSize used where no 'self' is available |
| 2026-06-05 | Terminal::Widgets | 0.3.2 | self-fail | FAILED: 00-use.rakutest \| Variable $.dwSize used where no 'self' is available |
| 2026-06-04 | PDF::Content | 0.9.11 | dep-fail | Getopt::Long |
| 2026-06-03 | path-utils | 0.0.25 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::const::BINARY_ENDIAN_LITTLE' |
| 2026-06-02 | Cro::HTTP | 0.8.13 | self-fail | FAILED: http-auth-basic-with-session.rakutest \| ===WARNING=== Module if EXPORT failed: No such method 'legacy … |
| 2026-06-02 | Cro::OpenAPI::RoutesFromDefinition | 1.0.5 | dep-fail | JSON::Pointer |
| 2026-06-01 | WWW::HorizonsEphemerisSystem | 0.0.2 | self-fail | FAILED: 00-sanity.rakutest \| Cannot convert string to number: base-10 number must begin with valid digits or  … |
| 2026-05-31 | Algorithm::KDimensionalTree | 0.1.2 | self-fail | FAILED: 01-basic-usage.rakutest \| The given vectors are expected to have the same number of elements. |
| 2026-05-31 | Math::DistanceFunctions | 0.1.4 | self-fail | FAILED: 03-distance-functions-native.rakutest \| Could not use native implementation. |
| 2026-05-31 | Math::Nearest | 0.0.7 | self-fail | FAILED: 01-Scan-method.rakutest \| The given vectors are expected to have the same number of elements. |
| 2026-05-30 | Duck::CSV | 0.0.2 | self-fail | FAILED: 01-test.rakutest \| Cannot load native library 'duckdb': dlopen(duckdb, 0x0009): tried: 'duckdb' (no s … |
| 2026-05-29 | Air::Plugin::Rakudoc | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 464: Error while compiling module Air::Base (lin … |
| 2026-05-28 | App::Rak | 0.3.20 | dep-fail | Git::Files |
| 2026-05-28 | App::Rak::Complete | 0.0.13 | dep-fail | Git::Files |
| 2026-05-28 | App::Rak::Markdown | 0.0.5 | dep-fail | Git::Files |
| 2026-05-28 | CLI::Version | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 14: expected } (got '') |
| 2026-05-28 | IRC::Client::Plugin::Rakkable | 0.0.3 | dep-fail | Git::Files |
| 2026-05-28 | META::constants | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| ===WARNING=== Module META::constants EXPORT failed: No such method 'new' for invo … |
| 2026-05-28 | highlighter | 0.0.23 | dep-fail | has-word |
| 2026-05-28 | rak | 0.0.67 | dep-fail | Git::Files |
| 2026-05-24 | Form | 1.1 | self-fail | FAILED: 03-textformatting.rakutest \| # Failed test 'Lines were correct' at 03-textformatting.rakutest line 28 |
| 2026-05-24 | zef | 1.1.3 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: Zef::Build' at 00-load.rakutest line 9 |
| 2026-05-23 | Math::NumberTheory | 0.1.2 | self-fail | FAILED: 01-integer-factors.rakutest \| ===SORRY!=== Parse error at line 1718: Error while compiling module Mat … |
| 2026-05-20 | Terminal::Capabilities | 0.0.21 | self-fail | FAILED: 01-symbol-set.rakutest \| No such method 'enum_value_list' for invocant of type 'SymbolSet' |
| 2026-05-20 | Terminal::Tests | 0.0.18 | self-fail | FAILED: 00-use.rakutest \| Undefined routine 'nqp::unipropcode' |
| 2026-05-19 | HTTP::UserAgent::Strict | 0.9.2 | self-fail | FAILED: 010-headers.rakutest \| # Failed test 'ETag.Str' at 010-headers.rakutest line 15 |
| 2026-05-18 | Grammar::Editor | 0.0.2 | dep-fail | Grammar::Extractor |
| 2026-05-18 | Selkie::UI | 0.0.4 | self-fail | FAILED: 01-button-textinput.rakutest \| ===SORRY!=== Parse error at line 39: Error while compiling module Selk … |
| 2026-05-17 | Grammar::Extractor | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 17: Error while compiling module Grammar::Extrac … |
| 2026-05-14 | LLM::Prompts | 0.2.15 | self-fail | FAILED: 02-prompt-parsing.rakutest \| # Failed test 'Parsing function cell spec' at 02-prompt-parsing.rakutest … |
| 2026-05-12 | Hilite | 0.2.8 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 11: Error while compiling module Hilite (line 11 … |
| 2026-05-11 | Hash::Ordered | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does .gist work ok' at 01-basic.rakutest line 16 |
| 2026-05-10 | FStrings | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'general g small' at 01-basic.rakutest line 53 |
| 2026-05-10 | Gnome::Pango | 0.1.14 | self-fail | FAILED: Context.rakutest \| Undefined routine 'void' |
| 2026-05-10 | Text::MiscUtils | 0.0.13 | self-fail | FAILED: 02-layout.rakutest \| Undefined routine 'nqp::unipropcode' |
| 2026-05-09 | Air::Plugin::Wordcloud | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 55: Error while compiling module Air::Plugin::Wo … |
| 2026-05-09 | Memoize | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| # Failed test '&unmemoize *not* imported?' at 01-basic.rakutest line 9 |
| 2026-05-09 | Physics::Unit | 2.0.31 | build-fail |  |
| 2026-05-08 | FatRatStr | 0.0.14 | self-fail | FAILED: 01-fatratstr.rakutest \| Variable '$sign' is not declared |
| 2026-05-08 | Physics::Measure | 2.0.26 | dep-build-fail | Physics::Unit |
| 2026-05-07 | AccountableBagHash | 0.0.7 | self-fail | FAILED: 01-basic.t \| # Failed test 'The object is-a 'AccountableBagHash'' at 01-basic.t line 9 |
| 2026-05-06 | Data::Importers | 0.1.8 | self-fail | FAILED: 01-basic-usage.rakutest \| # Failed test 'Text and JSON import' at 01-basic-usage.rakutest line 271 |
| 2026-05-06 | Gnome::Gdk4 | 0.1.23 | self-fail | FAILED: ContentProvider.rakutest \| Undefined routine 'void' |
| 2026-05-03 | Intl::LanguageTag | 0.12.7 | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 533: Error while compiling module Intl::Languag … |
| 2026-05-02 | ML::LatentSemanticAnalyzer | 0.0.3 | dep-fail | Lingua::StopwordsISO |
| 2026-05-02 | Timezones::ZoneInfo | 0.5.0 | build-fail |  |
| 2026-05-01 | DSL::English::LatentSemanticAnalysisWorkflows | 0.8.3 | self-fail | FAILED: 01-Creation-command-tests.rakutest \| Cannot parse the command; target 'use lsa object lsaObj' positio … |
| 2026-05-01 | DSL::Translators | 0.1.1 | dep-fail | Pretty::Table |
| 2026-04-30 | Air::Plugin::Hilite | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 11: Error while compiling module Hilite (line 11 … |
| 2026-04-30 | ML::SparseMatrixRecommender | 0.0.4 | self-fail | FAILED: 01-creation.rakutest \| # Failed test 'wide form, not native' at 01-creation.rakutest line 44 |
| 2026-04-30 | Math::SparseMatrix | 0.0.15 | self-fail | FAILED: 02-CSR-access.rakutest \| # Failed test at 02-CSR-access.rakutest line 114 |
| 2026-04-30 | Math::SparseMatrix::Native | 0.0.9 | self-fail | FAILED: 01-creation.rakutest \| The agument $ncol is expected to be a non-negative integer or Whatever. |
| 2026-04-29 | CRoaring | 0.2.3 | self-fail | FAILED: 02-bitmap.rakutest \| |
| 2026-04-29 | Monad | 0.2.1 | self-fail | FAILED: 01_maybe.t \| ===SORRY!=== Parse error at line 28: unexpected operator in term position (got '>>=') |
| 2026-04-29 | OpenRouter::API | 0.1.1 | dep-fail | Cro::HTTP |
| 2026-04-26 | Net::Whois::Async | 0.1.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'invalid IP rejected by IP subset' at 01-basic.rakutest line 1 |
| 2026-04-25 | Digest::SHA1::Native | 1.0.1 | pass |  |
| 2026-04-25 | Digest::SHA256::Native | 1.0.1 | pass |  |
| 2026-04-23 | Data::Translators | 0.1.18 | self-fail | FAILED: 01-basic-usage-HTML.rakutest \| Unsupported prefix 'dimslip' |
| 2026-04-23 | IO::Path::AutoDecompress | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| Too many levels of recursion |
| 2026-04-19 | Term::Choose | 2.0.6 | self-fail | FAILED: 20-arguments.t \| # Failed test 'invalid value hello for option default dies ok' at 20-arguments.t lin … |
| 2026-04-17 | shorten-sub-commands | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does foo give foozle?' at 01-basic.rakutest line 11 |
| 2026-04-12 | DBIish | 0.6.8 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'Can install driver for 'Oracle'' at 01-basic.rakutest line 17 |
| 2026-04-12 | P5built-ins | 0.0.30 | dep-fail | P5times |
| 2026-04-12 | P5pack | 0.0.16 | self-fail | FAILED: 01-basic.rakutest \| Variable 'EXPERIMENTAL-PACK' is not declared |
| 2026-04-11 | PDF::Tags | 0.2.8 | dep-fail | Getopt::Long |
| 2026-04-11 | PDF::Tags::Reader | 0.0.18 | dep-fail | Getopt::Long |
| 2026-04-11 | Pod::To::PDF | 0.1.14 | dep-fail | HarfBuzz::Shaper::Cairo |
| 2026-04-09 | LLM::Chat::Backend | 0.2.1 | dep-fail | LLM::Chat |
| 2026-04-08 | LLM::Resources | 0.0.2 | dep-fail | LLM::Graph |
| 2026-04-07 | Timezone::Simple | 0.0.1 | dep-build-fail | Timezones::ZoneInfo |
| 2026-04-06 | LLM::Graph | 0.0.11 | self-fail | FAILED: 01-initialization.rakutest \| ===SORRY!=== Parse error at line 104: Error while compiling module Binar … |
| 2026-04-06 | Recipe::Parser | 0.0.1 | self-fail | FAILED: 01-test-parse-valid-string.rakutest \| # Failed test 'valid-string('salt')' at 01-test-parse-valid-str … |
| 2026-04-05 | DSL::English::EpidemiologyModelingWorkflows | 0.5.0 | self-fail | FAILED: ECMMon-batch-simulation-commands.rakutest \| Cannot parse the command; target 'batch simulate using df … |
| 2026-04-04 | DSL::English::DataAcquisitionWorkflows | 0.1.0 | dep-fail | Pretty::Table |
| 2026-04-04 | DSL::English::FoodPreparationWorkflows | 0.1.0 | dep-fail | DSL::Entity::Geographics |
| 2026-04-03 | App::Crag | 0.0.57 | dep-fail | Math::Vector |
| 2026-04-02 | Data::TypeSystem | 0.1.8 | self-fail | FAILED: 01-random-data.rakutest \| No such method 'elems' for invocant of type 'Data::TypeSystem::Assoc' |
| 2026-03-31 | PDF::Class | 0.5.30 | dep-fail | Getopt::Long |
| 2026-03-30 | CSS::Stylesheet | 0.1.5 | self-fail | FAILED: at-font-face.t \| ===SORRY!=== Parse error at line 111: Error while compiling module CSS::Stylesheet ( … |
| 2026-03-29 | LaTeX::Grammar | 0.0.5 | self-fail | FAILED: 01-parse-formulas.rakutest \| # Failed test 'parse \left( ... \right) group' at 01-parse-formulas.raku … |
| 2026-03-27 | Net::Netmask::Fast | 0.1.0 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::coerce_is' |
| 2026-03-25 | IdClass | 0.0.3 | self-fail | FAILED: 02-coerce.rakutest \| Useless use of () in sink context (line 8) |
| 2026-03-23 | Text::MathematicalCase | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| Cannot invoke non-Callable value of type Any |
| 2026-03-22 | MongoDB::Queue | 0.1.1 | dep-fail | MongoDB::Fast |
| 2026-03-19 | Understitch | 0.0.2 | self-fail | FAILED: 07-properties.t \| No such method 'precedence' for invocant of type 'Sub' |
| 2026-03-18 | Slang::NumberBase | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 7: Malformed numeric literal |
| 2026-03-16 | Data::Dump::Tree | 2.9.4 | self-fail | FAILED: 00_use.rakutest \| ===SORRY!=== Parse error at line 1035: Error while compiling module Data::Dump::Tre … |
| 2026-03-16 | Duckie | 0.0.12 | self-fail | FAILED: 01-native.rakutest \| Cannot load native library 'duckdb': dlopen(duckdb, 0x0009): tried: 'duckdb' (no … |
| 2026-03-11 | Air::Plugin::Asciinema | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 42: Error while compiling module Air::Plugin::As … |
| 2026-03-10 | Geo::Polyline | 0.0.4 | self-fail | FAILED: 01-use.rakutest \| # Failed test 'The module can be use-d ok: Geo::Polyline' at 01-use.rakutest line 5 |
| 2026-03-09 | Chronic | 0.0.14 | timeout | Chronic |
| 2026-03-09 | Log::Async | 0.0.17 | pass |  |
| 2026-03-03 | IRC::Log::Perlgeek | 0.0.2 | dep-fail | has-word |
| 2026-03-02 | App::Raku::Log | 0.0.37 | dep-fail | has-word |
| 2026-03-02 | CSS::Selector::To::XPath | 0.0.9 | self-fail | FAILED: 01_xpath.t \| ===SORRY!=== Parse error at line 152: Error while compiling module CSS::Selector::To::XP … |
| 2026-03-02 | String::Utils | 0.0.40 | self-fail | FAILED: 01-basic.rakutest \| Useless use of constant string "bar" in sink context (line 6) |
| 2026-03-01 | Crypt::Argon2 | 0.3.0 | build-fail |  |
| 2026-03-01 | IRC::Log | 0.0.26 | dep-fail | has-word |
| 2026-02-28 | MermaidJS::Grammar | 0.0.2 | self-fail | FAILED: 01-flowchart.rakutest \| # Failed test 'sample 2 parses' at 01-flowchart.rakutest line 61 |
| 2026-02-26 | Physics::Constants | 1.0.10 | dep-build-fail | Physics::Unit |
| 2026-02-26 | Physics::Navigation | 0.0.5 | dep-build-fail | Physics::Unit |
| 2026-02-21 | LibXSLT | 0.1.8 | dep-fail | Getopt::Long |
| 2026-02-19 | EventSource::Client | 0.0.6 | timeout | EventSource::Server |
| 2026-02-19 | Sequence::Generator | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 1365: Error while compiling module Sequence::Gen … |
| 2026-02-18 | Hash::MutableKeys | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| Class 'Proxy' cannot inherit from / compose itself |
| 2026-02-17 | Method::Misspelt | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| Variable '$END' is not declared |
| 2026-02-17 | PDF | 0.6.15 | self-fail | FAILED: 00-helloworld.t \| Useless use of () in sink context (line 20) |
| 2026-02-15 | Stomp | 0.1.0 | dep-fail | Concurrent::Iterator |
| 2026-02-10 | Array::Agnostic | 0.0.18 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does .raku work ok' at 01-basic.rakutest line 28 |
| 2026-02-10 | Array::Sparse | 0.0.13 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does .raku work ok' at 01-basic.rakutest line 15 |
| 2026-02-10 | CRDT | 0.0.16 | self-fail | FAILED: 02-g-counter.rakutest \| # Failed test 'X++' at 02-g-counter.rakutest line 7 |
| 2026-02-10 | List::Agnostic | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does items 0 give correct value in MyOtherList' at 01-basic.rakute … |
| 2026-02-10 | paths | 10.2 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 100: Error while compiling module paths (line 10 … |
| 2026-02-09 | Math::Sequences | 0.1.2 | self-fail | FAILED: Integers.rakutest \| ===SORRY!=== Parse error at line 481: Error while compiling module Math::Sequence … |
| 2026-02-07 | LibXML::Writer | 0.0.5 | dep-fail | Getopt::Long |
| 2026-02-06 | mod-div-specs | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'div dies with dispatch error if non-int' at 01-basic.rakutest line … |
| 2026-02-04 | Template6 | 0.15.0 | self-fail | FAILED: 01-simple.rakutest \| Variable '$raw-words' is not declared |
| 2026-02-02 | App::ByWord | 0.0.5 | self-fail | FAILED: 02-orp.rakutest \| Variable $.dwSize used where no 'self' is available |
| 2026-01-30 | MCP | 0.33.6 | self-fail | FAILED: 01-types.rakutest \| # Failed test 'LogLevel' at 01-types.rakutest line 936 |
| 2026-01-26 | LLM::RetrievalAugmentedGeneration | 0.0.8 | self-fail | FAILED: 01-ingest-vector-database.rakutest \| ===SORRY!=== Parse error at line 128: Error while compiling modu … |
| 2026-01-19 | Jupyter::Kernel | 1.0.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: Jupyter::Kernel' at 01-basic.rakutest  … |
| 2026-01-18 | FastCGI | 0.9.0 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: FastCGI' at 01-basic.rakutest line 5 |
| 2026-01-18 | Math::Angle | 0.1.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 566: Error while compiling module Math::Angle (l … |
| 2026-01-18 | PSGI | 1.2.3 | self-fail | FAILED: encode-psgi-response.rakutest \| # Failed test 'buf body' at encode-psgi-response.rakutest line 17 |
| 2026-01-17 | MIME::Types | 0.3 | self-fail | FAILED: mime.rakutest \| # Failed test 'get an invalid type' at mime.rakutest line 40 |
| 2026-01-16 | Graphviz::DOT::Chessboard | 0.0.6 | self-fail | FAILED: 01-basic-usage.rakutest \| Potential difficulties: |
| 2026-01-12 | Template::Mustache | 1.2.6 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 614: Error while compiling module Template::Must … |
| 2026-01-11 | Spreadsheet::XLSX | 0.3.5 | dep-fail | Getopt::Long |
| 2026-01-09 | Map::Mapnik | 0.0.3 | self-fail | FAILED: 01-generate-xml.rakutest \| No such method 'list' for invocant of type 'XML::Text' |
| 2026-01-07 | Lumberjack::Application | 0.0.11 | dep-fail | Backtrace::AsHTML |
| 2026-01-07 | Pheix | 1.1.3 | dep-build-fail | Node::Ethereum::KZG |
| 2026-01-06 | Audio::Hydrogen | 0.0.7 | self-fail | FAILED: 020-drumkit.t \| # Failed test 'from-xml' at 020-drumkit.t line 14 |
| 2026-01-06 | Audio::Icecast | 0.0.6 | self-fail | FAILED: 020-stats-basic.t \| # Failed test 'create Stats from xml' at 020-stats-basic.t line 14 |
| 2026-01-06 | Audio::Libshout | 0.0.15 | dep-fail | AccessorFacade |
| 2026-01-06 | Audio::Playlist::JSPF | 0.0.6 | self-fail | FAILED: 020-use.t \| # Failed test 'can load the module' at 020-use.t line 7 |
| 2026-01-06 | Linux::Fuser | 0.0.16 | dep-fail | System::Passwd |
| 2026-01-06 | Lumberjack | 0.1.5 | self-fail | FAILED: 020-class.t \| Undeclared name 'Error' |
| 2026-01-06 | Lumberjack::Dispatcher::Syslog | 0.0.6 | self-fail | FAILED: 020-dispatcher.t \| Undeclared name 'Error' |
| 2026-01-06 | Lumberjack::Message::JSON | 0.1.11 | self-fail | FAILED: 030-message.t \| Undefined routine 'nqp::objprimspec' |
| 2026-01-06 | MessagePack::Class | 0.0.5 | self-fail | FAILED: 020-roundtrip.t \| # Failed test 'to messagepack' at 020-roundtrip.t line 27 |
| 2026-01-06 | Monitor::Monit | 0.0.6 | self-fail | FAILED: 030-status.t \| # Failed test 'from-xml' at 030-status.t line 12 |
| 2026-01-06 | Object::Permission | 0.0.7 | self-fail | FAILED: 020-basic.t \| # Failed test 'throws when it gets an exception' at 020-basic.t line 126 |
| 2026-01-06 | Pg::Notify | 0.0.8 | self-fail | FAILED: 005-use.t \| # Failed test 'The module can be use-d ok: Pg::Notify' at 005-use.t line 7 |
| 2026-01-06 | Tinky | 0.1.5 | self-fail | FAILED: 030-apply-simple.t \| # Failed test 'apply-transition with 'one-two' lives' at 030-apply-simple.t line … |
| 2026-01-06 | Tinky::JSON | 0.0.8 | self-fail | FAILED: 010-use.t \| # Failed test 'can use the module ok' at 010-use.t line 7 |
| 2026-01-05 | IO::Path::Mode | 0.1.0 | self-fail | FAILED: 020-basic.t \| # Failed test 'and it's the right sort of thing' at 020-basic.t line 10 |
| 2026-01-05 | ML::FindTextualAnswer | 0.2.8 | self-fail | FAILED: 01-llm-results-post-processing.rakutest \| No such method 'evaluator' for invocant of type 'Any' |
| 2026-01-05 | RedX::HashedPassword | 0.0.8 | dep-fail | Getopt::Long |
| 2026-01-04 | L10N::AF | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2026-01-04 | XML::Class | 0.0.11 | self-fail | FAILED: 020-basic-out.t \| # Failed test 'to-xml(:document)' at 020-basic-out.t line 19 |
| 2026-01-02 | AI::Gator | 0.0.14 | dep-fail | Log::Async |
| 2026-01-02 | Math::PascalTriangle | 0.1.0 | self-fail | FAILED: 02-triangle.t \| # Failed test ':line(0):col(0)' at 02-triangle.t line 93 |
| 2026-01-02 | SupplyTimeWindow | 0.0.1 | timeout | SupplyTimeWindow |
| 2026-01-01 | Crolite | 0.0.1 | dep-fail | Cro::HTTP::Test |
| 2026-01-01 | Jupyter::Converter | 0.1.0 | self-fail | FAILED: 01-markdown.rakutest \| # Failed test 'Markdown conversion from JSON string' at 01-markdown.rakutest l … |
| 2025-12-30 | Markdown::Grammar | 0.6.4 | self-fail | FAILED: 02-Mathematica.rakutest \| # Failed test 'Perl6 cell' at 02-Mathematica.rakutest line 115 |
| 2025-12-29 | Graph::RandomMaze | 0.0.1 | dep-fail | Math::Nearest |
| 2025-12-27 | Terminal::Gauge | 0.1.0 | self-fail | FAILED: 002-Example.rakutest \| ===SORRY!=== Parse error at line 422: Error while compiling module Terminal::G … |
| 2025-12-25 | CSS::Font::Resources | 0.0.11 | self-fail | FAILED: load.t \| ===SORRY!=== Parse error at line 145: Error while compiling module CSS::Properties::Calculat … |
| 2025-12-24 | HTML::Canvas | 0.1.4 | dep-fail | Getopt::Long |
| 2025-12-23 | CSS::Specification | 0.5.3 | self-fail | FAILED: 00basic.t \| ===SORRY!=== Parse error at line 173: expected } (got '') |
| 2025-12-23 | Raygui::Bindings | 0.0.7 | dep-build-fail | Raylib::Bindings |
| 2025-12-23 | Raylib::Bindings | 0.0.20 | build-fail |  |
| 2025-12-21 | PDF::API6 | 0.2.10 | dep-fail | Getopt::Long |
| 2025-12-19 | Terminal-Widgets-Plugins-Anolis | 1.0.0 | dep-fail | Terminal::Widgets |
| 2025-12-13 | Test::Time | 0.0.2 | self-fail | FAILED: 01-tdd.t \| # Failed test at 01-tdd.t line 12 |
| 2025-12-10 | Rakudo::Options | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::getlexdyn' |
| 2025-12-09 | ML::AssociationRuleLearning | 0.1.5 | dep-fail | Pretty::Table |
| 2025-12-05 | DataStar | 0.0.5 | self-fail | FAILED: 0-load-test.rakutest \| # Failed test 'Module 'DataStar' used okay' at 0-load-test.rakutest line 10 |
| 2025-12-05 | Slang::Nogil | 1.3 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 15: expected variable after declarator (got 'a') |
| 2025-11-30 | dosh | 9.0.0 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: DOSH::CLI' at 00-use.rakutest line 5 |
| 2025-11-28 | LLM::DWIM | 0.0.6 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: LLM::DWIM' at 00-use.rakutest line 4 |
| 2025-11-18 | Terminal-API | 1.0.5 | self-fail | FAILED: basic.rakutest \| Variable $.dwSize used where no 'self' is available |
| 2025-11-14 | Date::Utils | 0.7.0 | self-fail | FAILED: 4-weeks-in-month.t \| Cannot resolve caller weeks-in-month(); no matching multi candidate |
| 2025-11-14 | Inline::Perl5 | 0.61 | self-fail | FAILED: argv.t \| No compiler available for language 'Perl5' |
| 2025-11-13 | DOSH::CLI | 7.0.0 | dep-fail | dosh |
| 2025-11-13 | Red | 0.2.4 | dep-fail | Getopt::Long |
| 2025-11-06 | POFile | 0.9 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'msgid, msgstr with source reference comment' at 01-basic.rakutest  … |
| 2025-11-06 | Pod::To::HTML | 0.9.0 | self-fail | FAILED: 010-basic.rakutest \| ===SORRY!=== Parse error at line 614: Error while compiling module Template::Mus … |
| 2025-11-06 | Pod::To::Markdown | 0.2.2 | self-fail | FAILED: code-lang.rakutest \| ===SORRY!=== Parse error at line 614: Error while compiling module Template::Mus … |
| 2025-11-02 | Data::Reshapers | 0.3.8 | dep-fail | Pretty::Table |
| 2025-10-21 | CoreHackers::NfaChainsaw | 0.0.1.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 45: Error while compiling module CoreHackers::Nf … |
| 2025-10-21 | Logic::Ternary | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| Undeclared name 'Unknown' |
| 2025-10-17 | Terminal::LineEditor | 0.0.23 | self-fail | FAILED: 00-use.rakutest \| Variable $.dwSize used where no 'self' is available |
| 2025-10-15 | DSL::English::RecommenderWorkflows | 0.6.7 | self-fail | FAILED: 01-Profile-finding-commands.rakutest \| Cannot parse the command; target 'compute the profile for the  … |
| 2025-10-15 | DSL::Shared | 0.2.11 | self-fail | FAILED: Array-of-regexes-matches.rakutest \| # Failed test at Array-of-regexes-matches.rakutest line 36 |
| 2025-10-14 | Sub::Name | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| No such method 'set_name' for invocant of type 'Block' |
| 2025-10-14 | Sub::Util | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'is &subname NOT imported?' at 01-basic.rakutest line 11 |
| 2025-10-10 | Definitely | 2.1.4 | self-fail | FAILED: 01-Definitely.rakutest \| The attribute '$!s' is required, but you did not provide a value for it. |
| 2025-10-09 | Bencode | 0.2 | self-fail | FAILED: 00-Bencode.t \| # Failed test at 00-Bencode.t line 1 |
| 2025-10-06 | Gnome::Gio | 0.1.26 | self-fail | FAILED: Action.rakutest \| Could not find Gnome::N::N-GObject in: |
| 2025-09-27 | Cro::HTTP::RouterUtils | 0.0.2 | dep-fail | Cro::HTTP::Test |
| 2025-09-27 | Text::Plot | 0.1.4 | self-fail | FAILED: 01-basic-usage.rakutest \| No such method 'append' for invocant of type 'Str' |
| 2025-09-23 | L10N::ZH | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-09-20 | Pod::To::PDF::Lite | 0.1.16 | dep-fail | Getopt::Long |
| 2025-09-17 | Text::Template | 1.0.9 | self-fail | FAILED: 00_use.t \| Could not find Pod::To::Text in: |
| 2025-09-11 | PublicSuffix | 0.1.20250910 | self-fail | FAILED: external.t \| # Failed test 'uk.com -> null' at external.t line 203 |
| 2025-09-06 | has-word | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::hllbool' |
| 2025-09-05 | Chemistry::Stoichiometry | 0.1.10 | self-fail | FAILED: 01-Element-data-functions.rakutest \| ===SORRY!=== Parse error at line 223: Error while compiling modu … |
| 2025-09-03 | fez | 100.0.2 | self-fail | FAILED: 00-use.t \| # Failed test 'The module can be use-d ok: Fez::Bundle' at 00-use.t line 21 |
| 2025-09-01 | Version::RubyGems | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does cmp with itself give Same' at 01-basic.rakutest line 100 |
| 2025-08-31 | Version::Conan | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does cmp with itself give Same' at 01-basic.rakutest line 100 |
| 2025-08-31 | Version::Semverish | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does cmp with itself give Same' at 01-basic.rakutest line 100 |
| 2025-08-30 | Version::Nginx | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| No such method 'ACCEPTS' for invocant of type 'Any' |
| 2025-08-29 | Version::Raku | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| No such method 'ACCEPTS' for invocant of type 'Any' |
| 2025-08-28 | Cro::HTTP::Session::MySQL | 0.2 | dep-fail | DB |
| 2025-08-28 | DB::MySQL | 0.8 | dep-fail | DB |
| 2025-08-28 | Map::Leaflet | 0.0.12 | self-fail | FAILED: 01-tests.rakutest \| # Failed test 'Marker content included' at 01-tests.rakutest line 25 |
| 2025-08-26 | Version::Semver | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 62: Error while compiling module Version::Semver … |
| 2025-08-24 | Version::Repology | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| Cannot convert string to number: base-10 number must begin with valid digits or ' … |
| 2025-08-23 | WWW::CloudHosting::Hetzner | 0.0.9 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: WWW::CloudHosting::Hetzner' at 00-use.ra … |
| 2025-08-22 | VERS | 0.0.3 | dep-fail | Version::Repology |
| 2025-08-16 | Tee | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| '"\$one"' cannot be used as a source |
| 2025-08-13 | Audio::TagLib | 0.0.7 | build-fail |  |
| 2025-08-10 | Adverb::Eject | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| Unexpected adverbs passed to subscript: eject |
| 2025-08-10 | ForwardIterables | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| No such method 'next-iterator' for invocant of type 'Array' |
| 2025-08-10 | Native::Overflow | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| ===WARNING=== Module Native::Overflow EXPORT failed: No such symbol 'bit' |
| 2025-08-10 | ReverseIterables | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| No such method 'next-iterator' for invocant of type 'Array' |
| 2025-08-08 | Rakudo::CORE::META | 0.0.12 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::gethllsym' |
| 2025-08-03 | Algorithm::Diff | 0.0.5 | self-fail | FAILED: base.rakutest \| Undefined routine 'Algorithm::Diff::_longestCommonSubsequence' |
| 2025-08-03 | Slang::Piersing | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 9: expected ) (got '?') |
| 2025-08-03 | Slang::Tuxic | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| Useless use of constant string "-%d-" in sink context (line 13) |
| 2025-07-30 | CSS | 0.1.2 | dep-fail | Getopt::Long |
| 2025-07-29 | eigenstates | 0.0.12 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'did we get the eigenstates' at 01-basic.rakutest line 12 |
| 2025-07-28 | L10N::EO | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 12: Ending delimiter CODE not found for heredoc |
| 2025-07-28 | Resource::Wrangler | 2.0.3 | self-fail | FAILED: 00-load-resource-to-path.rakutest \| |
| 2025-07-27 | Gnome::Gsk4 | 0.2.0 | self-fail | FAILED: BlendNode.rakutest \| Class 'StreamClosure' cannot inherit from 'rw' because it is unknown |
| 2025-07-25 | Compress::PDF | 0.5.0 | dep-fail | File::Copy |
| 2025-07-25 | L10N::Complete | 0.0.3 | dep-fail | L10N::ZH |
| 2025-07-24 | App::FIT2GPX | 0.0.1 | pass |  |
| 2025-07-24 | ValueList | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| No such method '!SET-SELF' for invocant of type 'Array' |
| 2025-07-21 | PDF::NameTags | 0.0.2 | dep-fail | Getopt::Long |
| 2025-07-18 | Number::Denominate | 2.3 | self-fail | FAILED: 01-string.t \| Attempt to divide 0 by zero using infix:<div> |
| 2025-07-17 | SSH::LibSSH::Tunnel | 0.0.11 | dep-fail | Concurrent::Progress |
| 2025-07-14 | Test::Coverage | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 153: Error while compiling module Code::Coverabl … |
| 2025-07-13 | File-Path-Copy | 0.1.18 | self-fail | FAILED: 001-Meta-test.rakutest \| ===SORRY!=== Parse error at line 7: expected ) (got '«') |
| 2025-07-11 | Cromponent | 0.0.14 | dep-fail | Cro::HTTP::Test |
| 2025-07-11 | File::Copy | 0.1.0 | self-fail | FAILED: 001-test.rakutest \| ===SORRY!=== Parse error at line 183: Error while compiling module File::Path::Co … |
| 2025-07-11 | File::Path::Copy | 0.1.14 | dep-fail | File-Path-Copy |
| 2025-07-09 | Image::PNG::Portable | 0.3.2 | self-fail | FAILED: 02-read-and-write.rakutest \| Undefined routine 'String::CRC32::crc32' |
| 2025-06-30 | L10N::CY | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-30 | MoarVM::Remote | 0.0.4 | dep-fail | mv2d |
| 2025-06-30 | Pakku | celastrina.6 | self-fail | FAILED: cmd.rakutest \| # Failed test 'add nodeps' at cmd.rakutest line 238 |
| 2025-06-28 | L10N::DE | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-28 | L10N::NL | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-27 | L10N::EN | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-27 | L10N::FR | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-27 | L10N::HU | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-27 | L10N::IT | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-27 | L10N::JA | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-27 | L10N::PT | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2025-06-26 | Noise::Simplex | 0.1.2 | self-fail | FAILED: 02_noise2d.t \| # Failed test 'Different seed usually gives different output' at 02_noise2d.t line 143 |
| 2025-06-20 | Lingua::NumericWordForms | 0.6.2 | self-fail | FAILED: Automatic-language-determination.rakutest \| Undefined routine 'but' |
| 2025-06-20 | Text::CodeProcessing | 0.6.0 | self-fail | FAILED: 02-string-code-chunks-processing-markdown.rakutest \| Undefined routine 'nqp::getcomp' |
| 2025-06-19 | YakShave | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 441: Error while compiling module YakShave::Gram … |
| 2025-06-18 | Inline::Python3 | 0.0.1 | build-fail |  |
| 2025-06-18 | Zeco | 0.0.3 | dep-fail | LibUUID |
| 2025-06-17 | DAWG | 0.1.6 | self-fail | FAILED: 02-serialization.rakutest \| # Failed test 'Can load DAWG' at 02-serialization.rakutest line 22 |
| 2025-06-17 | Inline::BASIC | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| ?SYNTAX ERROR IN 10: Unknown statement '' |
| 2025-06-16 | Deps | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 100: Error while compiling module Deps (line 100 … |
| 2025-06-15 | Chinese | 0.0.3 | self-fail | FAILED: 01-operators.t \| ===SORRY!=== Parse error at line 10: expected ) (got '加') |
| 2025-06-15 | ClassicalChinese | 0.0.2 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 76: expected ) (got '加') |
| 2025-06-15 | English | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| Could not find , in: |
| 2025-06-15 | German | 0.0.3 | self-fail | FAILED: 01-operators.t \| ===SORRY!=== Parse error at line 10: expected ) (got 'plus') |
| 2025-06-15 | Japanese | 0.0.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 77: expected ) (got '足す') |
| 2025-06-15 | Korean | 0.0.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 77: expected ) (got '더하기') |
| 2025-06-15 | Russian | 0.0.1 | self-fail | FAILED: 01-basic-simple.t \| Undefined routine 'нуль' |
| 2025-06-15 | Spanish | 0.0.2 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 83: expected ) (got 'más') |
| 2025-06-14 | French | 0.0.2 | self-fail | FAILED: 01-operators.t \| ===SORRY!=== Parse error at line 10: expected ) (got 'plus') |
| 2025-06-14 | Lang::JA::Kana | 1.2.1 | self-fail | FAILED: 01-basic.t \| # Failed test 'Half-width voiced combinations' at 01-basic.t line 264 |
| 2025-06-14 | Portuguese | 0.0.1 | self-fail | FAILED: 01-operators.t \| ===SORRY!=== Parse error at line 10: expected ) (got 'mais') |
| 2025-06-14 | RKDS | 2.0.1 | dep-fail | Getopt::Long |
| 2025-06-10 | Gnome::Gtk3 | 0.49.1 | dep-fail | Gnome::Gdk3 |
| 2025-06-09 | Lang::Transliterate | 0.1.0 | self-fail | FAILED: 01-basic.t \| # Failed test 'Arabic BGNPCGN' at 01-basic.t line 94 |
| 2025-06-07 | RakuDoc::Test::Files | 0.3 | self-fail | FAILED: 01-methods.rakutest \| No such method 'INCLUDE' for invocant of type 'Rakudo::Internals' |
| 2025-06-06 | Protocol::MQTT | 0.0.4 | self-fail | FAILED: 01-filter.rakutest \| Method '!encode-body' must be resolved by class Protocol::MQTT::Packet::PubAck b … |
| 2025-06-06 | Protocol::Postgres | 0.0.13 | dep-fail | Auth::SCRAM::Async |
| 2025-06-05 | Rake | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| Variable '@types' is not declared |
| 2025-06-04 | InterceptAllMethods | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| No such method 'foo' for invocant of type 'Foo' |
| 2025-06-04 | ReadWriteLock | 0.3.3 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'Basic Lock operations' at 01-basic.rakutest line 92 |
| 2025-06-03 | IO::Socket::Async::SSL | 0.8.2 | self-fail | FAILED: client-server.rakutest \| Cannot listen on localhost:54341 |
| 2025-06-02 | HTML::Strip | 0.1.3 | self-fail | FAILED: test.rakutest \| # Failed test 'simple strip' at test.rakutest line 143 |
| 2025-06-02 | Moonphase | 0.0.2 | self-fail | FAILED: 01-moonillumination.rakutest \| # Failed test 'test1' at 01-moonillumination.rakutest line 9 |
| 2025-06-01 | Prime::Factor | 0.4.3 | self-fail | FAILED: 00-basic.rakutest \| # Failed test 'non-Integer string fails' at 00-basic.rakutest line 34 |
| 2025-05-31 | Pod::TreeWalker | 0.0.6 | self-fail | FAILED: basic.rakutest \| # Failed test 'single =head1' at basic.rakutest line 70 |
| 2025-05-29 | BSON::Simple | 0.0.2 | dep-fail | Getopt::Long |
| 2025-05-29 | Cro::CBOR | 0.0.7 | self-fail | FAILED: 01-http-router-websocket.rakutest \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' fo … |
| 2025-05-27 | GIO | 0.0.4 | dep-fail | DOM::Tiny |
| 2025-05-27 | GLib | 0.0.11 | dep-fail | DOM::Tiny |
| 2025-05-27 | JSON::GLib | 0.0.1 | dep-fail | DOM::Tiny |
| 2025-05-26 | Terminal::QuickCharts | 0.0.2 | self-fail | FAILED: 01-helpers.t \| tput: No value for $TERM and no -T specified |
| 2025-05-25 | CBOR::Simple | 0.1.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'cbor-encode produces correct blob' at 01-basic.rakutest line 23 |
| 2025-05-24 | DateTime::Julian | 1.1.2 | self-fail | FAILED: 1-class.rakutest \| Undeclared name 'MJD0' |
| 2025-05-23 | Math::Symbolic | 0.1 | self-fail | FAILED: 01-basics.rakutest \| ===SORRY!=== Parse error at line 58: Error while compiling module Math::Symbolic … |
| 2025-05-23 | RakuDoc::Load | 0.0.2 | self-fail | FAILED: 1-basic.rakutest \| # Testing strings |
| 2025-05-22 | Text::FriBidi | 0.0.7 | self-fail | FAILED: bidi-flags.t \| ===WARNING=== Module MacOS::NativeLib EXPORT failed: Failed to create symlink called ' … |
| 2025-05-18 | App::Prove6 | 0.0.18 | dep-fail | Getopt::Long |
| 2025-05-18 | Debugging::Tool | 0.3.1 | self-fail | FAILED: all.rakutest \| ===SORRY!=== Parse error at line 85: expected variable in declaration (got 'Any') |
| 2025-05-18 | TAP | 0.3.15 | self-fail | FAILED: source-file.rakutest \| ===SORRY!=== Parse error at line 457: Error while compiling module TAP (line 4 … |
| 2025-05-17 | Bin::Utils | 0.0.2 | self-fail | FAILED: 1-file-round-trip.t \| # Failed test 'bin file 'resources/ls' round trips okay' at 1-file-round-trip.t … |
| 2025-05-06 | BSON | 0.13.3 | self-fail | FAILED: Document-all-types.rakutest \| # Failed test 'Document encoding decoding types' at Document-all-types. … |
| 2025-05-05 | LLM::Containerization | 0.1.5 | dep-fail | Cro::HTTP::Test |
| 2025-05-05 | RakuConfig | 0.8.2 | self-fail | FAILED: 01-sanity.rakutest \| # Failed test 'compiles and loads' at 01-sanity.rakutest line 4 |
| 2025-05-04 | Image::Libexif | 0.1.3 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Image::Libexif::Raw' at 00-use.rakutest  … |
| 2025-05-03 | HTTP::UserAgent | 1.2.0 | self-fail | FAILED: 030-cookies.rakutest \| # Failed test 'set-cookie 1/11' at 030-cookies.rakutest line 24 |
| 2025-04-27 | Math::Matrix | 0.4.2 | self-fail | FAILED: 010-load.rakutest \| # Failed test 'The module can be use-d ok: Math::Matrix' at 010-load.rakutest lin … |
| 2025-04-26 | PDF::To::Cairo | 0.0.8 | dep-fail | Getopt::Long |
| 2025-04-25 | TOP | 0.0.7 | dep-fail | Slang::Otherwise |
| 2025-04-21 | Class::Loader::Dynamic | 0.0.6 | self-fail | FAILED: 01-basic-tests.rakutest \| # Failed test 'Passing class loaded OK' at 01-basic-tests.rakutest line 20 |
| 2025-04-20 | FontFactory::Type1 | 1.1.1 | dep-fail | Getopt::Long |
| 2025-04-20 | PDF::Document | 0.0.10 | dep-fail | Getopt::Long |
| 2025-04-19 | Glob::Grammar | 0.0.2 | self-fail | FAILED: 01-basic-tests.rakutest \| # Failed test 'Glob to Regex had expected result' at 01-basic-tests.rakutes … |
| 2025-04-18 | HTML::Canvas::To::PDF | 0.0.13 | dep-fail | Getopt::Long |
| 2025-04-18 | HarfBuzz | 0.1.7 | self-fail | FAILED: 10_basic.t \| ===WARNING=== Module MacOS::NativeLib EXPORT failed: Failed to create symlink called '/U … |
| 2025-04-09 | Dev::ContainerizedService | 0.3.4 | self-fail | FAILED: clickhouse.t \| # Failed test 'Successfully ran development script' at clickhouse.t line 46 |
| 2025-04-03 | DateTime::US | 0.1.6 | dep-fail | LocalTime |
| 2025-03-30 | Iter::Able | 0.2.0 | self-fail | FAILED: 00-usability.rakutest \| ===SORRY!=== Parse error at line 6: expected } (got '') |
| 2025-03-30 | Pyrint | 0.1.0 | self-fail | FAILED: 01.rakutest \| # Failed test 'no arg call ok' at 01.rakutest line 6 |
| 2025-03-27 | Method::Protected | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'can we increment' at 01-basic.rakutest line 10 |
| 2025-03-27 | Test::Mock | 1.8 | self-fail | FAILED: computing.rakutest \| No such method 'add_parent' for invocant of type '<anon\|1>' |
| 2025-03-25 | PDF::Lite | 0.0.15 | dep-fail | Getopt::Long |
| 2025-03-24 | APISports::Football | 0.2.3 | dep-fail | Cro::HTTP::Test |
| 2025-03-17 | CLI::AWS::EC2-Simple | 0.0.11 | build-fail |  |
| 2025-03-14 | EyeoftheBeholder3 | 0.1.5 | self-fail | FAILED: routes.t \| Type check failed in binding to parameter '$incoming'; expected Supply but got List (( )) |
| 2025-03-10 | Number::More | 0.2.12 | self-fail | FAILED: 3-general-base-transforms.t \| Cannot assign to a readonly variable or a value |
| 2025-03-09 | RakuAST::Utils | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 98: Error while compiling module RakuAST::Utils  … |
| 2025-03-09 | Redis | 0.1.5 | self-fail | FAILED: 01-connect.rakutest \| # Failed test at 01-connect.rakutest line 7 |
| 2025-03-07 | Date::Calendar::CopticEthiopic | 0.1.1 | self-fail | FAILED: 03-accessors.rakutest \| # Failed test at 03-accessors.rakutest line 22 |
| 2025-03-03 | SSH::LibSSH | 0.10.2 | dep-fail | Concurrent::Progress |
| 2025-02-24 | Ddt | 0.8.2 | dep-fail | License::Software |
| 2025-02-21 | XML | 0.3.6 | pass |  |
| 2025-02-20 | Data::MessagePack | 0.1.4 | self-fail | FAILED: 103-pack-float.rakutest \| No such method 'base' for invocant of type 'Rat' |
| 2025-02-17 | PDF::Font::Loader::HarfBuzz | 0.0.2 | dep-fail | Getopt::Long |
| 2025-02-15 | Chemistry::Elements | 0.2.2 | self-fail | FAILED: 005.ZInt-subtype.rakutest \| # Failed test 'Can't assign negative number to ZInt' at 005.ZInt-subtype. … |
| 2025-02-14 | App::Stouch | 0.3.2 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: App::Stouch' at 00-use.rakutest line 5 |
| 2025-02-14 | Data::TextOrBinary | 1.5 | self-fail | FAILED: blob.rakutest \| # Failed test 'Basic text with some UNIX newlines is considered text' at blob.rakutes … |
| 2025-02-14 | FunctionalParsers | 0.1.10 | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 70: Error while compiling module FunctionalPars … |
| 2025-02-14 | Tomty | 0.0.22 | dep-fail | Colorizable |
| 2025-02-13 | Rakudo::Cache | 0.0.4 | dep-fail | Git::Files |
| 2025-02-12 | Hash2Class | 0.1.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'did we get an A' at 01-basic.rakutest line 42 |
| 2025-02-12 | OO::Actors | 0.4 | self-fail | FAILED: basic.rakutest \| Undefined routine 'actor' |
| 2025-02-12 | hyperize | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| No such method 'hyper' for invocant of type 'Iterable' |
| 2025-02-11 | Gnome::Graphene | 0.1.11 | self-fail | FAILED: N-Box.rakutest \| Undefined routine 'void' |
| 2025-02-10 | Git::Files | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::const::BINARY_ENDIAN_LITTLE' |
| 2025-02-09 | Gnome::GObject | 0.1.12 | self-fail | FAILED: N-Value.rakutest \| Undefined routine 'void' |
| 2025-02-09 | Gnome::GdkPixbuf | 0.1.3 | self-fail | FAILED: Pixbuf.rakutest \| Undefined routine 'void' |
| 2025-02-05 | HTML::Functional | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| Useless use of constant string " |
| 2025-02-04 | CLI::Ecosystem | 0.0.7 | dep-fail | shorten-sub-commands |
| 2025-02-04 | Fortune | 0.1.0 | self-fail | FAILED: 01-fortune.rakutest \| Undeclared name 'FORTUNE_UNORDER' |
| 2025-02-04 | Qt::QtWidgets | 0.0.7 | build-fail |  |
| 2025-02-04 | Slang::Date | 0.1.4 | self-fail | FAILED: basic.t \| Potential difficulties: |
| 2025-02-01 | Code::Coverable | 0.0.13 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 153: Error while compiling module Code::Coverabl … |
| 2025-01-31 | File::Tudo | 0.03 | self-fail | FAILED: 10-tudo.rakutest \| # Failed test 'File::Tudo.write() writes TODO files correctly' at 10-tudo.rakutest … |
| 2025-01-30 | Slang::Lambda | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 6: expected ) (got '{') |
| 2025-01-29 | Pod::To::Man | 1.2.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'first pod renders ok' at 01-basic.rakutest line 126 |
| 2025-01-26 | Archive::Ar | 0.1.0 | self-fail | FAILED: 01-ar.rakutest \| Undeclared name 'AR_COMMON' |
| 2025-01-24 | Map::DeckGL | 0.0.4 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Map::DeckGL' at 00-use.rakutest line 6 |
| 2025-01-22 | Code::Coverage | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 153: Error while compiling module Code::Coverabl … |
| 2025-01-21 | App::Ecosystems | 0.0.9 | dep-fail | Commands |
| 2025-01-21 | GEOS | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| Cannot load native library 'geos_c': dlopen(geos_c, 0x0009): tried: 'geos_c' (no  … |
| 2025-01-20 | App::Ebread | 0.2.3 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: App::Ebread' at 00-use.rakutest line 5 |
| 2025-01-16 | IRC::Client::Plugin::Logger | 0.0.13 | self-fail | FAILED: 01-basic.rakutest \| No such method 'nick' for invocant of type 'IRC::Client::Message::Join' |
| 2025-01-15 | Cro::Core | 0.8.10 | timeout | Cro::Core |
| 2025-01-15 | Cro::WebSocket | 0.8.10 | timeout | Cro::WebSocket |
| 2025-01-15 | EcosystemMakerFaker | 0.1.1 | dep-fail | shorten-sub-commands |
| 2025-01-15 | Map::Ordered | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does .gist work ok' at 01-basic.rakutest line 16 |
| 2025-01-15 | OneSeq | 0.0.4 | dep-fail | ReverseIterables |
| 2025-01-15 | Slang::Emoji | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 6: expected variable after declarator (got '👍') |
| 2025-01-15 | Slang::Subscripts | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| Useless use of constant integer ₁ in sink context (line 11) |
| 2025-01-15 | Sub::Memoized | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'is memoized: positional, did we not execute the body this time' at … |
| 2025-01-15 | Unix::errno | 0.0.7 | timeout | Unix::errno |
| 2025-01-15 | cro | 0.8.10 | self-fail | FAILED: tools-crofile.rakutest \| # Failed test 'Correct name (defaulted to id)' at tools-crofile.rakutest lin … |
| 2025-01-15 | rakudoc2man | 0.0.2 | self-fail | FAILED: 00-use.rakutest \| Failed to open file /privatestd in: No such file or directory |
| 2025-01-14 | Slangify | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| Useless use of constant string ?"Successfully slanged" in sink context (line 11) |
| 2025-01-14 | Tuple | 0.0.12 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::eqaddr' |
| 2025-01-14 | ValueTypeCache | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| No such method 'new' for invocant of type 'ValueObjAt' |
| 2025-01-14 | immutable | 0.0.3 | dep-fail | ValuePair |
| 2025-01-13 | File::TreeBuilder | 0.2.0 | self-fail | FAILED: main.rakutest \| ===SORRY!=== Parse error at line 54: Cannot use underscore between digits unless it i … |
| 2025-01-11 | GDBM | 0.1.4 | build-fail |  |
| 2025-01-11 | roundrobin-slip | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| Cannot resolve caller roundrobin(); no matching multi candidate |
| 2025-01-11 | sourcery | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| No such method 'file' for invocant of type 'Method' |
| 2025-01-10 | App::termie | 0.2.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: Termie::Commands' at 01-basic.rakutest … |
| 2025-01-10 | Files::Containing | 0.0.17 | dep-fail | has-word |
| 2025-01-10 | Hash-with | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does lower case give right answer' at 01-basic.rakutest line 7 |
| 2025-01-10 | Hash::Restricted | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does STORE return self' at 01-basic.rakutest line 10 |
| 2025-01-10 | Hash::int | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'did it create a list' at 01-basic.rakutest line 23 |
| 2025-01-10 | Hash::str | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'did it create a list' at 01-basic.rakutest line 23 |
| 2025-01-10 | silently | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| No such method 'tell' for invocant of type 'FileHandle' |
| 2025-01-09 | Array::Circular | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test '4 time append' at 01-basic.rakutest line 15 |
| 2025-01-09 | Array::Rounded | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'is 1.50 ok on Array' at 01-basic.rakutest line 14 |
| 2025-01-09 | Array::Sorted::Map | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::div_i' |
| 2025-01-09 | Array::Sorted::Util | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'finds on simple Instant array' at 01-basic.rakutest line 42 |
| 2025-01-09 | Array::Unsorted::Map | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'did we get the right object type' at 01-basic.rakutest line 18 |
| 2025-01-09 | Attribute::Predicate | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| No such method 'set_name' for invocant of type 'Method' |
| 2025-01-09 | Bits | 0.0.7 | self-fail | FAILED: 01-basic.t \| Undefined routine 'nqp::isne_I' |
| 2025-01-09 | Concurrent::PriorityQueue | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'first List ok' at 01-basic.rakutest line 16 |
| 2025-01-09 | Fasta | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| Target is not assignable |
| 2025-01-09 | Hash::Sorted | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'are keys sorted' at 01-basic.rakutest line 9 |
| 2025-01-09 | Lines::Containing | 0.0.11 | dep-fail | has-word |
| 2025-01-09 | String::Color | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::div_i' |
| 2025-01-09 | WriteOnceHash | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The object is-a 'WriteOnceHash'' at 01-basic.rakutest line 9 |
| 2025-01-08 | span | 0.0.2 | dep-fail | snip |
| 2025-01-05 | MeCab | 0.0.19 | build-fail |  |
| 2025-01-01 | Terminal-MakeRaw | 1.0.1 | self-fail | FAILED: basic.rakutest \| No such method 'native-descriptor' for invocant of type 'FileHandle' |
| 2024-12-30 | SHAI | 5 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: SHAI::CLI' at 00-use.rakutest line 5 |
| 2024-12-29 | Acme::Anguish | 1.2 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Acme::Anguish' at 00-use.rakutest line 5 |
| 2024-12-29 | GeoIP2 | 1.1.4 | self-fail | FAILED: 00-reader.rakutest \| # Failed test 'open "MaxMind DB Decoder Test" database' at 00-reader.rakutest li … |
| 2024-12-27 | ASCII::To::Uni | 0.1.1 | self-fail | FAILED: 01-sanity.rakutest \| Attempt to return outside of any Routine |
| 2024-12-25 | BigRoot | 0.1.1 | self-fail | FAILED: 01-first-sqrt2-sqrt3-high-precision.rakutest \| ===SORRY!=== Parse error at line 558: Ending delimiter … |
| 2024-12-25 | Email::SendGrid | 0.2 | self-fail | FAILED: validation.rakutest \| # Failed test 'Simple heuristic catches swapping name and email' at validation. … |
| 2024-12-22 | Business::CreditCard | 0.2 | self-fail | FAILED: 01-original-tests.rakutest \| Undefined routine 'where' |
| 2024-12-21 | Sway::PreviewKeys | 0.2.4 | dep-fail | Gnome::Gdk3 |
| 2024-12-20 | Hash::LRU | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| Variable '$max' is not declared |
| 2024-12-19 | Date::Calendar::FrenchRevolutionary | 0.1.0 | self-fail | FAILED: 02-accessors.rakutest \| Index out of range. Is: 1, should be in 0..0 |
| 2024-12-19 | Date::Calendar::Hebrew | 0.1.0 | self-fail | FAILED: 02-accessors.rakutest \| ===WARNING=== Module List::MoreUtils EXPORT failed: List::MoreUtils doesn't k … |
| 2024-12-19 | Date::Calendar::Julian | 0.1.0 | pass |  |
| 2024-12-16 | CLDR::List | 0.2 | self-fail | FAILED: 01-load.rakutest \| Class 'CLDR::List' cannot inherit from 'rw' because it is unknown |
| 2024-12-16 | Geo::Region | 0.3 | self-fail | FAILED: enum.rakutest \| # Failed test 'world region' at enum.rakutest line 7 |
| 2024-12-16 | Math::Quaternion | 0.2.1 | self-fail | FAILED: empty_subclass.rakutest \| # Failed test '.unit, math, and .Str work' at empty_subclass.rakutest line  … |
| 2024-12-16 | String::FuzzyIndex | 0.3 | self-fail | FAILED: 00-sanity.rakutest \| Undefined routine 'indices' |
| 2024-12-15 | Acme::Text::UpsideDown | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'Q' |
| 2024-12-15 | Data::StaticTable | 0.1.1 | self-fail | FAILED: 001-basic.rakutest \| ===SORRY!=== Parse error at line 125: Error while compiling module Data::StaticT … |
| 2024-12-15 | WebService::Slack::Webhook | 0.1.3 | self-fail | FAILED: 01-use.rakutest \| # Failed test 'The module can be use-d ok: WebService::Slack::Webhook' at 01-use.ra … |
| 2024-12-15 | vCard::Parser | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: vCard::Parser' at 01-basic.rakutest li … |
| 2024-12-14 | Dice::Roller | 0.1.2 | self-fail | FAILED: 00-basics.t \| # Failed test 'Initial state is 1d20' at 00-basics.t line 234 |
| 2024-12-14 | Text::Caesar | 0.2 | pass |  |
| 2024-12-13 | Automata::Cellular | 0.2.3 | self-fail | FAILED: 01-print.rakutest \| Potential difficulties: |
| 2024-12-13 | Concurrent::BoundedChannel | 0.4.1 | self-fail | FAILED: 01-sanity.rakutest \| No such method 'signal' for invocant of type 'Lock' |
| 2024-12-13 | MQTT::Client | 0.2 | self-fail | FAILED: regex.rakutest \| ===SORRY!=== Parse error at line 93: Error while compiling module MQTT::Client (line … |
| 2024-12-13 | Math::Fitting | 0.0.5 | dep-fail | Math::Matrix |
| 2024-12-13 | Path::Map | 0.4 | self-fail | FAILED: path_map.rakutest \| Target is not assignable |
| 2024-12-13 | Test::Declare | 0.0.3 | self-fail | FAILED: 02-return-value.rakutest \| No such method 'new' for invocant of type 'Any' |
| 2024-12-12 | C::Parser | 0.3.3 | self-fail | FAILED: 01_null_main.rakutest \| # Failed test 'gives a Grammar' at 01_null_main.rakutest line 15 |
| 2024-12-12 | Grammar::Modelica | 0.1.3 | self-fail | FAILED: ClassDefinition.rakutest \| # Failed test at ClassDefinition.rakutest line 15 |
| 2024-12-12 | PostCocoon::Url | 1.1 | self-fail | FAILED: 00-url.rakutest \| # Failed test 'Encode null-char' at 00-url.rakutest line 50 |
| 2024-12-12 | SQL::NamedPlaceholder | 0.1.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test at 01-basic.rakutest line 13 |
| 2024-12-11 | ClassX::StrictConstructor | 0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'Can create an instance of a Class that does StrictClass' at 01-bas … |
| 2024-12-11 | Digest::PSHA1 | 1.0.2 | self-fail | FAILED: 01-basic.rakutest \| Cannot invoke non-Callable value of type Any |
| 2024-12-11 | Docker::File | 1.1 | self-fail | FAILED: generate.rakutest \| Variable '$ins' is not declared |
| 2024-12-11 | File::Ignore | 1.2 | self-fail | FAILED: charclass.rakutest \| # Failed test '"result-[AB].txt" ignores file result-A.txt' at charclass.rakutes … |
| 2024-12-11 | Getopt::Type | 0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test at 01-basic.rakutest line 17 |
| 2024-12-11 | Grammar::BNF | 1.1 | self-fail | FAILED: abnf.rakutest \| No such method 'chars' for invocant of type 'Any' |
| 2024-12-11 | Math::Arrow | 0.1.1 | self-fail | FAILED: arrow.rakutest \| ===SORRY!=== Parse error at line 9: unexpected operator in term position (got '↑') |
| 2024-12-11 | Net::SMTP | 1.2.3 | self-fail | FAILED: 02-simple.rakutest \| Bad client-send: MAIL FROM: |
| 2024-12-11 | Test::Scheduler | 1.2 | self-fail | FAILED: synopsis.rakutest \| # Failed test 'one value after 1s' at synopsis.rakutest line 42 |
| 2024-12-10 | Format::Lisp | 0.0.3 | self-fail | FAILED: 00-classes.rakutest \| ===SORRY!=== Parse error at line 403: Error while compiling module Format::Lisp … |
| 2024-12-08 | App::MoarVM::Debug | 0.2 | dep-fail | mv2d |
| 2024-12-07 | mv2d | 0.1.333 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 145: Error while compiling module App::MoarVM::D … |
| 2024-12-03 | PrettyDump | 1.2.3 | self-fail | FAILED: 00.load.rakutest \| ===SORRY!=== Parse error at line 160: Error while compiling module PrettyDump (lin … |
| 2024-12-02 | Terminal::Width | 1.1 | self-fail | FAILED: 00-use.t \| Could not determine terminal width |
| 2024-12-02 | Test::Stream | 0.1 | self-fail | FAILED: 000-suite.rakutest \| Undefined routine 'nqp::time_n' |
| 2024-11-30 | Concurrent::Iterator | 1.1 | self-fail | FAILED: convenience.rakutest \| # Failed test 'That Seq contains a Concurrent::Iterator' at convenience.rakute … |
| 2024-11-30 | Concurrent::Progress | 1.2 | self-fail | FAILED: basic.rakutest \| No such method 'share' for invocant of type 'Supply' |
| 2024-11-30 | Concurrent::Trie | 1.2 | self-fail | FAILED: basic.rakutest \| # Failed test 'Emptry Trie is falsey' at basic.rakutest line 11 |
| 2024-11-30 | Date::WorkdayCalendar | 0.1.3 | self-fail | FAILED: 01-WorkdayCalendar.rakutest \| ===SORRY!=== Parse error at line 14: Confused (got ',') |
| 2024-11-30 | Log::Timeline | 0.5.2 | self-fail | FAILED: output-cbor-sequence.rakutest \| Failed to open file p6-log-timeline-cbor-test: No such file or direct … |
| 2024-11-30 | Net::IMAP | 1.0.3 | dep-fail | Email::MIME |
| 2024-11-30 | SOAP::Client | 1.1 | self-fail | FAILED: 05-simple-service.rakutest \| No such method 'nsPrefix' for invocant of type 'Any' |
| 2024-11-30 | XML::Canonical | 1.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'convert newlines, preserve whitespace in text nodes' at 01-basic.r … |
| 2024-11-28 | snip | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| No such method '!SET-SELF' for invocant of type 'Array' |
| 2024-11-23 | SQL::Builder | 0.2.0 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 54: Error while compiling module SQL::Builder (l … |
| 2024-11-20 | WAT | 1 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: WAT::CLI' at 00-use.rakutest line 5 |
| 2024-11-20 | content-storage-cli | 0.0.1 | timeout | EventSource::Server |
| 2024-11-19 | WAT--CLI | 1 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: WAT::CLI' at 00-use.rakutest line 5 |
| 2024-11-18 | Graphviz::DOT::Grammar | 0.1.1 | self-fail | FAILED: 01-basic-parsing.rakutest \| ===SORRY!=== Parse error at line 18: Error while compiling module Graphvi … |
| 2024-11-18 | Sys::OsRelease | 0.0.2 | self-fail | FAILED: 02-basic.rakutest \| ./t/data/debian12.os-release line 1: Invalid os-release line |
| 2024-11-18 | Template::Nest::Fast | 0.3.0 | self-fail | FAILED: 00-basic.rakutest \| ===SORRY!=== Parse error at line 265: Error while compiling module Template::Nest … |
| 2024-11-17 | URL::Find | 0.2.3 | self-fail | FAILED: 01-url-find-unicode.rakutest \| # Failed test 'Test finding Unicode URL's' at 01-url-find-unicode.raku … |
| 2024-11-16 | Acme::Overreact | 0.0.1 | dep-fail | ASTQuery |
| 2024-11-13 | Digest::xxHash | 1.8.0 | self-fail | FAILED: 01-compare-32bit.rakutest \| # Failed test 'test 32-bit routines' at 01-compare-32bit.rakutest line 10 … |
| 2024-11-12 | Crane | 0.1.2 | self-fail | FAILED: add.rakutest \| ===SORRY!=== Parse error at line 67: Error while compiling module Crane::Transform (li … |
| 2024-11-09 | Concurrent::Queue | 1.2 | self-fail | FAILED: basic.rakutest \| # Failed test 'Empty queue is falsey' at basic.rakutest line 6 |
| 2024-11-09 | Concurrent::Stack | 1.3 | self-fail | FAILED: basic.rakutest \| No such method 'Failure' for invocant of type 'X::Concurrent::Stack::Empty' |
| 2024-11-07 | Badger | 1.2.0 | self-fail | FAILED: 01-sigils.rakutest \| ===WARNING=== Module Badger EXPORT failed: The attribute '$!by-name' is required … |
| 2024-11-07 | ECMA262Regex | 1.2 | self-fail | FAILED: 10-parsing.rakutest \| # Failed test at 10-parsing.rakutest line 150 |
| 2024-11-07 | JSON::Mask | 1.0 | self-fail | FAILED: 00-positive.rakutest \| Type check failed in binding to parameter '$mask'; expected Str but got Match  … |
| 2024-11-06 | Acme::Rautavistic::Sort | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'Num - double elements' at 01-basic.rakutest line 12 |
| 2024-11-05 | Gnome::Glib | 0.1.12 | self-fail | FAILED: N-Array.rakutest \| Undefined routine 'void' |
| 2024-11-01 | Inline::Scheme::Gambit | 0.2.2 | build-fail |  |
| 2024-10-28 | PDF::Grammar | 0.3.6 | self-fail | FAILED: 00objects.t \| ===SORRY!=== Parse error at line 39: expected } (got '/#6E') |
| 2024-10-18 | Terminal::ANSIParser | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| Variable '$sequence' is not declared |
| 2024-10-06 | Unicode::Security | 0.0.4 | self-fail | FAILED: 01-basic.t \| # Failed test 'paypal' at 01-basic.t line 36 |
| 2024-10-03 | raku-RandomColor | v0.12 | self-fail | FAILED: 01-basic.t \| # Failed test 'Can use the RandomColor module' at 01-basic.t line 10 |
| 2024-10-02 | Log::Syslog::Native | 0.1.2 | self-fail | FAILED: 020-syslog.t \| Undefined routine 'explicitly-manage' |
| 2024-09-25 | Discogs::API | 0.0.5 | dep-fail | Hash2Class |
| 2024-09-25 | META::verauthapi | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test ':ver on B at compile-time' at 01-basic.rakutest line 27 |
| 2024-09-25 | Updown | 0.0.8 | dep-fail | Hash2Class |
| 2024-09-24 | Text::Flags | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| Unrecognized character name [ |
| 2024-09-24 | ValueMap | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| Too many levels of recursion |
| 2024-09-24 | ValuePair | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| Too many levels of recursion |
| 2024-09-23 | Interval | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'seconds' |
| 2024-09-23 | String::Fields | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'did we iterate ok' at 01-basic.rakutest line 24 |
| 2024-09-23 | actions | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'actions' |
| 2024-09-18 | Crypt::RC4 | 0.0.6 | self-fail | FAILED: 00basic.t \| No such method 'new' for invocant of type 'Any' |
| 2024-09-14 | HTML::Entity::Fast | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'encoding with entities' at 01-basic.rakutest line 576 |
| 2024-09-11 | Math::DistanceFunctions::Edit | 0.1.3 | pass |  |
| 2024-08-28 | DSL::English::QuantileRegressionWorkflows | 0.8.3 | self-fail | FAILED: 01-Data-statistics-command-tests.rakutest \| Cannot parse the command; target 'summarize data' positio … |
| 2024-08-25 | Crypt::LibGcrypt | 1.0.11 | self-fail | FAILED: 01-basic.t \| Cannot load native library 'gcrypt': dlopen(gcrypt, 0x0009): tried: 'gcrypt' (no such fi … |
| 2024-08-25 | Crypt::LibScrypt | 0.0.9 | self-fail | FAILED: 030-hash.t \| # Failed test 'scrypt-hash' at 030-hash.t line 14 |
| 2024-08-24 | Router::Right | 0.0.61 | self-fail | FAILED: 01.t \| ===SORRY!=== Parse error at line 63: Error while compiling module Router::Right (line 63): exp … |
| 2024-08-24 | Trove | 0.0.41 | dep-fail | HTTP::Request::FormData |
| 2024-08-21 | ParaSeq | 0.2.7 | self-fail | FAILED: 01-basic.rakutest \| No such method 'cpu-cores-but-one' for invocant of type 'Kernel' |
| 2024-08-18 | Locale::Codes::Country | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'codeToCountry country=BANGLADESH' at 01-basic.rakutest line 66 |
| 2024-08-17 | DSL::English::ClassificationWorkflows | 0.1.5 | self-fail | FAILED: 01-Basic-commands.rakutest \| Cannot parse the command; target 'use data dfTitanic' position 0; parsed … |
| 2024-08-13 | IO::Prompt | 0.0.3 | self-fail | FAILED: 01-simple.rakutest \| No such method 'lang_prompt_yn' for invocant of type 'IO::Prompt::Testable' |
| 2024-08-10 | Pod::EOD | 0.1.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'rearrangement should be successful for feature 'sub example {}'' a … |
| 2024-08-09 | Grid | 0.0.5 | self-fail | FAILED: basic.t \| [3 4] is not subgrid of element |
| 2024-08-09 | Heap | 0.0.1 | self-fail | FAILED: 01-test.rakutest \| # Failed test 'Empty Heap' at 01-test.rakutest line 9 |
| 2024-08-09 | Injector | 0.0.1 | self-fail | FAILED: 02-test.rakutest \| Undefined routine 'bind' |
| 2024-08-09 | Protocol | 0.0.1 | self-fail | FAILED: 01-protocol.rakutest \| # Failed test at 01-protocol.rakutest line 9 |
| 2024-08-09 | Retry | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'we can retry for ∞' at 01-basic.rakutest line 45 |
| 2024-08-09 | Slang::Mosdef | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| Useless use of $n*2 in sink context (line 12) |
| 2024-08-09 | Trie | 0.0.1 | dep-fail | OrderedHash |
| 2024-08-08 | Algorithm::KdTree | 0.0.8 | self-fail | FAILED: 02-insert.t \| # Failed test 'It shouldn't insert a Str array' at 02-insert.t line 7 |
| 2024-08-08 | Algorithm::Kruskal | 0.0.1 | dep-fail | Algorithm::MinMaxHeap |
| 2024-08-08 | Algorithm::Treap | 0.10.3 | self-fail | FAILED: 01-basic.t \| # Failed test 'It shouldn't handle a type object' at 01-basic.t line 12 |
| 2024-08-08 | SixPM | 0.0.11 | self-fail | FAILED: 01-six-pm.rakutest \| Undefined routine 'nqp::objprimspec' |
| 2024-08-06 | Fortran::Grammar | 0.0.7 | self-fail | FAILED: FortranBasic.rakutest \| # the licence ‘GPL-3.0’ is valid but deprecated, you may want to use another  … |
| 2024-08-02 | Data::Summarizers | 0.2.6 | dep-fail | Pretty::Table |
| 2024-07-29 | Backtrace::Files | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 148: Error while compiling module Backtrace::Fil … |
| 2024-07-27 | RakupodObject | 0.0.7 | self-fail | FAILED: 1-critical.t \| # Failed test 'bad pod string' at 1-critical.t line 14 |
| 2024-07-19 | IO-Socket-TLSViaAsync | 1.0.0 | timeout | IO::Socket::Async::SSL |
| 2024-07-16 | FDF | 0.0.4 | dep-fail | Getopt::Long |
| 2024-07-07 | Functional::LinkedList | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 36: Error while compiling module Functional::Lin … |
| 2024-07-07 | Functional::Queue | 0.0.2 | dep-fail | Functional::LinkedList |
| 2024-07-07 | Functional::Stack | 0.0.3 | dep-fail | Functional::LinkedList |
| 2024-07-07 | MongoDB | 0.45.3 | dep-fail | PKCS5 |
| 2024-07-06 | Physics::Vector | 0.0.2 | dep-fail | Math::Vector |
| 2024-07-05 | Mmap::Native | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: Mmap::Native' at 01-basic.rakutest lin … |
| 2024-07-04 | EBNF::Grammar | 0.1.6 | dep-fail | FunctionalParsers |
| 2024-07-04 | Math::Vector | 0.6.0 | self-fail | FAILED: 01-basics.rakutest \| # Failed test 'Subtraction is anticommutative' at 01-basics.rakutest line 19 |
| 2024-07-02 | Config::Parser::NetRC | 0.0.1 | dep-fail | Net::NetRC |
| 2024-07-02 | Net::NetRC | 0.0.7 | self-fail | FAILED: 02-functionality.rakutest \| # Failed test at 02-functionality.rakutest line 59 |
| 2024-07-02 | ValueClass | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 12: expected variable after declarator (got 'val … |
| 2024-06-28 | ValueType | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| No such method 'new' for invocant of type 'ValueObjAt' |
| 2024-06-27 | ake | 0.1.3 | self-fail | FAILED: 00-original-task.t \| Stub code executed |
| 2024-06-25 | NYI | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| No such method 'new' for invocant of type 'X::NYI' |
| 2024-06-25 | ObjectCache | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does absence of ID die' at 01-basic.rakutest line 18 |
| 2024-06-25 | Set::Equality | 0.0.8 | self-fail | FAILED: set_equality.rakutest \| # Failed test 'a(-1) is NOT (==) of Mix' at set_equality.rakutest line 365 |
| 2024-06-25 | Test::Assertion | 0.0.6 | pass |  |
| 2024-06-25 | uniprop | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'proto' |
| 2024-06-24 | SeqSplitter | 0.0.3 | self-fail | FAILED: 02-all.t \| Type check failed in binding to parameter '$orig-seq'; expected Sequence but got Seq ((0,  … |
| 2024-06-22 | IO-Archive | 0.0.5 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: IO::Archive' at 00-use.rakutest line 6 |
| 2024-06-20 | Math::Polynomial::Chebyshev | 0.0.2 | self-fail | FAILED: 01-chebyshev-t.rakutest \| # Failed test 'Same result with function over data and data argument' at 01 … |
| 2024-06-17 | Collection | 0.18.0 | dep-fail | Test::Deeply::Relaxed |
| 2024-06-16 | Color::Palette | 0.0.1 | self-fail | FAILED: 02-gpl.rakutest \| # Failed test 'got palette' at 02-gpl.rakutest line 19 |
| 2024-06-14 | Raku::Pod::Render | 4.10.6 | dep-fail | Test::Deeply::Relaxed |
| 2024-06-12 | Term::termios | 0.2.8 | pass |  |
| 2024-06-11 | FastCGI::NativeCall | 0.0.12 | pass |  |
| 2024-06-07 | Data::Geographics | 0.1.5 | self-fail | FAILED: 03-GeoHash.rakutest \| # Failed test 'The object is-a 'Positional'' at 03-GeoHash.rakutest line 384 |
| 2024-06-07 | ML::TriesWithFrequencies | 0.6.7 | self-fail | FAILED: 01-creation.rakutest \| No such method 'trieRootLabel' for invocant of type 'ML::TriesWithFrequencies: … |
| 2024-06-01 | ML::Clustering | 0.2.0 | self-fail | FAILED: 02-K-means-signatures.rakutest \| Cannot resolve caller euclidean-distance(); no matching multi candid … |
| 2024-05-27 | python::itertools | 1.0.3 | self-fail | FAILED: itertools.t \| ===SORRY!=== Parse error at line 53: expected { (got '(') |
| 2024-05-26 | DSL::English::DataQueryWorkflows | 0.6.5 | self-fail | FAILED: Basic-commands-R-tidyverse.rakutest \| Cannot resolve caller ToWorkflowCode(); no matching multi candi … |
| 2024-05-21 | Cache::Dir | 0.0.2 | self-fail | FAILED: 01-cache.rakutest \| Cannot load native library '/Users/ash/eco-sweep/store2/resources/DA39A3EE5E6B4B0 … |
| 2024-05-08 | PDF::Extract | 0.0.4 | self-fail | FAILED: 01-san.rakutest \| ===SORRY!=== Parse error at line 17: Error while compiling module PDF::Extract (lin … |
| 2024-04-28 | Manifesto | 0.0.7 | self-fail | FAILED: 040-synopsis.t \| # Failed test 'got what we expected' at 040-synopsis.t line 29 |
| 2024-04-28 | SQL::Abstract | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| Useless use of () in sink context (line 403) |
| 2024-04-24 | DSL::FiniteStateMachines | 0.1.4 | dep-fail | Pretty::Table |
| 2024-04-17 | Contact::Name | 0.0.1 | dep-fail | Contact |
| 2024-04-17 | Data::ExampleDatasets | 0.2.4 | self-fail | FAILED: 01-required-packages.rakutest \| ===SORRY!=== Parse error at line 223: Error while compiling module Te … |
| 2024-04-11 | AttrX::Mooish | 1.0.10 | self-fail | FAILED: 010-base.custom.rakutest \| # Failed test 'Class Basics' at 010-base.custom.rakutest line 15 |
| 2024-04-11 | WWW::GCloud | 0.0.8 | self-fail | FAILED: 020-cpromise.rakutest \| ===SORRY!=== Parse error at line 31: Error while compiling module Test::Async … |
| 2024-04-11 | WWW::GCloud::API::Storage | 0.0.5 | self-fail | FAILED: 000-use.rakutest \| ===SORRY!=== Parse error at line 31: Error while compiling module Test::Async (lin … |
| 2024-04-11 | WWW::GCloud::API::Vision | 0.0.4 | dep-fail | WWW::GCloud::API::Storage |
| 2024-04-10 | Crypt::SodiumPasswordHash | 0.0.6 | self-fail | FAILED: 030-hash.t \| ===SORRY!=== Parse error at line 62: Confused (got ')') |
| 2024-04-10 | Math::Combinatorics | 0.0.8 | self-fail | FAILED: combinatorics.t \| ===SORRY!=== Parse error at line 63: Error while compiling module Math::Combinatori … |
| 2024-04-05 | Config::DataLang::Refine | 0.7.6 | self-fail | FAILED: 100-refine-toml.rakutest \| ===SORRY!=== Parse error at line 67: Error while compiling module Crane::T … |
| 2024-04-05 | Gnome::Cairo | 0.3.0 | self-fail | FAILED: Cairo.rakutest \| Undefined routine 'void' |
| 2024-04-05 | Gnome::Gdk3 | 0.20.0 | self-fail | FAILED: Atom.rakutest \| Could not find Gnome::N::N-GObject in: |
| 2024-03-26 | JSON::GLib::Node | 0.0.1 | dep-fail | DOM::Tiny |
| 2024-03-13 | Marrow | 0.1.5 | dep-fail | DB |
| 2024-03-12 | Audio::Convert::Samplerate | 0.0.11 | self-fail | FAILED: 010-constructor.t \| # Failed test 'create a new Audio::Convert::Samplerate' at 010-constructor.t line … |
| 2024-03-12 | Audio::Fingerprint::Chromaprint | 0.0.5 | self-fail | FAILED: 030-fingerprint.t \| # Failed test 'create object' at 030-fingerprint.t line 14 |
| 2024-03-12 | Audio::Sndfile | 0.0.16 | self-fail | FAILED: 030-constructor.t \| Cannot load native library 'sndfile 1': dlopen(sndfile 1, 0x0009): tried: 'sndfil … |
| 2024-03-08 | PDF::ISO_32000_2 | 0.0.3 | self-fail | FAILED: interface-roles.t \| # Failed test 'interface role missing method - dies' at interface-roles.t line 15 |
| 2024-03-03 | MUGS | 0.1.4 | dep-fail | Getopt::Long |
| 2024-03-03 | MUGS::Core | 0.1.4 | dep-fail | Getopt::Long |
| 2024-03-03 | MUGS::Games | 0.1.4 | dep-fail | Getopt::Long |
| 2024-03-03 | MUGS::UI::CLI | 0.1.4 | dep-fail | Getopt::Long |
| 2024-03-03 | MUGS::UI::TUI | 0.1.4 | dep-fail | Getopt::Long |
| 2024-03-03 | MUGS::UI::WebSimple | 0.1.4 | dep-fail | Getopt::Long |
| 2024-02-24 | Date::Easter | 0.0.5 | self-fail | FAILED: 2-easter-events.t \| # Failed test at 2-easter-events.t line 61 |
| 2024-02-22 | Asserter | 0.1.0 | self-fail | FAILED: 01.rakutest \| Could not find QAST in: |
| 2024-02-22 | Date::Event | 0.0.12 | self-fail | FAILED: 0-load-ok.t \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of type 'Ra … |
| 2024-02-22 | LocalTime | 0.0.3 | self-fail | FAILED: 01-write-format.t \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of ty … |
| 2024-02-16 | Arithmetic::PaperAndPencil | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 81: Error while compiling module Arithmetic::Pap … |
| 2024-02-16 | DSL::Entity::Metadata | 0.1.1 | self-fail | FAILED: Data-format-names-parsing.t \| Cannot parse the command; target 'integer 16' position 0; parsed '', un … |
| 2024-02-14 | HarfBuzz::Shaper::Cairo | 0.0.7 | self-fail | FAILED: basic.t \| ===WARNING=== Module MacOS::NativeLib EXPORT failed: Failed to create symlink called '/User … |
| 2024-02-12 | JSON::Hjson | 0.0.3 | self-fail | FAILED: 01-basic.t \| Input (383 characters) is not a valid JSON string |
| 2024-02-11 | Configuration | 0.0.11 | self-fail | FAILED: 01-config.rakutest \| Undefined routine 'with' |
| 2024-02-11 | DSL::Entity::AddressBook | 0.1.0 | self-fail | FAILED: 01-person-names-parsing.rakutest \| Cannot parse the command; target 'Orlando Bloom' position 0; parse … |
| 2024-02-09 | Holidays::Miscellaneous | 0.0.1 | dep-fail | Holidays::US::Federal |
| 2024-02-08 | Moneys | 0.0.4 | self-fail | FAILED: 00-use.rakutest \| ===SORRY!=== Parse error at line 34: expected ) (got 'USD') |
| 2024-02-07 | App::Cal | 0.9.1 | self-fail | FAILED: year.t \| # Failed test 'expected result for non-higlighted year' at year.t line 20 |
| 2024-02-07 | Holidays::US::Federal | 0.0.5 | self-fail | FAILED: 1-misc.t \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of type 'Raku' |
| 2024-02-07 | RSV | 0.0.1 | self-fail | FAILED: simple-cases.rakutest \| ===SORRY!=== Parse error at line 65: Error while compiling module RSV (line 6 … |
| 2024-02-05 | Linux::NFTables | 0.0.3 | build-fail |  |
| 2024-02-05 | Math::Libgsl::BLAS | 0.0.5 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Combination | 0.1.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Complex | 0.0.5 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Histogram | 0.1.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Interpolation | 0.1.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::LinearAlgebra | 0.0.4 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Matrix | 0.6.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Multiset | 0.1.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Permutation | 0.1.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Polynomial | 0.0.4 | build-fail |  |
| 2024-02-05 | Math::Libgsl::QuasiRandom | 0.1.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Random | 0.1.1 | build-fail |  |
| 2024-02-05 | Math::Libgsl::Wavelet | 0.0.3 | build-fail |  |
| 2024-02-03 | App::RakuCron | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| Could not find Configuration in: |
| 2024-01-31 | Rat::Power | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'Int' at 01-basic.rakutest line 4 |
| 2024-01-30 | DSL::Entity::MachineLearning | 0.1.2 | self-fail | FAILED: Classifier-measurement-names-parsing.rakutest \| Cannot parse the command; target 'roc curve' position … |
| 2024-01-28 | UML::Translators | 0.1.7 | self-fail | FAILED: 01-over-classes.rakutest \| Cannot load name space MyPackageClass. |
| 2024-01-22 | Gzz::Text::Utils | 0.1.23 | self-fail | FAILED: 000-Uitls-test.rakutest \| ===SORRY!=== Parse error at line 244: Error while compiling module Gzz::Tex … |
| 2024-01-21 | Grammar::PrettyErrors | 0.0.5 | self-fail | FAILED: 01-has-ws.rakutest \| callsame is not in the dynamic scope of a dispatcher |
| 2024-01-21 | Net::Whois | 0.0.3 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Net::Whois' at 00-use.rakutest line 6 |
| 2024-01-20 | Font::AFM | 1.24.10 | self-fail | FAILED: font-metrics-courier.t \| Type check failed in assignment to $glyph-name; expected Str but got Any (An … |
| 2024-01-17 | DateTime::React | 0.2.0 | dep-build-fail | Timezones::ZoneInfo |
| 2024-01-16 | Intl::CLDR | 0.7.6 | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 153: Error while compiling module Intl::CLDR::T … |
| 2024-01-14 | L10N::TLH | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| No such method 'AST' for invocant of type 'Str' |
| 2024-01-01 | File::Utils | 0.1.2 | self-fail | FAILED: 000-File::Utils.rakutest \| ===SORRY!=== Parse error at line 165: Error while compiling module File::U … |
| 2024-01-01 | P5localtime | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 20: Error while compiling module P5localtime (li … |
| 2023-12-27 | Digest | 1.1.0 | timeout | Digest |
| 2023-12-26 | Display::Listings | 0.1.9 | self-fail | FAILED: 000-Display::Listings.rakutest \| ===SORRY!=== Parse error at line 573: Error while compiling module D … |
| 2023-12-24 | Config::BINDish | 0.0.18 | dep-fail | IP::Addr |
| 2023-12-24 | RegexUtils | 0.1.5 | self-fail | FAILED: 020-compile.rakutest \| # Failed test 'fo\n, foo\n, FOO\n, ... whole string case insensitive' at 020-c … |
| 2023-12-17 | Object::Delayed | 0.0.13 | dep-fail | InterceptAllMethods |
| 2023-12-17 | Object::Trampoline | 0.0.12 | dep-fail | InterceptAllMethods |
| 2023-12-15 | Async::Workers | 0.3.1 | self-fail | FAILED: 010-workers.rakutest \| ===SORRY!=== Parse error at line 31: Error while compiling module Test::Async  … |
| 2023-12-15 | Parse::Paths | 0.1.2 | self-fail | FAILED: 001-Meta-test.rakutest \| ===SORRY!=== Parse error at line 244: Error while compiling module Gzz::Text … |
| 2023-12-13 | Math::Interval | 0.0.3 | self-fail | FAILED: 01-rop.rakutest \| # Failed test 'R+R' at 01-rop.rakutest line 20 |
| 2023-12-10 | Test::Async | 0.1.17 | self-fail | FAILED: 003-declaration.rakutest \| ===WARNING=== Module Test::Async::Decl EXPORT failed: Could not find NQPHL … |
| 2023-12-08 | Syntax::Highlighters | 0.1.5 | self-fail | FAILED: 000-syntax-highlighters.rakutest \| ===SORRY!=== Parse error at line 324: Error while compiling module … |
| 2023-12-07 | Email::Valid | 1.0.7 | self-fail | FAILED: 02-simple_valid.t \| # Failed test '-t.123+asd@aa.tr OK' at 02-simple_valid.t line 136 |
| 2023-12-03 | Path::Finder | 0.4.7 | self-fail | FAILED: basic.t \| Undefined routine 'nqp::stat' |
| 2023-12-03 | Readline | 0.1.8 | self-fail | FAILED: 01-load.t \| Cannot load native library '/usr/local/lib/libreadline.7.dylib': dlopen(/usr/local/lib/li … |
| 2023-11-20 | OrderedHash | 0.0.4 | self-fail | FAILED: 01-basic.t \| # Failed test 'default value type' at 01-basic.t line 10 |
| 2023-11-19 | CSV-Autoclass | 0.2.0 | self-fail | FAILED: 2-bin-tests.t \| # Failed test 'unknown named arg' at 2-bin-tests.t line 28 |
| 2023-11-15 | Clipboard | 0.1.2 | self-fail | FAILED: 01-copy-to-clipboard.t \| The spawned command 'echo '3433' \| xclip' exited unsuccessfully (exit code: … |
| 2023-11-09 | ANTLR4::Grammar | 0.6.3 | self-fail | FAILED: 02-corpus.t \| # Failed test 'IRI.g4' at 02-corpus.t line 39 |
| 2023-11-09 | PDF::Combiner | 0.0.1 | dep-fail | Getopt::Long |
| 2023-11-01 | App::Tasks | 0.4.0 | dep-fail | Terminal::ReadKey |
| 2023-10-30 | Text::CSV | 0.022 | self-fail | FAILED: 10_base.t \| ===SORRY!=== Parse error at line 18: expected ) (got '(') |
| 2023-10-29 | Net::Postgres::Abstract | 0.0.2 | dep-fail | Auth::SCRAM::Async |
| 2023-10-26 | Cro::FCGI | 1.0.1 | self-fail | FAILED: record-serializer.rakutest \| # Failed test 'Simple params record is not serialized!' at record-serial … |
| 2023-10-19 | Intl::Regex::CharClass | 0.1.0 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 130: Error while compiling module Intl::Regex::CharCla … |
| 2023-10-18 | Math::Constants | 0.2.2 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 1: unexpected operator in term position (got '﻿') |
| 2023-10-14 | GlotIO | 2.0.0 | self-fail | FAILED: 01-run-api.t \| # Failed test 'returned stuff looks sane' at 01-run-api.t line 12 |
| 2023-10-09 | Getopt::Long::Grammar | 0.0.2 | self-fail | FAILED: 01-parsing.t \| ===SORRY!=== Parse error at line 27: unexpected operator in term position (got '>>') |
| 2023-10-08 | Hash::Consistent | 0.0.4 | self-fail | FAILED: basic.rakutest \| # Failed test 'cardinality on insert' at basic.rakutest line 44 |
| 2023-10-08 | IO::String | 0.2.0 | dep-fail | Text::CSV |
| 2023-10-07 | Net::LibIDN2 | 0.1.1 | self-fail | FAILED: 01-basic.t \| Cannot load native library 'idn2': dlopen(idn2, 0x0009): tried: 'idn2' (no such file), ' … |
| 2023-10-07 | Subsets::Common | 0.0.6 | self-fail | FAILED: basic.rakutest \| # Failed test 'PosInt caught out of range' at basic.rakutest line 28 |
| 2023-10-07 | WebService::AWS::Auth::V4 | 0.0.5 | self-fail | FAILED: basic.rakutest \| # Failed test 'match aws date example' at basic.rakutest line 34 |
| 2023-09-30 | DB::Migration::Simple | 1.1 | self-fail | FAILED: 00-basic.rakutest \| Can't determine actual Offset |
| 2023-09-26 | Slang::Otherwise | 0.1.0 | self-fail | FAILED: 01-basic.rakutest \| ===WARNING=== Module Slang::Otherwise EXPORT failed: No such method 'slang_gramma … |
| 2023-09-26 | _ | 0.0.2 | self-fail | FAILED: 01-selective-imports.rakutest \| # Failed test 'Full import works' at 01-selective-imports.rakutest li … |
| 2023-09-19 | HTML::Template | 0.0.1 | self-fail | FAILED: 01.t \| # Failed test 'true condition' at 01.t line 143 |
| 2023-09-18 | App::Zef-Deps | 0.9.8 | self-fail | FAILED: basic.t \| Could not find Zef::Distribution in: |
| 2023-09-18 | Inline::J | 0.5.1 | self-fail | FAILED: basic.t \| ===WARNING=== Module Inline::J::Utils EXPORT failed: Unknown name for export: 'batched' |
| 2023-09-16 | Archive::SimpleZip | 0.8.0 | self-fail | FAILED: 001-use.t \| # Failed test 'Can load "Archive::SimpleZip" ok' at 001-use.t line 9 |
| 2023-09-12 | XML::Fast | 0.0.3 | dep-fail | Getopt::Long |
| 2023-09-03 | DateTime::Grammar | 0.1.3 | self-fail | FAILED: 01-asctime.rakutest \| # Failed test 'Returns a DateTime object' at 01-asctime.rakutest line 72 |
| 2023-09-03 | Text::SubParsers | 0.1.4 | self-fail | FAILED: 01-basic-usage.t \| # Failed test at 01-basic-usage.t line 148 |
| 2023-09-02 | Intl::Token::Number | 0.6 | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 153: Error while compiling module Intl::CLDR::T … |
| 2023-09-01 | Grammar::TokenProcessing | 0.1.10 | dep-fail | Pretty::Table |
| 2023-08-29 | Config::Parser::toml | 1.0.4 | self-fail | FAILED: 01-read.t \| ===SORRY!=== Parse error at line 67: Error while compiling module Crane::Transform (line  … |
| 2023-08-26 | Algorithm::LibSVM | 0.0.18 | self-fail | FAILED: 01-basic.t \| # Failed test 'Algorithm::LibSVM::Problem.from-matrix should create an instance from a s … |
| 2023-08-26 | Algorithm::XGBoost | 0.0.6 | build-fail |  |
| 2023-08-26 | WebService::HashiCorp::Vault | 0.1.0 | timeout | Cro::WebSocket |
| 2023-08-22 | Touch | 0.7.0 | self-fail | FAILED: 001-timespec.rakutest \| Can't determine actual Offset |
| 2023-08-20 | Deepgrep | 0.1.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'adverb :kv' at 01-basic.rakutest line 13 |
| 2023-08-19 | FiniteFields | 0.3.2 | self-fail | FAILED: basic.t \| # Failed test at basic.t line 6 |
| 2023-08-16 | Lingua::EN::Fathom | 0.0.3 | self-fail | FAILED: main.rakutest \| No such method 'sentences' for invocant of type 'Str' |
| 2023-08-16 | Net::Netmask | 0.2.1 | self-fail | FAILED: 01-basic.t \| Constraint type check failed in binding to parameter '@address' |
| 2023-08-11 | P5unlink | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| Too many levels of recursion |
| 2023-08-10 | Cooklang | 1.1.1 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Cooklang' at 00-use.rakutest line 8 |
| 2023-08-10 | List::MoreUtils | 0.0.10 | self-fail | FAILED: 02-basic.rakutest \| # Failed test 'is &bsearch_index imported?' at 02-basic.rakutest line 20 |
| 2023-08-10 | List::UtilsBy | 0.0.8 | self-fail | FAILED: max_by.rakutest \| ===WARNING=== Module List::UtilsBy EXPORT failed: List::UtilsBy doesn't know how to … |
| 2023-08-10 | P5caller | 0.0.13 | self-fail | FAILED: 01-basic.rakutest \| No such method 'new' for invocant of type 'Backtrace' |
| 2023-08-10 | P5getgrnam | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 28: Error while compiling module P5getgrnam (lin … |
| 2023-08-10 | P5getnetbyname | 0.0.9 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 31: Error while compiling module P5getnetbyname  … |
| 2023-08-10 | P5getprotobyname | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 32: Error while compiling module P5getprotobynam … |
| 2023-08-10 | P5getpwnam | 0.0.11 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 25: Error while compiling module P5getpwnam (lin … |
| 2023-08-10 | P5getservbyname | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 36: Error while compiling module P5getservbyname … |
| 2023-08-08 | App:Racl | 0.0.1 | dep-fail | DB |
| 2023-08-08 | App:Ralc | 0.0.2 | dep-fail | DB |
| 2023-08-07 | P5-X | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 23: expected ) (got '$file') |
| 2023-08-07 | P5chr | 0.0.9 | self-fail | FAILED: 02-chr.rakutest \| Default constructor for 'InvalidChr' only takes named arguments |
| 2023-08-07 | P5defined | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 18: Unsupported use of undef as a value; in Raku … |
| 2023-08-07 | P5math | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'exists' |
| 2023-08-07 | P5print | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'STDIN imported?' at 01-basic.rakutest line 10 |
| 2023-08-07 | P5quotemeta | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'Codepoint 0 [0] # using $_' at 01-basic.rakutest line 48 |
| 2023-08-07 | P5readlink | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::stat' |
| 2023-08-07 | P5ref | 0.0.8 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 27: unexpected operator in term position (got '/ … |
| 2023-08-07 | P5reset | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'was $a reset' at 01-basic.rakutest line 14 |
| 2023-08-03 | HarfBuzz::Subset | 0.0.5 | self-fail | FAILED: 01_basic.t \| Useless use of () in sink context (line 33) |
| 2023-08-02 | Polyglot::Regexen | 0.1.0 | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 118: Error while compiling module Polyglot::Reg … |
| 2023-08-02 | WebService::TMDB | 0.1.2 | dep-fail | Test::Mock |
| 2023-07-31 | Template::Mojo | 0.2.2 | self-fail | FAILED: 00-basic.rakutest \| # Failed test 'not enough arguments' at 00-basic.rakutest line 50 |
| 2023-07-26 | FontConverter | 0.0.1 | dep-fail | File::file |
| 2023-07-20 | File::file | 0.0.1 | self-fail | FAILED: 02-file.t \| No such method 'new' for invocant of type 'Backtrace' |
| 2023-07-17 | Shell::DSL | 0.0.4 | dep-fail | Fcntl |
| 2023-07-15 | Iec104Parser | 0.1 | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 47: Error while compiling module Iec104::Asdu:: … |
| 2023-07-15 | SOD | 0.2.5 | build-fail |  |
| 2023-07-14 | App::SerializerPerf | 0.0.5 | dep-fail | Getopt::Long |
| 2023-07-13 | Polyglot::Brainfuck | 0.1 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 4: Confused (got '}') |
| 2023-07-12 | Github::PublicKeys | 0.2.0 | self-fail | FAILED: 02-lives-ok.t \| # Failed test at 02-lives-ok.t line 5 |
| 2023-07-12 | Template::Nest::XS | 0.1.10 | self-fail | FAILED: 00-basic.rakutest \| Variable '$class_pointer' is not declared |
| 2023-07-10 | Net::Postgres | 0.0.4 | dep-fail | Auth::SCRAM::Async |
| 2023-07-09 | App::Gitstatus | 0.0.2 | self-fail | FAILED: 01-bin-prog.rakutest \| # Failed test at 01-bin-prog.rakutest line 8 |
| 2023-07-05 | App::APOTD | 1.0.9 | self-fail | FAILED: 01-basic.rakutest \| Usage: |
| 2023-07-05 | App::Lorea | 0.2.6 | dep-fail | Timer::Breakable |
| 2023-07-05 | Attribute::Lazy | 0.0.7 | self-fail | FAILED: 020-trait.t \| # Failed test 'got the value from the block ( basic )' at 020-trait.t line 21 |
| 2023-07-04 | URI::FetchFile | 0.0.6 | self-fail | FAILED: 070-fetch-uri.t \| # Failed test 'attempt a non-existent file' at 070-fetch-uri.t line 11 |
| 2023-07-03 | JSON::Infer | 0.1.2 | self-fail | FAILED: 020-attribute.t \| Type check failed in binding to parameter '$attr'; expected Attribute but got JSON: … |
| 2023-07-03 | LibraryCheck | 0.0.12 | self-fail | FAILED: 010-basic.t \| # Failed test 'not ok for a bogus one' at 010-basic.t line 90 |
| 2023-07-03 | Object::Permission::Group | 0.0.7 | dep-fail | Unix::Groups |
| 2023-07-02 | AccessorFacade | 0.1.2 | self-fail | FAILED: 010-compose.t \| # Failed test 'get works fine' at 010-compose.t line 51 |
| 2023-07-02 | Sys::Utmp | 0.1.3 | build-fail |  |
| 2023-07-02 | Unix::Groups | 0.0.7 | self-fail | FAILED: 020-groups.t \| Type check failed in assignment; expected Str but got Any |
| 2023-07-02 | octans | 0.2.5 | self-fail | FAILED: 00-basic.rakutest \| # Failed test 'The module can be use-d ok: Octans::CLI' at 00-basic.rakutest line … |
| 2023-07-01 | MQ::Posix | 0.0.5 | self-fail | FAILED: 010-use.t \| # Failed test 'The module can be use-d ok: MQ::Posix' at 010-use.t line 7 |
| 2023-07-01 | Oyatul | 0.0.9 | self-fail | FAILED: 030-detail.t \| # Failed test 'and it does the role we specified' at 030-detail.t line 21 |
| 2023-07-01 | URI::Template | 0.0.11 | self-fail | FAILED: 020-grammar.t \| Undeclared name 'URI::Template::Grammar' |
| 2023-07-01 | Ujumla | 0.0.5 | self-fail | FAILED: 030-synopsis.t \| # Failed test 'simple config' at 030-synopsis.t line 10 |
| 2023-06-29 | Data::Cryptocurrencies | 0.1.3 | dep-fail | Pretty::Table |
| 2023-06-16 | LibXML::Class | 0.0.1 | dep-fail | Getopt::Long |
| 2023-06-11 | DSL::Entity::WeatherData | 0.1.0 | self-fail | FAILED: Station-names-parsing.t \| Cannot parse the command; target 'F0664' position 0; parsed '', un-parsed ' … |
| 2023-06-02 | CLI::Wordpress | 0.0.2 | build-fail |  |
| 2023-06-02 | Dan | 0.0.3 | self-fail | FAILED: 01-ser.t \| ===SORRY!=== Parse error at line 43: unexpected operator in term position (got '=') |
| 2023-06-02 | Dan::Pandas | 0.0.3 | dep-fail | Inline::Python |
| 2023-06-02 | Dan::Polars | 0.0.3 | dep-fail | Timer |
| 2023-05-27 | SQL::Builder::ExecuteWithDBIish | 0.0.1 | dep-fail | SQL::Builder |
| 2023-05-26 | Terminal::Size | 1.0.1 | self-fail | FAILED: 01-load.t \| Cannot load native library 'libc.so.6': dlopen(libc.so.6, 0x0009): tried: 'libc.so.6' (no … |
| 2023-05-21 | File-TreeBuilder | 0.1.1 | self-fail | FAILED: main.rakutest \| Undeclared name 'File::Temp::tempdir' |
| 2023-05-19 | Email::MIME | 2.0.7 | self-fail | FAILED: base64.t \| Type Array does not support associative indexing |
| 2023-05-16 | raku-mailgun | 0.0.2 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Mailgun' at 00-use.rakutest line 5 |
| 2023-05-11 | PDF::ISO_32000 | 0.0.13 | self-fail | FAILED: interface-roles.t \| # Failed test 'interface role missing method - dies' at interface-roles.t line 15 |
| 2023-04-30 | MIDI::Make | 0.11.0 | self-fail | FAILED: all.rakutest \| No such method 'write-uint32' for invocant of type 'Buf' |
| 2023-04-27 | JSON::RPC | 1.0.6 | self-fail | FAILED: 00-client.rakutest \| No matching multi candidate for method BUILD |
| 2023-04-21 | HomoGlypher | 1.8.7 | self-fail | FAILED: 01-unwind.rakutest \| samewith is not in the dynamic scope of a dispatcher |
| 2023-04-16 | File::ExtendedAttributes | 0.0.3 | timeout | Unix::errno |
| 2023-04-16 | Intl::Format::DateTime | 0.3.0 | dep-build-fail | Timezones::ZoneInfo |
| 2023-04-15 | Progress::Bar | 1.0.1 | self-fail | FAILED: basic.rakutest \| # Failed test 'ProgressBar loads ok' at basic.rakutest line 6 |
| 2023-04-13 | App::Uni | 1.0.4 | self-fail | FAILED: basic.t \| No such method 'version' for invocant of type 'Unicode' |
| 2023-04-10 | DateTime::Timezones | 0.4.2 | dep-build-fail | Timezones::ZoneInfo |
| 2023-04-09 | Hey | 1.0.0-beta.9 | dep-fail | DB |
| 2023-04-09 | Windows::Test | 0.0.3 | dep-fail | Getopt::Long |
| 2023-04-01 | DB::Migration::Declare | 0.1.3 | dep-fail | Dev::ContainerizedService |
| 2023-03-28 | Math::FractionalPart | 0.0.6 | self-fail | FAILED: frac-methods.t \| # Failed test 'is f(Inf) = NaN for function fractional' at frac-methods.t line 157 |
| 2023-03-27 | rakudoc | 0.2.6 | dep-fail | Pod::Utils |
| 2023-03-26 | Intl::Format::Number | 0.4.0 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 172: Error while compiling module Intl::Format::Number … |
| 2023-03-23 | Sort-Fast | 0.0.1 | other | tar: Error exit delayed from previous errors. |
| 2023-03-11 | Slang::Forgiven | 0.1.1 | self-fail | FAILED: 00-usability.rakutest \| ===SORRY!=== Parse error at line 8: expected } (got '') |
| 2023-03-10 | Ask | 0.0.3 | self-fail | FAILED: ask-prompt-alias.t \| # Failed test 'return value' at ask-prompt-alias.t line 26 |
| 2023-03-07 | Calendar | 0.0.4 | dep-fail | Getopt::Long |
| 2023-03-07 | Gherkin::Grammar | 0.1.6 | self-fail | FAILED: 01-Parsing.rakutest \| # Failed test at 01-Parsing.rakutest line 1064 |
| 2023-03-01 | Rat::Precise | 0.1.2 | self-fail | FAILED: 01-rat.t \| # Failed test 'Rat can .precise' at 01-rat.t line 6 |
| 2023-02-27 | Math::Handy | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 4: expected ) (got '!') |
| 2023-02-26 | List::Allmax | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'all-max keys returns expected' at 01-basic.rakutest line 27 |
| 2023-02-26 | Math::Root | 0.0.4 | self-fail | FAILED: 01-integer.t \| Type check failed in binding to parameter '$integer'; expected Int but got Num (3.8146 … |
| 2023-02-25 | Monad-Result | 1.0.0 | self-fail | FAILED: 01-test-result.rakutest \| ===SORRY!=== Parse error at line 15: unexpected operator in term position ( … |
| 2023-02-24 | App::RaCoCo | 2.0.1 | dep-fail | Getopt::Long |
| 2023-02-24 | App::Racoco::Report::ReporterCoveralls | 2.0.2 | dep-fail | Getopt::Long |
| 2023-02-24 | Rakudo::Version | 0.0.6 | self-fail | FAILED: 01-basic.rakutest \| Rakudo::Version is supposed to run on Rakudo, not 'Raku++' |
| 2023-02-24 | User::Language | 0.5.2 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 533: Error while compiling module Intl::LanguageTag::B … |
| 2023-02-18 | XML::Actions | 0.5.1 | self-fail | FAILED: 100-Actions.t \| # Failed test 'Action object from file' at 100-Actions.t line 108 |
| 2023-02-15 | Distribution::Builder::Cmake | 0.0.10 | self-fail | FAILED: 01-basic.rakutest \| # Failed test at 01-basic.rakutest line 15 |
| 2023-02-15 | Getopt::Long | 0.4.2 | self-fail | FAILED: basic.rakutest \| Class 'Exception' cannot inherit from 'CORE::Exception' because it is unknown |
| 2023-02-15 | JSON-Simd | 0.0.3 | build-fail |  |
| 2023-02-14 | Game::Entities | 0.1.6 | self-fail | FAILED: entities.t \| ===SORRY!=== Parse error at line 378: Error while compiling module Game::Entities (line  … |
| 2023-02-14 | Pop | 0.0.3 | dep-fail | TOML::Thumb |
| 2023-02-13 | Proxee | 1.3 | self-fail | FAILED: 01-basics.rakutest \| # Failed test 'coercer, variables' at 01-basics.rakutest line 21 |
| 2023-02-06 | Chart::EasyGnuplot | 0.1.3 | dep-build-fail | Chart::Gnuplot |
| 2023-02-04 | Listicles | 1.6.0 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'Should die if insufficent elements' at 01-basic.rakutest line 92 |
| 2023-02-01 | CoreHackers::Q | 1.003000 | self-fail | FAILED: 00-basic.rakutest \| # Failed test 'Output created' at 00-basic.rakutest line 20 |
| 2023-01-31 | Humming-Bird::Core | 2.0.0 | dep-fail | ULID |
| 2023-01-28 | Archive::Libarchive::Raw | 0.1.5 | self-fail | FAILED: 02-list.rakutest \| Cannot load native library 'archive 13': dlopen(archive 13, 0x0009): tried: 'archi … |
| 2023-01-28 | Directory | 0.0.10 | dep-fail | IO::Dir |
| 2023-01-26 | Distribution::Extension::Updater | 0.0.3 | dep-fail | IO::Dir |
| 2023-01-22 | TimeBomb | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| Potential difficulties: |
| 2023-01-20 | Collection-Plugin-Development | 0.4.6 | dep-fail | Terminal::Spinners |
| 2023-01-20 | Dawa | 0.0.9 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Dawa' at 00-use.rakutest line 5 |
| 2023-01-19 | Archive::Libarchive | 0.0.17 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Archive::Libarchive' at 00-use.rakutest  … |
| 2023-01-18 | Time::Duration | 2.0.2 | self-fail | FAILED: 01_tdur.t \| next without loop construct |
| 2023-01-17 | CoreHackers::Sourcery | 2.2 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: CoreHackers::Sourcery' at 00-use.rakutes … |
| 2023-01-17 | Pastebin::Shadowcat | 2.003 | self-fail | FAILED: 01-paste.rakutest \| No such method 'recv' for invocant of type 'Any' |
| 2023-01-17 | TooLoo | 2.0.2 | dep-fail | DB |
| 2023-01-15 | Collection-Raku-Documentation | 0.12.3 | dep-fail | Test::Deeply::Relaxed |
| 2023-01-15 | TCP::LowLevel | 0.1.2 | dep-fail | StrictClass |
| 2023-01-14 | Lingua::Stem::Portuguese | 0.1.0 | self-fail | FAILED: 01-stem-word.rakutest \| Could not find Lingua::Stem::Portuguese in: |
| 2023-01-14 | Text::Markov | 2.0.0 | timeout | Text::Markov |
| 2023-01-13 | Slang:Date | 0.1.0 | unresolved | Slang (not in the ecosystem index) |
| 2023-01-12 | Native::Packing | 0.0.6 | self-fail | FAILED: 00-readme.t \| # Failed test 'code sample' at 00-readme.t line 14 |
| 2023-01-10 | Gauge | 1.0.3 | self-fail | FAILED: 01-version.rakutest \| # Failed test 'can use Gauge' at 01-version.rakutest line 6 |
| 2023-01-10 | Terminal::ReadKey | 0.0.2 | self-fail | FAILED: 99-bug-return-type.rakutest \| No such method 'add_parent' for invocant of type '<anon\|1>' |
| 2023-01-09 | Lingua::Stem::Russian | 0.1.0 | self-fail | FAILED: 01-stem-word.rakutest \| Could not find Lingua::Stem::Russian in: |
| 2023-01-07 | RPi::Device::DHT11 | 0.0.1 | build-fail |  |
| 2023-01-06 | Audio::PortAudio | 0.0.7 | self-fail | FAILED: 020-basic.t \| # some drivers may emit some output when they are initialised, sorry about that |
| 2023-01-03 | Grok | 0.0.3 | self-fail | FAILED: 01-sanity.rakutest \| # Failed test 'The module can be use-d ok: Grok' at 01-sanity.rakutest line 6 |
| 2023-01-01 | SparrowCI - super fun and flexible CI system with many programming languages support | 0.0.2 | dep-fail | Cro::WebApp |
| 2022-12-31 | App::pixel::pick | 0.1.0 | self-fail | FAILED: 01-basic.rakutest \| Needs to have an X11 widowing system available. |
| 2022-12-31 | App::pixelpick | 1.1 | self-fail | FAILED: 01-basic.rakutest \| Needs to have an X11 widowing system available. |
| 2022-12-30 | Color::Names | 2.0.0 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'X11 loads ok' at 01-basic.rakutest line 11 |
| 2022-12-30 | Pod::Utils | 0.0.2 | self-fail | FAILED: 03-first-.t \| # Failed test 'Basic test' at 03-first-.t line 63 |
| 2022-12-30 | X11::libxdo | 0.1.2 | self-fail | FAILED: 00-basic.t \| Needs to have an X11 widowing system available. |
| 2022-12-29 | Text::Sorensen | 0.0.2 | self-fail | FAILED: 01-basic.t \| # Failed test 'basic sorenson ok' at 01-basic.t line 10 |
| 2022-12-29 | Timer | 0.0.3 | self-fail | FAILED: 01-basic.t \| # Failed test 'Returns a reasonable time' at 01-basic.t line 10 |
| 2022-12-28 | SDL2-ttf | 0.0.3 | dep-fail | SDL2::Raw |
| 2022-12-28 | Smooth::Numbers | 0.0.3 | self-fail | FAILED: 00-basic.t \| Undeclared name 'Smooth' |
| 2022-12-28 | String::Rotate | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| No such method 'rotate' for invocant of type 'Str' |
| 2022-12-28 | String::Splice | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'whatevercode works' at 01-basic.rakutest line 53 |
| 2022-12-28 | Text::Center | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| Type check failed in binding to parameter '$width'; expected Int but got Pair (:f … |
| 2022-12-27 | Math::Libgsl::Constants | 0.0.13 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Constants' at 00-use.rakut … |
| 2022-12-26 | UpRooted | 1.8.3 | self-fail | FAILED: 20-tree.rakutest \| # Failed test 'UpRooted::Tree basics' at 20-tree.rakutest line 70 |
| 2022-12-24 | Date::Discordian | 0.0.3 | self-fail | FAILED: 01-basic.t \| # Failed test 'test Instant' at 01-basic.t line 191 |
| 2022-12-24 | Filetype::Magic | 0.0.5 | self-fail | FAILED: 00-basic.t \| Cannot load native library 'magic': dlopen(magic, 0x0009): tried: 'magic' (no such file) … |
| 2022-12-24 | FixedInt | 0.0.5 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 23: expected variable after declarator (got ':') |
| 2022-12-24 | Intl::Format::List | 0.6.0+ | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 533: Error while compiling module Intl::Languag … |
| 2022-12-24 | Repl::Tools | 0.0.2 | self-fail | FAILED: 01-w.rakutest \| No such method 'wrap' for invocant of type 'Any' |
| 2022-12-23 | Bitcoin | 0.7.1 | dep-fail | FiniteFields |
| 2022-12-23 | Lingua::EN::Numbers | 2.8.3 | self-fail | FAILED: 00-cardinal.t \| # Failed test 'Int list' at 00-cardinal.t line 7 |
| 2022-12-20 | DateTime::Monotonic | 0.1.0 | self-fail | FAILED: 01-basic.t \| # Failed test 'Procedural Basic usage' at 01-basic.t line 13 |
| 2022-12-20 | EC | 0.6.3 | dep-fail | FiniteFields |
| 2022-12-20 | Grammar::Message | 0.0.2 | self-fail | FAILED: 00-basic.t \| # Failed test 'Pointer created' at 00-basic.t line 32 |
| 2022-12-20 | IP::Random | 0.1.0 | self-fail | FAILED: 02-lists.t \| # Failed test 'exclude_ipv4_list' at 02-lists.t line 30 |
| 2022-12-20 | Keyring | 0.2.0 | self-fail | FAILED: 01-procedural.t \| Type check failed on attribute '$!backend-priority'; expected Keyring::Backend:U bu … |
| 2022-12-19 | Native::FindVersion | 0.0.4 | self-fail | FAILED: 01-basic.t \| Undefined routine 'latest-version' |
| 2022-12-19 | Net::BGP | 0.9.0 | dep-fail | Net::Netmask |
| 2022-12-19 | Services::PortMapping | v0.0.4 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: Services::PortMapping' at 00-load.t line 4 |
| 2022-12-19 | cmark::Simple | 0.0.3 | dep-fail | Native::FindVersion |
| 2022-12-14 | IO::Capture::Simple | v0.0.2 | self-fail | FAILED: capture.t \| OH HAI! |
| 2022-12-14 | Prettier::Table | 1.1.3 | self-fail | FAILED: pretty-table-basic.t \| # Failed test 'Basic setup with title' at pretty-table-basic.t line 993 |
| 2022-12-12 | Dist::META | 0.0.4 | self-fail | FAILED: 01-basic.t \| # Failed test 'Dependency is a Str' at 01-basic.t line 17 |
| 2022-12-11 | Proc::ZMQed | 0.1.1 | self-fail | FAILED: 01-raku-evaluation.rakutest \| Cannot load native library 'zmq': dlopen(zmq, 0x0009): tried: 'zmq' (no … |
| 2022-12-10 | envy | 0.0.2 | self-fail | FAILED: 00-use.rakutest \| Usage: |
| 2022-12-08 | Kind | 1.0.3 | self-fail | FAILED: 01-typecheck.t \| ===SORRY!=== Parse error at line 36: Error while compiling module Kind (line 36): Co … |
| 2022-12-08 | Math::Libgsl::Series | 0.0.1 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Series' at 00-use.rakutest … |
| 2022-12-07 | Math::Libgsl::DigitalFiltering | 0.0.2 | dep-fail | Math::Libgsl::MovingWindow |
| 2022-12-07 | Math::Libgsl::Eigensystem | 0.0.3 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Raw::Eigensystem' at 00-us … |
| 2022-12-07 | Math::Libgsl::MovingWindow | 0.0.4 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Raw::MovingWindow' at 00-u … |
| 2022-12-06 | Math::Libgsl::RandomDistribution | 0.0.4 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Raw::RandomDistribution' a … |
| 2022-12-06 | Math::Libgsl::RunningStatistics | 0.0.2 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Raw::RunningStatistics' at … |
| 2022-12-06 | Math::Libgsl::Statistics | 0.0.2 | self-fail | FAILED: 01-raw-num64.rakutest \| # Failed test 'mean, standard deviation and variance' at 01-raw-num64.rakutes … |
| 2022-12-04 | Kind::Subset::Parametric | 1.0.0 | self-fail | FAILED: 01-metamodel.t \| ===SORRY!=== Parse error at line 36: Error while compiling module Kind (line 36): Co … |
| 2022-12-04 | Math::Libgsl::Sort | 0.0.3 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Raw::Sort' at 00-use.rakut … |
| 2022-12-04 | Mathematica::Serializer | 0.1.1 | self-fail | FAILED: 01-basic-usage.t \| # Failed test at 01-basic-usage.t line 12 |
| 2022-12-01 | Getopt::Subcommands | 0.1.0 | dep-fail | Getopt::Long |
| 2022-11-30 | Math::Libgsl::Function | 0.0.3 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Math::Libgsl::Function' at 00-use.rakute … |
| 2022-11-29 | Array::Shaped::Console | 0.0.3 | self-fail | FAILED: 02-render.t \| Unsupported operator '=' |
| 2022-11-29 | Spreadsheet::Libxlsxio | 0.0.3 | self-fail | FAILED: 01-raw-read.rakutest \| # Failed test 'version' at 01-raw-read.rakutest line 11 |
| 2022-11-29 | Sys::Chown | 0.0.2 | dep-fail | UNIX::Privileges |
| 2022-11-29 | Terminal::Table | 0.1.1 | self-fail | FAILED: 00-load.rakutest \| ===SORRY!=== Parse error at line 74: Error while compiling module Terminal::Table: … |
| 2022-11-29 | Test::Script | 0.0.4 | self-fail | FAILED: 00-output-env.t \| # Failed test 'Prints environment ' at 00-output-env.t line 55 |
| 2022-11-28 | Math::Libgsl::Elementary | 0.0.4 | self-fail | FAILED: 01-elementary.rakutest \| # Failed test 'elementary functions' at 01-elementary.rakutest line 8 |
| 2022-11-27 | Hash::Timeout | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'does Hash::Timeout' at 01-basic.rakutest line 9 |
| 2022-11-27 | Math::FFT::Libfftw3 | 0.3.5 | self-fail | FAILED: 01-memory.rakutest \| # Failed test 'allocate memory' at 01-memory.rakutest line 8 |
| 2022-11-27 | Math::FFT::Libfftw3::C2C | 0.3.4 | dep-fail | Math::FFT::Libfftw3 |
| 2022-11-24 | Desktop::Notify | 1.0.1 | self-fail | FAILED: 01notify.rakutest \| Undeclared name 'NotifyUrgencyLow' |
| 2022-11-24 | Desktop::Notify::Progress | 0.0.3 | self-fail | FAILED: 01-basic.rakutest \| Cannot load native library 'notify 4': dlopen(notify 4, 0x0009): tried: 'notify 4 … |
| 2022-11-24 | File::Metadata::Libextractor | 0.0.3 | self-fail | FAILED: 01-raw.rakutest \| Cannot load native library 'extractor 3': dlopen(extractor 3, 0x0009): tried: 'extr … |
| 2022-11-22 | Image::QRCode | 0.0.3 | self-fail | FAILED: 02-create.rakutest \| Cannot find native symbol 'QRinput_new' |
| 2022-11-19 | Pod::Load | 0.7.2 | self-fail | FAILED: 01-basic.t \| # Testing strings |
| 2022-11-18 | User::Timezone | 0.3.3 | self-fail | FAILED: 01-override.t \| Undefined routine 'override-user-timezone' |
| 2022-11-15 | Function::Validation | 1.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 125: Error while compiling module Function::Vali … |
| 2022-11-11 | POSIX::PWDENT | 0.0.2 | self-fail | FAILED: 00-pwdent.t \| ===SORRY!=== Parse error at line 25: Error while compiling module POSIX::PWDENT (line 2 … |
| 2022-11-05 | IO::Maildir | 0.0.3 | self-fail | FAILED: maildir.t \| No such method 'base' for invocant of type 'Instant' |
| 2022-11-05 | Log::Dispatch | 0.0.8 | timeout | Log::Dispatch |
| 2022-11-02 | are | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| Undefined routine 'nqp::eqaddr' |
| 2022-10-30 | raku-pod-extraction | 0.3.0 | timeout | note: LibCurl::Easy — not in the zef index, resolved from the REA archive |
| 2022-10-27 | Git::File::History | 0.0.7 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 59: Error while compiling module Git::File::History (li … |
| 2022-10-26 | Random::Choice | 0.0.9 | self-fail | FAILED: 01-basic.t \| # Failed test at 01-basic.t line 5 |
| 2022-10-19 | Game::Sudoku | 1.1.4 | self-fail | FAILED: 02-create-and-display.t \| No matching multi candidate for method BUILD |
| 2022-10-19 | Intl::UserLanguage | 0.4.0 | dep-fail | User::Language |
| 2022-10-19 | Range::SetOps | 0.0.4 | self-fail | FAILED: 02-elem.t \| # Failed test 'ok 5 (elem) Set(1 2 3 4.5 5.5)' at 02-elem.t line 22 |
| 2022-10-19 | Test::HTTP::Server | 0.5.2 | self-fail | FAILED: 03-empty-folder.rakutest \| No such method 'recv' for invocant of type 'Any' |
| 2022-10-19 | Trait::Env | 1.1.2 | self-fail | FAILED: 02-env-only.rakutest \| Type check failed in binding to parameter '%env' |
| 2022-10-17 | UserTimezone | 0.3.1 | self-fail | FAILED: 01-override.t \| Undefined routine 'override-user-timezone' |
| 2022-10-16 | XDG::GuaranteedResources | 2.0.0 | self-fail | FAILED: 01-basic.rakutest \| The spawned command 'tree /private.test_88398' exited unsuccessfully (exit code:  … |
| 2022-10-06 | Clu | 1.0.1 | dep-fail | DB |
| 2022-10-01 | UUID::V4 | 1.0.0 | self-fail | FAILED: 01-uuid-v4.rakutest \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of  … |
| 2022-09-29 | IP::Addr | 0.0.7 | self-fail | FAILED: 010-ipv4-grammar.rakutest \| ===SORRY!=== Parse error at line 180: Error while compiling module IP::Ad … |
| 2022-09-26 | CLI::Help | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 12: expected } (got '') |
| 2022-09-18 | Int::polydiv | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test at 01-basic.rakutest line 13 |
| 2022-09-16 | App::tmeta | 0.1.1 | self-fail | FAILED: 01-basic.t \| # Failed test 'The module can be use-d ok: tmeta::commands' at 01-basic.t line 7 |
| 2022-09-16 | FindBin-libs | 0 | other | no META6.json in FindBin-libs's archive |
| 2022-09-15 | LWP::Simple | 0.109 | timeout | LWP::Simple |
| 2022-09-11 | Data::Record | 1.0.2 | dep-fail | annotations |
| 2022-09-09 | Getopt::Advance | 1.2.0 | dep-fail | Terminal::Table |
| 2022-09-09 | Text::CSV::LibCSV | 0.0.3 | build-fail |  |
| 2022-09-09 | pack6 | 0.4 | dep-fail | Terminal::Table |
| 2022-08-31 | IO::Stem | 0.0.2 | self-fail | FAILED: class.t \| # Failed test 'Basic IO::Path instantiation' at class.t line 25 |
| 2022-08-26 | Mac::Applications::List | 0.0.14 | self-fail | FAILED: basic.rakutest \| No such method 'osname' for invocant of type 'VM' |
| 2022-08-26 | annotations | 0.1.0 | self-fail | FAILED: 01-containers.rakutest \| ===SORRY!=== Parse error at line 28: Error while compiling module annotation … |
| 2022-08-23 | Proc::Easier | 0.0.5 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'creates object using simplified interface' at 01-basic.rakutest li … |
| 2022-08-14 | Cookie::Jar | 0.1.3 | self-fail | FAILED: add.t \| # Failed test 'simple key=value' at add.t line 329 |
| 2022-08-13 | Data::UkraineWar::MoD | 0.0.5 | dep-fail | ake |
| 2022-08-11 | SelectiveImporting | 0.1.3 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 86: Confused (got ',') |
| 2022-08-10 | HarfBuzz::Font::FreeType | 0.0.10 | self-fail | FAILED: basic.t \| Useless use of () in sink context (line 22) |
| 2022-08-10 | URI::Query::FromHash | 0.0.2 | self-fail | FAILED: basic.t \| Variable '$hash' is not declared |
| 2022-08-08 | Audio::Encode::LameMP3 | 0.0.15 | dep-fail | Audio::Taglib::Simple |
| 2022-08-08 | CheckSocket | 0.0.10 | timeout | CheckSocket |
| 2022-08-08 | Cro::HTTP::BodyParser::JSONClass | 0.0.3 | dep-fail | Cro::HTTP::Test |
| 2022-08-08 | Cro::HTTP::BodySerializerJSONClass | 0.0.3 | self-fail | FAILED: 020-serialize.t \| Undefined routine 'nqp::objprimspec' |
| 2022-08-08 | Crypt::Libcrypt | 0.1.3 | self-fail | FAILED: 020-crypt.t \| Cannot load native library 'crypt 1': dlopen(crypt 1, 0x0009): tried: 'crypt 1' (no suc … |
| 2022-08-08 | CustomImporting | 0.0.5 | self-fail | FAILED: 01-import.rakutest \| ===SORRY!=== Parse error at line 23: expected ) (got 'is') |
| 2022-08-08 | Doublephone | 0.1.3 | self-fail | FAILED: 020-words.t \| # Failed test ' is rendered to ' at 020-words.t line 19 |
| 2022-08-08 | Sys::Lastlog | 0.1.4 | dep-fail | System::Passwd |
| 2022-08-07 | AWS::SNS::Notification | 0.0.3 | self-fail | FAILED: 020-simple.t \| Undefined routine 'nqp::objprimspec' |
| 2022-08-07 | Audio::PortMIDI | 0.0.12 | self-fail | FAILED: 020-basic.t \| # Failed test 'create new Audio::PortMIDI object' at 020-basic.t line 14 |
| 2022-08-07 | Crypt::AnyPasswordHash | 0.1.3 | dep-fail | Crypt::Libcrypt |
| 2022-08-07 | Crypt::SodiumScrypt | 0.0.5 | self-fail | FAILED: 030-hash.t \| Cannot load native library 'sodium': dlopen(sodium, 0x0009): tried: 'sodium' (no such fi … |
| 2022-08-07 | EventSource::Server | 0.0.11 | timeout | EventSource::Server |
| 2022-08-07 | FastCGI::NativeCall::PSGI | 0.0.6 | self-fail | FAILED: 010-basic.t \| # Failed test 'old API' at 010-basic.t line 29 |
| 2022-08-07 | Igo | 0.0.8 | dep-fail | Oyatul |
| 2022-08-07 | Ikoko | 0.0.3 | dep-fail | WebService::AWS::Auth::V4 |
| 2022-08-07 | Kivuli | 0.0.4 | self-fail | FAILED: 020-creds.t \| Undefined routine 'nqp::objprimspec' |
| 2022-08-07 | Lumberjack::Config::JSON | 0.0.3 | self-fail | FAILED: 010-basic.t \| Undefined routine 'nqp::objprimspec' |
| 2022-08-07 | Lumberjack::Dispatcher::EventSource | 0.0.4 | timeout | EventSource::Server |
| 2022-08-07 | Printing::Jdf | 0.1.3 | self-fail | FAILED: 01-jdf.t \| Undefined routine 'Printing::Jdf::mm' |
| 2022-08-07 | Staticish | 0.0.9 | self-fail | FAILED: 020-test.t \| # Failed test 'called as a class method got the attribute' at 020-test.t line 12 |
| 2022-08-07 | Tinky::Declare | 0.0.2 | self-fail | FAILED: 010-use.t \| # Failed test 'The module can be use-d ok: Tinky::Declare' at 010-use.t line 7 |
| 2022-08-07 | UNIX::Privileges | 0.1.6 | self-fail | FAILED: 020-noroot.t \| fatal: could not get user info: no such user |
| 2022-08-07 | Util::Uuencode | 0.0.3 | self-fail | FAILED: 010-encode-decode.t \| # Failed test 'uuencode (binary file)' at 010-encode-decode.t line 65 |
| 2022-08-07 | WebService::Soundcloud | 0.0.10 | self-fail | FAILED: 020-me.t \| Undefined routine 'nqp::objprimspec' |
| 2022-08-02 | Test::ContainerizedService | 0.2 | dep-fail | Dev::ContainerizedService |
| 2022-08-01 | inode-dev-devtype | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| No such method 'FILETEST-E' for invocant of type 'Rakudo::Internals' |
| 2022-07-23 | Curry | 0.2.1 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: Curry:auth<zef:CIAvash>' at 00-load.rak … |
| 2022-07-23 | PatternMatching | 0.1.2 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: PatternMatching:auth<zef:CIAvash>' at 0 … |
| 2022-07-23 | Pod::Contents | 0.1.1 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: Pod::Contents:auth<zef:CIAvash>' at 00- … |
| 2022-07-23 | Sway::Config | 0.2.2 | dep-fail | Test::Run |
| 2022-07-23 | T | 0.1.2 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: T:auth<zef:CIAvash>' at 00-load.rakutes … |
| 2022-07-23 | Test::Run | 0.2.3 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: Test::Run:auth<zef:CIAvash>' at 00-load … |
| 2022-07-19 | CWT-Repository-Hash | 0.0.5 | self-fail | FAILED: 00-sanity.rakutest \| # Failed test 'The module can be use-d ok: Cro::WebApp::Template::Repository::Ha … |
| 2022-07-11 | BinaryHeap | 0.0.7 | self-fail | FAILED: 01-binaryheap.rakutest \| ===SORRY!=== Parse error at line 104: Error while compiling module BinaryHea … |
| 2022-07-11 | JSON-CSV | 0.0.1 | dep-fail | Module::Pod |
| 2022-07-07 | Tree::Binary | 0.0.7 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: Tree::Binary' at 01-basic.rakutest lin … |
| 2022-07-06 | FeiShuBot | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 71: Error while compiling module Digest (line 71 … |
| 2022-07-02 | Doc::Executable | 0.0.3 | dep-fail | Menu::Simple |
| 2022-07-02 | TimeUnit | 1.3.1 | self-fail | FAILED: 01-usage.rakutest \| # Failed test '===' at 01-usage.rakutest line 94 |
| 2022-07-01 | Menu::Simple | 0.17 | self-fail | FAILED: 00-sanity.rakutest \| # Failed test 'has display method' at 00-sanity.rakutest line 11 |
| 2022-06-29 | W3C::DOM | 0.0.3 | self-fail | FAILED: basic.t \| Method 'appendChild' must be implemented by Dtd because it is required by roles: W3C::DOM:: … |
| 2022-06-27 | Distribution::Resources::Menu | 0.0.4 | dep-fail | Menu::Simple |
| 2022-06-24 | Acme::BaseCJK | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| Cannot resolve caller comb(Any:U); the invocant is a type object, not an instance |
| 2022-06-22 | CSS::Module::CSS3::Selectors | 0.0.6 | self-fail | FAILED: 00basic.t \| ===SORRY!=== Parse error at line 26: expected } (got '') |
| 2022-06-10 | WebService::GitHub | 0.2.2 | dep-fail | OO::Monitors |
| 2022-06-09 | sortuk | 0.0.2 | self-fail | FAILED: 01-basic.t \| Target is not assignable |
| 2022-06-06 | SortUk | 0.0.1 | dep-fail | sortuk |
| 2022-06-04 | Math::Trig | 0.5.1 | self-fail | FAILED: 01-basic.t \| Undefined routine 'import' |
| 2022-06-01 | Pretty::Table | 0.0.4 | self-fail | FAILED: pretty-table-basic.t \| # Failed test 'Basic setup with title' at pretty-table-basic.t line 862 |
| 2022-05-29 | Grammar::Profiler::Simple | 0.05 | self-fail | FAILED: csv.rakutest \| Undefined routine 'n' |
| 2022-05-29 | HTTP::Roles | 0.2.3 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'Class must implement methods from role' at 00-use.rakutest line 13 |
| 2022-05-29 | HTTP::Server::Async | 0.2 | self-fail | FAILED: 01-request.rakutest \| No such method 'host' for invocant of type 'IO::Socket::INET' |
| 2022-05-29 | Statistics::OutlierIdentifiers | 0.1.0 | self-fail | FAILED: 01-basic-usage.rakutest \| ===SORRY!=== Parse error at line 71: Error while compiling module Stats (li … |
| 2022-05-28 | Parameterizable | 0.0.3 | self-fail | FAILED: parameterize.rakutest \| # Failed test 'my \answer = Ultimate[42]' at parameterize.rakutest line 19 |
| 2022-05-28 | System::Passwd | 0.1 | self-fail | FAILED: Passwd.rakutest \| This module is not compatible with the operating system macos |
| 2022-05-26 | Grammar::Common | 0.3.2 | self-fail | FAILED: 02-expression-prefix.rakutest \| # Failed test at 02-expression-prefix.rakutest line 119 |
| 2022-05-25 | IO::CatHandle::AutoLines | 1.1 | self-fail | FAILED: 01-CatHandle.rakutest \| ===SORRY!=== Parse error at line 49: expected ) (got ':') |
| 2022-05-25 | LN | 1.1 | dep-fail | IO::CatHandle::AutoLines |
| 2022-05-25 | Lingua::EN::Stem::Porter | 1.2 | self-fail | FAILED: 01-wordlist.rakutest \| # Failed test 'The stem of abatements, abatemen, should be abat' at 01-wordlis … |
| 2022-05-24 | Subsets::IO | 1.1 | self-fail | FAILED: 00-basics.rakutest \| No such method 'new' for invocant of type 'Compiler' |
| 2022-05-24 | Test::Builder | 0.0.4 | self-fail | FAILED: 01-load.rakutest \| # Failed test 'Created object isn't existing global singleton' at 01-load.rakutest … |
| 2022-05-21 | Geo::Coordinates::UTM | 0.3.1 | self-fail | FAILED: 02-points.t \| Ellipsoid Bessel 1841 Nambia unknown. |
| 2022-05-21 | Subset::Helper | 1.1 | self-fail | FAILED: 01-subset.t \| ===SORRY!=== Parse error at line 7: unexpected operator in term position (got '>') |
| 2022-05-20 | Games::TauStation::DateTime | 1.1 | self-fail | FAILED: 01-basic.rakutest \| Invalid arguments to Mu.new. Use any valid DateTime.new arguments, a GCT time (e. … |
| 2022-05-20 | Proc::Q | 1.1 | self-fail | FAILED: 01-basic.rakutest \| Type check failed in binding to parameter '$batch'; expected UInt but got Bool (B … |
| 2022-05-20 | Trait::IO | 1.1 | self-fail | FAILED: 01-auto-close.rakutest \| |
| 2022-05-19 | Inline::Brainfuck | 1.1 | self-fail | FAILED: 00-use.rakutest \| # Failed test 'The module can be use-d ok: Inline::Brainfuck' at 00-use.rakutest li … |
| 2022-05-19 | WhereList | 1.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 14: expected ) (got 'where') |
| 2022-04-29 | Test::Describe | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 114: unexpected operator in term position (got ' … |
| 2022-04-29 | WebService::AWS::S3 | 0.0.8 | self-fail | FAILED: 02-s3.t \| Please set AWS_SECRET_ACCESS_KEY |
| 2022-04-22 | IO::Path::ChildSecure | 1.2 | self-fail | FAILED: 01-operation.rakutest \| # Failed test 'code returned a Failure' at 01-operation.rakutest line 49 |
| 2022-04-22 | Repository::Precomp::Cleanup | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: Repository::Precomp::Cleanup' at 01-ba … |
| 2022-04-17 | Lingua::StopwordsISO | 0.1.0 | self-fail | FAILED: 01-basic-workflow.rakutest \| Unknown language spec: Bulgarian |
| 2022-04-12 | Pofig | 1.0.0 | other | no META6.json in POSIX's archive |
| 2022-04-11 | Lingua::Stem::Bulgarian | 0.1.2 | self-fail | FAILED: 01-basic-use.rakutest \| Could not find Lingua::Stem::Bulgarian in: |
| 2022-04-01 | DBIish::Pool | 1.1.0 | self-fail | FAILED: mysql.t \| Can't determine actual Offset |
| 2022-04-01 | List::Divvy | 0.0.5 | self-fail | FAILED: 00-basic.t \| ===SORRY!=== Parse error at line 12: expected method name after '.' (got '³') |
| 2022-04-01 | RedFactory | 0.0.3 | dep-fail | Getopt::Long |
| 2022-03-19 | Games::Wordle | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| samewith is not in the dynamic scope of a dispatcher |
| 2022-03-17 | Linenoise | 0.1.2 | pass |  |
| 2022-03-15 | Karabiner::CompModGenerator | 0.0.20 | dep-fail | Template::Classic |
| 2022-03-13 | DateTime::TimeZone | 0.10.2 | self-fail | FAILED: timezone.rakutest \| Undefined routine 'where' |
| 2022-03-13 | IO::MiddleMan | 1.001004 | self-fail | FAILED: 07-errors.t \| # Failed test '.new cannot be called' at 07-errors.t line 11 |
| 2022-03-13 | Syslog::Parse | 0.0.3 | self-fail | FAILED: 01-basic.t \| # Failed test 'Who' at 01-basic.t line 9 |
| 2022-03-13 | Wikidata::API | 0.0.5 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 1: unexpected operator in term position (got '﻿') |
| 2022-03-11 | IO::Dir | 1.001004 | self-fail | FAILED: 01-operation.t \| Testo::Test::Result |
| 2022-03-07 | Manifest::StopWar | 0.0.9 | self-fail | FAILED: 01.t \| # Failed test 'Stop this war 🙏' at 01.t line 15 |
| 2022-03-04 | Algorithm::Evolutionary::Simple | 0.0.8 | self-fail | FAILED: 00-functions.t \| Undefined routine 'þ' |
| 2022-03-02 | Datetime::Math | 0.6.2 | self-fail | FAILED: math.rakutest \| # Failed test 'DateTime <=> DateTime' at math.rakutest line 32 |
| 2022-02-27 | Operator::dB | 0.0.97 | self-fail | FAILED: 02-arithmetic.t \| No such method 'y' for invocant of type 'Num' |
| 2022-02-25 | App::Nopaste | 1.002 | dep-fail | Pastebin::Shadowcat |
| 2022-02-23 | List::Operator::DoublePlus | 0.1.0 | self-fail | FAILED: all.rakutest \| Useless use of constant integer 1 in sink context (line 14) |
| 2022-02-22 | HTTP::Server::Middleware::JSON | 0.0.3 | timeout | HTTP::Server::Middleware::JSON |
| 2022-02-19 | HexDump::Tiny | 0.6 | self-fail | FAILED: 00-basic.rakutest \| |
| 2022-02-18 | IO::Notification::Recursive | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| No such method 'watch' for invocant of type 'IO::Path' |
| 2022-02-17 | PSpec | 4.0.1 | self-fail | FAILED: basic.rakutest \| Undefined routine 'xxx' |
| 2022-02-14 | Pythonic::Str | 1.1 | self-fail | FAILED: 01-operation.rakutest \| Index out of range. Is: 3, should be in 0..0 |
| 2022-02-12 | Test::Script::Output | 0.1.0 | self-fail | FAILED: 01-basic.t \| # Failed test 'runnable output is OK' at 01-basic.t line 19 |
| 2022-02-10 | Auth::SCRAM::Async | 0.0.1 | self-fail | FAILED: 01-client.rakutest \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of t … |
| 2022-02-09 | Point | 1.2.1 | self-fail | FAILED: 01-basic.t \| No such method '!SET-SELF' for invocant of type 'Array' |
| 2022-02-03 | HarfBuzz::Font::Cairo | 0.0.2 | dep-fail | HarfBuzz::Shaper::Cairo |
| 2022-01-22 | MetamodelX::Dataclass | 0.0.2 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 14: Error while compiling module MetamodelX::Dataclass  … |
| 2022-01-21 | Spreadsheet::ODS | 0.1 | dep-fail | Getopt::Long |
| 2022-01-16 | Pleroma | 0.1.2 | dep-fail | JsonC |
| 2022-01-13 | Unicode::PRECIS | 0.5.2 | self-fail | FAILED: 100-precis.t \| # Failed test 'Test Identifier case mapped profile' at 100-precis.t line 30 |
| 2022-01-12 | Ecosystem::Archive | 0.0.5 | dep-fail | String::Utils |
| 2022-01-07 | Env::File | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| ===WARNING=== Module Env::File EXPORT failed: Variable '$val' is not declared |
| 2022-01-04 | GitHub::Actions | 0.0.1 | self-fail | FAILED: 00-meta.t \| # github source https://github.com/JJ/raku-github-actions needs to end in .git or be a pr … |
| 2022-01-02 | Transit::Network | 0.0.4 | dep-fail | Text::ShellWords |
| 2021-12-15 | AI::FANN | 0.2.0 | self-fail | FAILED: creation.t \| Cannot load native library 'fann': dlopen(fann, 0x0009): tried: 'fann' (no such file), ' … |
| 2021-12-14 | Whateverable | 1.0.11 | timeout | IO::Socket::Async::SSL |
| 2021-12-11 | Date::YearDay | 0.0.2 | self-fail | FAILED: 01-test-new.t \| # Failed test 'The object is-a 'Date::YearDay'' at 01-test-new.t line 22 |
| 2021-12-11 | Node::Ethereum::RLP | 0.0.15 | self-fail | FAILED: 01.t \| ===SORRY!=== Parse error at line 91: Error while compiling module Node::Ethereum::RLP (line 91 … |
| 2021-12-10 | GNU::Time | 0.0.2 | self-fail | FAILED: 030-process-time.t \| # Failed test at 030-process-time.t line 67 |
| 2021-12-08 | Acme::Advent::Highlighter | 1.003 | dep-fail | DOM::Tiny |
| 2021-12-08 | Text::Markdown | 1.1.1 | self-fail | FAILED: parse-link-with-code.t \| ===SORRY!=== Parse error at line 19: Ending delimiter TEXT not found for her … |
| 2021-12-06 | Modf | 0.0.4 | self-fail | FAILED: modf2.t \| # Failed test 'modf '-0o10', real: 0' at modf2.t line 21 |
| 2021-12-06 | Node::Ethereum::Keccak256::Native | 0.0.7 | self-fail | FAILED: 01.t \| Variable @.hash used where no 'self' is available |
| 2021-12-03 | Hash::File | 1.0 | self-fail | FAILED: 00-sanity.t \| only Windows and Linux are supported at the moment |
| 2021-12-01 | LogP6::Writer::StackDriver | 1.0.1 | dep-fail | LogP6 |
| 2021-11-28 | Rakudo::Perl6::Parsing | 1.0.1 | self-fail | FAILED: basic.t \| Could not find Perl6::Grammar in: |
| 2021-11-25 | fornax | 0.2.0 | self-fail | FAILED: 00-basic.rakutest \| # Failed test 'The module can be use-d ok: Fornax::CLI' at 00-basic.rakutest line … |
| 2021-11-23 | Text::Diff | 1.0.5 | self-fail | FAILED: general.t \| # Failed test 'Table, 1 context line' at general.t line 26 |
| 2021-11-11 | RPG::Base | 0.0.11 | self-fail | FAILED: 04-container.t \| # Failed test 'backpack contains flask recursively' at 04-container.t line 67 |
| 2021-11-10 | Cro::RPC::JSON | 0.1.6 | dep-fail | Cro::HTTP::Test |
| 2021-11-08 | PDF::Font::Loader::CSS | 0.0.1 | dep-fail | Getopt::Long |
| 2021-10-31 | KHPH | 0.2.0 | dep-fail | Getopt::Long |
| 2021-10-16 | Learn::Raku::With | 0.0.1 | timeout | Cro::TLS |
| 2021-10-12 | Protobuf | 0.0.2 | dep-fail | Grammar::PrettyErrors |
| 2021-10-08 | OO::Plugin | 0.0.8 | dep-fail | Async::Workers |
| 2021-10-07 | Concurrent::PChannel | 0.0.4 | self-fail | FAILED: 01-basic.rakutest \| ===SORRY!=== Parse error at line 31: Error while compiling module Test::Async (li … |
| 2021-10-06 | Curlie | 0.0.7 | self-fail | FAILED: 04-mock.rakutest \| callsame is not in the dynamic scope of a dispatcher |
| 2021-09-25 | AI::Agent | 0.2.10 | self-fail | FAILED: make-agent.t \| Target is not assignable |
| 2021-09-25 | Trait::Traced | 1.1.1 | dep-fail | Concurrent::Queue |
| 2021-09-17 | Sustenance | 0.0.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 67: Error while compiling module Crane::Transform (line … |
| 2021-09-10 | EERPG | 0.0.2 | dep-fail | WriteOnceHash |
| 2021-09-09 | Acme::Cow | 0.0.5 | self-fail | FAILED: balloon.t \| # Failed test at balloon.t line 15 |
| 2021-09-09 | Acme::Don't | 0.0.3 | other |  |
| 2021-09-09 | Hash::Util | 0.0.5 | self-fail | FAILED: 02-basic.t \| # Failed test 'is &all_ref_keys imported?' at 02-basic.t line 34 |
| 2021-09-09 | List::AllUtils | 0.0.6 | dep-fail | List::UtilsBy |
| 2021-09-09 | List::SomeUtils | 0.0.8 | dep-fail | List::MoreUtils |
| 2021-09-09 | List::Util | 0.0.10 | self-fail | FAILED: pair.t \| ===WARNING=== Module List::Util EXPORT failed: List::Util doesn't know how to export: pairgr … |
| 2021-09-09 | Net::netent | 0.0.5 | dep-fail | P5getnetbyname |
| 2021-09-09 | Net::protoent | 0.0.3 | dep-fail | P5getprotobyname |
| 2021-09-09 | Net::servent | 0.0.3 | dep-fail | P5getservbyname |
| 2021-09-09 | P5__FILE__ | 0.0.6 | self-fail | FAILED: 01-basic.t \| # Failed test 'is __PACKAGE__ imported?' at 01-basic.t line 7 |
| 2021-09-09 | P5chdir | 0.0.9 | self-fail | FAILED: 01-basic.t \| Too many levels of recursion |
| 2021-09-09 | P5each | 0.0.8 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &each externally NOT accessible?' at 01-basic.t line 8 |
| 2021-09-09 | P5fileno | 0.0.6 | self-fail | FAILED: 01-basic.t \| No such method 'opened' for invocant of type 'FileHandle' |
| 2021-09-09 | P5getpriority | 0.0.8 | self-fail | FAILED: getpriority.t \| Cannot find native symbol '_getpriority' |
| 2021-09-09 | P5index | 0.0.7 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &index externally NOT accessible?' at 01-basic.t line 8 |
| 2021-09-09 | P5seek | 0.0.5 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &seek externally NOT accessible?' at 01-basic.t line 8 |
| 2021-09-09 | P5sleep | 0.0.10 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &sleep externally NOT accessible?' at 01-basic.t line 8 |
| 2021-09-09 | P5study | 0.0.6 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &study externally NOT accessible?' at 01-basic.t line 8 |
| 2021-09-09 | P5substr | 0.0.7 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &substr externally NOT accessible?' at 01-basic.t line 8 |
| 2021-09-09 | P5times | 0.0.10 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &times externally NOT accessible?' at 01-basic.t line 9 |
| 2021-09-09 | Scalar::Util | 0.0.10 | self-fail | FAILED: 01-basic.t \| # Failed test 'is &blessed NOT imported?' at 01-basic.t line 14 |
| 2021-09-09 | Time::gmtime | 0.0.8 | dep-fail | P5localtime |
| 2021-09-09 | Time::localtime | 0.0.6 | dep-fail | P5localtime |
| 2021-09-09 | User::grent | 0.0.4 | dep-fail | P5getgrnam |
| 2021-09-09 | User::pwent | 0.0.4 | dep-fail | P5getpwnam |
| 2021-09-09 | subs | 0.0.4 | self-fail | FAILED: 01-basic.t \| # Failed test 'did the executing stub foo die' at 01-basic.t line 11 |
| 2021-09-04 | Algorithm::SpiralMatrix | 0.6.0 | self-fail | FAILED: line.rakutest \| # Failed test 'distance:clockwise line 11x3' at line.rakutest line 14 |
| 2021-09-04 | SP6 | 0.2.1 | self-fail | FAILED: 02-base-line.rakutest \| Error running template 'base-line.sp6': |
| 2021-09-03 | Game::Quest | 0.1.5 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: Game::Quest::Quest' at 00-load.t line 12 |
| 2021-08-26 | Matrix::Client | 0.7.0 | self-fail | FAILED: 10-use.t \| # Failed test 'The module can be use-d ok: Matrix::Client' at 10-use.t line 4 |
| 2021-08-25 | META6::bin | 1.0.0 | dep-fail | Git::Config |
| 2021-08-22 | ASN::BER | 0.7.3 | self-fail | FAILED: 00-sanity.t \| Cannot invoke non-Callable value of type Nil |
| 2021-08-22 | ASN::META | 0.5.3 | dep-fail | Type::EnumHOW |
| 2021-08-22 | colomon::App::TagTools | 0.5.0 | dep-fail | Audio::Taglib::Simple |
| 2021-08-20 | IO::Blob | 0.0.1 | self-fail | FAILED: 010_basic.t \| # Failed test 'Test for seek() and read()' at 010_basic.t line 172 |
| 2021-08-19 | taurus | 0.1.1 | self-fail | FAILED: 00-basic.rakutest \| # Failed test 'The module can be use-d ok: Taurus::CLI' at 00-basic.rakutest line … |
| 2021-08-18 | Timer::Stopwatch | 0.1.0 | dep-fail | Timer::Breakable |
| 2021-08-10 | BTree | 0.0.2 | self-fail | FAILED: 01-basic.rakutest \| # Failed test 'The module can be use-d ok: BTree' at 01-basic.rakutest line 2 |
| 2021-08-10 | Intl::Format::Unit | 0.2.0 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 153: Error while compiling module Intl::CLDR::Types::E … |
| 2021-08-05 | Hematite | 0.10.0 | dep-fail | IO::Blob |
| 2021-08-04 | Hematite::Middleware::Session | 0.1.0 | dep-fail | IO::Blob |
| 2021-08-01 | Proc::More | 0.4.0 | self-fail | FAILED: 030-process-time.t \| # Failed test at 030-process-time.t line 53 |
| 2021-07-29 | lacerta | 0.1.0 | dep-fail | Terminal::Spinners |
| 2021-07-26 | TOML::Thumb | 0.2 | self-fail | FAILED: invalid.t \| ===SORRY!=== Parse error at line 204: Error while compiling module TOML::Thumb (line 204) … |
| 2021-07-08 | Mac::Battery::Alerter | 0.0.4 | self-fail | FAILED: 00 - code-syntax-check.t \| # Failed test at 00 - code-syntax-check.t line 22 |
| 2021-07-07 | delete-old-until-size | 0.0.5 | self-fail | FAILED: 00-code-syntax-check.t \| # Failed test at 00-code-syntax-check.t line 22 |
| 2021-07-06 | Script::Hash | 0.0.3 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 71: Error while compiling module Digest (line 71): expe … |
| 2021-07-05 | FileSystem::Helpers | 0.1.2 | self-fail | FAILED: copy-dir.t \| Failed to open file file.txt: No such file or directory |
| 2021-07-02 | DB::Pg | 1.1 | dep-fail | LibUUID |
| 2021-06-29 | API::Discord | 0.6 | dep-fail | InterceptAllMethods |
| 2021-06-28 | Pod::Test::Code | 0.0.5 | dep-fail | Module::Pod |
| 2021-06-23 | Git::Config | 0.1.7 | dep-fail | LibGit2 |
| 2021-06-23 | JSON::Stream | 0.0.5 | dep-fail | Module::Pod |
| 2021-06-20 | CompUnit::Repository::Tar | 0.0.3 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: CompUnit::Repository::Tar' at 00-load.r … |
| 2021-06-17 | Cairo | 0.2.7 | self-fail | FAILED: matrix.t \| Class 'StreamClosure' cannot inherit from 'rw' because it is unknown |
| 2021-06-11 | Documentable | 2.0.1 | dep-fail | Terminal::Spinners |
| 2021-06-11 | Game::Decision | 0.1.10 | dep-fail | Game::Bayes |
| 2021-06-10 | Crypt::Passphrase | 0.0.1 | self-fail | FAILED: argon2.rakutest \| Could not find Crypt::Argon2 in: |
| 2021-06-09 | Game::Bayes | 0.2.10 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: Game::Bayes::Action' at 00-load.t line 12 |
| 2021-06-08 | FHIR | 0.0.1 | dep-fail | Getopt::Long |
| 2021-06-05 | Intl::Fluent | 0.8.1 | self-fail | FAILED: 01-Subtokens.t \| ===SORRY!=== Parse error at line 74: Error while compiling module Fluent::Classes (l … |
| 2021-06-03 | Game::Adventure | 0.1.1 | dep-fail | SDL2::Raw |
| 2021-06-02 | Solstice | 0.1.5 | dep-fail | SDL2::Raw |
| 2021-05-27 | Inline::Lua | * | self-fail | FAILED: 01-basics.t \| No such method 'value' for invocant of type 'Stash' |
| 2021-05-25 | antlia | 0.1.2 | dep-fail | Terminal::Table |
| 2021-05-19 | Archive::Tar::PP | v0.0.1 | self-fail | FAILED: 01-pax.t \| No such method 'push' for invocant of type 'Str' |
| 2021-05-14 | Template::Nest | 0.1.1 | self-fail | FAILED: 00_methods.t \| ===SORRY!=== Parse error at line 992: Error while compiling module Template::Nest (lin … |
| 2021-05-13 | Cro::APIToken | 0.1 | self-fail | FAILED: manager.rakutest \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of typ … |
| 2021-05-13 | Cro::APIToken::Store::Pg | 0.1 | dep-fail | LibUUID |
| 2021-05-13 | Cro::HTTP::Test | 0.8.1 | self-fail | FAILED: checks.t \| Type check failed in binding to parameter '$incoming'; expected Supply but got List (( )) |
| 2021-05-01 | TOML | 3 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: TOML' at 00-load.t line 4 |
| 2021-04-29 | DB::SQLite | 0.7 | dep-fail | DB |
| 2021-04-29 | caelum | 0.1.1 | dep-fail | Terminal::Table |
| 2021-04-18 | Actor | 0.0.1 | self-fail | FAILED: 01-basic.t \| This thread has no receiver |
| 2021-04-15 | HTTP::Parser | 0.0.2 | self-fail | FAILED: 01-request-parser.t \| # Failed test at 01-request-parser.t line 104 |
| 2021-04-12 | Slang::SQL | 0.1.5 | self-fail | FAILED: 01_basic.t \| ===SORRY!=== Parse error at line 45: Confused (got ',') |
| 2021-04-10 | IO::URing | 0.2.0 | dep-fail | NativeHelpers::iovec |
| 2021-04-09 | RPi::Device::ST7036 | 1.0.0 | self-fail | FAILED: 00-load.rakutest \| The attribute '$!v33' is required, but you did not provide a value for it. |
| 2021-04-08 | Perl6::Parser | 0.3.0 | self-fail | FAILED: 00-classes.t \| ===SORRY!=== Parse error at line 1771: Error while compiling module Perl6::Parser::Fac … |
| 2021-04-03 | Pakku::RecMan | 0.1.0 | dep-fail | Retry |
| 2021-04-01 | ASN::Grammar | 0.3.5 | self-fail | FAILED: 00-sanity.t \| # Failed test 'LDAP spec is parsed' at 00-sanity.t line 370 |
| 2021-04-01 | Config::Netrc | 1.0 | self-fail | FAILED: 10-testing.t \| Type Array does not support associative indexing |
| 2021-04-01 | Java::Generate | 1.0.0 | self-fail | FAILED: 01-hello-world.t \| ===SORRY!=== Parse error at line 37: Error while compiling module Java::Generate:: … |
| 2021-04-01 | Slang::Kazu | 1.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 25: Malformed numeric literal |
| 2021-04-01 | Text::Tabs | 1.0 | self-fail | FAILED: 10-sane.t \| # Failed test 'two tabs were converted to 8 spaces' at 10-sane.t line 34 |
| 2021-04-01 | deredere | 0.1.0 | dep-fail | Gumbo |
| 2021-03-31 | Blin | 1.0.1 | dep-fail | IO::Socket::Async::SSL |
| 2021-03-31 | Compress::Bzip2 | 0.4.1 | self-fail | FAILED: 10-basic.t \| 2 |
| 2021-03-24 | Shell::Piping | 0.11.0 | dep-fail | eigenstates |
| 2021-03-20 | ABC | 0.6.13 | self-fail | FAILED: 01-regexes.t \| # Failed test 'Got a match' at 01-regexes.t line 468 |
| 2021-03-14 | jmp | 10 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: JMP' at 00-load.t line 6 |
| 2021-03-12 | Uxmal | 1 | self-fail | FAILED: build-order.t \| No such method 'max_threads' for invocant of type 'Scheduler' |
| 2021-03-07 | Pakku::Meta | 0.0.4 | dep-fail | Pakku |
| 2021-03-07 | Pakku::RecMan::Client | 0.0.7 | dep-fail | Retry |
| 2021-03-07 | Pakku::Spec | 0.0.6 | dep-fail | Pakku |
| 2021-03-04 | Prometheus::Client | 0.4.0 | self-fail | FAILED: basic.t \| Cannot invoke non-Callable value of type Any |
| 2021-02-24 | Toi | 0.0.1 | self-fail | FAILED: 00-yaml-data.t \| No such method 'Date' for invocant of type 'Instant' |
| 2021-02-23 | LogP6 | 1.6.4 | self-fail | FAILED: 00-bad-config-file.t \| Undeclared name 'Level::trace' |
| 2021-02-16 | Intl::Number::Plural | 0.5.4 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 533: Error while compiling module Intl::LanguageTag::B … |
| 2021-02-13 | Game::Amazing | 0.9.05 | dep-build-fail | Termbox |
| 2021-02-13 | NativeHelpers::iovec | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| Class 'iovec' cannot inherit from 'rw' because it is unknown |
| 2021-02-12 | Pastebin::Pasteee | 0.1 | self-fail | FAILED: 00-sanity.t \| Undefined routine 'nqp::objprimspec' |
| 2021-01-29 | Log::Reader | 1 | self-fail | FAILED: 01-tests.t \| No such method 'parse' for invocant of type 'Any' |
| 2021-01-28 | Config::JSON | 1.001003 | self-fail | FAILED: 02-custom-file-in-use.t \| ===SORRY!=== Parse error at line 55: Error while compiling module Config::J … |
| 2021-01-28 | DateTime::Math | 0.6.0 | dep-fail | Datetime::Math |
| 2021-01-28 | File::LibMagic | 0.0.1 | self-fail | FAILED: basic.t \| Cannot load native library 'magic': dlopen(magic, 0x0009): tried: 'magic' (no such file), ' … |
| 2021-01-28 | Gumbo | 1.0 | self-fail | FAILED: 01_basic.t \| Variable $.original_name used where no 'self' is available |
| 2021-01-28 | IRC::Client::Plugin::Factoid | 1.001001 | self-fail | FAILED: 00-use.t \| # Failed test 'The module can be use-d ok: IRC::Client::Plugin::Factoid' at 00-use.t line  … |
| 2021-01-28 | Inline::Scheme::Guile | 0.1 | build-fail |  |
| 2021-01-28 | Reminders | 1.001004 | self-fail | FAILED: 01-basic.t \| Can't determine actual Offset |
| 2021-01-28 | TestML | 0.1 | self-fail | FAILED: arguments.t \| ===SORRY!=== Parse error at line 108: Error while compiling module TestML::Runner (line … |
| 2021-01-28 | WWW::vlc::Remote | 1.001008 | dep-fail | DOM::Tiny |
| 2021-01-28 | YAML | 0.1 | dep-fail | TestML |
| 2021-01-24 | Module2Rpm | 0.0.3 | self-fail | FAILED: 01-cro-client.t \| Promise broken |
| 2021-01-22 | Scheduler::DelayBetween | 1.3.2 | self-fail | FAILED: 00-test.t \| # Failed test 'did catch' at 00-test.t line 55 |
| 2021-01-20 | Template::Protone | 0.1.4 | self-fail | FAILED: 01-simple.t \| # Failed test 'Output is equal' at 01-simple.t line 11 |
| 2021-01-20 | Web::Scraper | 0.1.4 | self-fail | FAILED: 01_basic.t \| Undefined routine 'process' |
| 2021-01-19 | Bench | 0.2.1 | self-fail | FAILED: basic.t \| ===SORRY!=== Parse error at line 2: Error while compiling module Bench (line 2): Confused ( … |
| 2021-01-19 | Command::Despatch | 0.2 | self-fail | FAILED: test.t \| "test default with args" does not start with a recognised command |
| 2021-01-19 | DB::Xoos | 0.1.1 | dep-fail | DB |
| 2021-01-19 | DB::Xoos::MySQL | 0.1.1 | dep-fail | BitEnum |
| 2021-01-19 | DB::Xoos::Pg | 0.1.1 | dep-fail | DB |
| 2021-01-19 | DB::Xoos::SQLite | 0.1.1 | dep-fail | DB |
| 2021-01-19 | Event::Emitter | 1.0.3 | self-fail | FAILED: 02-supplytype.t \| |
| 2021-01-19 | Event::Emitter::Inter-Process | 1.0.1 | dep-fail | Event::Emitter |
| 2021-01-19 | Green | 0.1.4 | self-fail | FAILED: 00-use.t \| Type check failed in binding to parameter '$eval'; expected Bool but got Int (1) |
| 2021-01-19 | HTML::Parser::XML | 0.1.3 | self-fail | FAILED: 01_basic.t \| Undefined routine 'enum' |
| 2021-01-19 | HTTP::Server::Async::Plugins::Router::Simple | 0.1.2 | self-fail | FAILED: 01_basic.t \| Cannot resolve caller match(Any:U); the invocant is a type object, not an instance |
| 2021-01-19 | HTTP::Server::Logger | 0.1.4 | self-fail | FAILED: 00-use.t \| No such method 'bytes' for invocant of type '<anon\|2>' |
| 2021-01-19 | HTTP::Server::Router | 0.1.7 | other | no META6.json in HTTP::Server's archive |
| 2021-01-19 | HTTP::Server::Router::YAML | 0.0.1 | other | no META6.json in HTTP::Server's archive |
| 2021-01-19 | Hiker | 0.1.3 | other | no META6.json in HTTP::Server's archive |
| 2021-01-19 | Module::Does | 0.1.2 | self-fail | FAILED: 00-test.t \| callsame is not in the dynamic scope of a dispatcher |
| 2021-01-19 | Mux | 0.0.4 | self-fail | FAILED: 01-full.t \| No such method 'max_threads' for invocant of type 'Scheduler' |
| 2021-01-19 | Operator::feq | 0.1.2 | self-fail | FAILED: basic.t \| Target is not assignable |
| 2021-01-19 | Pluggable | 0.6 | self-fail | FAILED: 01-oop.t \| # Failed test at 01-oop.t line 18 |
| 2021-01-19 | YAML::Parser::LibYAML | 0.0.6 | build-fail |  |
| 2021-01-19 | flow | 0.0.6 | self-fail | FAILED: 00-Test.t \| |
| 2021-01-17 | Module2Rpm::Spec | 0.0.2 | dep-fail | Module2Rpm |
| 2021-01-12 | JsonC | 0.0.7 | self-fail | FAILED: 00-basic.t \| No such method 'fail' for invocant of type 'Failure' |
| 2021-01-10 | IntlFormatNumber | 0.1 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 533: Error while compiling module Intl::LanguageTag::B … |
| 2021-01-04 | PDF::Writer | 0.0.6 | dep-fail | Getopt::Long |
| 2021-01-03 | Hyperscript | 0.10.0 | self-fail | FAILED: 00-first-element.rakutest \| Type 'Style' is not declared |
| 2020-12-31 | IntlPromptYesNo | 0.1 | self-fail | FAILED: 00-sanity.rakutest \| ===SORRY!=== Parse error at line 533: Error while compiling module Intl::Languag … |
| 2020-12-29 | IO::Handle::Rollover | 1.3.1 | self-fail | FAILED: history-order.t \| Cannot resolve caller open(); no matching multi candidate |
| 2020-12-29 | Stache | 0.2.0 | self-fail | FAILED: app.t \| Could not find META6::Query in: |
| 2020-12-28 | Bio | 1.0 | self-fail | FAILED: sam.t \| ===SORRY!=== Error while compiling sam.t |
| 2020-12-28 | Method::Modifiers | 0.1.0 | self-fail | FAILED: 01-functions.t \| # Failed test 'wrapper was called' at 01-functions.t line 17 |
| 2020-12-14 | Astro::Almanac | 0.0.1 | dep-fail | Inline::Perl5 |
| 2020-12-08 | Debug::Transput | 0.1.1 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 4: Error while compiling module Debug::Transput (line  … |
| 2020-11-27 | DispatchMap | 0.2.3 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 114: Error while compiling module DispatchMap (line 114 … |
| 2020-11-26 | Pod::Parser | 0.1.0 | self-fail | FAILED: 01-parse.t \| # Failed test 'part 0 - content' at 01-parse.t line 36 |
| 2020-11-25 | META6::Query | 0.0.0 | dep-fail | Stache |
| 2020-11-20 | Toaster | 1.001002 | dep-fail | WhereList |
| 2020-11-19 | DateTime::Utils | 0.0.1 | dep-fail | Datetime::Math |
| 2020-11-17 | Dist::Helper | 1.0.1 | self-fail | FAILED: 00-use.t \| # Failed test 'The module can be use-d ok: Dist::Helper::Template' at 00-use.t line 13 |
| 2020-11-15 | Base::Any | 0.1.2 | self-fail | FAILED: 04-imaginary.t \| Cannot resolve caller to-base(); no matching multi candidate |
| 2020-11-14 | Auth::SCRAM | 0.4.8.2 | dep-fail | Unicode::PRECIS |
| 2020-11-11 | Perl6::TypeGraph | 2.1.3 | self-fail | FAILED: 02-type-graph.t \| No such method 'actions' for invocant of type 'Match' |
| 2020-11-07 | Algorithm::LBFGS | 0.0.6 | self-fail | FAILED: 02-status.t \| # Failed test 'Given the default parameter and an optimizable objective function, then  … |
| 2020-11-06 | Native::Exec | 0.2 | self-fail | FAILED: 01-basic.t \| Cannot load native library 'libc.so.6': dlopen(libc.so.6, 0x0009): tried: 'libc.so.6' (n … |
| 2020-11-02 | Terminal::Spinners | 1.6.0 | self-fail | FAILED: 02-types.t \| Cannot resolve caller chars(Any:U); the invocant is a type object, not an instance |
| 2020-10-31 | Pod::Utilities | 0.0.1 | self-fail | FAILED: 03-first-.t \| # Failed test 'Basic test' at 03-first-.t line 63 |
| 2020-10-29 | GPGME | 0.2 | dep-fail | BitEnum |
| 2020-10-29 | Gcrypt | 0.7 | self-fail | FAILED: 01-basic.t \| Cannot load native library 'gcrypt': dlopen(gcrypt, 0x0009): tried: 'gcrypt' (no such fi … |
| 2020-10-16 | License::Software | 0.3.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 198: Error while compiling module License::Software (li … |
| 2020-10-03 | Algorithm::AhoCorasick | 0.0.13 | self-fail | FAILED: 01-basic.t \| No such method 'new' for invocant of type 'Any' |
| 2020-10-01 | System::Stats::DISKUsage | 1.0.1 | self-fail | FAILED: 02-test.t \| |
| 2020-10-01 | System::Stats::MEMUsage | 1.0.0 | self-fail | FAILED: 02-test.t \| # Failed test 'Total Memory > 0' at 02-test.t line 11 |
| 2020-10-01 | Win32::DrivesAndTypes | 1.0.0 | self-fail | FAILED: 01-load.t \| # Failed test 'Kernel is win32' at 01-load.t line 11 |
| 2020-09-26 | XHTML::Writer | 0.1.1 | self-fail | FAILED: basic.t \| Target is not assignable |
| 2020-09-19 | Inline::Python | 0.5 | self-fail | FAILED: call.t \| No such method 'add_fallback' for invocant of type 'Mu' |
| 2020-09-09 | Math::Roman | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test at 01-basic.rakutest line 22 |
| 2020-09-06 | Pod::Weave | 0.0.2 | dep-fail | Pod::To::HTML |
| 2020-09-04 | Archive-Zip-SimpleZip | 0.037 | other | no META6.json in Archive-Zip-SimpleZip's archive |
| 2020-09-03 | TXN | 0.1.0 | dep-build-fail | TXN::Remarshal |
| 2020-09-03 | TXN::Parser | 0.1.0 | build-fail |  |
| 2020-09-03 | TXN::Remarshal | 0.1.0 | dep-build-fail | TXN::Parser |
| 2020-08-31 | JSONHound | 0.0.2 | dep-fail | JSON::Path |
| 2020-08-30 | Pod::To::BigPage | 0.5.2 | self-fail | FAILED: 000-functions.t \| Variable '$none-exclude' is not declared |
| 2020-08-29 | Fcntl | 0.0.2 | self-fail | FAILED: autoload.t \| # Failed test at autoload.t line 24 |
| 2020-08-26 | Proc::Async::Timeout | 0.1.2 | self-fail | FAILED: 020-basic.t \| # Failed test 'infinite timeout will not throw' at 020-basic.t line 15 |
| 2020-08-20 | LibUSB | 0.0.3 | self-fail | FAILED: 01-basic.t \| # Failed test 'Load shared libusb library' at 01-basic.t line 21 |
| 2020-08-18 | LibGit2 | 0.4 | self-fail | FAILED: 01-simple.t \| ===SORRY!=== Parse error at line 176: Error while compiling module Git::Index (line 176 … |
| 2020-08-11 | FileSystem::Capacity | 1.0.1 | self-fail | FAILED: 02-test.t \| # Failed test 'Size > 0' at 02-test.t line 16 |
| 2020-08-11 | Oddmuse6 | 0.0.5 | dep-fail | DOM::Tiny |
| 2020-08-08 | Opt::Handler | 0.0.3 | dep-fail | Getopt::Long |
| 2020-08-03 | Rakudo::Slippy::Semilist | 0.0.2 | self-fail | FAILED: 01-basic.t \| Unsupported prefix 'dimslip' |
| 2020-07-31 | Pod::To::Cached | 0.4.1 | self-fail | FAILED: 007-load-precompile.t \| No such method 'new' for invocant of type 'CompUnit::PrecompilationStore::Fil … |
| 2020-07-20 | App::pixel-pick | 0.6 | self-fail | FAILED: basic-00.t \| Needs to have an X11 widowing system available. |
| 2020-07-15 | Net::IP | 2.1.2 | dep-fail | Number::More |
| 2020-07-15 | kazmath | 0.0.1 | other | tar: Error exit delayed from previous errors. |
| 2020-07-13 | Bundle-Compress-Zlib | 2.094 | other | no META6.json in Bundle-Compress-Zlib's archive |
| 2020-07-13 | Bundle-IO-Compress-Bzip2 | 2.094 | other | no META6.json in Bundle-IO-Compress-Bzip2's archive |
| 2020-07-13 | Git::PurePerl | * | self-fail | FAILED: 01-basics.t \| Undefined routine 'where' |
| 2020-07-13 | IO-Compress | 2.094 | other | no META6.json in IO-Compress's archive |
| 2020-07-13 | IO-Compress-Lzf | 2.094 | other | no META6.json in IO-Compress-Lzf's archive |
| 2020-07-13 | IO-Compress-Lzma | 2.094 | other | no META6.json in IO-Compress-Lzma's archive |
| 2020-07-13 | IO-Compress-Lzop | 2.094 | other | no META6.json in IO-Compress-Lzop's archive |
| 2020-07-13 | Math::ThreeD | * | build-fail |  |
| 2020-07-12 | Smack | 0.5.2 | dep-fail | HTTP::Supply |
| 2020-07-12 | Text::ShellWords | 0.1.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 132: Couldn't find terminator } (corresponding { was at … |
| 2020-07-11 | Grammar-Common | 0.0.5 | self-fail | FAILED: 02-expression-prefix.t \| # Failed test at 02-expression-prefix.t line 118 |
| 2020-07-02 | DBDish::ODBC | 0.0.8 | self-fail | FAILED: 01-basic.t \| # Failed test 'Can install driver' at 01-basic.t line 13 |
| 2020-07-01 | Uzu | 0.3.6 | dep-fail | IO::Blob |
| 2020-06-30 | DBIish::Transaction | 1.0.4 | self-fail | FAILED: mysql.t \| Can't determine actual Offset |
| 2020-06-29 | Time::Crontab | 1.0.0 | self-fail | FAILED: crontab.t \| # Failed test '* * * * * matches 2026-08-24T02:23:00+03:00' at crontab.t line 73 |
| 2020-06-25 | raku-pod-from-cache | 0.1 | self-fail | FAILED: 10-cache.t \| # Failed test 'Instantiates with minimum parameters' at 10-cache.t line 14 |
| 2020-06-23 | Clifford | 6.1.9 | self-fail | FAILED: axioms.t \| ===SORRY!=== Parse error at line 20: Error while compiling module MultiVector (line 20): e … |
| 2020-06-23 | Spit | 0.0.31 | dep-fail | DispatchMap |
| 2020-06-20 | Algorithm::MinMaxHeap | 0.13.5 | self-fail | FAILED: 02-insert.t \| # Failed test at 02-insert.t line 31 |
| 2020-06-17 | Config::Parser::json | 1.0.0 | self-fail | FAILED: 01-read.t \| # Failed test 'Got first' at 01-read.t line 97 |
| 2020-06-12 | Cmark | 0.1 | self-fail | FAILED: 02-basic.t \| Cannot load native library 'cmark': dlopen(cmark, 0x0009): tried: 'cmark' (no such file) … |
| 2020-06-10 | Vikna | 0.0.3 | dep-fail | DB |
| 2020-06-06 | SDL2 | 0.0.3 | dep-fail | SDL2::Raw |
| 2020-06-05 | JS::Minify | 0.0.2 | self-fail | FAILED: 00-basics.t \| # Failed test 'The module can be use-d ok: JS::Minify' at 00-basics.t line 8 |
| 2020-06-03 | JSON::simd | 0.1 | build-fail |  |
| 2020-06-02 | DB | 0.5 | self-fail | FAILED: 01-connection.t \| # Failed test '1 Connection created' at 01-connection.t line 13 |
| 2020-05-27 | Radamsa | 0.0.1 | build-fail |  |
| 2020-05-21 | HTTP::Client | 0.0.3 | self-fail | FAILED: 01-get.t \| # Failed test 'Result was successful.' at 01-get.t line 149 |
| 2020-05-17 | CucumisSextus | 0.2 | self-fail | FAILED: 01-gherkin.rakutest \| # Failed test 'Parsing an existing file should not die' at 01-gherkin.rakutest  … |
| 2020-05-17 | Log::ZMQ | 0.0.5 | dep-fail | Net::ZMQ |
| 2020-05-17 | Net::Jupyter | 0.1.3 | dep-fail | Log::ZMQ |
| 2020-05-11 | Cro::HTTP::Session::Pg | 0.1 | dep-fail | LibUUID |
| 2020-05-11 | Trait::Enum | 0.1 | self-fail | FAILED: 01-basic.t \| No such method 'foo-type' for invocant of type 'Foo' |
| 2020-05-05 | Audio::OggVorbis | 0.0.1 | self-fail | FAILED: 01-ogg-use.t \| # Failed test 'can use Ogg.pm' at 01-ogg-use.t line 8 |
| 2020-05-05 | CCChart | 0.0.4 | dep-build-fail | CCLog |
| 2020-05-05 | CCLog | 0.0.6 | build-fail |  |
| 2020-05-04 | Algorithm::ZhangShasha | 0.0.3 | dep-fail | DOM::Tiny |
| 2020-04-28 | Numeric::Pack | 0.4.1 | self-fail | FAILED: 01-basic.t \| Type 'ByteOrder' is not declared |
| 2020-04-23 | BitEnum | 0.5 | self-fail | FAILED: 01-simple.t \| Variable '$prefix' is not declared |
| 2020-04-18 | Algorithm::NaiveBayes | 0.0.5 | self-fail | FAILED: 01-basic.t \| No matching multi candidate for method BUILD |
| 2020-04-18 | ProcStats | 0.3.1 | dep-fail | FindBin |
| 2020-04-14 | Cache::LRU | 0.1.0 | self-fail | FAILED: 00base.t \| # Failed test at 00base.t line 83 |
| 2020-04-12 | Imlib2 | 0.0.3 | build-fail |  |
| 2020-04-11 | Excel | 0.0.1 | dep-fail | Inline::Perl5 |
| 2020-04-11 | Excel::Text::Template | 0.0.2 | dep-fail | Inline::Perl5 |
| 2020-04-11 | Geo::Hash | 0.0.3 | self-fail | FAILED: 01-basic.t \| # Failed test at 01-basic.t line 25 |
| 2020-04-11 | Template::Classic | 0.0.3 | self-fail | FAILED: test.t \| Undefined routine 'qw' |
| 2020-04-10 | Gnome::Gtk3::Glade | 0.8.9.1 | dep-fail | Gnome::Gdk3 |
| 2020-04-07 | ArrayHash | 1.0.0 | self-fail | FAILED: array-hash.t \| ===SORRY!=== Parse error at line 18: Error while compiling module ArrayHash (line 18): … |
| 2020-04-05 | Termbox | 0.0.4 | build-fail |  |
| 2020-03-31 | Colorizable | 0.1.1 | self-fail | FAILED: basic.t \| # Failed test 'test color and text effect application' at basic.t line 8 |
| 2020-03-30 | Backtrace::AsHTML | 0.0.1 | self-fail | FAILED: 01-basic.t \| No such method 'new' for invocant of type 'Backtrace' |
| 2020-03-28 | Algorithm::LDA | 0.0.10 | self-fail | FAILED: 01-basic.t \| # Failed test 'Check if it could process a very short document. (just a smoke test)' at  … |
| 2020-03-26 | Crypt::CAST5 | 0.1.1 | self-fail | FAILED: 01-basic.t \| # Failed test '128 bit key test encrypts its plaintext with the given key properly' at 0 … |
| 2020-03-26 | Path::Router | 0.5.1 | self-fail | FAILED: 001_basic.t \| ===SORRY!=== Parse error at line 29: unexpected operator in term position (got '<=') |
| 2020-03-25 | Regex::FuzzyToken | 0.5.1 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 8: Error while compiling module Regex::FuzzyToken (lin … |
| 2020-03-20 | Draw2D::Furniture | 1.0.0 | dep-fail | Inline::Perl5 |
| 2020-03-20 | Net::Osc | 0.2.4 | self-fail | FAILED: basic.t \| Actions must be provided as a tuple of format: (Regex, Callable), recieved (Regex)! |
| 2020-03-17 | Crust | 0.0.1 | dep-fail | IO::Blob |
| 2020-03-09 | Carp | 0.1 | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 26: unexpected operator in term position (got ' ') |
| 2020-03-04 | SSL | 0.2.1 | self-fail | FAILED: basic.t \| # Failed test 'MD4' at basic.t line 14 |
| 2020-03-03 | Net::SMTP::Client::Async | 0.0.1 | dep-fail | Auth::SASL |
| 2020-02-27 | Cro::ZeroMQ | 0.8.3 | self-fail | FAILED: zeromq-client.t \| Cannot load native library 'zmq': dlopen(zmq, 0x0009): tried: 'zmq' (no such file), … |
| 2020-02-25 | Algorithm::GooglePolylineEncoding | 1.0.3 | self-fail | FAILED: 02-encode-number.rakutest \| # Failed test '-179.9832104 encodes to `~oia@' at 02-encode-number.rakute … |
| 2020-02-22 | Supply::Folds | 0.0.1 | self-fail | FAILED: 01-fold-and-scan.t \| # Failed test 'can fold supplies' at 01-fold-and-scan.t line 10 |
| 2020-02-17 | Text::VimColour | 0.5.0 | self-fail | FAILED: vim_colour.t \| Undefined routine 'where' |
| 2020-02-08 | CompUnit::DynamicLib | 0.2.1 | self-fail | FAILED: basic.t \| No such method 'use-repository' for invocant of type 'CompUnit::RepositoryRegistry' |
| 2020-02-08 | Getopt::ForClass | 0.3 | self-fail | FAILED: basic.t \| # Failed test 'should have three candidates' at basic.t line 26 |
| 2020-02-08 | HTTP::Supply | 0.5.0 | self-fail | FAILED: request-deprecated.t \| Type check failed in binding to parameter '$buf'; expected Buf but got Str ("p … |
| 2020-02-07 | CheckedSQL | 0.1 | self-fail | FAILED: 01-sigils.t \| ===WARNING=== Module CheckedSQL EXPORT failed: The attribute '$!by-name' is required, b … |
| 2020-02-06 | Auth::SASL | 0.1.1 | self-fail | FAILED: anonymous.t \| No matching multi candidate for method attempt-mechanisms |
| 2020-02-02 | App::Rakuman | 0.1.0 | dep-fail | Text::BorderedBlock |
| 2020-02-01 | Data::Selector | v1.02 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 403: Error while compiling module Data::Selector (line  … |
| 2020-01-28 | LogP6-Writer-Journald | 1.3.3 | dep-fail | LogP6 |
| 2020-01-26 | Scalar::History | 0.0.1 | self-fail | FAILED: 00-all.t \| # Failed test 'untyped' at 00-all.t line 15 |
| 2020-01-22 | Getopt::Std | 1.0.2 | dep-fail | Test::Deeply::Relaxed |
| 2020-01-22 | Shell::Capture | 0.2.4 | self-fail | FAILED: 02-nl-enc.rakutest \| # Failed test 'raku -e "print" said the right thing' at 02-nl-enc.rakutest line  … |
| 2020-01-22 | Test::Deeply::Relaxed | 0.1.3 | self-fail | FAILED: 05-cache.rakutest \| # Failed test 'non-cached - did not preserve the sequence' at 05-cache.rakutest l … |
| 2020-01-21 | FileSystem::Parent | 0.4.3 | dep-fail | FindBin |
| 2020-01-21 | FindBin | 0.6.3 | self-fail | FAILED: 01-FindBin.rakutest \| # Madness: 'FindBin' (01-FindBin.rakutest) |
| 2020-01-21 | FindBin::libs | 0.3.0 | dep-fail | FindBin |
| 2020-01-17 | Hydrate | * | self-fail | FAILED: 01-basic.t \| Extra attributes while building Inner |
| 2020-01-17 | Sprockets | * | self-fail | FAILED: file.t \| # Failed test 'Gets the correct content, and make sure an included file remembers where it i … |
| 2020-01-17 | Stats | 0.0.6 | self-fail | FAILED: correlation.t \| ===SORRY!=== Parse error at line 71: Error while compiling module Stats (line 71): ex … |
| 2020-01-12 | CI::Gen | 0.0.2 | dep-fail | Getopt::Long |
| 2020-01-09 | OLE::Storage_Lite | 0.0.2 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: OLE::Storage_Lite' at 00-load.t line 7 |
| 2020-01-06 | ScaleVec | 0.0.6 | self-fail | FAILED: ScaleVec.t \| # Failed test 'Pitch vector out matches pitch vector in' at ScaleVec.t line 78 |
| 2019-12-29 | Algorithm::HierarchicalPAM | 0.0.2 | self-fail | FAILED: 01-basic.t \| # Failed test 'Check if it could process a very short document. (just a smoke test)' at  … |
| 2019-12-25 | Parser::FreeXL::Native | 0.0.3 | self-fail | FAILED: 01-basic.t \| Undefined routine 'guess_library_name' |
| 2019-12-22 | RakuAdvent::WordPress | 0.0.2 | self-fail | FAILED: 01-wp-html.t \| The spawned command 'perl6 -Ilib ./bin/make-wp-input test.html' exited unsuccessfully  … |
| 2019-12-06 | Sanity | 0.0.1 | self-fail | FAILED: 01-basic.rakutest \| # Failed test at 01-basic.rakutest line 5 |
| 2019-12-02 | HTTP::Headers | 0.5.0 | self-fail | FAILED: content-type.t \| No matching multi candidate for method header |
| 2019-11-28 | Proc::Feed | 1.0.3 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 59: unexpected operator in term position (got '==>') |
| 2019-11-27 | DateTime::DST | 0.3 | self-fail | FAILED: is-dst.t \| Cannot find native symbol 'is_dst' |
| 2019-11-04 | Cro::SSL | 0.7 | self-fail | FAILED: ssl.t \| # Failed test 'Listening for connections once the Supply is tapped' at ssl.t line 39 |
| 2019-11-01 | CUID | 0.0.1 | self-fail | FAILED: cuid.t \| # Failed test 'CUID field specification -' at cuid.t line 12 |
| 2019-10-28 | ModelDB | 0.6.0 | self-fail | FAILED: basic.t \| Undefined routine 'model' |
| 2019-10-20 | App::ecogen | 0.0.8 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: App::ecogen' at 00-load.rakutest line 4 |
| 2019-10-20 | Failable | 0.1.2 | self-fail | FAILED: 01-basic.t \| Useless use of :_(True) in sink context (line 80) |
| 2019-10-17 | Operator::grandpa | 1.001002 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 33: unexpected operator in term position (got '==') |
| 2019-10-14 | Intl::BCP47 | 0.8.5 | dep-fail | Intl::LanguageTag |
| 2019-10-14 | SAT | 0.0.1 | self-fail | FAILED: 00-use.t \| # Failed test 'The module can be use-d ok: Test::SAT' at 00-use.t line 9 |
| 2019-10-14 | SAT::Solver::MiniSAT | 0.0.1 | dep-fail | SAT |
| 2019-10-06 | 6pm | 0.0.11 | self-fail | FAILED: 00-test-meta.t \| # Failed test 'META parses okay' at 00-test-meta.t line 86 |
| 2019-09-29 | LibZip | * | self-fail | FAILED: 01-basic.t \| Can't determine actual Offset |
| 2019-09-27 | Cofra | 0.2.0 | dep-fail | HTTP::Supply |
| 2019-09-16 | Symbol | 0.0.4 | self-fail | FAILED: basic.t \| Unrecognized regex adverb: :foo1 |
| 2019-09-12 | Compress::Zstd | 0.0.3 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 50: Error while compiling module Compress::Zstd (line 5 … |
| 2019-09-11 | Proxy::Watched | 0.0.2 | self-fail | FAILED: 01-basic.t \| # Failed test 'Type error when assigning string to watched-int' at 01-basic.t line 23 |
| 2019-08-29 | Perl6::Documentable | 3.0.3 | dep-fail | JSON::Fast |
| 2019-08-25 | Device::HIDAPI | 0.0.1 | build-fail |  |
| 2019-08-18 | Bailador | 0.0.19 | dep-fail | HTTP::Supply |
| 2019-08-11 | Module::Pod | 0.9.2 | self-fail | FAILED: export.t \| # Failed test 'With import, function is available' at export.t line 17 |
| 2019-08-11 | OEIS | 1.0.1 | self-fail | FAILED: 00-use.t \| # Failed test 'The module can be use-d ok: OEIS::Entry' at 00-use.t line 8 |
| 2019-08-09 | Supply::Timeout | 0.0.2 | self-fail | FAILED: 02-supply.t \| # Failed test 'Timeout not happened' at 02-supply.t line 13 |
| 2019-08-07 | App::Squashathons | 0.0.2 | self-fail | FAILED: 01-basic.t \| Undefined routine 'headers' |
| 2019-08-07 | Docker::API | 0.1 | dep-fail | BitEnum |
| 2019-08-01 | App::nm2perl6 | 0.0.3 | self-fail | FAILED: 10-gnu.t \| Variable '$x' is not declared |
| 2019-07-31 | DateTime::Parse | 0.9.1 | self-fail | FAILED: 01-asctime.t \| # Failed test 'Fri Mar 23 13:20:46 2018 UTC+3 parses' at 01-asctime.t line 48 |
| 2019-07-30 | Libui | 0.0.3 | build-fail |  |
| 2019-07-28 | Module::Loader | 0 | dep-fail | Module::Pod |
| 2019-07-20 | App::MoarVM::ConfprogCompiler | 0.0.8 | self-fail | FAILED: 01-example-programs.t \| ===SORRY!=== Parse error at line 36: Error while compiling module App::MoarVM … |
| 2019-07-16 | Matrix::Bot | 0.2.0 | dep-fail | Matrix::Client |
| 2019-07-16 | Matrix::Bot::Plugin::AutoAcceptInvites | 0.1.0 | dep-fail | Matrix::Client |
| 2019-07-10 | OpenAPI::Model | 1.0.4 | dep-fail | JSON::Pointer |
| 2019-07-10 | OpenAPI::Schema::Validate | 1.0.7 | dep-fail | JSON::Pointer |
| 2019-07-08 | Libarchive | 0.1 | dep-fail | BitEnum |
| 2019-07-01 | Digest::BubbleBabble | 0.0.3 | self-fail | FAILED: 01-basic.t \| # Failed test 'Encoding '' gives the correct fingerprint' at 01-basic.t line 18 |
| 2019-06-30 | sake | 0.0.2 | self-fail | FAILED: 00-original-task.t \| Stub code executed |
| 2019-06-27 | LMDB | 0.0.4 | self-fail | FAILED: 01-basic.t \| # Failed test 'The module can be use-d ok: LMDB' at 01-basic.t line 7 |
| 2019-06-24 | Type::EnumHOW | 0.0.2 | self-fail | FAILED: 01-basic.t \| Useless use of :delete(True) in sink context (line 47) |
| 2019-06-21 | Audio::Taglib::Simple | 0.0.8 | self-fail | FAILED: basic.t \| Cannot load native library 'tag_c 0': dlopen(tag_c 0, 0x0009): tried: 'tag_c 0' (no such fi … |
| 2019-06-18 | X11::Xlib::Raw | * | self-fail | FAILED: 01-sanity.t \| Can't determine actual Offset |
| 2019-06-13 | QM | 0.0.2 | dep-fail | Terminal::Table |
| 2019-06-10 | Path::Through | 0.0.1 | self-fail | FAILED: 01-basic.t \| Cannot resolve caller pop(); no matching multi candidate |
| 2019-06-07 | Lingua::EN::Numbers::Cardinal | 2.3.0 | self-fail | FAILED: 00-cardinal.t \| # Failed test 'Int list' at 00-cardinal.t line 7 |
| 2019-05-22 | Seq::Bounded | 1.0.1 | self-fail | FAILED: bounds.t \| # Failed test 'Test sequence created' at bounds.t line 5 |
| 2019-05-15 | Cro::HTTP::Session::Red | 0.0.2 | dep-fail | Getopt::Long |
| 2019-05-13 | Net::Telnet | 0.0.2 | self-fail | FAILED: 01-basic.t \| No such method 'new' for invocant of type 'Any' |
| 2019-05-03 | Git::Blame | 0.0.1 | dep-fail | LibGit2 |
| 2019-04-25 | Acme::Cow6 | 0.0.1 | self-fail | FAILED: balloon.t \| Variable '$cow' is not declared |
| 2019-04-15 | p6-RandomColor | v0.11 | self-fail | FAILED: 01-basic.t \| # Failed test 'Can use the RandomColor module' at 01-basic.t line 10 |
| 2019-04-13 | Pygments | 0.0.1 | dep-fail | Inline::Python |
| 2019-04-11 | XML::XPath | 0.9.3 | self-fail | FAILED: 01_simple_basic_find.t \| # Failed test 'found one node' at 01_simple_basic_find.t line 27 |
| 2019-04-06 | Acme::Polyglot::Levenshtein::Damerau | 0.1 | self-fail | FAILED: basic.t \| # Failed test at basic.t line 7 |
| 2019-04-05 | LibUUID | 0.5 | self-fail | FAILED: 01-basic.t \| Cannot load native library 'uuid': dlopen(uuid, 0x0009): tried: 'uuid' (no such file), ' … |
| 2019-03-26 | Net::HTTP | 0.0.9 | self-fail | FAILED: 00-load.rakutest \| # Failed test 'The module can be use-d ok: Net::HTTP::Transport' at 00-load.rakute … |
| 2019-03-22 | Serialize::Tiny | 1 | self-fail | FAILED: 01-test.t \| # Failed test 'Nested type is present' at 01-test.t line 65 |
| 2019-03-15 | AWS::Session | 0.8.0 | self-fail | FAILED: credentials-env.t \| # Failed test 'default-profile' at credentials-env.t line 375 |
| 2019-03-11 | Compress::Zlib | 1.1.0 | self-fail | FAILED: 02-stream.t \| # Failed test 'Get data when a chunk is deflated' at 02-stream.t line 146 |
| 2019-03-04 | Proc::InvokeEditor | 0.0.6 | self-fail | FAILED: 03-editors-method.t \| No matching multi candidate for method editors |
| 2019-02-24 | Perl6::Tidy | 0.0.7 | dep-fail | Perl6::Parser |
| 2019-02-23 | Calculator | 0.0.1 | self-fail | FAILED: 01-basic.t \| # Failed test 'No Calculator object (y < 0)' at 01-basic.t line 6 |
| 2019-02-14 | Perl6::Ecosystem | 0.0.3 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: Perl6::Ecosystem' at 00-load.t line 5 |
| 2019-02-12 | Math::Factorial::Operator | 0.1.2.0 | self-fail | FAILED: tests.t \| ===SORRY!=== Parse error at line 8: Confused (got '}') |
| 2019-02-12 | Text::Names | 0.0.2 | self-fail | FAILED: test.t \| Failed to open file /./resources/dist.male.first: no such file or directory |
| 2019-02-12 | ULID | 0.1.0 | self-fail | FAILED: basic.t \| # Failed test 'ulid-time matches expectation' at basic.t line 32 |
| 2019-02-11 | GraphQL | 0.6.1 | self-fail | FAILED: 01-parse-bad.t \| # Failed test 'Caught parse error' at 01-parse-bad.t line 24 |
| 2019-02-11 | GraphQL::Server | 0.1 | dep-fail | HTTP::Supply |
| 2019-02-10 | Amazon::DynamoDB | 0.4.0 | dep-fail | WebService::AWS::Auth::V4 |
| 2019-02-10 | Binary::Structured | 0.0.2 | self-fail | FAILED: 010-basic-numeric.t \| # Failed test 'int8' at 010-basic-numeric.t line 41 |
| 2019-02-10 | Compress::Snappy | 0.0.3 | self-fail | FAILED: encoding.t \| Cannot load native library 'snappy': dlopen(snappy, 0x0009): tried: 'snappy' (no such fi … |
| 2019-02-10 | File::Find::Duplicates | * | dep-fail | File::Compare |
| 2019-02-10 | Image::RGBA | 0.2.1 | self-fail | FAILED: 01-sanity.t \| No such method 'decode' for invocant of type 'Block' |
| 2019-02-10 | Image::RGBA::Text | * | dep-fail | Image::RGBA |
| 2019-02-10 | IoC | 0.0.4 | self-fail | FAILED: 01-basic.t \| No such method 'fetch' for invocant of type 'Any' |
| 2019-02-09 | File::Compare | * | self-fail | FAILED: 01-multiple.t \| Undeclared name 'SeekFromBeginning' |
| 2019-02-09 | HTTP::Server::Ogre | 0.0.4 | dep-fail | HTTP::Supply |
| 2019-02-09 | Text::Indented | 0.1 | other | no META6.json in Text::Indented's archive |
| 2019-02-08 | Cache::Memcached | 0.0.11 | other | no META6.json in Cache::Memcached's archive |
| 2019-02-08 | Unicode::GCB | 0.3.0 | self-fail | FAILED: 01-sanity.t \| Undefined routine 'nqp::unipropcode' |
| 2019-02-08 | epoll | 0.3 | self-fail | FAILED: 01-simple.t \| Cannot find native symbol 'epoll_create1' |
| 2019-02-04 | SDL2::Raw | 0.3 | self-fail | FAILED: 01-load.t \| # Failed test 'The module can be use-d ok: SDL2::Raw' at 01-load.t line 6 |
| 2019-01-30 | App::Platform | 0.4.3 | dep-fail | CommandLine::Usage |
| 2019-01-16 | App::CPAN | 0.0.2 | self-fail | FAILED: parse-feed.t \| # Failed test 'Found 75 items' at parse-feed.t line 34 |
| 2019-01-15 | Do123 | 0.4 | dep-fail | jmp |
| 2019-01-15 | Net::NNG | 0.0.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 92: Error while compiling module Net::NNG (line 92): Co … |
| 2019-01-10 | Reaper::Control | 0.0.2 | self-fail | FAILED: 01-basic.t \| No such method 'bind-udp' for invocant of type 'IO::Socket::Async' |
| 2019-01-06 | DOM::Tiny | 0.5.2 | self-fail | FAILED: 005-entities.t \| # Failed test 'right HTML unescaped result' at 005-entities.t line 46 |
| 2019-01-03 | StrictClass | 0.0.3 | self-fail | FAILED: 01-basic.t \| # Failed test 'Can create an instace of a Class that does StrictClass' at 01-basic.t lin … |
| 2018-12-21 | Graphics::TinyTIFF | 0.0.6 | self-fail | FAILED: 01-basic.t \| # Failed test at 01-basic.t line 19 |
| 2018-12-18 | HTTP::Request::Supply | 0.2.0 | dep-fail | HTTP::Supply |
| 2018-12-18 | X11::Xdo | 0.0.1 | self-fail | FAILED: 00-basic.t \| # Failed test 'is usable' at 00-basic.t line 5 |
| 2018-11-29 | Xmav::JSON | 0.0.1 | self-fail | FAILED: 02_values.t \| # Failed test 'formfeed and unicode' at 02_values.t line 6 |
| 2018-11-28 | FanFou | 0.0.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 71: Error while compiling module Digest (line 71): expe … |
| 2018-11-25 | Log::Any | 0.9.5 | self-fail | FAILED: 02-basic.t \| No such method 'new' for invocant of type 'Backtrace' |
| 2018-11-25 | SQL::Lexer | 0.2.2 | self-fail | FAILED: compound_statement.t \| # Failed test 'Successful match returned' at compound_statement.t line 19 |
| 2018-11-22 | Duo | 0.0.1 | self-fail | FAILED: 00-basic.t \| # Failed test '.Numeric' at 00-basic.t line 14 |
| 2018-11-17 | CroX::HTTP::FallbackPassthru | 0.1 | timeout | CroX::HTTP::FallbackPassthru |
| 2018-11-17 | Text::Levenshtein::Damerau | 0.2.0 | self-fail | FAILED: dld.rakutest \| # Failed test 'matching' at dld.rakutest line 51 |
| 2018-11-16 | BioInfo | 0.4.3 | self-fail | FAILED: sequences.t \| # Failed test 'Translated Amino acid sequence has the correct length when coerced to Nu … |
| 2018-11-15 | Lingua::Unihan | 0.1.0 | self-fail | FAILED: 02.query.t \| Can't determine actual Offset |
| 2018-11-15 | Serialize::Naive | 0.2.5 | self-fail | FAILED: 01-simple.t \| # Failed test 'trivial - radius' at 01-simple.t line 59 |
| 2018-11-14 | JSON::WebToken | 0.0.1 | self-fail | FAILED: 01_basic.t \| # Failed test at 01_basic.t line 6 |
| 2018-11-13 | HTTP::Request::FormData | 0.2 | self-fail | FAILED: basic.t \| # Failed test at basic.t line 7 |
| 2018-11-12 | Template::Anti | 0.5.2 | dep-fail | DOM::Tiny |
| 2018-11-10 | Email::Address | 0.2 | self-fail | FAILED: basic.t \| ===SORRY!=== Parse error at line 21: Error while compiling module Email::Address::Format (l … |
| 2018-11-10 | Hash::MultiValue | 0.7 | self-fail | FAILED: basic.t \| # Failed test 'correct value after change elsewhere' at basic.t line 34 |
| 2018-11-08 | Acme::Mangle | v0.1.0 | other | no META6.json in Acme::Mangle's archive |
| 2018-11-08 | Tika | 0.1.0 | self-fail | FAILED: 01-version.t \| No such method 'recv' for invocant of type 'Any' |
| 2018-11-06 | Pod::Coverage | v0.1.1 | self-fail | FAILED: selftest.t \| Could not find Pod::To::Text in: |
| 2018-11-03 | ENIGMA::Machine | 0.0.2 | self-fail | FAILED: machine.t \| ===SORRY!=== Parse error at line 60: Error while compiling module ENIGMA::Machine (line 6 … |
| 2018-11-03 | Libclang | 0.2.0 | build-fail |  |
| 2018-10-24 | Algorithm::SkewHeap | 0.0.1 | self-fail | FAILED: basics.p6.t \| Type check failed in assignment; expected Node but got Any |
| 2018-10-23 | Math::Vector3D | 0.0.1 | self-fail | FAILED: basics.p6.t \| # Failed test 'length-squared' at basics.p6.t line 46 |
| 2018-10-23 | Sparrowdo::VSTS::YAML::Angular::Build | 0.0.6 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-10-18 | Geo::IP2Location::Lite | 0.10.0 | self-fail | FAILED: 004_exceptions.t \| Undeclared name 'SeekFromBeginning' |
| 2018-10-18 | Sparrowdo::VSTS::YAML::Cordova | 0.0.17 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Bash in: |
| 2018-10-16 | Cro::H | 0.0.2 | self-fail | FAILED: H.t \| # Failed test 'StrSink gets all the values' at H.t line 123 |
| 2018-10-16 | Sparrowdo::Cordova::OSx::Build | 0.0.7 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-10-12 | Inline::Ruby | 0.4.1 | self-fail | FAILED: eval.t \| ===SORRY!=== Parse error at line 226: Error while compiling module Inline::Ruby (line 226):  … |
| 2018-10-09 | Sparrowdo::VSTS::YAML::Build | 0.0.7 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-10-08 | File::XML::DMARC::Google | 0.1.1 | dep-fail | XML::XPath |
| 2018-10-08 | Test::Fuzz | 0.2.1 | self-fail | FAILED: 00-load.t \| # Failed test 'The object is-a 'Int'' at 00-load.t line 13 |
| 2018-10-07 | Task::Galaxy | 0.1 | dep-fail | IO::Blob |
| 2018-10-03 | Sparrowdo::VSTS::YAML::Build::Assembly::Patch | 1.0.0 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-10-02 | App::snippet | 0.2.1 | dep-fail | Terminal::Table |
| 2018-10-01 | JSON::Schema | 0.9 | dep-fail | JSON::Pointer |
| 2018-09-30 | BioPerl6 | 0.0.1 | self-fail | FAILED: PrimarySeq.t \| No matching multi candidate for method subseq |
| 2018-09-25 | TAP::Harness | 0.0.4 | dep-fail | TAP |
| 2018-09-23 | IRC::Client::Plugin::UrlTitle | 1.1.3 | other | no META6.json in HTML::Entity's archive |
| 2018-09-21 | Wkhtmltox | 0.0.1 | self-fail | FAILED: 00-compile.t \| # Failed test 'The module can be use-d ok: Wkhtmltox::PDF' at 00-compile.t line 7 |
| 2018-09-19 | Sparrowdo::Cordova::OSx::Fortify | 0.0.1 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-09-17 | Avro | 0.1.1 | self-fail | FAILED: 0-Usage.t \| Type check failed in assignment; expected Iterable but got Hash |
| 2018-09-16 | App::FindSource | 0.0.6 | dep-fail | Terminal::Table |
| 2018-09-16 | Parse::STDF | 0.1.3 | self-fail | FAILED: 01-new.t \| ===WARNING=== Module RakudoPrereq EXPORT failed: Type check failed in binding to parameter … |
| 2018-09-15 | Rabble | 0.3.1 | self-fail | FAILED: 010-Rabble.t \| Undeclared name 'Rabble::Verbs::Shufflers' |
| 2018-09-09 | Auth::PAM::Simple | 1.0.1 | self-fail | FAILED: 01-basic.t \| Cannot find native symbol 'auth' |
| 2018-09-07 | Sparrowdo::VSTS::YAML::Update::Azure::SSL | 0.0.1 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-09-03 | App::Assixt | 1.0.0 | dep-fail | Version::Semantic |
| 2018-08-29 | CompUnit::Repository::Github | 0.0.2 | dep-fail | Distribution::Common::Remote |
| 2018-08-28 | Distribution::Common | 0.0.2 | self-fail | FAILED: 00-sanity.rakutest \| # Failed test 'The module can be use-d ok: Distribution::Common' at 00-sanity.ra … |
| 2018-08-28 | Distribution::Common::Remote | 0.0.2 | self-fail | FAILED: 00-sanity.rakutest \| # Failed test 'The module can be use-d ok: Distribution::Common::Remote::Github' … |
| 2018-08-13 | Plosurin | 0.02 | self-fail | FAILED: t01.t \| ===SORRY!=== Parse error at line 106: Error while compiling module Plosurin (line 106): Confu … |
| 2018-08-10 | ProblemSolver | 0.0.1 | self-fail | FAILED: 01-domain.t \| Variable '@set' is not declared |
| 2018-08-06 | RDF::Turtle | 0.0.3 | self-fail | FAILED: 01-grammar.t \| callsame is not in the dynamic scope of a dispatcher |
| 2018-08-05 | Digest::MurmurHash3 | 0.1.5 | self-fail | FAILED: 02-murmurhash.t \| # Failed test 'Test murmurhash3_32' at 02-murmurhash.t line 12 |
| 2018-08-05 | Lingua::Number | * | self-fail | FAILED: 00-basic.t \| Type Array does not support associative indexing |
| 2018-08-05 | Locale::Codes | 0.1.0 | self-fail | FAILED: country.t \| # Failed test at country.t line 17 |
| 2018-08-05 | Perl6::Maven | * | dep-fail | HTTP::Supply |
| 2018-08-05 | String::CamelCase | 1.0.3 | self-fail | FAILED: 04-wordsplit.t \| # Failed test at 04-wordsplit.t line 28 |
| 2018-08-05 | WebService::SOP | 0.0.6 | self-fail | FAILED: 03-client.t \| # Failed test 'Fails without app-id' at 03-client.t line 12 |
| 2018-08-04 | HTTP::Signature | 0.1.1 | self-fail | FAILED: 010-load.t \| # Failed test 'The module can be use-d ok: HTTP::Signature' at 010-load.t line 5 |
| 2018-08-04 | IO::Path::Dirstack | 0.1.1 | self-fail | FAILED: 01-pop-push.t \| # Failed test 'Changed directory correctly' at 01-pop-push.t line 23 |
| 2018-08-04 | Testing | * | other | no META6.json in Testing's archive |
| 2018-08-03 | Acme::WTF | 1.0 | self-fail | FAILED: 02_wtf.t \| |
| 2018-08-03 | HTML::Restrict | 0.2 | self-fail | FAILED: 02-defang.t \| Undefined routine 'enum' |
| 2018-08-03 | Net::ZMQ | 0.8 | self-fail | FAILED: 00-basic.t \| Cannot load native library 'zmq': dlopen(zmq, 0x0009): tried: 'zmq' (no such file), '/Sy … |
| 2018-08-02 | DateTime::Extended | 0.1.0 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: DateTime::Extended' at 00-load.t line 6 |
| 2018-08-01 | HTTP::MultiPartParser | * | self-fail | FAILED: 030_basic.t \| # Failed test 't/data//001-content.dat' at 030_basic.t line 50 |
| 2018-07-30 | Propius | 0.1.1 | dep-fail | TimeUnit |
| 2018-07-26 | Sparrowdo::VSTS::YAML::DotNet | 0.0.2 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-07-26 | Sparrowdo::VSTS::YAML::MsBuild | 0.0.2 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-07-26 | Sparrowdo::VSTS::YAML::Solution | 0.0.4 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-07-25 | Future | 0.1 | self-fail | FAILED: 00-basic.t \| # Failed test 'Future' at 00-basic.t line 103 |
| 2018-07-20 | Mortgage | * | timeout | Mortgage |
| 2018-07-16 | Sparrowdo::VSTS::YAML::Nuget::Build | 0.0.3 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Directory in: |
| 2018-07-15 | P6Repl::Helper | 0.0.3 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 208: Error while compiling module P6Repl::Helper (line  … |
| 2018-07-10 | Version::Semantic | 0.1.0 | self-fail | FAILED: 01-new.t \| Version::Semantic.new(major => Int, minor => Int, patch => Int) |
| 2018-07-06 | Timer::Breakable | 0.1.1 | self-fail | FAILED: 01-basic.t \| # Failed test 'Timer 1 has been stopped' at 01-basic.t line 11 |
| 2018-07-04 | LendingClub | 0.1.0 | self-fail | FAILED: 00-load.t \| # Failed test 'The module can be use-d ok: LendingClub' at 00-load.t line 5 |
| 2018-07-03 | NativeHelpers::CBuffer | 0.0.3 | self-fail | FAILED: 02-allocfree.t \| Undefined routine 'nqp::box_i' |
| 2018-07-03 | Scheduler::DRMAA | 0.0.1 | dep-fail | NativeHelpers::CBuffer |
| 2018-06-29 | Text::BorderedBlock | 0.1.0 | self-fail | FAILED: blocks.t \| # Failed test 'Empty block' at blocks.t line 116 |
| 2018-06-25 | Git::Wrapper | 0.0.8 | self-fail | FAILED: 00-basic.t \| # Failed test 'Can load module: Git::Wrapper' at 00-basic.t line 13 |
| 2018-06-19 | Net::OSC | 0.2.2 | dep-fail | Net::Osc |
| 2018-06-18 | Crypt::TweetNacl | 0.0.3 | self-fail | FAILED: 00-load.t \| Could not find Pod::To::Text in: |
| 2018-06-15 | DBIx::NamedQueries | 0.0.3 | self-fail | FAILED: 01-handles.t \| # Failed test 'DBIish handle tests' at 01-handles.t line 87 |
| 2018-06-14 | Sparrowdo::VSTS::YAML::Artifact | 0.0.1 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-06-14 | Sparrowdo::VSTS::YAML::Nuget | 0.0.2 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Template in: |
| 2018-06-14 | US-ASCII | 0.6.6 | self-fail | FAILED: 01-basic.t \| # Failed test 'alpha with "_" correct US-ASCII char subset' at 01-basic.t line 54 |
| 2018-06-11 | Acme::Skynet | 0.0.3 | self-fail | FAILED: dumb.t \| # Failed test 'Strips plural' at dumb.t line 161 |
| 2018-06-07 | IConv | 0.0.2 | self-fail | FAILED: 01-load.t \| Cannot load native library 'libc.so.6': dlopen(libc.so.6, 0x0009): tried: 'libc.so.6' (no … |
| 2018-06-07 | Magento | 0.0.2 | self-fail | FAILED: 00-basics.t \| # Failed test 'The module can be use-d ok: Magento::Auth' at 00-basics.t line 9 |
| 2018-06-04 | RPi::GpioDirect | 0.1.0 | self-fail | FAILED: gpio-direct.t \| Unknown distro macos |
| 2018-05-20 | Time::Repeat | 0.0101 | self-fail | FAILED: 03-hhmm.t \| Died |
| 2018-05-18 | P5__DATA__ | 0.0.1 | self-fail | FAILED: 01-basic.t \| ===WARNING=== Module P5__DATA__ EXPORT failed: No such method 'slang_grammar' for invoca … |
| 2018-05-14 | Algorithm::Heap::Binary | 0.0.1 | self-fail | FAILED: 02-binary-heap.t \| # Failed test 'size tests' at 02-binary-heap.t line 37 |
| 2018-04-30 | Auth::SAML2 | 1.0.0 | dep-fail | XML::Canonical |
| 2018-04-30 | HTTP::Server::Tiny | 0.0.2 | dep-fail | IO::Blob |
| 2018-04-30 | Web::RF | 1.0.0 | dep-fail | IO::Blob |
| 2018-04-26 | Crust::Handler::SCGI | 1.0.0 | dep-fail | IO::Blob |
| 2018-04-26 | XML::Signature | 1.0.0 | dep-fail | XML::Canonical |
| 2018-04-25 | Bailador::Plugin::NamedQueries | 0.1001 | dep-fail | HTTP::Supply |
| 2018-04-25 | JSON::Pointer | 1.0 | self-fail | FAILED: 01-basic.t \| # Failed test 'dies on non-existed element' at 01-basic.t line 70 |
| 2018-04-25 | SDL | 0.2.0 | self-fail | FAILED: core.t \| ===SORRY!=== Parse error at line 10: Error while compiling module SDL::Structures (line 10): … |
| 2018-04-24 | I18N::LangTags | 0.1.0 | timeout | I18N::LangTags |
| 2018-04-21 | Ops::SI | 0.1.1 | self-fail | FAILED: 01-use.t \| # Failed test 'Module can be used' at 01-use.t line 9 |
| 2018-04-19 | Net::FTP | 0.6.1 | self-fail | FAILED: 02-login.t \| No such method 'recv' for invocant of type 'Any' |
| 2018-04-19 | Slang::AltTernary | 0.0.3 | self-fail | FAILED: 03-basic.t \| ===SORRY!=== Parse error at line 9: unexpected operator in term position (got '⁈') |
| 2018-04-13 | SVG::Plot | * | self-fail | FAILED: series.t \| No such method 'add_to_keys' for invocant of type 'SVG::Plot::Data::Series' |
| 2018-04-11 | Pod::To::HTMLBody | 0.0.1 | self-fail | FAILED: 00-core.t \| Method 'to-node' must be resolved by class Pod::To::Tree because it exists in multiple ro … |
| 2018-04-05 | Text::Table::Simple | 0.0.7 | self-fail | FAILED: basics.rakutest \| # Failed test 'Create a table (header + body + footer + custom options)' at basics. … |
| 2018-04-03 | App::Football | 0.1.5 | dep-fail | WebService::FootballData |
| 2018-04-03 | WebService::FootballData | 0.1.4 | self-fail | FAILED: 02-id.t \| No such method 'get_attribute_for_usage' for invocant of type 'A' |
| 2018-03-31 | Net::LibIDN | 0.0.2 | self-fail | FAILED: 01-native.t \| Cannot load native library 'idn': dlopen(idn, 0x0009): tried: 'idn' (no such file), '/S … |
| 2018-03-30 | Lingua::Stopwords | 0.0.3 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 9: Error while compiling module Lingua::Stopwords (line … |
| 2018-03-27 | App::Miroku | 0.0.2 | self-fail | FAILED: 04-path-utils.t \| # Failed test at 04-path-utils.t line 7 |
| 2018-03-22 | App::OrgSleep | 0.0.8 | dep-fail | Inline::Perl5 |
| 2018-03-19 | ANTLR4 | 0.0.5 | self-fail | FAILED: 02-corpus.t \| # Failed test 'IRI.g4' at 02-corpus.t line 31 |
| 2018-03-12 | WebSocket | 0.0.1 | dep-fail | Cro::HTTP::Test |
| 2018-03-04 | CommonMark | 0.0.5 | self-fail | FAILED: 10-core.t \| Cannot load native library 'cmark': dlopen(cmark, 0x0009): tried: 'cmark' (no such file), … |
| 2018-03-02 | Sparky::Plugin::Notify::Telegram | 0.0.2 | dep-fail | Time::Crontab |
| 2018-02-20 | Crust::Middleware::Session | * | dep-fail | IO::Blob |
| 2018-02-13 | LREP | 0.2.1 | dep-fail | Text::CSV |
| 2018-02-12 | Music::Engine | 0.0.2 | dep-fail | ScaleVec |
| 2018-02-04 | AttrX::Lazy | 0.0.3 | self-fail | FAILED: 020-basic.t \| # Failed test 'Building of the attribute is correct' at 020-basic.t line 18 |
| 2018-02-04 | AttrX::PrivateAccessor | 0.0.2 | self-fail | FAILED: 020-basic.t \| No such method '!diary' for invocant of type 'Teenager' |
| 2018-02-04 | Test::Harness | 0.0.1 | self-fail | FAILED: file.t \| Malformed TAP output |
| 2018-02-01 | Seq::PreFetch | 0.1.0 | self-fail | FAILED: 01-basic.t \| Type check failed in binding to parameter '$s'; expected Seq but got List (()) |
| 2018-01-23 | Pod::PerlTricks | 0.01 | self-fail | FAILED: Grammar.t \| # Failed test 'match pod section' at Grammar.t line 15 |
| 2018-01-22 | DBI::Async | 0.1 | self-fail | FAILED: 01-basic.t \| # Failed test 'Statement' at 01-basic.t line 12 |
| 2018-01-22 | Slang::Predicate | 0.0.1 | self-fail | FAILED: 01-basic.t \| Undeclared name 'T' |
| 2018-01-08 | DateTime::Format::W3CDTF | * | self-fail | FAILED: main.t \| # Failed test at main.t line 19 |
| 2018-01-08 | XML::Parser::Tiny | * | self-fail | FAILED: 02-actions.t \| # Failed test '<?aaa x='1' ?><?bbb y='2' ?><doc></doc>' at 02-actions.t line 120 |
| 2018-01-04 | Verge::RPC::Client | 0.0.2 | dep-fail | Getopt::Long |
| 2017-12-30 | Crypt::RSA | 0.1.0 | timeout | Crypt::RSA |
| 2017-12-28 | Bitcoin::RPC::Client | 0.0.2 | dep-fail | Getopt::Long |
| 2017-12-17 | LIVR | 2.1.0 | self-fail | FAILED: 00-simple.t \| # Failed test 'POSITIVE: required' at 00-simple.t line 38 |
| 2017-12-06 | IRC::Client::Plugin::Github | 0.1.5 | dep-fail | HTTP::Supply |
| 2017-12-06 | Sparky::Plugin::Hello | 0.0.2 | dep-fail | Time::Crontab |
| 2017-12-06 | Sparky::Plugin::Notify::Email | 0.0.1 | dep-fail | Time::Crontab |
| 2017-12-04 | Net::FTPlib | 0.2.0 | build-fail |  |
| 2017-12-02 | Inline::Go | 0.0.4 | self-fail | FAILED: 001-basic.t \| Undefined routine 'n' |
| 2017-11-22 | CommandLine::Usage | 0.1 | self-fail | FAILED: 10-parse.t \| No such method 'constraint_list' for invocant of type 'Parameter' |
| 2017-11-21 | ADT | 0.5 | self-fail | FAILED: 01-tree.t \| Cannot invoke non-Callable value of type Nil |
| 2017-11-14 | bamboo | 0.0.3 | fetch-fail | https://raw.githubusercontent.com/raku/REA/main/archive/B/bamboo/bamboo%3Aver%3C0.0.3%3E%3Aauth%3Cgithub%3Aser … |
| 2017-11-12 | DNS::Zone | 0.1.2 | self-fail | FAILED: 00_class.t \| ===SORRY!=== Parse error at line 384: Error while compiling module DNS::Zone::Grammars:: … |
| 2017-11-08 | MsgPack | 0.0.7 | build-fail |  |
| 2017-10-25 | Text::Markdown::Discount | 0.3.0 | self-fail | FAILED: 01_lib.t \| # Failed test 'libmarkdown is installed' at 01_lib.t line 6 |
| 2017-10-25 | Tinky::Hash | 0.4.2 | self-fail | FAILED: 100-th.t \| # Workflow wf1 |
| 2017-10-22 | Coro::Simple | * | self-fail | FAILED: fibonacci.t \| # Failed test at fibonacci.t line 22 |
| 2017-10-20 | Color::Named | 1.001002 | self-fail | FAILED: tests.t \| # Failed test at tests.t line 5 |
| 2017-10-17 | Sparrowdo::Prometheus | 0.0.1 | self-fail | FAILED: 00-load.t \| Could not find Sparrowdo::Core::DSL::Directory in: |
| 2017-10-16 | Grammar::ErrorReporting | 0.2 | self-fail | FAILED: 01-basic.t \| # Failed test 'error-position 2' at 01-basic.t line 27 |
| 2017-10-15 | Operator-grandpa | * | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 10: Error while compiling module Operator::grandpa (lin … |
| 2017-10-14 | Config::Parser::yaml | 1.0.1 | self-fail | FAILED: 01-read.t \| # Failed test 'Get simple key' at 01-read.t line 97 |
| 2017-10-14 | MPD::Client | 0.1.3 | self-fail | FAILED: 00-client.t \| No such method 'get' for invocant of type 'Any' |
| 2017-10-12 | Config::Simple | * | self-fail | FAILED: 01_perl6.t \| # Failed test 'Changing subtitle format' at 01_perl6.t line 11 |
| 2017-10-12 | NCurses | 0.6.3 | self-fail | FAILED: 02-basic.t \| Error opening terminal: unknown. |
| 2017-10-08 | App::MPD::AutoQueue | 0.1.1 | dep-fail | MPD::Client |
| 2017-10-08 | App::MPD::Notify | 0.1.0 | dep-fail | MPD::Client |
| 2017-10-08 | Math::Model | 0.3 | dep-fail | Math::RungeKutta |
| 2017-10-08 | Math::RungeKutta | 0.1 | self-fail | FAILED: 01-rk-integrate.t \| # took 0 seconds |
| 2017-10-05 | META6::To::Man | 0.2.0 | self-fail | FAILED: 010-exe-test.t \| # Failed test 'valid args' at 010-exe-test.t line 43 |
| 2017-09-30 | Build::Graph | 0.0.2 | self-fail | FAILED: basic.t \| Type check failed on attribute '$!graph'; expected Build::Graph:D but got Build::Graph |
| 2017-09-19 | Semaphore::ReadersWriters | 0.2.6 | self-fail | FAILED: 100-rw.t \| # Failed test 'basic tests' at 100-rw.t line 30 |
| 2017-09-13 | Logger::ZMQ | 0.0.1 | self-fail | FAILED: 00.t \| # Failed test 'The module can be use-d ok: Log::ZMQ::Logger.pm' at 00.t line 15 |
| 2017-08-31 | System::DiskAndUpdatesAlerts | 1.0.0 | dep-fail | FileSystem::Capacity |
| 2017-08-22 | Grammar::HTTP | 0.0.2 | self-fail | FAILED: 01-http-grammar.rakutest \| # Failed test at 01-http-grammar.rakutest line 19 |
| 2017-08-21 | XML::Rabbit | 0.1.0 | dep-fail | XML::XPath |
| 2017-08-20 | Adventure::Engine | 0.4.0 | other | no META6.json in Adventure::Engine's archive |
| 2017-08-20 | Game::Crypt | 0.2.3 | other | no META6.json in Adventure::Engine's archive |
| 2017-08-18 | Powerline::Prompt | 0.0.4 | dep-fail | Git::Simple |
| 2017-08-09 | Punnable | 0.1.0 | self-fail | FAILED: 01-punnable.t \| # Failed test at 01-punnable.t line 12 |
| 2017-08-07 | UNIX::Daemonize | 0.0.4 | self-fail | FAILED: 02-daemon-creates-pidfile-and-cleans-up.t \| # Failed test 'Dead' at 02-daemon-creates-pidfile-and-cle … |
| 2017-08-06 | panda | 2016.02 | self-fail | FAILED: build-hook.t \| # Failed test 'run test and build with a Build.pm present' at build-hook.t line 14 |
| 2017-08-04 | Getopt::Kinoko | 0.3.5 | dep-fail | Terminal::Table |
| 2017-08-04 | Log::Minimal | 0.0.1 | self-fail | FAILED: 010_f.t \| 2026-08-24T02:29:47.454398+03:00 [CRITICAL] critical at 010_f.t line 63 |
| 2017-08-04 | Stream::Buffered | 0.0.1 | dep-fail | IO::Blob |
| 2017-07-30 | HTTP::Server::Simple | 0.1.2 | self-fail | FAILED: basic.t \| ===SORRY!=== Parse error at line 5: Error while compiling module HTTP::Server::Simple (line … |
| 2017-07-30 | Operator::defined-alternation | 0.0.1 | self-fail | FAILED: 00-load.t \| # Failed test 'load module Operator::defined-alternation' at 00-load.t line 7 |
| 2017-07-30 | Slippy::Semilist | 0.0.2 | self-fail | FAILED: 01-basic.t \| Unsupported prefix 'dimslip' |
| 2017-07-18 | Finance::GDAX::API | 0.0.5 | self-fail | FAILED: 020-api.t \| # Failed test 'External secret file key' at 020-api.t line 34 |
| 2017-07-08 | Font::QueryInfo | 0.6.2 | self-fail | FAILED: 01-fontinfo.t \| 3rdparty/NotoSans-Bold.ttf Didn't get any queries |
| 2017-07-01 | HandleSupplier | 0.0.1 | dep-fail | IO::Blob |
| 2017-07-01 | Router::Boost | 0.0.1 | self-fail | FAILED: 01-basic.t \| # Failed test at 01-basic.t line 113 |
| 2017-06-30 | Crust::Middleware::Session::Store::DBIish | 0.0.1 | dep-fail | IO::Blob |
| 2017-06-24 | Class::Utils | 0.1.0 | self-fail | FAILED: 01-can-has.t \| Too many levels of recursion |
| 2017-06-24 | IO::Path::More | * | self-fail | FAILED: 00-more.t \| # Failed test 'append method available' at 00-more.t line 7 |
| 2017-06-24 | Masquerade | * | self-fail | FAILED: asif-json.t \| No such method 'has-accessor' for invocant of type 'Attribute' |
| 2017-06-24 | Semantic::Versioning | * | self-fail | FAILED: 01-version-manipulation.t \| # Failed test 'major version from version' at 01-version-manipulation.t l … |
| 2017-06-24 | UEncoding | 0.1.0 | self-fail | FAILED: 200-encode.t \| # Failed test 'cstring' at 200-encode.t line 18 |
| 2017-06-23 | TCC | 0.1 | self-fail | FAILED: 01-basic.t \| Cannot load native library 'tcc': dlopen(tcc, 0x0009): tried: 'tcc' (no such file), '/Sy … |
| 2017-06-15 | Algorithm::Trie::libdatrie | 0.2 | self-fail | FAILED: 02-trie.t \| Cannot find native symbol 'alpha_map_new' |
| 2017-06-15 | Crust::Middleware::Syslog | 1.0.0 | dep-fail | IO::Blob |
| 2017-06-15 | Net::POP3 | 1.0.0 | self-fail | FAILED: 01-raw.t \| ===SORRY!=== Parse error at line 71: Error while compiling module Digest (line 71): expect … |
| 2017-06-15 | Search::Dict | 0.2 | self-fail | FAILED: 02-lookup.t \| # search for 1 existing words |
| 2017-06-15 | Text::Fortune | 0.03 | self-fail | FAILED: 01_empty.t \| # Failed test 'matches empty.dat' at 01_empty.t line 170 |
| 2017-06-12 | LibYAML | 0.2.1 | self-fail | FAILED: 10.parse.t \| Variable $.stream-start used where no 'self' is available |
| 2017-06-11 | Zef::CPANReporter | 0.0.2 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 20: Error while compiling module Net::HTTP::POST (line  … |
| 2017-06-10 | Texas::To::Uni | 0.1.0 | self-fail | FAILED: 01-sanity.t \| Attempt to return outside of any Routine |
| 2017-06-10 | Text::Abbrev | 0.1.0 | self-fail | FAILED: 00-basic.t \| # Failed test 'Non stringy arguments should be stringified.' at 00-basic.t line 18 |
| 2017-05-26 | XML::Writer | * | self-fail | FAILED: structure.t \| # Failed test 'Can either pass named or positional' at structure.t line 11 |
| 2017-05-25 | Perl6-Math-Quaternion | * | self-fail | FAILED: empty_subclass.t \| # Failed test '.unit, math, and .Str work' at empty_subclass.t line 30 |
| 2017-05-23 | Grammar::Highlighter | 0.1 | self-fail | FAILED: highlight.t \| No such method 'add_fallback' for invocant of type 'Grammar::Highlighter' |
| 2017-05-22 | CompUnit::Repository::Mask | 0.0.1 | self-fail | FAILED: 01-basic.t \| ===SORRY!=== Parse error at line 17: Couldn't find final ')' (corresponding ( was at lin … |
| 2017-05-22 | TinyCC | 0.2.3 | self-fail | FAILED: 01-basic.t \| No such method 'set_error_func' for invocant of type 'TCCState' |
| 2017-05-17 | Text::Emotion | 0.0.5 | self-fail | FAILED: basic.t \| Variable '%dictionary' is not declared |
| 2017-05-16 | LCS::BV | 0.4.0 | self-fail | FAILED: 01-basic.t \| Target is not assignable |
| 2017-05-14 | Compress::Brotli | 0.1.0 | build-fail |  |
| 2017-05-12 | Typed::Subroutines | * | self-fail | FAILED: 01-basic.t \| # Failed test at 01-basic.t line 11 |
| 2017-05-11 | Hashids | 0.1 | self-fail | FAILED: 01-basic.t \| Cannot resolve caller chars(Any:U); the invocant is a type object, not an instance |
| 2017-05-10 | Gravatar::URL | 0.1.0 | self-fail | FAILED: gravatar.t \| No such method 'new' for invocant of type 'Digest::MD5' |
| 2017-05-09 | OO::Schema | 0.2.1 | self-fail | FAILED: 01-userland.t \| ===WARNING=== Module OO::Schema EXPORT failed: can only be called at BEGIN time |
| 2017-05-05 | AWS::Pricing | 0.2.3 | self-fail | FAILED: 02-cache.t \| Constraint type check failed in binding to parameter '$region' |
| 2017-05-04 | Facter | 0.04 | self-fail | FAILED: basic.t \| # Failed test 'test fact is loaded' at basic.t line 17 |
| 2017-05-04 | String::CRC32 | 0.05 | self-fail | FAILED: crc.t \| Undefined routine 'String::CRC32::crc32' |
| 2017-05-02 | File::Spec::Case | * | self-fail | FAILED: 00_basic.t \| # Failed test 'tolerant is True in cwd' at 00_basic.t line 58 |
| 2017-04-30 | Concurrent::File::Find | 0.1.2 | self-fail | FAILED: 01-basic.t \| Cannot invoke non-Callable value of type Any |
| 2017-04-30 | Dependency::Sort | 1 | self-fail | FAILED: all.t \| ===SORRY!=== Error while compiling all.t |
| 2017-04-30 | Ini::Storage | 0.8 | dep-fail | Path::Util |
| 2017-04-30 | Log::D | 0.8 | self-fail | FAILED: all.t \| Variable '%bans' is not declared |
| 2017-04-30 | Path::Util | * | self-fail | FAILED: all.t \| # Failed test 'getdirlevel' at all.t line 236 |
| 2017-04-30 | Rakudo::Perl6::Format | 0.1 | self-fail | FAILED: 00-use-module.t \| # Failed test 'The module can be use-d ok: Perl6::Format' at 00-use-module.t line 7 |
| 2017-04-30 | Rakudo::Perl6::Tracer | 0.1.1 | self-fail | FAILED: 00-use-module.t \| # Failed test 'The module can be use-d ok: Rakudo::Perl6::Tracer' at 00-use-module. … |
| 2017-04-29 | Crypt::Bcrypt | 1.3.2 | pass |  |
| 2017-04-29 | Num::HexFloat | 0.1.2 | self-fail | FAILED: 00-basic.t \| No such method 'base' for invocant of type 'Num' |
| 2017-04-29 | OpenCV | 0.0.5 | build-fail |  |
| 2017-04-29 | Selenium::WebDriver | 0.0.1 | self-fail | FAILED: 01-load.rakutest \| ===SORRY!=== Parse error at line 19: Error while compiling module Selenium::WebDri … |
| 2017-04-28 | Cookie::Baker | * | self-fail | FAILED: 02_crush.t \| Target is not assignable |
| 2017-04-28 | HTML::MyHTML | 0.3.0 | self-fail | FAILED: 010-use.t \| # Failed test 'The module can be use-d ok: HTML::MyHTML' at 010-use.t line 4 |
| 2017-04-28 | PKCS5 | 0.1.6 | self-fail | FAILED: 100-pbkdf2.t \| # Failed test 'Tests from rfc6070' at 100-pbkdf2.t line 59 |
| 2017-04-28 | Text::LTSV | * | self-fail | FAILED: 01-basic.t \| # Failed test 'Basic parse-text' at 01-basic.t line 51 |
| 2017-04-27 | Git::Simple | 0.0.2 | self-fail | FAILED: 20-detached.t \| # Failed test 'git add + commit' at 20-detached.t line 24 |
| 2017-04-27 | JavaScript::SpiderMonkey | 0.0.1 | other | no META6.json in JavaScript::SpiderMonkey's archive |
| 2017-04-27 | MinG | 1.2.7 | self-fail | FAILED: basic.t \| Type check failed in assignment; expected MinG::Grammar but got Any |
| 2017-04-27 | Modular | 0.1.2 | other | no META6.json in Modular's archive |
| 2017-04-27 | String::Koremutake | 0.2 | self-fail | FAILED: 01_sane.t \| # Failed test 'got ba' at 01_sane.t line 26 |
| 2017-04-27 | Uni63 | 0.2.0 | self-fail | FAILED: 01-sanity.t \| ===SORRY!=== Parse error at line 31: Error while compiling module Uni63 (line 31): unex … |
| 2017-04-26 | Acme::Flutterby | 0.01 | other | no META6.json in Acme::Flutterby's archive |
| 2017-04-26 | Algorithm::BloomFilter | 0.1.0 | dep-fail | Digest::MurmurHash3 |
| 2017-04-26 | Apache::LogFormat | * | self-fail | FAILED: 00-sanity.t \| ===SORRY!=== Parse error at line 65: Error while compiling module Apache::LogFormat::Co … |
| 2017-04-26 | App::P6Dx | v0.3.0 | other | no META6.json in App::P6Dx's archive |
| 2017-04-26 | Crypt::Random | 0.4.1 | self-fail | FAILED: 01-buf.t \| ===WARNING=== Module if EXPORT failed: No such method 'legacy' for invocant of type 'Raku' |
| 2017-04-26 | Frinfon | * | dep-fail | IO::Blob |
| 2017-04-26 | IRC::Art | * | other | no META6.json in IRC::Art's archive |
| 2017-04-26 | Math::Polynomial | * | other | no META6.json in Math::Polynomial's archive |
| 2017-04-26 | MessagePack | * | self-fail | FAILED: 00-unpack.t \| ===SORRY!=== Parse error at line 47: Error while compiling module MessagePack::Unpacker … |
| 2017-04-26 | November | * | self-fail | FAILED: config.t \| ===SORRY!=== Parse error at line 111: Error while compiling module Text::Markup::Wiki::Med … |
| 2017-04-26 | Test::Lab | 1.0.0 | self-fail | FAILED: 010-lab.rakutest \| # Failed test 'pre-procedure context is kept' at 010-lab.rakutest line 79 |
| 2017-04-26 | Yapsi | 2011.02 | other | no META6.json in Yapsi's archive |
| 2017-04-25 | Control::Bail | v0.0.1 | self-fail | FAILED: bail.t \| Could not find QAST in: |
| 2017-04-25 | Digest::MD5 | 0.05 | timeout | Digest |
| 2017-04-25 | Flower | * | self-fail | FAILED: 01-basics.t \| # Failed test 'tal:define and tal:content' at 01-basics.t line 85 |
| 2017-04-25 | Hinges | * | self-fail | FAILED: 01-xml-parsing.t \| # Failed test '<a/> is valid' at 01-xml-parsing.t line 26 |
| 2017-04-25 | IO::Prompter | * | other | no META6.json in Testing's archive |
| 2017-04-25 | Inline | 1.0.1 | self-fail | FAILED: c.t \| ===SORRY!=== Parse error at line 7: Error while compiling module Inline::C (line 7): You cannot … |
| 2017-04-25 | List::Utils | * | self-fail | FAILED: 01-sliding-window.t \| # Failed test 'one at a time works lazily' at 01-sliding-window.t line 13 |
| 2017-04-25 | PerlMongers::Hannover | 0.8.6 | self-fail | FAILED: perlmongers_hannover.t \| # Failed test at perlmongers_hannover.t line 13 |
| 2017-04-25 | Sum | v0.1.0 | self-fail | FAILED: adler.t \| ===SORRY!=== Parse error at line 598: Error while compiling module Sum (line 598): expected … |
| 2017-04-25 | Text::Table::List | * | self-fail | FAILED: ascii.t \| No such method 'base' for invocant of type 'Num' |
| 2017-04-25 | X::Protocol | v0.0.1 | self-fail | FAILED: base.t \| # Failed test 'Can create a simple one-off' at base.t line 10 |
| 2017-04-24 | Druid | 0.1 | other | no META6.json in Druid's archive |
| 2017-04-23 | TagLibC | 1.0 | self-fail | FAILED: 00-reading.t \| Cannot load native library 'libtag_c.dylib': dlopen(libtag_c.dylib, 0x0009): tried: 'l … |
| 2017-04-21 | PriorityQueue | 0.1.0 | self-fail | FAILED: basic.t \| # Failed test at basic.t line 21 |
| 2017-02-26 | GTK::Scintilla | 0.0.5 | build-fail |  |
| 2017-02-23 | App::jsonv | 0.01 | other | no META6.json in App::jsonv's archive |
| 2017-02-04 | Net::XMPP | 0.0.1 | other | no META6.json in Net::XMPP's archive |
| 2017-01-21 | Typesafe::HTML | 0.1.0 | self-fail | FAILED: HTML.t \| ===SORRY!=== Parse error at line 32: Error while compiling module Typesafe::HTML (line 32):  … |
| 2017-01-21 | Typesafe::XHTML::Writer | 0.1.1 | dep-fail | Typesafe::HTML |
| 2016-11-18 | Test::Junkie | * | other | no META6.json in Test::Junkie's archive |
| 2016-11-10 | HTTP::Server | 0.1.0 | other | no META6.json in HTTP::Server's archive |
| 2016-11-02 | Acme::Addslashes | 0.1.2 | other | no META6.json in Acme::Addslashes's archive |
| 2016-10-30 | Getopt::Tiny | * | self-fail | FAILED: 01-basic.t \| Could not find Pod::To::Text in: |
| 2016-10-30 | Math::ChebyshevPolynomial | * | other | no META6.json in Math::ChebyshevPolynomial's archive |
| 2016-10-28 | Acme::DSON | 0.2.1 | other | no META6.json in Acme::DSON's archive |
| 2016-10-28 | BreakDancer | * | other | no META6.json in BreakDancer's archive |
| 2016-10-28 | Term::ProgressBar | * | other | no META6.json in Term::ProgressBar's archive |
| 2016-10-27 | Build::Simple | 0.0.1 | self-fail | FAILED: basic.t \| Undefined routine 'where' |
| 2016-10-26 | Phaser::ATEXIT | * | other | no META6.json in Phaser::ATEXIT's archive |
| 2016-10-26 | ufo | * | other | no META6.json in ufo's archive |
| 2016-10-22 | Math::OddFunctions | * | other | no META6.json in Math::OddFunctions's archive |
| 2016-08-19 | CompUnit::Search | 2.0.0 | self-fail | FAILED: 01-meta.t \| # license ‘Artistic’ is not one of the standardized SPDX license identifiers. |
| 2016-08-18 | Printer::ESCPOS | 1.0.1 | self-fail | FAILED: 01-meta.t \| # license ‘Artistic’ is not one of the standardized SPDX license identifiers. |
| 2016-05-27 | P6SGI | 0.6.Draft | other | no META6.json in P6SGI's archive |
| 2016-05-20 | Music::Helpers | 0.0.5 | self-fail | FAILED: 01-use.t \| # Failed test 'The module can be use-d ok: Music::Helpers' at 01-use.t line 7 |
| 2016-04-29 | Module::Toolkit | 0.7 | dep-fail | TAP |
| 2016-04-23 | App::redpanda | 0.1 | dep-fail | TAP |
| 2016-03-27 | Data::Section::Simple | * | self-fail | FAILED: 01-function.t \| Private method call to 'parse' outside the defining class |
| 2016-03-09 | Bamboo | 0.0.2 | fetch-fail | https://raw.githubusercontent.com/raku/REA/main/archive/B/bamboo/bamboo%3Aver%3C0.0.3%3E%3Aauth%3Cgithub%3Aser … |
| 2016-03-08 | RPi-native | 0.1 | other | no META6.json in RPi-native's archive |
| 2016-02-21 | wiringPI | 2.25.0 | other | no META6.json in wiringPI's archive |
| 2016-01-17 | AttrX::InitArg | 0.1.1 | self-fail | FAILED: class.t \| No such method 'roles_to_compose' for invocant of type 'SecretEnvoy' |
| 2016-01-11 | Serialize-Naive | 0.1.0 | other | no META6.json in Serialize-Naive's archive |
| 2016-01-08 | TweetNacl | 0.0.1 | self-fail | FAILED: 02-random.t \| # Failed test at 02-random.t line 8 |
| 2016-01-07 | Add | * | dep-fail | TweetNacl |
| 2015-12-30 | Kains | v0.0.2 | other | no META6.json in Kains's archive |
| 2015-12-10 | GGE | * | other | no META6.json in GGE's archive |
| 2015-11-29 | JSON5::Tiny | * | other | no META6.json in JSON5::Tiny's archive |
| 2015-11-10 | POSIX | 0.1.1 | other | no META6.json in POSIX's archive |
| 2015-11-08 | Net::Ftp | 0.1.0 | other | no META6.json in Net::Ftp's archive |
| 2015-11-01 | Test::Base | * | self-fail | FAILED: 01-text.t \| # Failed test at 01-text.t line 16 |
| 2015-10-03 | Net::Packet | v0.0.1 | other | no META6.json in Net::Packet's archive |
| 2015-09-25 | WebService::TelegramBot | 0.1.0 | other | no META6.json in WebService::TelegramBot's archive |
| 2015-09-13 | Perl6::Literate | * | other | no META6.json in Perl6::Literate's archive |
| 2015-09-12 | Math::ContinuedFractions | * | other | no META6.json in Math::ContinuedFractions's archive |
| 2015-07-14 | Perl6::Tracer | 0.1 | other | no META6.json in Perl6::Tracer's archive |
| 2015-06-11 | Net::Pcap | v0.0.1 | other | no META6.json in Net::Pcap's archive |
| 2015-06-09 | Editsrc::Uggedit | * | other | no META6.json in Editsrc::Uggedit's archive |
| 2015-05-31 | Pod::Strip | v0.0.1 | other | no META6.json in Pod::Strip's archive |
| 2015-05-26 | HTML::Entity | * | other | no META6.json in HTML::Entity's archive |
| 2015-01-29 | Webservice::Lastfm | 0.0.1 | other | no META6.json in Webservice::Lastfm's archive |
| 2014-09-19 | Data::Pretty | 0.0.2 | other | no META6.json in Data::Pretty's archive |
| 2014-05-14 | XXX | * | dep-fail | TestML |
| 2014-05-13 | Algorithm::Viterbi | * | other | no META6.json in Algorithm::Viterbi's archive |
| 2013-02-19 | Time-Duration | 1.04 | other | no META6.json in Time-Duration's archive |
| 2012-10-19 | Farabi6 | 0.01 | other | no META6.json in Farabi6's archive |
| 2012-08-01 | Crypt::Game | 0.2.0 | other | no META6.json in Adventure::Engine's archive |
