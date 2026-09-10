# rakupp install — the module installer (docs/dev/plans/MODULES-PLAN.md, Part A).
#
# A Raku program compiled INTO the binary: cmake/EmbedTools.cmake turns this
# file into a byte array at build time, and `rakupp install` runs it from
# there. Nothing is looked up beside the executable, and nothing generated is
# checked in — edit this file, rebuild, done.
#
# It is Raku because the project's own tooling runs on the interpreter it
# ships — an installer is index JSON, version ranges, paths and subprocesses,
# and this is the language for that. The engine gains no network code either
# way: fetching is curl and unpacking is tar, both in a subprocess, so there is
# no HTTP client, no TLS stack, no tar reader and no index parser anywhere in
# rakupp, in any language. That is a property of THIS design, not of where the
# code is compiled.
#
#   rakupp install Foo::Bar              install newest satisfying, plus deps
#   rakupp install Foo:ver<1.2.3>        a specific version (still additive)
#   rakupp install --dry-run Foo         the full plan, nothing written
#   rakupp install --list                what is installed in the home store:
#                                        identity, installer, module files,
#                                        bin wrappers (-q: identities only)
#
# The store it writes is the CURI layout rakupp already READS (and Rakudo and
# zef share): install is the writer for a reader that already exists. Updates
# are ADDITIVE — a newer version lands beside the old one and resolution picks
# among them; nothing is ever removed here (uninstall is a separate, later,
# harder thing — see the plan's M6).
#
# Transport, v1: `curl` (TLS certificate verification on, as it always is
# unless someone passes -k, which this program never does) and `tar` — both
# present on every platform this targets. The dlopen-libssl/zlib transport
# the plan sketches remains the self-containment refinement; the checksum
# gate is ours either way: a fetched archive must hash to the SHA-1 its index
# path names, or it is refused.
#
# Resolution is zef-index-first with the community's REA archive
# (github.com/Raku/REA — a copy of every distribution ever released) as the
# fallback for names and exact pins the live index no longer carries: 48 of
# the battery's top-200 dists are REA-only, and zef itself resolves them
# through the same archive. The REA index is fetched lazily — only on the
# first zef-index miss — and cached like the zef index.
# RAKUPP_INSTALL_REA_INDEX overrides its source (file path or URL); when
# RAKUPP_INSTALL_INDEX is overridden and no REA source is given, the
# fallback stays OFF, so an offline suite or a mirror sees no surprise
# network fetch. REA archive URLs carry no content hash, so for REA fetches
# TLS is the only integrity — the install says so, per dist, out loud.
#
# Test gate: a distribution's own t/ suite runs under rakupp before it is
# marked installed (--no-test to skip). Dependencies install first, so a
# dist's tests see its deps.

my constant $INDEX-URL = 'https://360.zef.pm/index.json';
my constant $REA-INDEX-URL = 'https://raw.githubusercontent.com/raku/REA/main/META.json';
my constant $CACHE-TTL = 24 * 3600;

# The engine's built-in JSON codec — the installer must not depend on an
# ecosystem JSON module, since installing those is its own job. The codec's
# first-party name is Rakupp::Internals::JSON; the Rakudo spelling is the
# same codec under its compatibility alias, and doubles as the fallback that
# keeps this file runnable under Rakudo (docs/dev/ecosystem/
# RAKUDO-INTERNALS.md). Probed FUNCTIONALLY: a type object is undefined, so
# a `//` against the lookup would always reject it.
my $have-own-json = ?(try { ::('Rakupp::Internals::JSON').from-json('1') === 1 });
sub json-decode(Str $text) {
    $have-own-json
        ?? ::('Rakupp::Internals::JSON').from-json($text)
        !! Rakudo::Internals::JSON.from-json($text)
}


