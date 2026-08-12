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
# Test gate: a distribution's own t/ suite runs under rakupp before it is
# marked installed (--no-test to skip). Dependencies install first, so a
# dist's tests see its deps.

my constant $INDEX-URL = 'https://360.zef.pm/index.json';
my constant $CACHE-TTL = 24 * 3600;

# The engine's built-in JSON codec (the same Rakudo::Internals::JSON Rakudo
# ships) — the installer must not depend on an ecosystem JSON module, since
# installing those is its own job.
sub json-decode(Str $text) {
    Rakudo::Internals::JSON.from-json($text)
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
    sha1-file($t.Str)
}

# The store is shared with zef and Rakudo: every mutation happens under its
# repo.lock (a real flock — the engine builtin; on Windows the lock call
# answers -1 and we proceed unlocked, which the docs say out loud).
sub with-repo-lock(Str $prefix, &code) {
    my $tok = rakupp-repo-lock("$prefix/repo.lock");
    LEAVE rakupp-repo-unlock($tok) if $tok >= 0;
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

# Every index entry providing `name` (as dist name or module), constraints
# applied, newest version first.
sub candidates(@index, %want) {
    my @c = @index.grep(-> %e {
        (%e<name> // '') eq %want<name> || (%e<provides> // {}){%want<name>}:exists
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
        my @c = candidates(@index, %want);
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
        for (%e<depends> // []).flat -> $dep {
            if $dep ~~ Str {
                @work.push(parse-identity($dep));
            }
            else {
                %notes{~$dep} = 'a structured dependency this installer does not resolve yet';
            }
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

# The engine's CompUnit install takes any object with .meta and .IO.
class InstallableDist {
    has %.meta;
    has $.root;
    method IO {
        $.root
    }
}

sub run-dist-tests(%e, $root) {
    my @tests = $root.IO.add('t').d
        ?? $root.IO.add('t').dir.grep({ .extension eq 't' | 'rakutest' }).sort
        !! ();
    return True unless @tests;
    note "testing %e<name>: {@tests.elems} file{@tests.elems == 1 ?? '' !! 's'}";
    for @tests -> $t {
        my $p = run $*EXECUTABLE.absolute, '-I', $root.IO.add('lib').Str, $t.Str,
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

sub install-one(%e, Str $prefix, Bool :$no-test, Bool :$force) {
    my $url = base-url() ~ '/' ~ %e<path>;
    my $tmp = $*TMPDIR.add("rakupp-install-{$*PID}-{%e<name>.subst(/<-alnum>/, '-', :g)}");
    $tmp.mkdir;
    LEAVE { run 'rm', '-rf', $tmp.Str }

    my $tarball = $tmp.add('dist.tar.gz').Str;
    note "fetching $url";
    fetch-file($url, $tarball);

    # The archive must hash to the SHA-1 its index path names (fez archives
    # are content-addressed). Refuse anything else — this is the checksum
    # gate the plan's M2 requires, and it holds for mirrors too.
    if %e<path> ~~ / ( <[0..9 a..f]> ** 40 ) '.tar.gz' $ / {
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

    if !$no-test && !run-dist-tests(%e, $root) {
        die "%e<name>: its own test suite fails under rakupp — not installing (--no-test to override)";
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
                if $src-sha && !$p.add('sources').add($src-sha).e {
                    say "BROKEN: short/{$sdir.basename}/{$entry.basename} needs missing blob sources/$src-sha";
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
            $unref++;
        }
    }
    say "store check: {%dists.elems} distribution{%dists.elems == 1 ?? '' !! 's'}, "
        ~ "$broken broken, $unref unreferenced blob{$unref == 1 ?? '' !! 's'}"
        ~ ($unref ?? ' (wasted disk, not damage)' !! '');
    $broken ?? 1 !! 0
}

# ---- uninstall (M6): mark-and-sweep over a shared, content-addressed store --
sub do-uninstall(@names, Str $prefix, Bool :$force) {
    my $p = $prefix.IO;
    unless $p.add('dist').d {
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
            ((%m<name> // '') eq %want<name> || (%m<provides> // {}){%want<name>}:exists)
            && ver-ok(%m<version> // '', %want<ver>)
            && (%want<auth> eq '' || (%m<auth> // '') eq %want<auth>)
        });
        if !@hits {
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

        # ours? (a zef-installed dist is zef's; --force means you mean it)
        if !$force && !is-owned($prefix, $dist-id) {
            note "$identity was not installed by `rakupp install` — refusing (--force to override)";
            return 1;
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
            note "$identity is still depended on by {@dependents.unique.join(', ')} — refusing (--force to override)";
            return 1;
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
            #    broken `use`; an orphaned blob is only wasted disk
            for @provided -> $mod {
                my $e = $p.add('short').add(sha1-str($mod)).add($dist-id);
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

sub MAIN(
    *@modules,                 #= modules or dists to install (Foo, Foo:ver<1.2+>)
    Bool :$dry-run,            #= resolve and print the plan; write nothing
    Bool :$list,               #= list what is installed in the target store
    Bool :$check,              #= check the store's integrity; fix nothing
    Bool :$uninstall,          #= remove distributions (rakupp uninstall Foo)
    Bool :$no-test,            #= skip the per-distribution test suites
    Bool :$force,              #= reinstall / uninstall despite refusals
    Bool :$refresh,            #= refetch the ecosystem index (else cached 24h)
    Str  :$to = %*ENV<HOME> ~ '/.raku',  #= the CURI store prefix to write
) {
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
    unless @modules {
        note "usage: rakupp install [--dry-run] [--check] [--no-test] [--force] [--refresh] Module ...";
        exit 2;
    }

    my @index = load-index($refresh // False).list;
    my %notes;
    my @plan = resolve(@index, @modules, %notes);

    if !@plan && %notes {
        note "cannot resolve: {%notes.map({ "{.key} ({.value})" }).join('; ')}";
        exit 1;
    }

    say "plan ({@plan.elems} distribution{@plan.elems == 1 ?? '' !! 's'}, dependencies first):";
    for @plan -> %e {
        say "  {%e<dist> // %e<name>}   {base-url()}/{%e<path>}";
    }
    for %notes.sort -> $n {
        say "  skipped: {$n.key} — {$n.value}";
    }

    if $dry-run {
        say "dry run: nothing written";
        return;
    }

    for @plan -> %e {
        my $done = try install-one(%e, $to, :$no-test, :$force);
        unless $done {
            if $!.Str.contains('already installed') {
                say "already installed: {%e<dist> // %e<name>} (use --force to reinstall)";
                next;
            }
            note $!.Str;
            exit 1;
        }
    }
    say "done: {@plan.elems} distribution{@plan.elems == 1 ?? '' !! 's'} processed into $to";
}
