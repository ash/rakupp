# rakupp install — the module installer (docs/dev/plans/MODULES-PLAN.md, Part A).
#
# A Raku program shipped WITH the release and dispatched by `rakupp install`,
# deliberately not C++ in the binary: a compiled --exe binary and an embedded
# librakupp must not carry an HTTP client, an index parser or a tar reader.
#
#   rakupp install Foo::Bar              install newest satisfying, plus deps
#   rakupp install Foo:ver<1.2.3>        a specific version (still additive)
#   rakupp install --dry-run Foo         the full plan, nothing written
#   rakupp install --list                what is installed in the home store
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
    # a local-file index (RAKUPP_INSTALL_INDEX=/path) yields local archive
    # paths — the t/install suite and offline mirrors
    if !$url.starts-with('http') {
        die "no such archive: $url" unless $url.IO.f;
        $to.IO.spurt($url.IO.slurp(:bin), :bin);
        return;
    }
    my $p = run 'curl', '-sSL', '--fail', '--max-time', '600', '-o', $to, $url, :out, :err;
    my $err = $p.err.slurp(:close);
    die "fetch failed: $url\n$err" if $p.exitcode != 0;
}

sub sha1-file(Str $path) {
    for ('shasum', '-a', '1'), ('sha1sum',), ('openssl', 'sha1', '-r') -> @tool {
        my $p = try run |@tool, $path, :out, :err;
        next unless $p && $p.exitcode == 0;
        my $line = $p.out.slurp(:close);
        return ~$0 if $line ~~ / ( <[0..9 a..f]> ** 40 ) /;
    }
    die "no SHA-1 tool found (need shasum, sha1sum or openssl) — refusing to install unverified archives";
}

sub cache-dir {
    my $d = %*ENV<HOME>.IO.add('.raku').add('rakupp-install');
    $d.mkdir unless $d.d;
    $d
}