# META6 lets a dependency LIST be written three ways, and the ecosystem uses
# all three. A plain identity string is the common case. 223 dists in the zef
# index instead write the PHASE hash —
#
#     "depends": { "runtime": { "requires": [ "File::Directory::Tree:ver<0.2+>" ] } }
#
# — File::Temp 0.0.12 among them, which is rank 3 by dependents, so every dist
# that needs it inherited the failure. And inside either shape one dependency
# may itself be an object, { "name": "g++", "from": "bin" }, or an
# alternation, { "any": [ "elinks:from<bin>", "lynx:from<bin>" ] }.
#
# Everything below flattens to the identity strings parse-identity() reads.
# `recommends`/`suggests`/`conflicts` are dropped on purpose: zef does not
# install them either. An alternation is NOT chosen between — picking one of
# somebody's alternatives is a policy call an installer should not make
# silently — except when every branch is a system binary or library, which
# nothing here installs anyway, and then there is nothing to report.
sub dep-identities($spec --> List) {
    return () without $spec;
    return ($spec,)                                       if $spec ~~ Str;
    return $spec.map({ dep-identities($_) }).flat.List     if $spec ~~ Positional;
    return () unless $spec ~~ Associative;

    # { name => …, ver|version => …, auth => …, from => … } — one dependency
    # written as an object rather than as an identity string.
    if $spec<name>:exists {
        my $id  = ~$spec<name>;
        my $ver = ~($spec<ver> // $spec<version> // '');
        $id ~= ":ver<$ver>"                if $ver ne '';
        $id ~= ":auth<{$spec<auth>}>"      if ~($spec<auth> // '') ne '';
        $id ~= ":from<{$spec<from>}>"      if ~($spec<from> // '') ne '';
        return ($id,);
    }
    return ()                                             if $spec<any>:exists;

    my @out;
    @out.append(dep-identities($spec{$_})) for <runtime test build>;
    @out.append(dep-identities($spec<requires>));
    @out.List
}

# The half of a spec this installer will not act on: an alternation whose
# branches are not all system binaries. Reported, never guessed at.
sub dep-unresolved($spec --> List) {
    return ()                                              if $spec ~~ Str;
    return $spec.map({ dep-unresolved($_) }).flat.List      if $spec ~~ Positional;
    return () unless $spec ~~ Associative;
    return ()                                              if $spec<name>:exists;
    if $spec<any>:exists {
        my @alts = dep-identities($spec<any>);
        my @raku = @alts.grep({ parse-identity(~$_)<from> ne 'bin' && parse-identity(~$_)<from> ne 'native' });
        return () unless @raku;
        return ('any(' ~ @alts.join(' ') ~ ')',);
    }
    my @out;
    @out.append(dep-unresolved($spec{$_})) for <runtime test build>;
    @out.append(dep-unresolved($spec<requires>));
    @out.List
}

# A dep spelled :from<native> or :from<bin> is a system library or tool, not
# a Raku distribution — reportable, not fetchable.
sub parse-identity(Str $id) {
    my %r = name => $id, ver => '', auth => '', from => '';
    # the NAME may contain '::' — segments joined by double colons; the
    # single-colon adverbs (:ver<>, :auth<>, :from<>) end it
    if $id ~~ / ^ ( [ <-[:]>+ ]+ % '::' ) / {
        %r<name> = ~$0;
    }
    if $id ~~ / ':ver<' (<-[>]>+) '>' / {
        %r<ver> = ~$0;
    }
    if $id ~~ / ':auth<' (<-[>]>+) '>' / {
        %r<auth> = ~$0;
    }
    if $id ~~ / ':from<' (<-[>]>+) '>' / {
        %r<from> = ~$0;
    }
    %r
}

sub ver-segs(Str $v) {
    $v.comb(/\d+/).map(*.Int).List
}

# `want` forms: '' (any), '1.2.3' (exact), '1.2+' (at least), '1.2.*' (prefix).
sub ver-ok(Str $have, Str $want) {
    return True if $want eq '' || $want eq '*';
    return False if $have eq '';
    my $w = $want;
    my $plus = $w.ends-with('+');
    $w = $w.chop if $plus;
    my $star = $w.ends-with('.*');
    $w = $w.substr(0, $w.chars - 2) if $star;
    my @a = ver-segs($have);
    my @b = ver-segs($w);
    if $star {
        for @b.kv -> $i, $seg {
            return False if (@a[$i] // 0) != $seg;
        }
        return True;
    }
    for ^(@a.elems max @b.elems) -> $i {
        my $x = @a[$i] // 0;
        my $y = @b[$i] // 0;
        if $x != $y {
            return $plus ?? $x > $y !! False;
        }
    }
    True
}

sub newer(Str $a, Str $b) {
    my @a = ver-segs($a);
    my @b = ver-segs($b);
    for ^(@a.elems max @b.elems) -> $i {
        my $x = @a[$i] // 0;
        my $y = @b[$i] // 0;
        return $x > $y if $x != $y;
    }
    False
}

sub fetch-text(Str $url) {
    my $p = run 'curl', '-sSL', '--fail', '--max-time', '120', $url, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    die "fetch failed: $url\n$err" if $p.exitcode != 0;
    $out
}

sub fetch-file(Str $url, Str $to) {
    # file:// is a real URL that names a real local file, so `rakupp install
    # file:///…/Foo.tar.gz` works and the installer gate can exercise the whole
    # URL path — fetch, unpack, find the dist, test, install — without a network.
    my $src = $url.starts-with('file://')
        ?? $url.subst(/^ 'file://' ['localhost']? /, '')
        !! $url;
    # a local-file index (RAKUPP_INSTALL_INDEX=/path) yields local archive
    # paths — the t/install suite and offline mirrors
    if !$src.starts-with('http') {
        die "no such archive: $src" unless $src.IO.f;
        $to.IO.spurt($src.IO.slurp(:bin), :bin);
        return;
    }
    my $p = run 'curl', '-sSL', '--fail', '--max-time', '600', '-o', $to, $src, :out, :err;
    my $err = $p.err.slurp(:close);
    die "fetch failed: $url\n$err" if $p.exitcode != 0;
}

# LOWERCASE, because it is compared against the hash in a fez archive's own URL.
#
# The engine's own SHA-1 first. The three command-line tools below are a POSIX
# assumption that Windows does not meet: it ships curl.exe and tar.exe (both in
# System32 since Windows 10 1803), but no shasum, no sha1sum and no openssl —
# so `rakupp install` on Windows reached the M2 checksum gate and died there
# with "no SHA-1 tool found", having already fetched the archive. Nothing in CI
# noticed, because the installer gate is POSIX-only.
#
# The primitive was there the whole time: sha1-blob has used it since the
# store check made 623 subprocesses too slow to bear. This one just never got
# it. The subprocess chain stays as the fallback, for running this program
# under Rakudo, where `::('&rakupp-sha1-hex')` is Nil.
sub sha1-file(Str $path) {
    my $native = try ::('&rakupp-sha1-hex');
    if $native ~~ Callable {
        my $bytes = try $path.IO.slurp(:bin);
        return ~$native($bytes).lc with $bytes;
    }
    for ('shasum', '-a', '1'), ('sha1sum',), ('openssl', 'sha1', '-r') -> @tool {
        my $p = try run |@tool, $path, :out, :err;
        next unless $p && $p.exitcode == 0;
        my $line = $p.out.slurp(:close);
        return ~$0 if $line ~~ / ( <[0..9 a..f]> ** 40 ) /;
    }
    die "no SHA-1 tool found (need shasum, sha1sum or openssl) — refusing to install unverified archives";
}

# The SHA-1 of a FILE's bytes, uppercase, for the store's content-addressed
# blobs. Hashed in-process: the store check runs this over every blob of every
# distribution it verifies (623 blobs, 12.8 MB, 0.17s here), and sha1-file
# spawns a subprocess per call — 623 of those is the forty seconds sha1-str
# already documents. The subprocess path stays as the fallback for running
# this tool under Rakudo. '' when the file cannot be read, which the callers
# treat as "not the bytes it was named for".
sub sha1-blob(IO::Path $f --> Str) {
    my $native = try ::('&rakupp-sha1-hex');
    if $native ~~ Callable {
        my $bytes = try $f.slurp(:bin);
        return '' without $bytes;
        return ~$native($bytes).uc;
    }
    my $hex = try sha1-file($f.absolute);
    $hex.defined ?? ~$hex.uc !! ''
}

# Where a blob id lives, or Nil. sources/ holds modules, resources/ the rest,
# and bin/ is where an older layout put bin scripts (the named wrappers beside
# them are not content-addressed and no record names them).
sub blob-path(IO::Path $p, Str $sha) {
    <sources resources bin>.map({ $p.add($_).add($sha) }).first(*.e)
}

# $*HOME, not %*ENV<HOME>. Windows does not set HOME, so `%*ENV<HOME>.IO` was
# a method call on Any there and `rakupp install Foo` died with
# "No such method 'IO' for invocant of type 'Any'" before it fetched anything.
# $*HOME falls back to USERPROFILE and HOMEDRIVE+HOMEPATH, as Rakudo's does.
sub home-dir(--> IO::Path) {
    my $h = $*HOME;
    die "cannot find your home directory (no HOME, USERPROFILE or HOMEDRIVE+HOMEPATH)"
        without $h;
    $h
}

sub cache-dir {
    my $d = home-dir().add('.raku').add('rakupp-install');
    $d.mkdir unless $d.d;
    $d
}

# ---- the trace log ----------------------------------------------------------
# Every actionable run appends a step-by-step account of itself — engine
# build, OS, arguments, resolution, fetches, checksums, hook and suite
# verdicts, store writes — to ~/.raku/rakupp-install/trace.log, so "install
# did not work on my machine" can arrive as one attachable file instead of a
# recollection of a terminal. A failure prints the file's path. Append-only
# across runs (a later --list must not erase the failure before it); at
# 512 KB the file rotates once, to trace.log.1. Tracing is a bystander:
# every write is a `try`, and a HOME where nothing can be written turns the
# whole thing off rather than adding a failure mode of its own.
my $TRACE-PATH = '';

sub trace(Str $msg) {
    return unless $TRACE-PATH;
    try $TRACE-PATH.IO.spurt("{DateTime.now.Str.substr(11, 8)} $msg\n", :append);
}

# Child output quoted into the trace is capped: a chatty suite must not turn
# the log into the thing nobody attaches because it is 40 MB.
sub clip(Str $s, Int $max = 8192) {
    $s.chars <= $max
        ?? $s
        !! $s.substr(0, $max) ~ "\n[… clipped {$s.chars - $max} more chars]"
}

sub trace-start() {
    try {
        my $f = cache-dir.add('trace.log');
        if $f.e && $f.s > 512 * 1024 {
            my $old = cache-dir.add('trace.log.1');
            $old.unlink if $old.e;
            $f.rename($old.Str);
        }
        $TRACE-PATH = $f.Str;
        $f.spurt("\n==== rakupp install trace | {DateTime.now.Str} ====\n", :append);
        # the engine's own stamp — WHICH BUILD is the first question of every
        # report, and it is not knowable from inside the language, so ask the
        # binary the way a person would
        my $stamp = 'unknown';
        with try run $*EXECUTABLE.absolute, '--version', :out, :err {
            my @l = .out.slurp(:close).lines;
            .err.slurp(:close);
            $stamp = (@l[0], @l[2]).grep({ .defined && .chars }).join(' | ') if .exitcode == 0;
        }
        trace("engine: $stamp");
        trace("os: {$*KERNEL.name} / {(try $*DISTRO.Str) // '?'}");
        trace("exe: {$*EXECUTABLE.absolute}");
        trace("argv: " ~ (@*ARGS ?? @*ARGS.join(' ') !! '(none)'));
    }
}

sub trace-pointer() {
    note "trace: $TRACE-PATH — attach this file when reporting the problem"
        if $TRACE-PATH;
}

# ---- quiet ------------------------------------------------------------------
# -q / --quiet (issue #50, the same option every rakupp mode takes). The lines
# that only NARRATE — an index or archive being fetched, a hook building, a
# suite running, a dist installed, a plan entry the store already holds,
# `done:` — go through progress() (stderr, where `note` had them) or inform()
# (stdout, where `say` had them), and both fall silent under -q. Warnings,
# refusals, failures, usage, and the PRODUCTS of --list, --check and
# --dry-run keep their plain say/note: a quiet run that succeeds prints
# nothing at all, and one that fails prints exactly what it always did.
# --list's product is its identity lines; the detail under each (who
# installed the dist, where its module files are, which commands it put in
# bin/) is the one report part -q drops — see list-installed.
# The trace log is the record, not chatter, and is written either way.
my $QUIET = False;
sub progress(Str $msg) { note $msg unless $QUIET }
sub inform(Str $msg)   { say  $msg unless $QUIET }

# The wrappers the engine writes carry a `raku` shebang (Rakudo's own
# template, so they run under either engine) — whether that name resolves on
# THIS machine decides whether a freshly installed command runs at all.
sub raku-on-path(--> Bool) {
    my $sep = $*KERNEL.name.starts-with('win') ?? ';' !! ':';
    ?((%*ENV<PATH> // '').split($sep).first({ $_ ne '' && .IO.add('raku').e }))
}

# The link target for a `raku` name this installer provides: the PATH-stable
# spelling of the engine when there is one (…/bin/rakupp survives a Homebrew
# upgrade; the versioned Cellar path $*EXECUTABLE resolves to does not),
# else the running binary's real path.
sub rakupp-target(--> Str) {
    my $sep = $*KERNEL.name.starts-with('win') ?? ';' !! ':';
    my $hit = (%*ENV<PATH> // '').split($sep).first({ $_ ne '' && .IO.add('rakupp').e });
    $hit ?? $hit.IO.add('rakupp').Str !! $*EXECUTABLE.absolute.Str
}

# A machine where NOTHING answers to `raku` would run every wrapper into
# "env: raku: No such file or directory" — the freshly installed command
# dead on arrival. So the store's own bin/ — the one directory the user
# already must put on PATH for named commands to work at all — gains a
# `raku` symlink to this engine. Guarded three ways: only when no `raku`
# resolves anywhere on PATH (a machine with Rakudo keeps its Rakudo), only
# when the name is free in the store (a dangling leftover counts as free),
# and said out loud when it happens. Uninstall leaves it alone: it is store
# infrastructure, like the bin/ directory itself, not any dist's file —
# the checker knows it by name.
my $RAKU-NAME-DONE = False;
sub ensure-raku-name(Str $prefix) {
    return if $RAKU-NAME-DONE;
    $RAKU-NAME-DONE = True;
    return if $*KERNEL.name.starts-with('win');   # shebangs are a POSIX story
    if raku-on-path() {
        trace("env: `raku` resolves on PATH — wrapper shebangs will run");
        return;
    }
    my $link = $prefix.IO.add('bin').add('raku');
    if $link.e {
        trace("env: {$link} already exists — leaving it");
        return;
    }
    try $link.unlink;   # .e is false for a DANGLING symlink; clear it
    my $target = rakupp-target();
    # `ln`, not .symlink: the engine builtin does not exist yet, and this
    # file stays runnable under Rakudo either way
    my $p = try run 'ln', '-s', $target, $link.Str, :out, :err;
    if $p && $p.exitcode == 0 && $link.e {
        progress("linked: {$link} -> $target (no `raku` was on PATH, and the bin wrappers' shebang needs one)");
        trace("env: linked {$link} -> $target");
    }
    else {
        trace("env: no `raku` on PATH and linking {$link} failed — wrappers run as `rakupp <wrapper>`");
    }
}

sub sha1-str(Str $s) {
    # The engine's own SHA-1 when this tool runs under rakupp — hashing a
    # short string by writing a temp file and SPAWNING shasum cost a whole
    # subprocess per index key, and `uninstall fez` (~70 keys) spent forty
    # seconds looking hung. The subprocess path stays as the fallback for
    # running this tool under Rakudo.
    my $native = try ::('&rakupp-sha1-hex');
    return $native($s).uc if $native ~~ Callable;
    my $t = $*TMPDIR.add("rakupp-install-sha-$*PID");
    $t.spurt($s);
    # guarded like the other cleanups in this file: the `return` above skips this
    # declaration whenever the engine HAS a native sha1, and a phaser still runs
    # for a block it is leaving — with $t undefined (Rakudo does the same)
    LEAVE { .unlink with $t }
    # UPPERCASE: the engine names short/ index directories in uppercase hex,
    # and on a case-SENSITIVE filesystem a lowercase path is a different path
    # — the checker reported every provided module as broken on Linux, and
    # uninstall left dangling index entries, while macOS's case-insensitive
    # APFS hid both. sha1-file stays lowercase for the fez archive stems.
    sha1-file($t.Str).uc
}

# The store is shared with zef and Rakudo: every mutation happens under its
# repo.lock (a real flock — the engine builtin; on Windows the lock call
# answers -1 and we proceed unlocked, which the docs say out loud). The
# builtins are looked up DYNAMICALLY so this file stays compilable by other
# engines (Rakudo's undeclared-routine check is static and fires even on
# calls that would never run there); an engine without them proceeds
# unlocked, exactly like Windows.
sub with-repo-lock(Str $prefix, &code) {
    my $lock   = try ::('&rakupp-repo-lock');
    my $unlock = try ::('&rakupp-repo-unlock');
    my $tok = $lock ~~ Callable ?? $lock("$prefix/repo.lock") !! -1;
    LEAVE $unlock($tok) if $tok >= 0 && $unlock ~~ Callable;
    code()
}

# Provenance: the dist-ids THIS TOOL installed, one per line. Uninstall
# refuses to touch anything else by default — a distribution zef put there
# is zef's, and removing it silently is a surprise in someone else's tool.
sub owned-file(Str $prefix) {
    $prefix.IO.add('rakupp-install').add('owned')
}

sub record-owned(Str $prefix, Str $dist-id) {
    my $f = owned-file($prefix);
    $f.parent.mkdir unless $f.parent.d;
    # push into ONE array — `(@have, $x).flat.join` space-joins the array
    # whole under rakupp (flat does not descend into it), which silently
    # wrote several ids onto one line and broke every later lines-match
    my @all = $f.e ?? $f.lines.grep(*.chars) !! ();
    return if @all.first(* eq $dist-id);
    @all.push($dist-id);
    $f.spurt(@all.join("\n") ~ "\n");
}

sub is-owned(Str $prefix, Str $dist-id) {
    my $f = owned-file($prefix);
    $f.e && ?$f.lines.first(* eq $dist-id)
}

sub drop-owned(Str $prefix, Str $dist-id) {
    my $f = owned-file($prefix);
    return unless $f.e;
    $f.spurt($f.lines.grep(* ne $dist-id).join("\n") ~ "\n");
}

sub load-index(Bool $refresh) {
    # RAKUPP_INSTALL_INDEX overrides the source: a file path or URL. This is
    # how the t/install suite runs without the network, and how a mirror or
    # REA snapshot slots in.
    my $override = %*ENV<RAKUPP_INSTALL_INDEX> // '';
    if $override ne '' && !$override.starts-with('http') {
        trace("index: local file $override");
        return json-decode($override.IO.slurp);
    }
    my $url = $override ne '' ?? $override !! $INDEX-URL;
    my $cache = cache-dir.add('index.json');
    if !$refresh && $cache.e && (now.to-posix[0] - $cache.modified.to-posix[0]) < $CACHE-TTL {
        trace("index: cache hit ({$cache}, age {((now.to-posix[0] - $cache.modified.to-posix[0]) / 60).Int} min)");
        return json-decode($cache.slurp);
    }
    progress("fetching ecosystem index: $url");
    trace("index: fetching $url");
    my $text = fetch-text($url);
    my $idx = json-decode($text);    # parse BEFORE caching: a bad fetch must not poison the cache
    $cache.spurt($text);
    $idx
}

# The REA archive index, loaded lazily on the first zef-index miss. ~18 MB,
# one entry per released dist-version; the engine's JSON codec parses it in
# well under a second, so the whole index is held rather than line-scanned.
my @REA;
my $REA-STATE = 'unloaded';
my $REA-REFRESH = False;

sub rea-index() {
    return @REA if $REA-STATE eq 'ready';
    $REA-STATE = 'ready';
    my $override = %*ENV<RAKUPP_INSTALL_REA_INDEX> // '';
    if $override eq '' && (%*ENV<RAKUPP_INSTALL_INDEX> // '') ne '' {
        # a mirror or the offline suite overrode the zef index without
        # giving an REA source — no surprise fallback, no surprise network
        return @REA;
    }
    if $override ne '' && !$override.starts-with('http') {
        trace("rea-index: local file $override");
        @REA = json-decode($override.IO.slurp).list;
        return @REA;
    }
    my $url = $override ne '' ?? $override !! $REA-INDEX-URL;
    my $cache = cache-dir.add('rea-meta.json');
    if !$REA-REFRESH && $cache.e && (now.to-posix[0] - $cache.modified.to-posix[0]) < $CACHE-TTL {
        trace("rea-index: cache hit ({$cache})");
        @REA = json-decode($cache.slurp).list;
        return @REA;
    }
    progress("fetching the REA archive index: $url");
    trace("rea-index: fetching $url");
    my $text = fetch-text($url);
    my $idx = json-decode($text);    # parse BEFORE caching — same rule as the zef index
    $cache.spurt($text);
    @REA = $idx.list;
    @REA
}

# Names the ENGINE provides — never fetched, however a dist spells the dep.
# (The REA archive even carries a `Rakudo` pseudo-dist that claims to provide
# them, with no archive behind it — candidates() refuses it below.)
my constant @CORE-NAMES = <Test NativeCall lib strict v6 v6.c v6.d v6.e perl6 Perl6
                           experimental newline MONKEY MONKEY-TYPING nqp>;

# Every index entry providing `name` (as dist name or module), constraints
# applied, newest version first.
sub candidates(@index, %want) {
    my @c = @index.grep(-> %e {
        (%e<name> // '') ne 'Rakudo'    # the REA pseudo-dist: provides core, has no archive
    }).grep(-> %e {
        # the extra parens are load-bearing: a subscript adverb binds to the
        # TOP operator of its expression, so without them `:exists` lands on
        # the `||` and Rakudo refuses to compile (rakupp is laxer — it binds
        # the adverb to the subscript either way)
        (%e<name> // '') eq %want<name> || ((%e<provides> // {}){%want<name>}:exists)
    });
    @c .= grep(-> %e { ver-ok(%e<version> // '', %want<ver>) });
    @c .= grep(-> %e { (%e<auth> // '') eq %want<auth> }) if %want<auth> ne '';
    # A dist NAMED for what was asked outranks one that merely lists the module
    # in `provides`, however much newer the second is. Real case: `Log` is a
    # dist (0.3.2), and Logger 0.4.0's META claims to provide `Log` as well —
    # a typo its own 0.4.1 corrected. On version alone Logger 0.4.0 won, so
    # `install Log` installed Logger, whose Logger.rakumod opens `use
    # Logger::NDC` — a module 0.4.0 registers under the wrong name — and
    # every dist that loads Log (Config #50) died at `use Log`.
    @c.sort(-> %a, %b {   # exact dist name first, then newest version
        my $an = (%a<name> // '') eq %want<name>;
        my $bn = (%b<name> // '') eq %want<name>;
        $an != $bn ?? ($an ?? Order::Less !! Order::More)
        !! newer(%a<version> // '', %b<version> // '') ?? Order::Less
        !! newer(%b<version> // '', %a<version> // '') ?? Order::More
        !! Order::Same
    }).List
}

# The store as index-shaped entries, so candidates() can be asked "does
# anything already installed satisfy this?" with the same matcher the index
# gets. Only the fields that matcher reads — there is no archive behind these.
sub installed-entries(Str $prefix) {
    my @e;
    my $d = $prefix.IO.add('dist');
    return @e unless $d.d;
    for $d.dir.grep(*.f) -> $f {
        my %m = try json-decode($f.slurp);
        next unless %m && %m<name>;
        @e.push: { name     => %m<name>,
                   version  => (%m<version> // %m<ver> // ''),
                   auth     => (%m<auth> // ''),
                   api      => (%m<api> // ''),
                   provides => (%m<provides> // {}) };
    }
    @e
}

# The dependency-first install plan for the requested identities. Each plan
# entry is the index entry hash; %notes collects what was skipped and why.
sub resolve(@index, @wants, %notes, Str :$prefix = '') {
    my @plan;
    my %planned;   # dist identity -> True
    my @installed = $prefix eq '' ?? () !! installed-entries($prefix);
    my @work = @wants.map({ parse-identity($_) });
    while @work {
        my %want = @work.shift;
        next if %want<from> eq 'native' | 'bin';
        next if %want<name> eq any(@CORE-NAMES);   # the engine ships these
        my @c = candidates(@index, %want);
        # The archive keeps EXACT identities the live index has dropped —
        # consult it before loosening any pin (the zef-then-REA order zef
        # itself uses). Real case: JSON::Fast:ver<0.19>:auth<cpan:TIMOTIMO>
        # predates the author's cpan→zef migration and exists only in REA.
        if !@c {
            @c = candidates(rea-index(), %want);
            progress("note: {%want<name>} — not in the zef index, resolved from the REA archive")
                if @c;
        }
        my $pin = %want<name>
            ~ (%want<ver>  ne '' ?? ":ver<{%want<ver>}>"   !! '')
            ~ (%want<auth> ne '' ?? ":auth<{%want<auth>}>" !! '');
        # Neither index has it — but the STORE might, and a dist installed
        # from a checkout (`rakupp install .`) is in no index at all. That is
        # exactly what a `ver<X+>` floor on an unreleased version describes.
        # Satisfied is satisfied: nothing to plan, and nothing below runs —
        # the loosening under it would answer "at least X" with an older
        # release and downgrade the dependency out from under whoever asked.
        if !@c && @installed {
            my @have = candidates(@installed, %want);
            if @have {
                my %h = @have[0];
                progress("note: $pin — not in the index; the installed "
                       ~ "{%h<name>}:ver<{%h<version>}>:auth<{%h<auth>}> satisfies it");
                %planned{%h<name>} = True;
                %planned{$_} = True for (%h<provides> // {}).keys;
                next;
            }
        }
        # A pin that matches nothing ANYWHERE loosens, loudly. :auth goes
        # first — the cpan→zef migrations are why this exists — and the
        # VERSION only when the pin is not a floor: `X+` is a requirement,
        # and the one answer it cannot take is a release older than X.
        my $floor = %want<ver>.ends-with('+');
        if !@c && %want<auth> ne '' {
            my %no-auth = name => %want<name>, ver => %want<ver>, auth => '', from => %want<from>;
            @c = candidates(@index, %no-auth);
            @c = candidates(rea-index(), %no-auth) unless @c;
            progress("note: $pin is not in the index — using {@c[0]<dist> // @c[0]<name>} (the pin may predate an ecosystem migration)")
                if @c;
        }
        if !@c && %want<ver> ne '' && !$floor {
            my %bare = name => %want<name>, ver => '', auth => '', from => %want<from>;
            @c = candidates(@index, %bare);
            if @c {
                progress("note: $pin is not in the index — using {@c[0]<dist> // @c[0]<name>} (the pin may predate an ecosystem migration)");
            }
            else {
                @c = candidates(rea-index(), %bare);
                progress("note: $pin matches nothing — using {@c[0]<dist> // @c[0]<name>} from the REA archive")
                    if @c;
            }
        }
        if !@c {
            unless %planned{%want<name>} {   # a planned dist also PROVIDES names
                %notes{%want<name>} = $floor
                    ?? "nothing at {%want<ver>} in the index, the archive or the store"
                    !! 'not in the ecosystem index';
            }
            next;
        }
        my %e = @c[0];
        my $identity = %e<dist> // "%e<name>:ver<{%e<version> // ''}>";
        next if %planned{$identity};
        %planned{$identity} = True;
        %planned{$_} = True for (%e<provides> // {}).keys;
        %planned{%e<name>} = True;
        # build-depends power the Build.rakumod hook, test-depends the suite —
        # both run here, so both install (zef's default stance)
        for <depends build-depends test-depends> -> $field {
            @work.push(parse-identity($_)) for dep-identities(%e{$field});
            %notes{$_} = 'an alternation this installer does not choose between'
                for dep-unresolved(%e{$field});
        }
        @plan.push(%e);
    }
    # Dependencies first — a topological order over the plan, NOT the reverse
    # of discovery. Discovery is breadth-first, and reversing it puts a dist
    # before its dependency whenever that dependency was discovered earlier
    # by another route: `install File::Directory::Tree OpenSSL` names both,
    # so the tree module is queued first and OpenSSL second, and the reverse
    # ran OpenSSL's Build.rakumod — which imports the tree module — against
    # a store that did not hold it yet (issue #49). Any dist whose META lists
    # a dependency before a sibling that itself needs it hit the same wall.
    plan-order(@plan)
}

# Depth-first over the plan: a dist is emitted after every planned dist it
# depends on. Dependencies name MODULES and plan entries name dists, so the
# lookup goes through `provides` too. A cycle (two dists that test-depend on
# each other) breaks at the back edge — whichever was discovered first goes
# first — rather than looping.
sub plan-order(@plan --> List) {
    my %at;   # every module or dist name a plan entry answers to -> its index
    for @plan.kv -> $i, %e {
        %at{%e<name>} //= $i;
        %at{$_} //= $i for (%e<provides> // {}).keys;
    }
    my @order;
    my @state = 0 xx @plan.elems;   # 0 unseen, 1 on the path, 2 emitted
    sub visit(Int $i) {
        return if @state[$i];
        @state[$i] = 1;
        for <depends build-depends test-depends> -> $field {
            for dep-identities(@plan[$i]{$field}) -> $id {
                my $j = %at{parse-identity(~$id)<name>};
                visit($j) if $j.defined && $j != $i;
            }
        }
        @state[$i] = 2;
        @order.push(@plan[$i]);
    }
    visit($_) for ^@plan.elems;
    @order.List
}

sub base-url {
    my $override = %*ENV<RAKUPP_INSTALL_INDEX> // '';
    if $override ne '' {
        return $override.starts-with('http')
            ?? $override.subst(/ '/' <-[/]>+ $ /, '')
            !! $override.IO.parent.Str;
    }
    'https://360.zef.pm'
}

# fez entries carry a store-relative `path`; REA entries carry an absolute
# `source-url`. Either way, this is where the archive lives.
sub archive-url(%e) {
    %e<source-url> // (base-url() ~ '/' ~ (%e<path> // ''))
}

# A PATH identity — an argument that names a directory holding a distribution
# (META6.json at its root), never an ecosystem module. `rakupp install .` is
# the development loop: THIS dist, its ecosystem dependencies first, no fetch
# and no checksum for the dist itself (there is no archive to hash — the
# directory is the source of truth, and the test gate still stands between it
# and the store).
#
# zef's rule is `.`- or `/`-prefixed and nothing else, and it was copied here
# verbatim. It is a rule about SPELLING, and people do not spell a directory
# that way when it is sitting in front of them: they type its name. So this
# went further than zef's — see is-path-arg() below for the shapes and the one
# filesystem question it asks.
#
# A URL is not a path either, but it is not a name — it is a third thing the
# installer does not do, and it should say so instead of spending two index
# downloads finding out. url-arg() below is that check.
#
# `rakupp install https://…` used to look the whole URL up as a distribution
# NAME: the zef index, a miss, REA's ~18 MB, another miss, and a "cannot
# resolve" that described the URL as if it were a module nobody had published.
sub url-arg(Str $arg --> Bool) {
    so $arg ~~ /^ <[A..Za..z]> <[A..Za..z 0..9 + . -]>* '://' /
}

# An IDENTITY, and therefore never a path however the filesystem is arranged:
# `Foo::Bar`, or anything carrying a version/auth/api/from adverb. This is the
# guard that lets the shape rules below be generous — whatever a directory in
# the way happens to be called, `Foo::Bar` and `Foo:ver<1.2>` still resolve in
# the ecosystem.
sub identity-arg(Str $arg --> Bool) {
    so $arg.contains('::') || $arg ~~ / ':' <[a..z]>+ '<' /
}

# `~/dists/Foo` from a shell that does not expand tilde — which on Windows is
# every shell, and on POSIX is any quoted argument. The expansion is this
# program's, so it is the same everywhere: only a leading `~` alone or a
# leading `~/` (`~\` on Windows), never `~user`, whose home only the system
# knows.
sub expand-tilde(Str $arg --> Str) {
    return $arg unless $arg eq '~' || $arg.starts-with('~/') || $arg.starts-with('~\\');
    my $home = try home-dir();
    return $arg without $home;
    $arg eq '~' ?? $home.Str !! $home.add($arg.substr(2)).Str
}

# Is this argument a PATH to a distribution, rather than a name to resolve?
#
# It used to be `.` or `/` and nothing else, which misread five shapes and
# misread them expensively — anything not a path becomes a name, a name goes to
# the zef index, and a zef miss is what lazily pulls REA's ~18 MB.
#
#   C:\dist, C:/dist   a Windows absolute path. So `rakupp install C:\…` was a
#                      name lookup, and an absolute path could not be installed
#                      from on Windows at all; only `.\dist` worked.
#   \\server\share     a UNC path, same story.
#   dists/Foo          a relative path that does not START with a separator but
#                      plainly contains one. No distribution name holds `/` or
#                      `\` — identity-arg() above has already taken the two
#                      spellings that could — so a separator anywhere is a path.
#   Foo                the bare directory name, `rakupp install Foo` beside a
#                      Foo/ checkout, which is what a person types before they
#                      type `./Foo`. This one is decided by the filesystem
#                      rather than by shape: a path only when the directory
#                      exists AND holds the META6.json that makes it a
#                      distribution root. A bare word that is not that stays a
#                      name, so `rakupp install Test` still means the module
#                      even in a tree with a Test/ directory in it.
#   META6.json         the file that MAKES a directory a distribution root, so
#                      a path to it is a path to the distribution — tab
#                      completion hands it over, and it was "not a directory".
#
# The shape rules do NOT consult the filesystem, on purpose: `./nope` must fail
# as a missing directory, not travel to the ecosystem index as a module name.
sub is-path-arg(Str $arg is copy) {
    return False if url-arg($arg) || identity-arg($arg);
    $arg = expand-tilde($arg);
    return True if $arg.starts-with('.') || $arg.starts-with('/');
    return True if $arg.starts-with('\\');                      # UNC, or a rooted Windows path
    return True if $arg ~~ /^ <[A..Za..z]> ':' <[\\ /]> /;        # C:\dist or C:/dist
    return True if $arg.contains('/') || $arg.contains('\\');   # dists/Foo, dists\Foo
    so (try ($arg.IO.d && $arg.IO.add('META6.json').e)
            || ($arg eq 'META6.json' && $arg.IO.f))
}

# The distribution directory a path argument names. Tilde-expanded, so every
# reader of a path argument gets the same answer as is-path-arg() did, and a
# path to the META6.json stands for the directory holding it.
sub arg-path(Str $arg --> IO::Path) {
    my $p = expand-tilde($arg).IO;
    $p.basename eq 'META6.json' && $p.f ?? $p.parent !! $p
}

# A URL that names a distribution, turned into (tarball-url, subdirectory).
#
# Two shapes are understood. A tarball URL is taken as it is. A GitHub page URL
# is rewritten to the tarball GitHub already serves for it — because the URL a
# person has in their hand is the one from the address bar, not one they would
# have to construct:
#
#   …/O/R                          the default branch (main, then master)
#   …/O/R/tree/REF                 that branch or tag
#   …/O/R/tree/REF/sub/dir         …and the distribution inside it, which is
#                                  how a monorepo of modules is laid out
#
# /archive/REF.tar.gz rather than codeload: it resolves branches AND tags
# through one spelling, and curl -sSL is already following redirects.
sub github-tarball(Str $url) {
    my $u = $url.subst(/ '/' + $ /, '').subst(/ '.git' $ /, '');
    return Nil unless $u ~~ m{ ^ 'https://' ['www.']? 'github.com/'
                               $<owner>=<-[/]>+ '/' $<repo>=<-[/]>+
                               [ '/tree/' $<ref>=<-[/]>+ [ '/' $<sub>=(.+) ]? ]? $ };
    my $owner = ~$<owner>;
    my $repo  = ~$<repo>;
    my $sub   = $<sub>.defined ?? ~$<sub> !! '';
    # No ref in the URL: GitHub does not say which branch is default without an
    # API call, so try the two names that are it in practice. HEAD.tar.gz would
    # be one request, but GitHub does not serve it.
    my @refs = $<ref>.defined ?? (~$<ref>,) !! <main master>;
    # A HASH, not a two-element list: `my ($u, $s) = f()` flattens the list of
    # candidate URLs into the first variable and slides $sub into the second,
    # which is a quiet wrong answer rather than an error.
    %( urls => [@refs.map({ "https://github.com/$owner/$repo/archive/$_.tar.gz" })],
       sub  => $sub )
}

sub archive-url-arg(Str $url) {
    with github-tarball($url) -> %gh { return %gh }
    return %( urls => [$url], sub => '' ) if $url ~~ / [ '.tar.gz' | '.tgz' ] $ /;
    die "$url: not a distribution URL\n"
      ~ "  understood: a .tar.gz archive, or a github.com repo/tree page\n"
      ~ "  (a name resolves through the ecosystem index: rakupp install Prompt::Hidden)";
}

# Fetch a URL, unpack it, and hand back the same entry shape a directory on
# disk produces — everything after this point (dependencies, the build hook,
# the test gate, the store write) is then the local-directory path exactly.
#
# No checksum is possible: nothing in the URL names the bytes it should
# deliver, which is the REA situation, and it earns the same note rather than
# a quieter one.
sub url-dist-entry(Str $url) {
    my %spec = archive-url-arg($url);
    my @urls = @(%spec<urls>);
    my $sub  = %spec<sub>;
    my $tmp = $*TMPDIR.add("rakupp-install-url-{$*PID}-{$url.subst(/<-alnum>/, '-', :g).substr(*-40)}");
    $tmp.mkdir;
    my $tarball = $tmp.add('dist.tar.gz').Str;

    my $err = '';
    my $got = @urls.first({
        progress("fetching $_");
        trace("url: fetching $_");
        my $ok = try { fetch-file($_, $tarball); True };
        $err = ~$! unless $ok;
        $ok
    });
    die "$url: could not fetch it\n  $err" without $got;
    note "note: a URL carries no checksum — TLS is the only integrity here";
    trace("url: fetched {$tarball.IO.s} bytes from $got");

    my $p = run 'tar', '-xzf', $tarball, '-C', $tmp.Str, :err;
    die "$url: tar failed: {$p.err.slurp(:close)}" if $p.exitcode != 0;

    # A GitHub archive unpacks under one directory named <repo>-<ref>, and a
    # ref with a slash in it is not spelled the way the URL spells it. So find
    # the directory rather than predicting its name.
    my $top = $tmp.add('META6.json').e
        ?? $tmp
        !! ($tmp.dir.grep({ .d && .basename ne 'dist.tar.gz' }).first
            // die "$url: the archive unpacked to nothing");
    my $root = $sub ?? $top.add($sub) !! $top;
    die "$url: no META6.json in {$sub || 'the archive'}\n"
      ~ "  (a subdirectory of a monorepo needs the path to the dist itself:\n"
      ~ "   …/tree/main/Prompt-Hidden, not …/tree/main)"
        unless $root.add('META6.json').e;

    my %e = local-dist-entry($root.Str);
    %e<source-url> = $url;      # the plan line should print what was ASKED for
    %e
}

sub local-dist-entry(Str $arg) {
    my $root = arg-path($arg).absolute.IO.cleanup;   # `.` spells the cwd, not a path segment to keep
    die "$arg: not a directory" unless $root.d;
    die "$arg: no META6.json at {$root} — not a distribution root"
        unless $root.add('META6.json').e;
    my %m = (try json-decode($root.add('META6.json').slurp))
        // die "$arg: META6.json does not parse";
    die "$arg: META6.json carries no name" unless %m<name>;
    my %e;
    for <name version auth api provides depends build-depends test-depends> -> $k {
        %e{$k} = %m{$k} if %m{$k}:exists;
    }
    %e<dist> = "%m<name>:ver<{%m<version> // ''}>:auth<{%m<auth> // ''}>";
    %e<local-root> = $root.Str;
    %e<source-url> = $root.Str;   # what the plan line prints
    %e
}

# The engine's CompUnit install takes any object with .meta and .IO.
class InstallableDist {
    has %.meta;
    has $.root;
    method IO {
        $.root
    }
}

# zef's build protocols, run BEFORE the tests. One: META6 names a `builder`
# CLASS — the whole META hash goes to its .new, and .build($cwd) compiles
# whatever the dist ships in source form (Term::termios and the LibraryMake
# family write their C helpers into resources/libraries this way; without
# this leg the suite dies loading a dylib nobody built). Two: a Build.rakumod
# (or Build.pm6/Build.pm) at the dist root with `method build($cwd)` —
# OpenSSL generates its resources/libraries.json in it, which is why the
# file is in META resources but in nobody's archive. Either child sees the
# target store as `-I inst#<prefix>`, so the builder dist (a build-depends
# this plan just installed) and any `use JSON::Fast` resolve.
# The ecosystem's build recipes (MakeFromJSON, LibraryMake and their kin) read
# their toolchain out of `$*VM.config`, behind a `$*VM.name eq 'moar'` gate —
# no branch exists for anything else. For the DURATION OF A HOOK the child
# answers in that dialect, with this platform's honest toolchain values;
# nothing outside the hook sees it, and the engine's own identity is
# untouched. Prepended to both hook children (builder class, Build.rakumod).
sub vm-toolchain-shim(--> Str) {
    q:to/SHIM/
        my %tc = do {
            my $dylib = $*KERNEL.name eq 'darwin';
            obj      => '.o',
            dll      => 'lib%s' ~ ($dylib ?? '.dylib' !! '.so'),
            cc       => 'cc',
            ccshared => '-fPIC',
            ccout    => '-o ',
            cflags   => '-O2 -fPIC',
            ld       => 'cc',
            ldshared => ($dylib ?? '-dynamiclib' !! '-shared'),
            ldflags  => '',
            ldlibs   => '',
            ldout    => '-o ',
            ldusr    => '-l%s',
            make     => 'make',
            exe      => '',
        };
        my $*VM = class :: {
            has $.name = 'moar';
            has %.config;
            method platform-library-name($file, :$version) {
                my $s = $file.Str;
                my $slash = $s.rindex('/');
                my ($d, $b) = $slash.defined
                    ?? ($s.substr(0, $slash + 1), $s.substr($slash + 1))
                    !! ('', $s);
                $d ~ 'lib' ~ $b ~ ($*KERNEL.name eq 'darwin' ?? '.dylib' !! '.so')
            }
        }.new(:config(%tc));
        SHIM
}

sub run-build-hook(%e, $root, Str $prefix --> Bool) {
    my $meta-file = $root.IO.add('META6.json');
    my %m = $meta-file.e ?? ((try json-decode($meta-file.slurp)) // {}) !! {};
    if %m<builder> {
        my $builder = ~%m<builder>;
        # zef's shorthand: a bare name lives under Distribution::Builder::
        $builder = "Distribution::Builder::$builder" unless $builder.contains('::');
        # the name is spliced into a generated program: module-name matter only
        unless $builder ~~ /^ <[A..Za..z0..9_:'-]>+ $/ {
            note "BUILD FAILED: builder name '$builder' is not a module name";
            trace("BUILD FAILED: %e<name> — builder name '$builder' is not a module name");
            return False;
        }
        progress("building %e<name>: builder $builder");
        trace("build: %e<name> via builder $builder");
        my $prog = vm-toolchain-shim() ~ q:to/END/.subst('BUILDER', $builder, :g);
            use BUILDER;
            my $t = 'META6.json'.IO.slurp;
            my %m = (try ::('Rakupp::Internals::JSON').from-json($t))
                    // Rakudo::Internals::JSON.from-json($t);
            my $r = ::('BUILDER').new(:meta(%m)).build($*CWD.Str);
            exit(($r === False) ?? 1 !! 0);
            END
        my $p = run $*EXECUTABLE.absolute, '-I', "inst#$prefix", '-I', $root.IO.Str,
                    '-e', $prog, :out, :err, :cwd($root);
        $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);
        if $p.exitcode != 0 {
            note "BUILD FAILED: builder $builder";
            note $err.indent(2);
            trace("BUILD FAILED: builder $builder (exit {$p.exitcode})\n" ~ clip($err).indent(4));
            return False;
        }
        trace("build: builder ok");
        return True;
    }
    my $hook = <Build.rakumod Build.pm6 Build.pm>.map({ $root.IO.add($_) }).first(*.e);
    without $hook {
        trace("build: no hook");
        return True;
    }
    progress("building %e<name>: {$hook.basename}");
    trace("build: %e<name> via {$hook.basename}");
    my $p = run $*EXECUTABLE.absolute, '-I', "inst#$prefix", '-I', $root.IO.Str, '-e',
                vm-toolchain-shim()
                  ~ 'use Build; my $r = Build.new.build($*CWD.Str); exit(($r === False) ?? 1 !! 0)',
                :out, :err, :cwd($root);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    if $p.exitcode != 0 {
        note "BUILD FAILED: {$hook.basename}";
        note $err.indent(2);
        trace("BUILD FAILED: {$hook.basename} (exit {$p.exitcode})\n" ~ clip($err).indent(4));
        return False;
    }
    trace("build: {$hook.basename} ok");
    True
}

sub run-dist-tests(%e, $root, Str $prefix) {
    my @tests = $root.IO.add('t').d
        ?? $root.IO.add('t').dir.grep({ .extension eq 't' | 'rakutest' }).sort
        !! ();
    unless @tests {
        trace("tests: none shipped");
        return True;
    }
    progress("testing %e<name>: {@tests.elems} file{@tests.elems == 1 ?? '' !! 's'}");
    trace("tests: {@tests.elems} file{@tests.elems == 1 ?? '' !! 's'}");
    for @tests -> $t {
        # `-I inst#<prefix>`: the suite must see the dependencies this plan
        # installed into the TARGET store, wherever --to pointed it
        my $p = run $*EXECUTABLE.absolute, '-I', $root.IO.add('lib').Str,
                    '-I', "inst#$prefix", $t.Str,
                    :out, :err, :cwd($root);
        my $out = $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);
        if $p.exitcode != 0 {
            note "FAILED: {$t.basename}";
            note $err.indent(2);
            # the child's own words are the diagnosis a remote report needs
            trace("test FAILED: {$t.basename} (exit {$p.exitcode})\n"
                ~ clip(($err ~ ($out.trim ?? "--- stdout ---\n$out" !! '')).trim).indent(4));
            return False;
        }
    }
    trace("tests: all green");
    True
}

# Does rakupp already ship every module this dist provides? The shadows live in
# `rakulib/` beside the binary (a checkout) or under `libexec/rakupp/` (an
# installed prefix) — the same two places the interpreter looks.
sub rakulib-dirs() {
    state @d = do {
        my $bin = $*EXECUTABLE.parent;
        (($bin.add('../rakulib'), $bin.add('../libexec/rakupp/rakulib'),
          $*CWD.add('rakulib')).grep(*.d).map(*.absolute)).unique;
    }
}
sub shadowed-by-rakulib(%e --> Bool) {
    # The DIST's own name is the question: NativeHelpers::Blob also provides
    # `MoarVM::Guts::REPRs`, which reads MoarVM's object headers and exists for
    # no other purpose — shadowing that would be a lie, and nothing that runs
    # here can want it.
    my $name = %e<name> // '';
    return False unless $name;
    my $rel = $name.split('::').join('/');
    ?rakulib-dirs().first({
        (.IO.add("$rel.rakumod").e || .IO.add("$rel.pm6").e || .IO.add("$rel.raku").e)
    })
}

sub install-one(%e, Str $prefix, Bool :$no-test, Bool :$force, Bool :$test-only,
                :%have, :%broken) {
    my $tmp;   # download scratch — set only when there is a download
    LEAVE { run 'rm', '-rf', $tmp.Str if $tmp }   # never a user's directory
    my $root;
    if %e<local-root> {
        # a path install: the directory IS the dist — nothing to fetch,
        # nothing to hash, nothing to unpack, nothing of the user's to
        # clean up. The build hook and the test gate still stand between
        # it and the store, exactly as for a fetched archive.
        $root = %e<local-root>.IO;
        progress("installing {%e<dist> // %e<name>} from {$root}");
        trace("dist {%e<dist> // %e<name>}: local directory {$root} — no fetch, no checksum");
    }
    else {
        my $url = archive-url(%e);
        $tmp = $*TMPDIR.add("rakupp-install-{$*PID}-{%e<name>.subst(/<-alnum>/, '-', :g)}");
        $tmp.mkdir;

        my $tarball = $tmp.add('dist.tar.gz').Str;
        progress("fetching $url");
        trace("dist {%e<dist> // %e<name>}: fetching $url");
        fetch-file($url, $tarball);
        trace("fetched: {$tarball.IO.s} bytes");

        # The archive must hash to the SHA-1 its URL names (fez archives are
        # content-addressed). Refuse anything else — this is the checksum gate
        # the plan's M2 requires, and it holds for mirrors too. REA archive
        # URLs carry no hash and fall to the TLS-only note below.
        if $url ~~ / ( <[0..9 a..f]> ** 40 ) '.tar.gz' $ / {
            my $want = ~$0;
            my $got = sha1-file($tarball);
            die "checksum mismatch for %e<name>: archive is $got, index says $want"
                if $got ne $want;
            trace("checksum: ok ($want)");
        }
        else {
            note "note: index path carries no checksum for %e<name> — TLS is the only integrity here";
            trace("checksum: none in the index path — TLS only");
        }

        my $p = run 'tar', '-xzf', $tarball, '-C', $tmp.Str, :err;
        die "tar failed: {$p.err.slurp(:close)}" if $p.exitcode != 0;

        # the dist root is wherever META6.json landed (top level, or one dir down)
        $root = $tmp.add('META6.json').e
            ?? $tmp
            !! $tmp.dir.first({ .d && .add('META6.json').e })
                // die "no META6.json in %e<name>'s archive";
    }

    my %meta = json-decode($root.add('META6.json').slurp);
    trace("meta: {%meta<name> // '?'} ver<{%meta<version> // '?'}> auth<{%meta<auth> // ''}> (root {$root.basename})");

    # The index and the dist can disagree about auth — REA rewrites it, so
    # Hash::Merge 2.0.0 is auth<cpan:TYIL> in the index and auth<github:scriptkitties>
    # in its own META6.json. The store is keyed by what the META says, so the
    # plan's pre-check asked with the wrong identity and missed. Ask again with
    # the dist's own identity, now that it is known, rather than paying for a
    # build hook and a whole test suite that end in the engine's refusal.
    if !$force && !$test-only
       && %have{identity-key(%meta<name>, %meta<version> // %meta<ver>, %meta<auth>, %meta<api>)} {
        inform("already installed: {%e<dist> // %e<name>} (use --force to reinstall)");
        trace("already installed: {%e<dist> // %e<name>} — the dist's own identity "
              ~ "({%meta<name>}:auth<{%meta<auth> // ''}>) is in the store; the index calls it "
              ~ "auth<{%e<auth> // ''}>");
        return True;
    }

    if !run-build-hook(%e, $root, $prefix) {
        die "%e<name>: its build hook fails under rakupp — not installing";
    }

    if !$no-test && !run-dist-tests(%e, $root, $prefix) {
        die "%e<name>: its own test suite fails under rakupp — not installing (--no-test to override)";
    }

    # `rakupp test`: measurement, not installation — the suite verdict IS the
    # product, and the store stays exactly as the plan's dependencies left it
    if $test-only {
        progress("tested {%e<dist> // %e<name>} — suite green, not installed (--test)");
        trace("tested: {%e<dist> // %e<name>} — suite green, not installed");
        return True;
    }

    # bin/ scripts and declared resources ride through meta<files>
    # (rel-path => source; an empty source means "the rel-path under the dist
    # root"). That default is right for every plain resource and WRONG for one
    # under libraries/: META declares those by their LOGICAL name
    # (`libraries/json`) while the file a build hook writes carries the
    # platform's spelling (libjson.dylib / libjson.so / json.dll) — the same
    # mapping %?RESOURCES applies on lookup. Hand the engine the real file, or
    # it slurps a path that does not exist and the store receives empty bytes:
    # a compiled extension that quietly never leaves the build directory, and a
    # module that silently runs on its fallback for ever after.
    my %files;
    for (%meta<resources> // []).flat -> $r {
        my $plain = $root.add("resources/$r");
        if "$r".starts-with('libraries/') && !$plain.e {
            my $stem  = "$r".substr('libraries/'.chars);
            my $lib   = $*DISTRO.is-win                ?? "$stem.dll"
                     !! $*KERNEL.name eq 'darwin'      ?? "lib$stem.dylib"
                     !!                                   "lib$stem.so";
            my $built = $root.add("resources/libraries/$lib");
            if $built.e {
                %files{"resources/$r"} = $built.absolute;
                # Rakudo keys these records by the PLATFORM spelling and maps
                # before consulting them, so the same blob rides in under that
                # name too — one content, two entries, both engines answered.
                %files{"resources/libraries/$lib"} = $built.absolute;
                next;
            }
        }
        note "warning: %e<name>: declared resource $r has no file in the dist — storing it empty"
            unless $plain.e;
        %files{"resources/$r"} = '';
    }
    if $root.add('bin').d {
        %files{"bin/{.basename}"} = '' for $root.add('bin').dir.grep(*.f);
    }
    %meta<files> = %files if %files;
    trace("files: " ~ (%files ?? %files.keys.sort.join(' ') !! 'none'));

    # Damaged record for THIS identity? The engine's refusal is decided by the
    # short/ entry alone, so it answered "already installed" to every attempt
    # to fix one while `use` kept failing with "Could not find" — the one state
    # the installer could not cure. Overwrite it instead. Asked with the META's
    # identity, like the skip above: the store is keyed by that, not the index.
    my $repair = ?%broken{identity-key(%meta<name>, %meta<version> // %meta<ver>,
                                       %meta<auth>, %meta<api>)};
    if $repair {
        progress("repairing {%e<dist> // %e<name>}: the store has its record, but its files are missing or damaged");
        trace("repair: {%e<dist> // %e<name>} — the store's blobs are missing or do not match their names; overwriting");
    }

    # repository-for-spec, not .new: it is the constructor that carries the
    # prefix through to the engine's writer (the same path zef used here).
    # The engine returns the dist-id; recording it is what makes uninstall
    # able to refuse everything we did NOT install.
    my $repo = CompUnit::RepositoryRegistry.repository-for-spec("inst#$prefix");
    my $dist = InstallableDist.new(meta => %meta, root => $root.Str);
    my $dist-id = with-repo-lock($prefix, {
        my $id = $repo.install($dist, :force($force || $repair));
        record-owned($prefix, ~$id) if $id ~~ Str;
        $id
    });
    # Verify what the engine says it wrote, and say so in the trace. The
    # engine's file writes are silent on failure (a read-only store, a full
    # disk), and a wrapper that never hit the disk surfaces months later as
    # "the command is not in ~/.raku/bin" — the least diagnosable words a
    # report can open with. Catch it HERE, where the trace still knows
    # everything.
    if $dist-id ~~ Str {
        trace("engine install: dist-id $dist-id");
        my $rec = $prefix.IO.add('dist').add(~$dist-id);
        unless $rec.e {
            trace("VERIFY FAILED: dist record {$rec} missing");
            die "%e<name>: the engine answered dist-id $dist-id but wrote no dist record — is $prefix writable?";
        }
        for %files.keys.grep({ .starts-with('bin/') && .chars > 4 && !.substr(4).contains('/') }).sort -> $rel {
            my $w = $prefix.IO.add('bin').add($rel.substr(4));
            if $w.e && ($w.x || $*KERNEL.name.starts-with('win')) {
                trace("verify: wrapper {$w} ok");
            }
            else {
                trace("VERIFY FAILED: wrapper {$w} {$w.e ?? 'is not executable' !! 'missing'}");
                note "warning: %e<name>: bin wrapper {$w} {$w.e ?? 'is not executable' !! 'was not written'}";
            }
        }
        ensure-raku-name($prefix) if %files.keys.first(*.starts-with('bin/'));
    }
    progress("installed {%e<dist> // %e<name>}");
    trace("installed: {%e<dist> // %e<name>}");
    True
}

# Which blobs under sources/, resources/ and bin/ nothing points at any more.
# ONE routine, so `--check`'s count and `--gc`'s deletions can never disagree:
# a blob the report calls live is a blob the collector keeps.
sub unreferenced-blobs(IO::Path $p, %dists, %referenced) {
    my @out;
    for <sources resources bin> -> $sub {
        next unless $p.add($sub).d;
        for $p.add($sub).dir.grep(*.f) -> $b {
            next if %referenced{$b.basename};
            # bin/ holds NAMED wrappers beside (legacy) blobs. A wrapper is not
            # content-addressed: it is live while any dist carries bin/<name>,
            # and BROKEN-adjacent only in the sense of wasted disk otherwise.
            # The `raku` name is store infrastructure (ensure-raku-name wrote
            # it for the wrappers' shebang), owned by no dist and never stale.
            next if $sub eq 'bin' && $b.basename eq 'raku';
            if $sub eq 'bin' && $b.basename !~~ / ^ <[0..9 a..f A..F]> ** 40 $ / {
                next if %dists.values.first({ (.<files> // {}){"bin/" ~ $b.basename}:exists });
            }
            @out.push($b);
        }
    }
    @out
}

# ---- the store checker (M6, written BEFORE the delete path) -----------------
# Reports; fixes nothing. Exit 1 on BROKEN (a dangling index entry or a
# missing blob behind a live entry — a `use` that will fail); unreferenced
# blobs are only wasted disk and do not fail the check.
sub store-check(Str $prefix) {
    my $p = $prefix.IO;
    say "store: {$p.absolute}";   # every sources/… and short/… below is under here
    unless $p.add('dist').d {
        say "store check: no dist/ — nothing installed, nothing broken";
        return 0;
    }
    my $broken = 0;
    my %dists;          # dist-id -> meta hash
    my %referenced;     # blob sha -> True (from dist files maps and short entries)
    my %said-missing;   # blob sha -> True, so one absent blob is reported once
    my %owned = owned-set($p.absolute);   # the records this tool wrote

    for $p.add('dist').dir.grep(*.f) -> $f {
        my %m = try json-decode($f.slurp);
        if !%m {
            say "BROKEN: dist/{$f.basename} is unreadable";
            $broken++;
            next;
        }
        %dists{$f.basename} = %m;
        %referenced{$_} = True for (%m<files> // {}).values.grep(* ne '');
    }

    if $p.add('short').d {
        for $p.add('short').dir.grep(*.d) -> $sdir {
            for $sdir.dir.grep(*.f) -> $entry {
                # the FILENAME is the dist-id in both formats; the engine's
                # writer also repeats it as line 5, zef's four-line entries
                # do not — never read the id from the content
                my @l = $entry.lines;
                my $dist-id = $entry.basename;
                my $src-sha = @l[3] // '';
                if !%dists{$dist-id} {
                    say "BROKEN: short/{$sdir.basename}/{$entry.basename} points at missing dist $dist-id";
                    $broken++;
                }
                if $src-sha && !blob-path($p, $src-sha) {
                    say "BROKEN: short/{$sdir.basename}/{$entry.basename} needs a missing blob ($src-sha in sources/, resources/ or bin/)";
                    %said-missing{$src-sha} = True;
                    $broken++;
                }
                %referenced{$src-sha} = True if $src-sha;
            }
        }
    }

    # Every blob each record names: on disk, and — for the records this tool
    # wrote — still hashing to its own name. The store is content-addressed, so
    # that second question has an exact answer and needs nothing recorded
    # alongside; a blob whose bytes changed under it (a truncated write, a full
    # disk, an interrupted install) is provably not what the record points at.
    #
    # zef and Rakudo name their blobs by something other than the content, so
    # theirs are checked for presence only. The summary says how many, rather
    # than letting "0 broken" imply more than was asked.
    for %dists.kv -> $dist-id, %m {
        my $verify = ?%owned{$dist-id};
        for (%m<files> // {}).kv -> $rel, $sha {
            next if (~$sha) eq '';
            my $blob = blob-path($p, ~$sha);
            without $blob {
                unless %said-missing{~$sha} {
                    say "BROKEN: {%m<name>} ($dist-id) needs $rel but no blob $sha is in sources/, resources/ or bin/";
                    %said-missing{~$sha} = True;
                    $broken++;
                }
                next;
            }
            next unless $verify;
            my $got = sha1-blob($blob);
            next if $got eq (~$sha).uc;
            say "BROKEN: {%m<name>} ($dist-id) $rel: {$blob.parent.basename}/$sha holds different bytes"
                ~ ($got ?? " (SHA-1 $got)" !! " (unreadable)");
            $broken++;
        }
    }

    # index entries present for every provided module of every dist?
    for %dists.kv -> $dist-id, %m {
        for (%m<provides> // {}).keys -> $mod {
            my $e = $p.add('short').add(sha1-str($mod)).add($dist-id);
            unless $e.e {
                say "BROKEN: {%m<name>} ($dist-id) provides $mod but short/{sha1-str($mod)}/$dist-id is missing";
                $broken++;
            }
        }
    }

    my $foreign = %dists.keys.grep({ !%owned{$_} }).elems;
    say "  $foreign distribution{$foreign == 1 ?? '' !! 's'} not installed by rakupp: "
        ~ "blobs checked for presence, not content" if $foreign;

    my @unref = unreferenced-blobs($p, %dists, %referenced);
    my $unref = @unref.elems;
    say "store check: {%dists.elems} distribution{%dists.elems == 1 ?? '' !! 's'}, "
        ~ "$broken broken, $unref unreferenced blob{$unref == 1 ?? '' !! 's'}"
        ~ ($unref ?? ' (wasted disk, not damage)' !! '');
    $broken ?? 1 !! 0
}

# ---- reclaiming what nothing references ------------------------------------
# `--check` names the waste; this removes it. The live picture is built exactly
# as the checker builds it — same dist files, same short/ entries, same sweep —
# and a store that does not typecheck is left ALONE: an unreadable dist file
# means the live set is incomplete, and deleting against an incomplete live set
# is how a collector eats someone's installation.
sub store-gc(Str $prefix, Bool :$dry) {
    my $p = $prefix.IO;
    say "store: {$p.absolute}";
    unless $p.add('dist').d {
        say "store gc: no dist/ — nothing installed, nothing to reclaim";
        return 0;
    }
    my %dists; my %referenced; my $unreadable = 0;
    for $p.add('dist').dir.grep(*.f) -> $f {
        my %m = try json-decode($f.slurp);
        unless %m { $unreadable++; next }
        %dists{$f.basename} = %m;
        %referenced{$_} = True for (%m<files> // {}).values.grep(* ne '');
    }
    if $unreadable {
        note "store gc: $unreadable unreadable dist file{$unreadable == 1 ?? '' !! 's'} — "
           ~ "refusing to sweep against an incomplete picture (`rakupp install --check` names them)";
        return 1;
    }
    if $p.add('short').d {
        for $p.add('short').dir.grep(*.d) -> $sdir {
            for $sdir.dir.grep(*.f) -> $entry {
                my $sha = ($entry.lines)[3] // '';
                %referenced{$sha} = True if $sha;
            }
        }
    }

    my @unref = unreferenced-blobs($p, %dists, %referenced);
    unless @unref {
        say "store gc: nothing to reclaim";
        return 0;
    }
    my $bytes = @unref.map({ (try .s) // 0 }).sum;
    if $dry {
        say "  {.parent.basename}/{.basename}  {human-bytes((try .s) // 0)}" for @unref;
        say "store gc: {@unref.elems} blob{@unref.elems == 1 ?? '' !! 's'}, "
            ~ "{human-bytes($bytes)} would be reclaimed (--dry-run: nothing removed)";
        return 0;
    }
    my $freed = 0; my $failed = 0;
    for @unref -> $b {
        my $size = (try $b.s) // 0;
        if (try $b.unlink) { $freed += $size }
        else { $failed++; note "store gc: cannot remove {$b.absolute}" }
    }
    trace("gc: removed {@unref.elems - $failed} blobs, {$freed} bytes");
    say "store gc: reclaimed {human-bytes($freed)} from "
        ~ "{@unref.elems - $failed} blob{@unref.elems - $failed == 1 ?? '' !! 's'}"
        ~ ($failed ?? " ($failed could not be removed)" !! '');
    $failed ?? 1 !! 0
}

# Bytes as a person reads them; the store's blobs run from a few hundred bytes
# to a few hundred KB, so one decimal is enough and MB is as far as it goes.
sub human-bytes(Int() $n) {
    return "$n B"                              if $n < 1024;
    return "{($n / 1024).round(0.1)} KB"       if $n < 1024 * 1024;
    "{($n / (1024 * 1024)).round(0.1)} MB"
}

# ---- one distribution out of the store -------------------------------------
# The gates first (provenance, dependents), then the mark-and-sweep. False
# means a gate refused it and nothing was touched. %dists is the live picture
# of the store and shrinks here: the sweep asks it which blobs are still
# referenced, so a second removal in the same run sees the first one gone.
sub remove-one(Str $prefix, Str $dist-id, %meta, %dists,
               Bool :$force, Bool :$for-reinstall) {
    my $p = $prefix.IO;
    my $identity = "{%meta<name>}:ver<{%meta<version> // ''}>:auth<{%meta<auth> // ''}>";

    # ours? (a zef-installed dist is zef's; --force means you mean it).
    # REINSTALL only warns: its destruction is bounded — the dist is
    # replaced in the same run and becomes rakupp-owned, which the warning
    # says out loud. Plain uninstall (pure removal) keeps the hard refusal.
    if !$force && !is-owned($prefix, $dist-id) {
        if $for-reinstall {
            note "warning: $identity was not installed by `rakupp install` — reinstalling anyway; it becomes rakupp-owned";
        }
        else {
            note "$identity was not installed by `rakupp install` — refusing (--force to override)";
            trace("uninstall refused: $identity is not rakupp-owned");
            return False;
        }
    }
    # reverse dependencies: anything still installed that depends on a
    # name this dist provides?
    my @provided = (%meta<provides> // {}).keys;
    my @dependents;
    for %dists.kv -> $oid, %om {
        next if $oid eq $dist-id;
        for (%om<depends> // []).flat.grep(Str) -> $dep {
            my %d = parse-identity($dep);
            @dependents.push("{%om<name>}:ver<{%om<version> // ''}>")
                if @provided.first(* eq %d<name>);
        }
    }
    if @dependents && !$force {
        if $for-reinstall {
            progress("$identity is depended on by {@dependents.unique.join(', ')} — reinstalling in place");
        }
        else {
            note "$identity is still depended on by {@dependents.unique.join(', ')} — refusing (--force to override)";
            trace("uninstall refused: $identity has dependents ({@dependents.unique.join(', ')})");
            return False;
        }
    }

    with-repo-lock($prefix, {
        # blobs THIS dist references, and blobs everything ELSE references
        my @mine = (%meta<files> // {}).values.grep(* ne '');
        my %still;
        for %dists.kv -> $oid, %om {
            next if $oid eq $dist-id;
            %still{$_} = True for (%om<files> // {}).values.grep(* ne '');
        }
        # 1. index entries FIRST: a missing blob behind a live entry is a
        #    broken `use`; an orphaned blob is only wasted disk. Provided
        #    modules AND files — bin/resources entries are indexed under
        #    sha1 of their rel-path, which is what Rakudo's `.files` reads.
        for |@provided, |(%meta<files> // {}).keys -> $key {
            my $e = $p.add('short').add(sha1-str($key)).add($dist-id);
            $e.unlink if $e.e;
            my $sdir = $e.parent;
            $sdir.rmdir if $sdir.d && !$sdir.dir;
        }
        # 2. blobs nothing else references (content-addressed and shared)
        for @mine.unique -> $sha {
            next if %still{$sha};
            for <sources resources bin> -> $sub {
                my $b = $p.add($sub).add($sha);
                $b.unlink if $b.e;
            }
        }
        # 2b. named bin wrappers (bin/<script>, the engine writes one per
        #     bin/ entry at install). Kept while ANY remaining dist still
        #     carries a script of that name — wrappers dispatch by name,
        #     not by dist, so the survivor keeps answering.
        for (%meta<files> // {}).keys.grep(*.starts-with('bin/')) -> $rel {
            my $script = $rel.substr(4);
            next if $script eq '' || $script.contains('/');
            my $still-provided = False;
            for %dists.kv -> $oid, %om {
                next if $oid eq $dist-id;
                $still-provided = True if (%om<files> // {}){$rel}:exists;
            }
            unless $still-provided {
                my $w = $p.add('bin').add($script);
                $w.unlink if $w.e;
            }
        }
        # 3. the dist record LAST: a crash mid-way leaves a record that
        #    still describes what to finish
        $p.add('dist').add($dist-id).unlink;
        drop-owned($prefix, $dist-id);
    });
    %dists{$dist-id}:delete;
    inform("uninstalled $identity");
    trace("uninstalled: $identity ($dist-id)");
    True
}

# ---- uninstall (M6): mark-and-sweep over a shared, content-addressed store --
# :for-reinstall relaxes three refusals, because the dist is coming right
# back: "not installed" becomes a fresh install (note, skip the removal),
# installed DEPENDENTS do not block (a removal would strand them; a reinstall
# restores what they depend on), and foreign PROVENANCE downgrades to a
# warning — the replacement becomes rakupp-owned, and the warning says so.
sub do-uninstall(@names, Str $prefix, Bool :$force, Bool :$for-reinstall) {
    trace("uninstall: {@names.join(' ')} from $prefix" ~ ($for-reinstall ?? ' (for reinstall)' !! ''));
    my $p = $prefix.IO;
    unless $p.add('dist').d {
        if $for-reinstall {
            progress("nothing installed in $prefix — installing fresh");
            return 0;
        }
        note "nothing installed in $prefix";
        trace("uninstall refused: nothing installed in $prefix");
        return 1;
    }
    my %dists;
    for $p.add('dist').dir.grep(*.f) -> $f {
        my %m = try json-decode($f.slurp);
        %dists{$f.basename} = %m if %m;
    }

    # Names are independent: one that refuses does not stop the rest, and the
    # exit code reports that something asked for did not happen.
    my $rc = 0;
    for @names -> $want-str {
        my %want = parse-identity($want-str);
        my @hits = %dists.grep(-> $kv {
            my %m = $kv.value;
            ((%m<name> // '') eq %want<name> || ((%m<provides> // {}){%want<name>}:exists))
            && ver-ok(%m<version> // '', %want<ver>)
            && (%want<auth> eq '' || (%m<auth> // '') eq %want<auth>)
        });
        if !@hits {
            if $for-reinstall {
                progress("not installed: $want-str — installing fresh");
                next;
            }
            note "not installed: $want-str";
            trace("uninstall refused: $want-str is not installed");
            $rc = 1;
            next;
        }
        # More than one installed distribution answers to this name — the
        # ordinary shape after an upgrade, and what `zef uninstall` handles by
        # matching every installed dist against the spec and removing each one
        # that matches. Refusing instead made the name unremovable in practice:
        # the identity the refusal asked to be typed instead (`Foo:ver<1.0>`)
        # is a redirection to every shell that would have to pass it through.
        if @hits.elems > 1 {
            progress("$want-str matches {@hits.elems} installed distributions — removing all");
            trace("uninstall: $want-str matches {@hits.elems} distributions");
        }
        for @hits.sort(-> $a, $b {   # newest first
            newer($a.value<version> // '', $b.value<version> // '') ?? Order::Less
            !! newer($b.value<version> // '', $a.value<version> // '') ?? Order::More
            !! Order::Same
        }) -> $hit {
            $rc = 1 unless remove-one($prefix, $hit.key, $hit.value, %dists,
                                      :$force, :$for-reinstall);
        }
    }
    $rc
}

# The dist-ids this tool installed, read once for a whole listing
# (is-owned re-reads the file per call — right for one uninstall, wrong for
# eighty dists).
sub owned-set(Str $prefix) {
    my $f = owned-file($prefix);
    my %o;
    %o{$_} = True for ($f.e ?? $f.lines.grep(*.chars) !! ());
    %o
}

# The blob behind one provided module. Both writers record provides as
# { "Foo": { "lib/Foo.rakumod": { "file": <sha> } } }; the raw META6 shape
# ({ "Foo": "lib/Foo.rakumod" }) resolves through the files map instead.
sub provided-blob(%m, Str $mod --> Str) {
    my $v = (%m<provides> // {}){$mod};
    return '' without $v;
    if $v ~~ Associative {
        for $v.values -> $rec {
            return ~$rec<file> if $rec ~~ Associative && ($rec<file> // '') ne '';
        }
        return '';
    }
    ~((%m<files> // {}){~$v} // '')
}

# One dist per identity line, as always. Under it (not under -q): who
# installed the dist — `rakupp` if its id is in this tool's owned record,
# else `zef`, the only other writer of this store in practice — then one
# line per provided module with the store path of its source blob, and one
# per bin/ script with the path of its wrapper. Absolute paths, so a line
# can be pasted into an editor as it stands; a blob or wrapper that should
# be on disk and is not says so beside its path (--check is the full audit).
sub list-installed(Str $prefix) {
    my $p = $prefix.IO;
    my $dist-dir = $p.add('dist');
    unless $dist-dir.d {
        say "nothing installed in $prefix";
        return;
    }
    my %owned = owned-set($prefix);
    for $dist-dir.dir.grep(*.f).sort -> $f {
        my %m = try json-decode($f.slurp);
        next unless %m;
        my @mods = (%m<provides> // {}).keys.sort;
        say "{%m<name>}:ver<{%m<version> // '?'}>:auth<{%m<auth> // ''}>  ({@mods.join(', ')})";
        # the identity line is the product; the rest is what -q drops
        next if $QUIET;
        say "    installed by: {%owned{$f.basename} ?? 'rakupp' !! 'zef'}";
        my @rows;
        for @mods -> $mod {
            my $sha = provided-blob(%m, $mod);
            if $sha eq '' {
                @rows.push: $mod => '(no file recorded)';
                next;
            }
            my $blob = $p.add('sources').add($sha);
            @rows.push: $mod => $blob.absolute ~ ($blob.e ?? '' !! '  (missing)');
        }
        for (%m<files> // {}).keys.grep({ .starts-with('bin/') && .chars > 4 && !.substr(4).contains('/') }).sort -> $rel {
            my $w = $p.add('bin').add($rel.substr(4));
            @rows.push: $rel => $w.absolute ~ ($w.e ?? '' !! '  (wrapper missing)');
        }
        next unless @rows;
        my $wide = @rows.map(*.key.chars).max;
        say "    {.key}{' ' x ($wide - .key.chars + 2)}{.value}" for @rows;
    }
}

# The identity key the store and the index agree on: name, version, auth,
# api — api<0> and no api are the same thing to both, so they key the same.
sub identity-key($name, $ver, $auth, $api) {
    my $a = ~($api // '');
    $a = '' if $a eq '0';
    (~($name // ''), ~($ver // ''), ~($auth // ''), $a).join("\0")
}

# What the store ALREADY holds, read from dist/ in one pass. The engine
# refuses an identity it has — but only at the END of install-one, after the
# archive is fetched, the build hook has run and the dist's whole suite has
# passed. Asking the store FIRST is what makes a repeated
# `rakupp install Sparrow6` a listing instead of seventeen fetch-build-test
# cycles that all end in "already installed".
#
# …and DAMAGED, in the same pass: a record whose blobs are gone, or whose blobs
# no longer hold what they were named for. Neither is an installation — one
# fails `use` with "Could not find", the other loads an empty module — so
# neither may answer "already installed" to the plan, and install-one overwrites
# it rather than letting the engine refuse. (--check reports the same two
# conditions as BROKEN; this is what makes install the cure for what it names.)
#
# %verify names the identities to check by CONTENT, and the caller passes the
# plan: reading every blob of every installed distribution would put the whole
# store through SHA-1 on each install, while the only records whose damage
# changes this run are the ones about to be installed. Presence is still checked
# for all of them, and it is free.
sub store-state(Str $prefix, %verify = {}) {
    my (%have, %broken);
    my $dist-dir = $prefix.IO.add('dist');
    return { have => %have, broken => %broken } unless $dist-dir.d;
    my %owned = owned-set($prefix);
    for $dist-dir.dir.grep(*.f) -> $f {
        my %m = try json-decode($f.slurp);
        next unless %m;
        my $key = identity-key(%m<name>, %m<version> // %m<ver>, %m<auth>, %m<api>);
        my $verify = ?%verify{$key} && ?%owned{$f.basename};
        (store-blobs-intact($prefix, %m, :$verify) ?? %have !! %broken){$key} = True;
    }
    { have => %have, broken => %broken }
}

# Every blob a dist record names, still on disk — and, with :verify, still the
# bytes it was named for. The store is content-addressed: a blob's FILE NAME is
# the SHA-1 of its content, so an intact copy proves itself and a damaged one
# cannot.
#
# Presence alone was not enough. A blob truncated to nothing stat()s fine, so
# the record answered "already installed" for ever; the module it holds compiles
# to an empty unit, so `use` SUCCEEDS and imports nothing. That is issue #72:
# fez's installed script is one `use Fez::CLI`, an empty Fez::CLI left the
# program with no MAIN, and every fez subcommand answered with silence and
# exit 0 — no error to go on, and no reinstall allowed.
#
# :verify only for records THIS TOOL wrote. zef and Rakudo name their blobs by
# something other than the content, so hashing theirs reports every one of them
# damaged — and what the caller does about damage is overwrite the distribution.
sub store-blobs-intact(Str $prefix, %m, Bool :$verify --> Bool) {
    my $p = $prefix.IO;
    for (%m<files> // {}).values.grep(* ne '') -> $sha {
        my $want = (~$sha).uc;
        my $blob = blob-path($p, ~$sha);
        return False without $blob;
        return False if $verify && sha1-blob($blob) ne $want;
    }
    True
}

# Raku's default MAIN parsing stops reading options at the first positional, so
# `rakupp install Config --no-test` put `--no-test` in @modules and installed
# with the test gate still on — silently, and the failure message that suggests
# the option is printed AFTER the name it follows. Every option this command
# takes reads naturally on either side of the dist name; let both spellings work.
my %*SUB-MAIN-OPTS = :named-anywhere;

sub MAIN(
    *@modules,                 #= modules or dists to install (Foo, Foo:ver<1.2+>)
    Bool :$dry-run,            #= resolve and print the plan; write nothing
    Bool :$list,               #= list what is installed in the target store (identity, installer, files, bin; -q: identities only)
    Bool :$check,              #= check the store's integrity; fix nothing
    Bool :$gc,                 #= remove blobs nothing references (--dry-run lists them)
    Bool :$uninstall,          #= remove distributions (rakupp uninstall Foo)
    Bool :$reinstall,          #= uninstall then install fresh (rakupp reinstall Foo)
    Bool :$no-test,            #= skip the per-distribution test suites
    Bool :$test-only,          #= build + run the named dists' suites; install only their deps (rakupp test)
    Bool :$force,              #= reinstall / uninstall despite refusals
    Bool :$refresh,            #= refetch the ecosystem index (else cached 24h)
    Bool :q(:$quiet),          #= only warnings and failures; nothing on success
    Str  :$to = home-dir().add('.raku').Str,  #= the CURI store prefix to write
) {
    $QUIET = ?$quiet;
    # `rakupp uninstall --list` is a mode mix, not a synonym for install
    # --list — refuse it rather than silently answering as a different command
    if ($uninstall || $reinstall || $test-only) && ($list || $check || $gc) {
        note "rakupp {$uninstall ?? 'uninstall' !! $reinstall ?? 'reinstall' !! 'test'} "
           ~ "--{$list ?? 'list' !! $check ?? 'check' !! 'gc'}: --list/--check/--gc belong to "
           ~ "`rakupp install` — pick one";
        exit 2;
    }
    if 1 < ($list, $check, $gc).grep(?*).elems {
        note "rakupp install: --list, --check and --gc each answer on their own — pick one";
        exit 2;
    }
    if $test-only && !@modules {
        note "usage: rakupp test Module ...";
        exit 2;
    }
    # Anything actionable from here on leaves its account in the trace log,
    # and a fatal ANYWHERE below (a dead mirror, a store that will not lock,
    # a JSON the codec refuses) still lands there and points the reporter at
    # the file. `exit` is not an exception, so the usage and store-check
    # verdicts above and below keep their codes.
    CATCH {
        default {
            trace("FATAL: " ~ .Str);
            note .Str;
            trace-pointer();
            exit 1;
        }
    }
    trace-start();
    trace("store: $to");
    if $list {
        list-installed($to);
        return;
    }
    if $check {
        exit store-check($to);
    }
    if $gc {
        # the lock, because this REMOVES files another process may be about to
        # reference; --dry-run still takes it, so what it lists is what a real
        # run would have found
        exit with-repo-lock($to, { store-gc($to, :dry($dry-run)) });
    }
    # `rakupp uninstall .` / `reinstall .` — the store knows dists by NAME,
    # so a path argument stands for whatever its directory's META6 names
    if $uninstall {
        with @modules.first({ url-arg($_) }) -> $u {
            note "rakupp uninstall: $u";
            note "  the store knows distributions by NAME, and finding the name behind";
            note "  a URL would mean downloading it. Give the name:  rakupp uninstall Foo::Bar";
            note "  (`rakupp install --list` prints what is installed)";
            exit 2;
        }
    }
    my @removal-names = @modules.map(-> $a {
        my $meta = is-path-arg($a) ?? arg-path($a).add('META6.json') !! Nil;
        $meta && $meta.e
            ?? ((try json-decode($meta.slurp))<name> // $a)
            !! $a
    });
    if $uninstall {
        unless @modules {
            note "usage: rakupp uninstall [--force] Module ...";
            exit 2;
        }
        exit do-uninstall(@removal-names, $to, :force($force // False));
    }
    if $reinstall {
        unless @modules {
            note "usage: rakupp reinstall [--no-test] [--force] Module ...";
            exit 2;
        }
        # the uninstall half; the normal install flow below is the other half
        my $rc = do-uninstall(@removal-names, $to, :force($force // False), :for-reinstall);
        exit $rc if $rc != 0;
    }
    if $refresh && !@modules {
        # bare --refresh is a complete command: refetch the index, report,
        # stop. The REA cache refreshes only if it exists — nobody pays for
        # the 18 MB archive index before their first REA-resolved install.
        my @index = load-index(True).list;
        inform("index refreshed: {@index.elems} distributions");
        if cache-dir.add('rea-meta.json').e {
            $REA-REFRESH = True;
            my @rea = rea-index();
            inform("REA archive index refreshed: {@rea.elems} archived releases");
        }
        return;
    }
    unless @modules {
        note q:to/END/.trim;
            usage: rakupp install [options] Module|Path|URL ...
                   rakupp install .            this directory's dist
                   rakupp install my-dist      a Path: a directory holding a META6.json, however
                                               spelled — my-dist, ./x, ~/x, /x, dists/x, C:\x, \\\\host\x
                   rakupp install https://github.com/OWNER/REPO[/tree/REF[/SUBDIR]]
                   rakupp install https://host/Foo-1.0.tar.gz
                   rakupp install --list | --check | --gc | --refresh
                   rakupp test Module|Path ... run the dists' own suites; installs only their deps
                   rakupp uninstall [--force] Module|Path ...
                   rakupp reinstall [--no-test] [--force] Module|Path ...
            options:
              --dry-run        resolve and print the plan; write nothing
              --list           what is installed in the target store: identity,
                               installer, module files, bin wrappers (-q: identities only)
              --check          store integrity report (exit 1 on damage); fixes nothing
              --gc             remove the blobs --check counts as unreferenced;
                               with --dry-run, list them and remove nothing
              --no-test        skip the per-distribution test suites
              --force          reinstall / uninstall despite refusals
              --refresh        refetch the ecosystem index(es) (else cached 24h);
                               alone: refresh and stop
              --to=PATH        the store prefix to use (default: ~/.raku)
              -q, --quiet      only warnings and failures; nothing on success
                               (with any command, before or after it)
            END
        exit 2;
    }

    $REA-REFRESH = $refresh // False;
    # is-path-arg(): an argument that spells a path, or bare-names a directory
    # with a META6.json in it, is a directory to install from; everything else
    # resolves in the ecosystem.
    # A path dist contributes its DEPENDENCIES to the resolver — they install
    # first, like any plan's — while the dist itself installs from its
    # directory, never from the index's copy of the same name.
    #
    # A URL is fetched and unpacked into a directory FIRST, and is a local dist
    # from there on: same dependency handling, same build hook, same test gate,
    # same store write. Nothing downstream knows the difference.
    my @local-entries = @modules.grep({ is-path-arg($_) }).map({ local-dist-entry($_) });
    @local-entries.append(@modules.grep({ url-arg($_) }).map({ url-dist-entry($_) }));
    my @names = @modules.grep({ !is-path-arg($_) && !url-arg($_) });
    my %notes;
    my @wants = @names;
    for @local-entries -> %e {
        trace("local: {%e<dist>} from {%e<local-root>}");
        for <depends build-depends test-depends> -> $field {
            @wants.append(dep-identities(%e{$field}));
            %notes{$_} = 'an alternation this installer does not choose between'
                for dep-unresolved(%e{$field});
        }
    }
    # a pure-path install with no ecosystem wants needs no index at all —
    # `rakupp install .` on a dependency-free dist works offline
    my @index = @wants ?? load-index($refresh // False).list !! ();
    if @wants {
        trace("index: {@index.elems} distributions");
        trace("resolve: {@wants.join(' ')}");
    }
    my %local-names;
    %local-names{.<name>} = True for @local-entries;
    my @plan = (@wants ?? resolve(@index, @wants, %notes, :prefix($to)) !! ())
        .grep(-> %e { !%local-names{%e<name>} });
    @plan.append(@local-entries);

    if !@plan && %notes {
        note "cannot resolve: {%notes.map({ "{.key} ({.value})" }).join('; ')}";
        # A bare name that is ALSO a directory here is a path the caller meant
        # as one, and the only reason it came this far is the missing
        # META6.json — is-path-arg() would have taken it otherwise. Say that,
        # rather than leaving "not in the ecosystem index" to describe a
        # checkout on their disk. (An identity is never a path, whatever the
        # directory beside it is called, so it earns no such note.)
        for %notes.keys.sort.grep({ !identity-arg($_) && (try .IO.d)
                                    && !(try .IO.add('META6.json').e) }) -> $d {
            note "  $d is a directory here, but has no META6.json — a distribution root needs one";
            trace("cannot resolve: $d is a META6-less directory");
        }
        trace("cannot resolve: {%notes.map({ "{.key} ({.value})" }).join('; ')}");
        trace-pointer();
        exit 1;
    }
    trace("plan: {@plan.elems} — " ~ (@plan ?? @plan.map(-> %e { %e<dist> // %e<name> }).join(', ') !! '(empty)'));
    trace("skipped: {.key} — {.value}") for %notes.sort;

    # --force means "do it anyway", so it asks the store nothing. Under
    # `rakupp reinstall` this runs AFTER the removal above — the dists it
    # took out are gone from dist/ and install fresh.
    my (%have, %broken);
    unless $force {
        my %planned;
        %planned{identity-key(.<name>, .<version>, .<auth>, .<api>)} = True for @plan;
        my %state = store-state($to, %planned);
        %have   = %state<have>;
        %broken = %state<broken>;
    }

    # The plan is narration on a real run and THE product of --dry-run, so -q
    # drops it only on the former. What was skipped is neither: a dependency
    # this run will not provide is always said.
    my $show-plan = $dry-run || !$QUIET;
    say "plan ({@plan.elems} distribution{@plan.elems == 1 ?? '' !! 's'}, dependencies first):"
        if $show-plan;
    for @plan -> %e {
        my $known = %have{identity-key(%e<name>, %e<version>, %e<auth>, %e<api>)};
        say "  {%e<dist> // %e<name>}   {archive-url(%e)}{$known ?? '   (already installed)' !! ''}"
            if $show-plan;
    }
    for %notes.sort -> $n {
        say "  skipped: {$n.key} — {$n.value}";
    }

    if $dry-run {
        say "dry run: nothing written";
        return;
    }

    # under `rakupp test`, only the NAMED dists stay uninstalled — their
    # dependencies really install, or the suites would have nothing to import
    my %target;
    if $test-only {
        %target{parse-identity($_)<name>} = True for @names;
        %target{.<name>} = True for @local-entries;   # `rakupp test .` targets the dist the dir names
    }
    for @plan -> %e {
        my $is-target = $test-only
            && ?(%target{%e<name>} || (%e<provides> // {}).keys.first({ %target{$_} }));
        # A dist rakupp SHADOWS (rakulib/) is already provided, by a module written
        # for this engine. The ecosystem original reads MoarVM's own memory layout
        # to do its job — NativeHelpers::Blob finds a Blob's bytes by scanning
        # object headers — so it cannot run here (nor on JVM Rakudo), and its suite
        # cannot pass. Installing it would only put a broken copy behind the shadow.
        if !$is-target && shadowed-by-rakulib(%e) {
            inform("provided by rakupp: {%e<dist> // %e<name>} — using the bundled shadow");
            trace("shadowed by rakulib: {%e<dist> // %e<name>} — skipped");
            next;
        }
        # Already in the store? install-one would fetch it, build it, run its
        # suite and only THEN be refused by the engine — say so now instead.
        # (`rakupp test` still tests the dists it was NAMED, installed or not.)
        if !$is-target && %have{identity-key(%e<name>, %e<version>, %e<auth>, %e<api>)} {
            inform("already installed: {%e<dist> // %e<name>} (use --force to reinstall)");
            trace("already installed: {%e<dist> // %e<name>} — skipped");
            next;
        }
        my $done = try install-one(%e, $to, :$no-test, :$force, :test-only($is-target),
                                   :%have, :%broken);
        unless $done {
            if $!.Str.contains('already installed') {
                inform("already installed: {%e<dist> // %e<name>} (use --force to reinstall)");
                trace("already installed: {%e<dist> // %e<name>} — the engine refused");
                next;
            }
            trace("FAILED: {%e<dist> // %e<name>} — {$!.Str}");
            note $!.Str;
            note "reinstall: the previous installation was already removed — `rakupp install {%e<name>}` (or --no-test) to restore"
                if $reinstall;
            trace-pointer();
            exit 1;
        }
    }
    inform("done: {@plan.elems} distribution{@plan.elems == 1 ?? '' !! 's'} processed into $to");
    trace("done: {@plan.elems} distribution{@plan.elems == 1 ?? '' !! 's'} processed into $to");
}