sub sha1-str(Str $s) {
    my $t = $*TMPDIR.add("rakupp-install-sha-$*PID");
    $t.spurt($s);
    LEAVE $t.unlink;
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
        return json-decode($override.IO.slurp);
    }
    my $url = $override ne '' ?? $override !! $INDEX-URL;
    my $cache = cache-dir.add('index.json');
    if !$refresh && $cache.e && (now.to-posix[0] - $cache.modified.to-posix[0]) < $CACHE-TTL {
        return json-decode($cache.slurp);
    }
    note "fetching ecosystem index: $url";
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
        @REA = json-decode($override.IO.slurp).list;
        return @REA;
    }
    my $url = $override ne '' ?? $override !! $REA-INDEX-URL;
    my $cache = cache-dir.add('rea-meta.json');
    if !$REA-REFRESH && $cache.e && (now.to-posix[0] - $cache.modified.to-posix[0]) < $CACHE-TTL {
        @REA = json-decode($cache.slurp).list;
        return @REA;
    }
    note "fetching the REA archive index: $url";
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
    @c.sort(-> %a, %b {   # newest version first
        newer(%a<version> // '', %b<version> // '') ?? Order::Less
        !! newer(%b<version> // '', %a<version> // '') ?? Order::More
        !! Order::Same
    }).List
}

# The dependency-first install plan for the requested identities. Each plan
# entry is the index entry hash; %notes collects what was skipped and why.
sub resolve(@index, @wants, %notes) {
    my @plan;
    my %planned;   # dist identity -> True
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
            note "note: {%want<name>} — not in the zef index, resolved from the REA archive"
                if @c;
        }
        # A pinned :ver/:auth that matches nothing ANYWHERE falls back to
        # name-only, loudly — first the live index, then the archive.
        if !@c && (%want<ver> ne '' || %want<auth> ne '') {
            my %bare = name => %want<name>, ver => '', auth => '', from => %want<from>;
            my $pin = %want<name>
                ~ (%want<ver>  ne '' ?? ":ver<{%want<ver>}>"   !! '')
                ~ (%want<auth> ne '' ?? ":auth<{%want<auth>}>" !! '');
            @c = candidates(@index, %bare);
            if @c {
                note "note: $pin is not in the index — using {@c[0]<dist> // @c[0]<name>} (the pin may predate an ecosystem migration)";
            }
            else {
                @c = candidates(rea-index(), %bare);
                note "note: $pin matches nothing — using {@c[0]<dist> // @c[0]<name>} from the REA archive"
                    if @c;
            }
        }
        if !@c {
            %notes{%want<name>} = 'not in the ecosystem index'
                unless %planned{%want<name>};   # a planned dist also PROVIDES names
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
    # dependencies first: a dist queued later was queued BY something earlier
    @plan.reverse.List
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

# The engine's CompUnit install takes any object with .meta and .IO.
class InstallableDist {
    has %.meta;
    has $.root;
    method IO {
        $.root
    }
}

# zef's build protocol: a Build.rakumod (or Build.pm6/Build.pm) at the dist
# root with `method build($cwd)`, run BEFORE the tests — OpenSSL generates
# its resources/libraries.json in it, which is why the file is in META
# resources but in nobody's archive. The child sees the target store as
# `-I inst#<prefix>`, so `use JSON::Fast` inside a hook resolves against the
# build-depends this plan just installed.
sub run-build-hook(%e, $root, Str $prefix --> Bool) {
    my $hook = <Build.rakumod Build.pm6 Build.pm>.map({ $root.IO.add($_) }).first(*.e);
    return True without $hook;
    note "building %e<name>: {$hook.basename}";
    my $p = run $*EXECUTABLE.absolute, '-I', "inst#$prefix", '-I', $root.IO.Str, '-e',
                'use Build; my $r = Build.new.build($*CWD.Str); exit(($r === False) ?? 1 !! 0)',
                :out, :err, :cwd($root);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    if $p.exitcode != 0 {
        note "BUILD FAILED: {$hook.basename}";
        note $err.indent(2);
        return False;
    }
    True
}

sub run-dist-tests(%e, $root, Str $prefix) {
    my @tests = $root.IO.add('t').d
        ?? $root.IO.add('t').dir.grep({ .extension eq 't' | 'rakutest' }).sort
        !! ();
    return True unless @tests;
    note "testing %e<name>: {@tests.elems} file{@tests.elems == 1 ?? '' !! 's'}";
    for @tests -> $t {
        # `-I inst#<prefix>`: the suite must see the dependencies this plan
        # installed into the TARGET store, wherever --to pointed it
        my $p = run $*EXECUTABLE.absolute, '-I', $root.IO.add('lib').Str,
                    '-I', "inst#$prefix", $t.Str,
                    :out, :err, :cwd($root);
        if $p.exitcode != 0 {
            note "FAILED: {$t.basename}";
            note $p.err.slurp(:close).indent(2);
            $p.out.slurp(:close);
            return False;
        }
        $p.out.slurp(:close);
        $p.err.slurp(:close);
    }
    True
}

sub install-one(%e, Str $prefix, Bool :$no-test, Bool :$force, Bool :$test-only) {
    my $url = archive-url(%e);
    my $tmp = $*TMPDIR.add("rakupp-install-{$*PID}-{%e<name>.subst(/<-alnum>/, '-', :g)}");
    $tmp.mkdir;
    LEAVE { run 'rm', '-rf', $tmp.Str }

    my $tarball = $tmp.add('dist.tar.gz').Str;
    note "fetching $url";
    fetch-file($url, $tarball);

    # The archive must hash to the SHA-1 its URL names (fez archives are
    # content-addressed). Refuse anything else — this is the checksum gate
    # the plan's M2 requires, and it holds for mirrors too. REA archive
    # URLs carry no hash and fall to the TLS-only note below.
    if $url ~~ / ( <[0..9 a..f]> ** 40 ) '.tar.gz' $ / {
        my $want = ~$0;
        my $got = sha1-file($tarball);
        die "checksum mismatch for %e<name>: archive is $got, index says $want"
            if $got ne $want;
    }
    else {
        note "note: index path carries no checksum for %e<name> — TLS is the only integrity here";
    }

    my $p = run 'tar', '-xzf', $tarball, '-C', $tmp.Str, :err;
    die "tar failed: {$p.err.slurp(:close)}" if $p.exitcode != 0;

    # the dist root is wherever META6.json landed (top level, or one dir down)
    my $root = $tmp.add('META6.json').e
        ?? $tmp
        !! $tmp.dir.first({ .d && .add('META6.json').e })
            // die "no META6.json in %e<name>'s archive";

    my %meta = json-decode($root.add('META6.json').slurp);

    if !run-build-hook(%e, $root, $prefix) {
        die "%e<name>: its build hook fails under rakupp — not installing";
    }

    if !$no-test && !run-dist-tests(%e, $root, $prefix) {
        die "%e<name>: its own test suite fails under rakupp — not installing (--no-test to override)";
    }

    # `rakupp test`: measurement, not installation — the suite verdict IS the
    # product, and the store stays exactly as the plan's dependencies left it
    if $test-only {
        note "tested {%e<dist> // %e<name>} — suite green, not installed (--test)";
        return True;
    }

    # bin/ scripts and declared resources ride through meta<files>
    my %files;
    for (%meta<resources> // []).flat -> $r {
        %files{"resources/$r"} = '';
    }
    if $root.add('bin').d {
        %files{"bin/{.basename}"} = '' for $root.add('bin').dir.grep(*.f);
    }
    %meta<files> = %files if %files;

    # repository-for-spec, not .new: it is the constructor that carries the
    # prefix through to the engine's writer (the same path zef used here).
    # The engine returns the dist-id; recording it is what makes uninstall
    # able to refuse everything we did NOT install.
    my $repo = CompUnit::RepositoryRegistry.repository-for-spec("inst#$prefix");
    my $dist = InstallableDist.new(meta => %meta, root => $root.Str);
    my $dist-id = with-repo-lock($prefix, {
        my $id = $repo.install($dist, :force($force));
        record-owned($prefix, ~$id) if $id ~~ Str;
        $id
    });
    note "installed {%e<dist> // %e<name>}";
    True
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
                if $src-sha && !<sources resources bin>.first({ $p.add($_).add($src-sha).e }) {
                    say "BROKEN: short/{$sdir.basename}/{$entry.basename} needs a missing blob ($src-sha in sources/, resources/ or bin/)";
                    $broken++;
                }
                %referenced{$src-sha} = True if $src-sha;
            }
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

    my $unref = 0;
    for <sources resources bin> -> $sub {
        next unless $p.add($sub).d;
        for $p.add($sub).dir.grep(*.f) -> $b {
            next if %referenced{$b.basename};
            # bin/ holds NAMED wrappers beside (legacy) blobs. A wrapper is not
            # content-addressed: it is live while any dist carries bin/<name>,
            # and BROKEN-adjacent only in the sense of wasted disk otherwise.
            if $sub eq 'bin' && $b.basename !~~ / ^ <[0..9 a..f A..F]> ** 40 $ / {
                next if %dists.values.first({ (.<files> // {}){"bin/" ~ $b.basename}:exists });
            }
            $unref++;
        }
    }
    say "store check: {%dists.elems} distribution{%dists.elems == 1 ?? '' !! 's'}, "
        ~ "$broken broken, $unref unreferenced blob{$unref == 1 ?? '' !! 's'}"
        ~ ($unref ?? ' (wasted disk, not damage)' !! '');
    $broken ?? 1 !! 0
}

# ---- uninstall (M6): mark-and-sweep over a shared, content-addressed store --
# :for-reinstall relaxes three refusals, because the dist is coming right
# back: "not installed" becomes a fresh install (note, skip the removal),
# installed DEPENDENTS do not block (a removal would strand them; a reinstall
# restores what they depend on), and foreign PROVENANCE downgrades to a
# warning — the replacement becomes rakupp-owned, and the warning says so.
sub do-uninstall(@names, Str $prefix, Bool :$force, Bool :$for-reinstall) {
    my $p = $prefix.IO;
    unless $p.add('dist').d {
        if $for-reinstall {
            note "nothing installed in $prefix — installing fresh";
            return 0;
        }
        note "nothing installed in $prefix";
        return 1;
    }
    my %dists;
    for $p.add('dist').dir.grep(*.f) -> $f {
        my %m = try json-decode($f.slurp);
        %dists{$f.basename} = %m if %m;
    }

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
                note "not installed: $want-str — installing fresh";
                next;
            }
            note "not installed: $want-str";
            return 1;
        }
        if @hits.elems > 1 {
            note "ambiguous: $want-str matches {@hits.elems} distributions — name a version:";
            note "  {.value<name>}:ver<{.value<version>}>:auth<{.value<auth> // ''}>" for @hits;
            return 1;
        }
        my $dist-id = @hits[0].key;
        my %meta = @hits[0].value;
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
                return 1;
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
                note "$identity is depended on by {@dependents.unique.join(', ')} — reinstalling in place";
            }
            else {
                note "$identity is still depended on by {@dependents.unique.join(', ')} — refusing (--force to override)";
                return 1;
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
        say "uninstalled $identity";
    }
    0
}

sub list-installed(Str $prefix) {
    my $dist-dir = $prefix.IO.add('dist');
    unless $dist-dir.d {
        say "nothing installed in $prefix";
        return;
    }
    for $dist-dir.dir.sort -> $f {
        my %m = try json-decode($f.slurp);
        next unless %m;
        my $provides = (%m<provides> // {}).keys.sort.join(', ');
        say "{%m<name>}:ver<{%m<version> // '?'}>:auth<{%m<auth> // ''}>  ({$provides})";
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
sub installed-identities(Str $prefix) {
    my %have;
    my $dist-dir = $prefix.IO.add('dist');
    return %have unless $dist-dir.d;
    for $dist-dir.dir.grep(*.f) -> $f {
        my %m = try json-decode($f.slurp);
        next unless %m;
        %have{identity-key(%m<name>, %m<version> // %m<ver>, %m<auth>, %m<api>)} = True;
    }
    %have
}

sub MAIN(
    *@modules,                 #= modules or dists to install (Foo, Foo:ver<1.2+>)
    Bool :$dry-run,            #= resolve and print the plan; write nothing
    Bool :$list,               #= list what is installed in the target store
    Bool :$check,              #= check the store's integrity; fix nothing
    Bool :$uninstall,          #= remove distributions (rakupp uninstall Foo)
    Bool :$reinstall,          #= uninstall then install fresh (rakupp reinstall Foo)
    Bool :$no-test,            #= skip the per-distribution test suites
    Bool :$test-only,          #= build + run the named dists' suites; install only their deps (rakupp test)
    Bool :$force,              #= reinstall / uninstall despite refusals
    Bool :$refresh,            #= refetch the ecosystem index (else cached 24h)
    Str  :$to = %*ENV<HOME> ~ '/.raku',  #= the CURI store prefix to write
) {
    # `rakupp uninstall --list` is a mode mix, not a synonym for install
    # --list — refuse it rather than silently answering as a different command
    if ($uninstall || $reinstall || $test-only) && ($list || $check) {
        note "rakupp {$uninstall ?? 'uninstall' !! $reinstall ?? 'reinstall' !! 'test'} "
           ~ "--{$list ?? 'list' !! 'check'}: --list/--check belong to `rakupp install` — pick one";
        exit 2;
    }
    if $test-only && !@modules {
        note "usage: rakupp test Module ...";
        exit 2;
    }
    if $list {
        list-installed($to);
        return;
    }
    if $check {
        exit store-check($to);
    }
    if $uninstall {
        unless @modules {
            note "usage: rakupp uninstall [--force] Module ...";
            exit 2;
        }
        exit do-uninstall(@modules, $to, :force($force // False));
    }
    if $reinstall {
        unless @modules {
            note "usage: rakupp reinstall [--no-test] [--force] Module ...";
            exit 2;
        }
        # the uninstall half; the normal install flow below is the other half
        my $rc = do-uninstall(@modules, $to, :force($force // False), :for-reinstall);
        exit $rc if $rc != 0;
    }
    if $refresh && !@modules {
        # bare --refresh is a complete command: refetch the index, report,
        # stop. The REA cache refreshes only if it exists — nobody pays for
        # the 18 MB archive index before their first REA-resolved install.
        my @index = load-index(True).list;
        say "index refreshed: {@index.elems} distributions";
        if cache-dir.add('rea-meta.json').e {
            $REA-REFRESH = True;
            my @rea = rea-index();
            say "REA archive index refreshed: {@rea.elems} archived releases";
        }
        return;
    }
    unless @modules {
        note q:to/END/.trim;
            usage: rakupp install [options] Module ...
                   rakupp install --list | --check | --refresh
                   rakupp test Module ...      run the dists' own suites; installs only their deps
                   rakupp uninstall [--force] Module ...
                   rakupp reinstall [--no-test] [--force] Module ...
            options:
              --dry-run        resolve and print the plan; write nothing
              --list           what is installed in the target store
              --check          store integrity report (exit 1 on damage); fixes nothing
              --no-test        skip the per-distribution test suites
              --force          reinstall / uninstall despite refusals
              --refresh        refetch the ecosystem index(es) (else cached 24h);
                               alone: refresh and stop
              --to=PATH        the store prefix to use (default: ~/.raku)
            END
        exit 2;
    }

    $REA-REFRESH = $refresh // False;
    my @index = load-index($refresh // False).list;
    my %notes;
    my @plan = resolve(@index, @modules, %notes);

    if !@plan && %notes {
        note "cannot resolve: {%notes.map({ "{.key} ({.value})" }).join('; ')}";
        exit 1;
    }

    # --force means "do it anyway", so it asks the store nothing. Under
    # `rakupp reinstall` this runs AFTER the removal above — the dists it
    # took out are gone from dist/ and install fresh.
    my %have = $force ?? {} !! installed-identities($to);

    say "plan ({@plan.elems} distribution{@plan.elems == 1 ?? '' !! 's'}, dependencies first):";
    for @plan -> %e {
        my $known = %have{identity-key(%e<name>, %e<version>, %e<auth>, %e<api>)};
        say "  {%e<dist> // %e<name>}   {archive-url(%e)}{$known ?? '   (already installed)' !! ''}";
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
        %target{parse-identity($_)<name>} = True for @modules;
    }
    for @plan -> %e {
        my $is-target = $test-only
            && ?(%target{%e<name>} || (%e<provides> // {}).keys.first({ %target{$_} }));
        # Already in the store? install-one would fetch it, build it, run its
        # suite and only THEN be refused by the engine — say so now instead.
        # (`rakupp test` still tests the dists it was NAMED, installed or not.)
        if !$is-target && %have{identity-key(%e<name>, %e<version>, %e<auth>, %e<api>)} {
            say "already installed: {%e<dist> // %e<name>} (use --force to reinstall)";
            next;
        }
        my $done = try install-one(%e, $to, :$no-test, :$force, :test-only($is-target));
        unless $done {
            if $!.Str.contains('already installed') {
                say "already installed: {%e<dist> // %e<name>} (use --force to reinstall)";
                next;
            }
            note $!.Str;
            note "reinstall: the previous installation was already removed — `rakupp install {%e<name>}` (or --no-test) to restore"
                if $reinstall;
            exit 1;
        }
    }
    say "done: {@plan.elems} distribution{@plan.elems == 1 ?? '' !! 's'} processed into $to";
}
