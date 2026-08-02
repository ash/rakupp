#!/usr/bin/env raku
#
# modinfo — a Raku distribution inspector.
#
# Reads META6.json files (from unpacked distributions on disk, or from a
# Rakudo installation database), builds the dependency graph, validates the
# metadata, fingerprints the source, and reports in table / JSON / YAML / XML.
#
# This is the ecosystem showcase: everything below the argument parser is done
# with modules from the Raku ecosystem's top 50 rather than hand-rolled. Run
# `modinfo about` for the list.
#
# Runs unchanged on Rakudo and on Raku++.

use JSON::Fast;                 # META6.json in, JSON reports out
use YAMLish;                    # YAML config in, YAML reports out
use XML;                        # XML reports (built as a document, not printf)
use Config;                     # layered configuration (defaults < file < CLI)
use Config::Parser;             # ... and its parser interface, implemented below
use Hash::Merge;                # merging those layers
use IO::Glob;                   # finding META6.json / the installed dist db
use File::Find;                 # walking lib/ to catch undeclared files
use File::Which;                # locating the engine we are running under
use File::Directory::Tree;      # creating the export directory
use File::Temp;                 # atomic writes (temp file, then rename)
use Digest::SHA1;               # per-file content hashes
use MIME::Base64;               # npm-style `sha1-<base64>` integrity strings
use Abbreviations;              # `modinfo show JSON::F` resolves to JSON::Fast
# Imported by tag rather than :ALL — Text::Utils and Abbreviations both export
# a `sort-list` and its enum, and :ALL from both would collide.
use Text::Utils :wrap-paragraph, :commify, :list2text;
use Terminal::ANSIColor;        # named styles
use Color;                      # the dependency-count heat gradient
use Data::Dump;                 # --debug

my constant VERSION = '1.0.0';

# Without this, options are only recognised before the first positional, so
# `modinfo show Gadget --no-color` would read --no-color as a distribution name.
my %*SUB-MAIN-OPTS = :named-anywhere;

# The ecosystem modules this program is built on, for `modinfo about`.
my constant @MODULES-USED =
    'Abbreviations',   'Color',                'Config',
    'Data::Dump',      'Digest',               'File::Directory::Tree',
    'File::Find',      'File::Temp',           'File::Which',
    'Hash::Merge',     'IO::Glob',             'JSON::Fast',
    'MIME::Base64',    'Terminal::ANSIColor',  'Text::Utils',
    'XML',             'YAMLish';

#| A YAML back end for Config. Config ships only a NULL parser; every real
#| format lives in a separate distribution. Rather than depend on one more,
#| modinfo satisfies the interface itself, on top of YAMLish.
class Config::Parser::YAMLish is Config::Parser {
    multi method read(IO() $path --> Hash) {
        my $parsed = load-yaml($path.slurp);
        $parsed ~~ Associative ?? $parsed.Hash !! {}
    }
    multi method write(IO() $path, Hash $config --> Bool) {
        $path.spurt(save-yaml($config));
        True
    }
}

#| One entry of a `depends` list, after the `Name:ver<…>:auth<…>:api<…>` form
#| has been taken apart.
class DepSpec {
    has Str $.name is required;
    has Str $.ver  = '';
    has Str $.auth = '';
    has Str $.api  = '';
    has Str $.phase = 'runtime';   # runtime | build | test

    method gist(--> Str) {
        my @bits;
        @bits.push("ver $!ver")   if $!ver;
        @bits.push("auth $!auth") if $!auth;
        @bits.push("api $!api")   if $!api;
        @bits ?? "$!name ({@bits.join(', ')})" !! $!name
    }
}

#| One distribution, as described by its META6.json.
class Dist {
    has Str      $.name is required;
    has Str      $.version    = '';
    has Str      $.auth       = '';
    has Str      $.api        = '';
    has Str      $.description = '';
    has Str      $.license    = '';
    has Str      $.source-url = '';
    has          @.authors;
    has          @.tags;
    has          %.provides;
    has          @.resources;
    has DepSpec  @.depends;
    has          %.meta;             # the raw decoded META6.json
    has IO::Path $.root;             # Nil for installed-database entries
    has Str      $.origin = 'path';  # path | installed

    method runtime-deps { @!depends.grep(*.phase eq 'runtime') }
    method all-deps     { @!depends }

    #| `Name:ver<…>` — how zef and the ecosystem index refer to a release.
    method spec(--> Str) {
        my $s = $!name;
        $s ~= ":ver<$!version>" if $!version;
        $s ~= ":auth<$!auth>"   if $!auth;
        $s
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Reading metadata
# ─────────────────────────────────────────────────────────────────────────────

#| Take apart `JSON::Fast:ver<0.19+>:auth<zef:timo>:api<1>`.
sub parse-dep(Str $raw, Str :$phase = 'runtime' --> DepSpec) {
    my $name = $raw;
    my (%adverb);
    # Adverbs are `:key<value>` and values may themselves contain colons
    # (`auth<zef:timo>`), so match the brackets rather than splitting on ':'.
    while $name ~~ /^ (.*?) ':' (\w+) '<' (<-[>]>*) '>' $/ {
        %adverb{ ~$1 } = ~$2;
        $name = ~$0;
    }
    DepSpec.new(
        name => $name.trim,
        ver  => %adverb<ver> // '',
        auth => %adverb<auth> // '',
        api  => %adverb<api> // '',
        :$phase,
    )
}

#| `depends` has two shapes in the wild: a flat list, or a hash keyed by phase
#| with `requires` lists inside. Accept both, and accept hash-form entries.
sub collect-deps($raw, Str $phase --> Array[DepSpec]) {
    my DepSpec @out;
    return @out unless $raw.defined;

    sub add($entry, Str $ph) {
        given $entry {
            when Str        { @out.push(parse-dep($_, phase => $ph)) }
            when Associative {
                my $n = .<name> // .<from> // '';
                @out.push(DepSpec.new(name => ~$n, phase => $ph)) if $n;
            }
            when Positional { add($_, $ph) for .list }
        }
    }

    if $raw ~~ Associative {
        for $raw.keys.sort -> $ph {
            my $sect = $raw{$ph};
            my $ph-name = $ph eq 'runtime' ?? $phase !! $ph;
            if $sect ~~ Associative {
                add($sect{$_}, $ph-name) for $sect.keys.sort;
            }
            else {
                add($sect, $ph-name);
            }
        }
    }
    else {
        add($raw, $phase);
    }
    @out
}

#| Build a Dist from decoded META6.json data.
sub dist-from-meta(%meta, IO::Path :$root, Str :$origin = 'path' --> Dist) {
    my DepSpec @deps;
    @deps.append(collect-deps(%meta<depends>,       'runtime'));
    @deps.append(collect-deps(%meta<build-depends>, 'build'));
    @deps.append(collect-deps(%meta<test-depends>,  'test'));

    Dist.new(
        name        => ~(%meta<name> // '?'),
        version     => ~(%meta<version> // ''),
        auth        => ~(%meta<auth> // %meta<author> // ''),
        api         => ~(%meta<api> // ''),
        description => ~(%meta<description> // ''),
        license     => ~(%meta<license> // ''),
        source-url  => ~(%meta<source-url> // ''),
        authors     => (%meta<authors> // []).list.map(~*),
        tags        => (%meta<tags> // []).list.map(~*).sort,
        provides    => (%meta<provides> // {}).Hash,
        resources   => (%meta<resources> // []).list.map(~*),
        depends     => @deps,
        meta        => %meta,
        :$root,
        :$origin,
    )
}

#| Scan a directory of unpacked distributions. IO::Glob finds the META6.json
#| files; the results are sorted, because glob order is filesystem order and
#| this report has to be reproducible.
sub scan-path(IO::Path $dir --> Array[Dist]) {
    my Dist @dists;
    die "no such directory: $dir" unless $dir.d;

    my @metas = glob('*/META6.json').dir($dir)».IO;
    @metas.push($dir.add('META6.json')) if $dir.add('META6.json').f;

    for @metas.map(*.absolute).unique.sort -> $path {
        my %meta;
        try {
            %meta = from-json($path.IO.slurp);
            CATCH { default { note "  ! unreadable META6.json: $path"; } }
        }
        next unless %meta;
        @dists.push(dist-from-meta(%meta, root => $path.IO.parent));
    }
    @dists
}

#| Read a Rakudo installation database — one JSON file per installed
#| distribution under `<repo>/dist/`.
sub scan-installed(--> Array[Dist]) {
    my Dist @dists;
    my @roots = (%*ENV<RAKUDO_HOME>, $*HOME ?? $*HOME.add('.raku').Str !! Str)
                    .grep(*.defined).grep(*.chars);
    @roots.push($_) with %*ENV<HOME> ~~ Str ?? "%*ENV<HOME>/.raku" !! Str;

    my %seen;
    for @roots.unique -> $root {
        my $dir = $root.IO.add('dist');
        next unless $dir.d;
        for glob('*').dir($dir)».IO.map(*.absolute).sort -> $path {
            next unless $path.IO.f;
            my %meta;
            try {
                %meta = from-json($path.IO.slurp);
                CATCH { default { next } }
            }
            next unless %meta && %meta<name>;
            my $key = "%meta<name> %meta<version>";
            next if %seen{$key}++;
            @dists.push(dist-from-meta(%meta, root => IO::Path, origin => 'installed'));
        }
    }
    @dists
}

# ─────────────────────────────────────────────────────────────────────────────
# The graph
# ─────────────────────────────────────────────────────────────────────────────

#| Everything the commands below need to know about how the distributions
#| relate: forward edges, reverse edges, and which names are out of the set.
class Graph {
    has Dist @.dists;
    has      %.by-name;
    has      %.edges;      # name => sorted list of in-set dependency names
    has      %.rdeps;      # name => sorted list of in-set dependents
    has      %.external;   # name => number of in-set dists that want it

    method build(Dist @dists, Bool :$all = False) {
        my %by-name = @dists.map({ .name => $_ });
        my (%edges, %rdeps, %external);
        %edges{.name}  = [] for @dists;
        %rdeps{.name}  = [] for @dists;

        for @dists -> $d {
            my @deps = $all ?? $d.all-deps !! $d.runtime-deps;
            for @deps.map(*.name).unique.sort -> $dep {
                next if $dep eq $d.name;          # self-dependency: not an edge
                if %by-name{$dep}:exists {
                    %edges{$d.name}.push($dep);
                    %rdeps{$dep}.push($d.name);
                }
                else {
                    %external{$dep}++;
                }
            }
        }
        %edges{$_} = %edges{$_}.unique.sort.Array for %edges.keys;
        %rdeps{$_} = %rdeps{$_}.unique.sort.Array for %rdeps.keys;

        self.new(:@dists, :%by-name, :%edges, :%rdeps, :%external)
    }

    method dist(Str $name) { %!by-name{$name} }
    method names           { %!by-name.keys.sort }
    method edge-count      { %!edges.values.map(*.elems).sum }

    #| Kahn's algorithm. Returns the order, plus whatever is left over —
    #| which is exactly the set of distributions caught in a cycle.
    method topo-sort() {
        my %indeg = %!edges.map({ .key => .value.elems });
        my @ready = %indeg.grep(*.value == 0)».key.sort;
        my @order;
        while @ready {
            my $n = @ready.shift;
            @order.push($n);
            for %!rdeps{$n}.list.sort -> $up {
                %indeg{$up}--;
                if %indeg{$up} == 0 {
                    @ready.push($up);
                    @ready = @ready.sort;
                }
            }
        }
        my @stuck = %indeg.grep(*.value > 0)».key.sort;
        (@order, @stuck)
    }

    #| Depth-first walk that finds one concrete cycle per stuck node, so the
    #| report can name the loop instead of just the members.
    method find-cycles(--> Array) {
        my @cycles;
        my %seen;
        for self.names -> $start {
            next if %seen{$start};
            my @stack;
            my %on-stack;
            my sub walk(Str $n) {
                if %on-stack{$n} {
                    my $i = @stack.first($n, :k);
                    @cycles.push([|@stack[$i .. *], $n]) if $i.defined;
                    return;
                }
                return if %seen{$n};
                %on-stack{$n} = True;
                @stack.push($n);
                walk($_) for %!edges{$n}.list;
                @stack.pop;
                %on-stack{$n} = False;
                %seen{$n} = True;
            }
            walk($start);
        }
        # Normalise each cycle so the same loop is reported identically no
        # matter which node the walk entered it from.
        my %uniq;
        for @cycles -> @c {
            my @body = @c[0 ..^ @c.end];
            my $min  = @body.min;
            my $i    = @body.first($min, :k) // 0;
            my @rot  = |@body[$i .. *], |@body[0 ..^ $i];
            %uniq{@rot.join(' -> ')} = [|@rot, @rot[0]];
        }
        %uniq{$_} for %uniq.keys.sort;
        my @out = %uniq.keys.sort.map({ %uniq{$_} });
        @out
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Metadata validation
# ─────────────────────────────────────────────────────────────────────────────

class Finding {
    has Str $.level is required;   # ERROR | WARN | INFO
    has Str $.text  is required;
}

#| The rules a well-formed META6.json is expected to satisfy. These follow the
#| checks the ecosystem's own metadata tests make, plus a few filesystem
#| cross-checks that are only possible with the distribution unpacked.
sub check-dist(Dist $d --> Array[Finding]) {
    my Finding @f;
    sub err($t)  { @f.push(Finding.new(level => 'ERROR', text => $t)) }
    sub warning($t) { @f.push(Finding.new(level => 'WARN',  text => $t)) }
    sub info($t) { @f.push(Finding.new(level => 'INFO',  text => $t)) }

    err('name is missing')          unless $d.name && $d.name ne '?';
    err('version is missing')       unless $d.version;
    err('version is "*" — a distribution must publish a concrete version')
        if $d.version eq '*';
    err('provides is empty')        unless $d.provides;
    err('description is missing')   unless $d.description;
    warning('license is missing')     unless $d.license;
    warning('auth is missing')        unless $d.auth;
    warning('authors is empty')       unless $d.authors;
    warning('tags is empty')          unless $d.tags;
    warning('source-url is missing')  unless $d.source-url;

    unless $d.meta<raku> // $d.meta<perl> {
        warning('neither a "raku" nor a "perl" language-version field');
    }

    # Duplicate and self-referential dependencies.
    my %count;
    %count{.name}++ for $d.all-deps;
    for %count.keys.sort -> $n {
        warning("dependency $n is listed {%count{$n}} times") if %count{$n} > 1;
    }
    err("depends on itself") if %count{$d.name};

    # Filesystem cross-checks, only meaningful for an unpacked distribution.
    with $d.root {
        for $d.provides.keys.sort -> $mod {
            my $rel = ~$d.provides{$mod};
            err("provides $mod => $rel (file not found)")
                unless $d.root.add($rel).f;
        }
        for $d.resources.sort -> $res {
            warning("resource $res is declared but missing")
                unless $d.root.add('resources').add($res).f;
        }

        # Anything under lib/ that provides forgot about. File::Find walks the
        # tree; the module map is keyed by path, so compare on paths.
        my $lib = $d.root.add('lib');
        if $lib.d {
            my %declared = $d.provides.values.map({ $d.root.add(~$_).absolute => True });
            my @orphans = find(dir => $lib, name => /'.' [rakumod | pm6 | pm] $/)
                            .map(*.absolute).grep({ !%declared{$_} }).sort;
            for @orphans -> $o {
                my $rel = $o.subst($d.root.absolute ~ '/', '');
                info("$rel is not listed in provides");
            }
        }

        # An unpacked distribution is conventionally `Name-Version`, with `::`
        # written either `-` or `--` depending on who unpacked it. Accept both.
        if $d.version && $d.version ne '*' {
            my $dirname = $d.root.basename;
            my @expect = ('-', '--').map({ $d.name.subst('::', $_, :g) ~ '-' ~ $d.version });
            info("directory is $dirname, not {@expect[0]}")
                unless $dirname eq any(@expect);
        }
    }
    @f
}

# ─────────────────────────────────────────────────────────────────────────────
# Fingerprinting
# ─────────────────────────────────────────────────────────────────────────────

sub hex-of(Blob $b --> Str) { $b.list.map({ .fmt('%02x') }).join }

#| SHA-1 of one file's bytes.
sub file-digest(IO::Path $p --> Blob) { sha1($p.slurp(:bin)) }

#| A distribution's fingerprint is the SHA-1 of `<module> <sha1>` lines, one
#| per provided module in sorted order — so it depends on the content and the
#| module map, not on directory order or timestamps.
sub dist-digest(Dist $d --> Blob) {
    return Blob.new unless $d.root;
    my $manifest = $d.provides.keys.sort.map(-> $mod {
        my $p = $d.root.add(~$d.provides{$mod});
        my $h = $p.f ?? hex-of(file-digest($p)) !! ('-' x 40);
        "$mod $h"
    }).join("\n");
    sha1($manifest)
}

#| The format npm calls Subresource Integrity: algorithm, dash, base64.
sub integrity(Blob $b --> Str) {
    return '-' unless $b.elems;
    'sha1-' ~ MIME::Base64.encode($b, :oneline)
}

# ─────────────────────────────────────────────────────────────────────────────
# Presentation
# ─────────────────────────────────────────────────────────────────────────────

my Bool $USE-COLOR = True;
my Int  $WIDTH     = 92;

sub style(Str $text, Str $spec --> Str) {
    $USE-COLOR ?? colored($text, $spec) !! $text
}

#| Map a count onto a green→red gradient. Color does the colour-space work;
#| this only picks the hue and emits the terminal escape.
sub heat(Str $text, Int $value, Int $max --> Str) {
    return $text unless $USE-COLOR;
    return $text unless $max > 0;
    my $frac = ($value / $max).Num;
    my $hue  = (120 - 120 * $frac).round;
    my $c    = Color.new(hsl => [$hue, 65, 55]);
    my ($r, $g, $b) = $c.rgb;
    "\e[38;2;{$r};{$g};{$b}m$text\e[0m"
}

sub header(Str $text --> Str) { style($text, 'bold') }

sub rule(Int $n --> Str) { '─' x $n }

#| Lay out rows under headings, sizing each column to its widest cell. Column
#| indices named in :right are right-aligned, which is what the count columns
#| want.
sub table(@headings, @rows, :@right --> Str) {
    return '' unless @rows;
    my %right = @right.map({ $_ => True });
    my @w = @headings.map(*.chars);
    for @rows -> @r {
        for @r.kv -> $i, $cell {
            @w[$i] = max(@w[$i] // 0, plain($cell).chars);
        }
    }
    my sub cell(Int $i, Str $s) { %right{$i} ?? lpad($s, @w[$i]) !! pad($s, @w[$i]) }

    my @lines;
    @lines.push(header(@headings.kv.map(-> $i, $h { cell($i, $h) }).join('  ')).trim-trailing);
    @lines.push(rule(@w.sum + 2 * (@w.elems - 1)));
    for @rows -> @r {
        @lines.push(@r.kv.map(-> $i, $c { cell($i, $c) }).join('  ').trim-trailing);
    }
    @lines.join("\n")
}

#| Pad to a visible width, ignoring any ANSI escapes the cell carries.
sub pad(Str $s, Int $w --> Str) {
    my $vis = plain($s).chars;
    $vis >= $w ?? $s !! $s ~ (' ' x ($w - $vis))
}

sub lpad(Str $s, Int $w --> Str) {
    my $vis = plain($s).chars;
    $vis >= $w ?? $s !! (' ' x ($w - $vis)) ~ $s
}

#| Paths are shown relative to the working directory when they are under it,
#| so reports do not depend on where the checkout lives.
sub tidy-path(IO::Path $p --> Str) {
    my $cwd = $*CWD.absolute;
    my $abs = $p.absolute;
    $abs.starts-with($cwd ~ '/') ?? $abs.substr($cwd.chars + 1) !! $abs
}

sub plural(Int $n, Str $one, Str $many --> Str) {
    commify($n) ~ ' ' ~ ($n == 1 ?? $one !! $many)
}

sub plain(Str $s --> Str) { $s.subst(/\e '[' <[0..9;]>* 'm'/, '', :g) }

#| Wrap a description into the space left after a label. Text::Utils does the
#| line breaking; this only re-indents the continuation lines.
sub field(Str $label, Str $value, Int :$indent = 15 --> Str) {
    my $head = pad($label, $indent);
    return ('  ' ~ $head).trim-trailing unless $value.chars;

    my $room  = max(20, $WIDTH - $indent);
    my @lines = wrap-paragraph($value.words, max-line-length => $room);
    ('  ' ~ $head ~ (@lines.head // ''))
        ~ @lines.skip.map({ "\n  " ~ (' ' x $indent) ~ $_ }).join
}

# ─────────────────────────────────────────────────────────────────────────────
# Commands
# ─────────────────────────────────────────────────────────────────────────────

sub cmd-list(Graph $g --> Str) {
    my $max-r = $g.names.map({ $g.rdeps{$_}.elems }).max // 0;
    my @rows = $g.names.map(-> $n {
        my $d = $g.dist($n);
        [
            $n,
            $d.version || '—',
            $d.auth || '—',
            $g.edges{$n}.elems.Str,
            heat($g.rdeps{$n}.elems.Str, $g.rdeps{$n}.elems, $max-r),
            $d.provides.elems.Str,
        ]
    });
    my @out;
    @out.push(table(<distribution version auth deps rdeps provides>, @rows,
                    right => [3, 4, 5]));
    @out.push('');
    @out.push(plural($g.dists.elems, 'distribution', 'distributions') ~ ', '
              ~ plural($g.edge-count, 'internal edge', 'internal edges') ~ ', '
              ~ plural($g.external.elems, 'external reference', 'external references'));
    @out.join("\n")
}

sub cmd-show(Graph $g, Dist $d --> Str) {
    my @out;
    @out.push(header("{$d.name} {$d.version}"));
    @out.push(field('auth', $d.auth)) if $d.auth;
    @out.push(field('api', $d.api))   if $d.api;
    @out.push(field('license', $d.license)) if $d.license;
    # The Oxford comma only belongs in a list of three or more.
    @out.push(field('authors', list2text($d.authors,
                                         optional-comma => $d.authors > 2))) if $d.authors;
    @out.push(field('description', $d.description)) if $d.description;
    @out.push(field('tags', $d.tags.join(', '))) if $d.tags;
    @out.push(field('source-url', $d.source-url)) if $d.source-url;

    if $d.provides {
        my @keys = $d.provides.keys.sort;
        my $w = @keys.map(*.chars).max;
        @out.push(field('provides', ''));
        @out.push('  ' ~ (' ' x 15) ~ pad($_, $w) ~ '  ' ~ $d.provides{$_})
            for @keys;
    }

    for <runtime build test> -> $phase {
        my @deps = $d.all-deps.grep(*.phase eq $phase).sort(*.name);
        next unless @deps;
        my $label = $phase eq 'runtime' ?? 'depends' !! $phase ~ '-depends';
        @out.push(field($label, ''));
        for @deps -> $dep {
            my $ext = $g.dist($dep.name) ?? '' !! style('  [external]', 'yellow');
            @out.push('  ' ~ (' ' x 15) ~ $dep.gist ~ $ext);
        }
    }

    my @r = $g.rdeps{$d.name}.list;
    @out.push(field('rdeps', @r ?? @r.join(', ') !! '(none in this set)'));

    if $d.root {
        @out.push(field('fingerprint', integrity(dist-digest($d))));
        @out.push(field('root', tidy-path($d.root)));
    }
    else {
        @out.push(field('origin', 'installation database'));
    }
    @out.grep(*.chars).join("\n")
}

#| Print a dependency tree. `%seen` is per-branch so a diamond still prints
#| twice, but a cycle is cut and labelled rather than recursed into.
sub tree-lines(Graph $g, Str $name, %path, Str $prefix, Bool :$reverse --> Array) {
    my @out;
    my @next = ($reverse ?? $g.rdeps{$name} !! $g.edges{$name}).list;
    for @next.kv -> $i, $child {
        my $last = $i == @next.end;
        my $branch = $last ?? '└── ' !! '├── ';
        if %path{$child} {
            @out.push($prefix ~ $branch ~ style("$child (cycle)", 'red'));
            next;
        }
        @out.push($prefix ~ $branch ~ $child ~ ' ' ~ style($g.dist($child).version, 'blue'));
        my %deeper = %path.clone;
        %deeper{$child} = True;
        @out.append(tree-lines($g, $child, %deeper,
                               $prefix ~ ($last ?? '    ' !! '│   '), :$reverse));
    }
    @out
}

sub cmd-deps(Graph $g, Dist $d, Bool :$reverse --> Str) {
    my @out;
    @out.push(header("{$d.name} {$d.version}")
              ~ ($reverse ?? '  (what depends on it)' !! '  (what it depends on)'));
    my @lines = tree-lines($g, $d.name, { $d.name => True }, '', :$reverse);
    @out.append(@lines ?? @lines !! ['  (nothing)']);

    unless $reverse {
        my @ext = $d.runtime-deps.map(*.name).grep({ !$g.dist($_) }).unique.sort;
        if @ext {
            @out.push('');
            @out.push('external: ' ~ @ext.join(', '));
        }
    }
    @out.join("\n")
}

sub cmd-graph(Graph $g --> Str) {
    my ($order, $stuck) = $g.topo-sort;
    my @out;
    @out.push(header('graph'));
    @out.push('  ' ~ plural($g.dists.elems, 'distribution', 'distributions')
              ~ ', ' ~ plural($g.edge-count, 'internal edge', 'internal edges'));

    my @roots = $g.names.grep({ !$g.rdeps{$_}.elems });
    my @leaves = $g.names.grep({ !$g.edges{$_}.elems });
    @out.push(field('roots', @roots.join(', ') || '(none)', indent => 12));
    @out.push(field('leaves', @leaves.join(', ') || '(none)', indent => 12));

    @out.push('');
    @out.push(header('build order'));
    if $order.elems {
        @out.append(wrap-paragraph($order.join(' → ').words,
                                   max-line-length => $WIDTH - 2).map({ '  ' ~ $_ }));
    }
    else {
        @out.push('  (none — every distribution is in a cycle)');
    }

    my @cycles = $g.find-cycles;
    if @cycles {
        @out.push('');
        @out.push(header('cycles'));
        @out.push('  ' ~ style(.join(' → '), 'red')) for @cycles;
        @out.push('  ' ~ plural($stuck.elems, 'distribution', 'distributions')
                  ~ ' cannot be ordered');
    }

    if $g.external {
        @out.push('');
        @out.push(header('external references'));
        my $max = $g.external.values.max;
        for $g.external.keys.sort({ -$g.external{$_}, $_ }) -> $n {
            @out.push('  ' ~ pad($n, 32) ~ heat($g.external{$n}.Str, $g.external{$n}, $max));
        }
    }
    @out.join("\n")
}

#| Rank by reverse dependencies — the measurement that produces an ecosystem's
#| "most depended-on" list.
sub cmd-rank(Graph $g, Int $top --> Str) {
    my %score;
    %score{$_} = $g.rdeps{$_}.elems for $g.names;
    %score{$_} += $g.external{$_} for $g.external.keys;

    my @ranked = %score.keys.sort({ -%score{$_}, $_ }).head($top);
    my $max = @ranked.map({ %score{$_} }).max // 0;
    my @rows = @ranked.kv.map(-> $i, $n {
        [
            ($i + 1).Str,
            $n,
            $g.dist($n) ?? ($g.dist($n).version || '—') !! style('(external)', 'yellow'),
            heat(%score{$n}.Str, %score{$n}, $max),
        ]
    });
    table(<# distribution version dependents>, @rows, right => [0, 3])
}

sub cmd-check(Graph $g, @dists --> List) {
    my @out;
    my ($errors, $warnings, $notes, $clean) = 0, 0, 0, 0;
    for @dists.sort(*.name) -> $d {
        my @f = check-dist($d);
        my $e = @f.grep(*.level eq 'ERROR').elems;
        my $w = @f.grep(*.level eq 'WARN').elems;
        if    $e     { $errors++ }
        elsif $w     { $warnings++ }
        elsif @f     { $notes++ }
        else         { $clean++ }
        next unless @f;

        @out.push(header("{$d.name} {$d.version}")
                  ~ ($d.root ?? '  (' ~ tidy-path($d.root) ~ ')' !! ''));
        for @f -> $f {
            my $tag = do given $f.level {
                when 'ERROR' { style('ERROR', 'bold red') }
                when 'WARN'  { style('WARN ', 'yellow') }
                default      { style('INFO ', 'blue') }
            };
            @out.push("  $tag  {$f.text}");
        }
        @out.push('');
    }
    @out.push(plural(@dists.elems, 'distribution', 'distributions') ~ ' checked: '
              ~ "$errors with errors, $warnings with warnings, "
              ~ "$notes with notes only, $clean clean");
    (@out.join("\n"), $errors)
}

sub cmd-about(--> Str) {
    my @out;
    @out.push(header("modinfo {VERSION}") ~ ' — a Raku distribution inspector');
    @out.push('');
    @out.push('Built on these ecosystem distributions:');
    for @MODULES-USED.sort -> $m {
        @out.push('  ' ~ $m);
    }
    @out.push('');
    @out.push(field('engine', $*RAKU.compiler.name ~ ' ' ~ $*RAKU.compiler.version, indent => 10));
    @out.push(field('binary', which($*EXECUTABLE.basename) // $*EXECUTABLE.Str, indent => 10));
    @out.join("\n")
}

# ─────────────────────────────────────────────────────────────────────────────
# Reports
# ─────────────────────────────────────────────────────────────────────────────

#| One plain data structure, rendered three ways. Keys are emitted in sorted
#| order everywhere so the three files diff cleanly against each other and
#| against another engine's run.
sub report-data(Graph $g --> Hash) {
    my ($order, $stuck) = $g.topo-sort;
    my %report =
        generator    => "modinfo {VERSION}",
        distributions => $g.names.map(-> $n {
            my $d = $g.dist($n);
            my %h =
                name        => $d.name,
                version     => $d.version,
                auth        => $d.auth,
                description => $d.description,
                license     => $d.license,
                spec        => $d.spec,
                provides    => $d.provides.keys.sort.list,
                depends     => $g.edges{$n}.list,
                external    => $d.runtime-deps.map(*.name).grep({ !$g.dist($_) }).unique.sort.list,
                rdeps       => $g.rdeps{$n}.list,
                findings    => check-dist($d).map({ %( level => .level, text => .text ) }).list,
                ;
            %h<integrity> = integrity(dist-digest($d)) if $d.root;
            %h
        }).list,
        summary => %(
            count       => $g.dists.elems,
            edges       => $g.edge-count,
            externals   => $g.external.keys.sort.list,
            build-order => $order.list,
            cycles      => $g.find-cycles.map(*.list).list,
            unordered   => $stuck.list,
        );
    %report
}

sub report-json(%data --> Str) { to-json(%data, :sorted-keys, :pretty) }

sub report-yaml(%data --> Str) { save-yaml(%data) }

sub xml-escape(Str $s --> Str) {
    $s.subst('&', '&amp;', :g).subst('<', '&lt;', :g).subst('>', '&gt;', :g)
      .subst('"', '&quot;', :g)
}

#| Walk the document and lay it out with indentation. XML serialises to a
#| single line, and this report is meant to be read and diffed. Attributes are
#| emitted in sorted order — they live in a Hash, so their natural order varies
#| from run to run and would make two identical reports differ.
sub xml-pretty(XML::Node $node, Int $depth = 0 --> Str) {
    my $pad = '  ' x $depth;

    if $node ~~ XML::Text {
        my $t = $node.text.trim;
        return $t ?? $pad ~ xml-escape($t) !! '';
    }
    unless $node ~~ XML::Element {
        return $pad ~ $node.Str.trim;
    }

    my $attrs = $node.attribs.keys.sort
                    .map({ ' ' ~ $_ ~ '="' ~ xml-escape(~$node.attribs{$_}) ~ '"' }).join;
    my @kids = $node.nodes.grep({ !($_ ~~ XML::Text) || .text.trim.chars });

    # `$pad<` inside a string would be read as a hash subscript, so the tags
    # are concatenated rather than interpolated.
    return $pad ~ '<' ~ $node.name ~ $attrs ~ '/>' unless @kids;

    # An element holding nothing but text stays on one line.
    if @kids.all ~~ XML::Text {
        my $text = @kids.map({ .text.trim }).join(' ');
        return $pad ~ '<' ~ $node.name ~ $attrs ~ '>'
               ~ xml-escape($text) ~ '</' ~ $node.name ~ '>';
    }

    ($pad ~ '<' ~ $node.name ~ $attrs ~ '>',
     |@kids.map({ xml-pretty($_, $depth + 1) }).grep(*.chars),
     $pad ~ '</' ~ $node.name ~ '>').join("\n")
}

#| The XML report is built as a document rather than printed as text — that is
#| the point of using XML here.
sub report-xml(%data --> Str) {
    my $root = XML::Element.new(:name<modinfo>);
    $root.append(make-xml('generator', %data<generator>));

    my $dists = XML::Element.new(:name<distributions>);
    for %data<distributions>.list -> %d {
        my $e = XML::Element.new(:name<distribution>, :attribs(%( :name(%d<name>) )));
        $e.append(make-xml('version', %d<version>));
        $e.append(make-xml('auth', %d<auth>)) if %d<auth>;
        $e.append(make-xml('license', %d<license>)) if %d<license>;
        $e.append(make-xml('description', %d<description>)) if %d<description>;
        $e.append(make-xml('integrity', %d<integrity>)) if %d<integrity>;

        my $prov = XML::Element.new(:name<provides>);
        $prov.append(XML::Element.new(:name<module>, :attribs(%( :name($_) ))))
            for %d<provides>.list;
        $e.append($prov);

        my $deps = XML::Element.new(:name<depends>);
        $deps.append(XML::Element.new(:name<dependency>, :attribs(%( :name($_) ))))
            for %d<depends>.list;
        $deps.append(XML::Element.new(:name<external>, :attribs(%( :name($_) ))))
            for %d<external>.list;
        $e.append($deps);

        if %d<findings>.list {
            my $fs = XML::Element.new(:name<findings>);
            for %d<findings>.list -> %f {
                my $f = XML::Element.new(:name<finding>, :attribs(%( :level(%f<level>) )));
                $f.append(XML::Text.new(text => %f<text>));
                $fs.append($f);
            }
            $e.append($fs);
        }
        $dists.append($e);
    }
    $root.append($dists);

    my $sum = XML::Element.new(:name<summary>);
    $sum.append(make-xml('count', ~%data<summary><count>));
    $sum.append(make-xml('edges', ~%data<summary><edges>));
    my $order = XML::Element.new(:name<build-order>);
    $order.append(XML::Element.new(:name<step>, :attribs(%( :name($_) ))))
        for %data<summary><build-order>.list;
    $sum.append($order);
    for %data<summary><cycles>.list -> @c {
        my $cy = XML::Element.new(:name<cycle>);
        $cy.append(XML::Element.new(:name<step>, :attribs(%( :name($_) )))) for @c;
        $sum.append($cy);
    }
    $root.append($sum);

    qq{<?xml version="1.0" encoding="UTF-8"?>\n} ~ xml-pretty($root)
}

#| Write through a temporary file in the same directory, then rename, so an
#| interrupted run cannot leave a half-written report behind.
sub atomic-spurt(IO::Path $path, Str $content) {
    my ($tmp-name, $tmp-handle) = tempfile(:tempdir($path.parent.Str), :prefix('.modinfo-'));
    $tmp-handle.print($content);
    $tmp-handle.close;
    $tmp-name.IO.rename($path);
}

sub cmd-export(Graph $g, Str $format, Str $out --> Str) {
    my %data = report-data($g);
    my %rendered =
        json => report-json(%data),
        yaml => report-yaml(%data),
        xml  => report-xml(%data);

    unless $out {
        die "unknown format: $format (json, yaml or xml)" unless %rendered{$format}:exists;
        return %rendered{$format};
    }

    mktree($out) unless $out.IO.d;
    my @written;
    for (%rendered.keys.sort) -> $f {
        next unless $format eq 'all' || $format eq $f;
        my $path = $out.IO.add("modinfo.$f");
        atomic-spurt($path, %rendered{$f} ~ "\n");
        @written.push('  ' ~ tidy-path($path) ~ '  ('
                      ~ plural(%rendered{$f}.chars, 'byte', 'bytes') ~ ')');
    }
    die "unknown format: $format (json, yaml, xml or all)" unless @written;
    (header('wrote'), |@written).join("\n")
}

# ─────────────────────────────────────────────────────────────────────────────
# Name resolution
# ─────────────────────────────────────────────────────────────────────────────

#| Accept an exact name, the shortest unique abbreviation of one (which is
#| what Abbreviations computes for the whole set), or any unique prefix.
sub resolve(Graph $g, Str $want --> Dist) {
    return $g.dist($want) if $g.dist($want);

    my %abbrev = abbreviations($g.names.list);
    for %abbrev.keys.sort -> $full {
        return $g.dist($full) if %abbrev{$full}.lc eq $want.lc;
    }

    my @hits = $g.names.grep({ .lc.starts-with($want.lc) });
    return $g.dist(@hits[0]) if @hits == 1;

    if @hits > 1 {
        die "'$want' is ambiguous: {@hits.join(', ')}";
    }
    die "no such distribution: $want (try `modinfo list`)";
}

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

#| Defaults, then the config file, then the command line — merged in that
#| order. Hash::Merge does the overlay; Config gives it dotted-path access.
sub load-config(Str $file, %overrides --> Config) {
    my %defaults =
        scan => %( path => 'fixtures/dists', installed => False ),
        report => %( format => 'table', width => 92, color => True ),
        rank => %( top => 20 );

    my %data = %defaults;
    my $path = $file || (<modinfo.yml modinfo.yaml>.first({ .IO.f }) // '');
    if $path && $path.IO.f {
        %data = merge-hash(%data, Config::Parser::YAMLish.read($path.IO));
    }
    %data = merge-hash(%data, %overrides) if %overrides;

    Config.new.read(%data)
}

# ─────────────────────────────────────────────────────────────────────────────

my constant USAGE = q:to/END/;
modinfo — a Raku distribution inspector

  modinfo list                     every distribution found, as a table
  modinfo show <dist>              one distribution in detail
  modinfo deps <dist>              what it depends on, as a tree
  modinfo rdeps <dist>             what depends on it, as a tree
  modinfo graph                    roots, leaves, build order, cycles
  modinfo rank                     rank by number of dependents
  modinfo check [<dist>]           validate META6.json
  modinfo export                   a full report as json, yaml or xml
  modinfo about                    what modinfo itself is built on

Options:
  --path=DIR         scan unpacked distributions under DIR
  --installed        read the Rakudo installation database instead
  --format=FMT       export format: json, yaml, xml or all   (default json)
  --out=DIR          write the export to DIR instead of stdout
  --config=FILE      YAML configuration file  (default ./modinfo.yml)
  --top=N            how many rows `rank` prints                (default 20)
  --width=N          wrap width                                 (default 92)
  --all-phases       count build- and test-depends as edges too
  --no-color         plain output
  --debug            dump the parsed model

<dist> may be given as an exact name, its shortest unique abbreviation, or
any unique prefix: `modinfo show Core` and `modinfo show Corelib` agree.
END

sub MAIN(
    $command = 'list',
    $target?,
    Str  :$path,
    Str  :$format,
    Str  :$out,
    Str  :$config,
    Str  :$top,
    Str  :$width,
    Bool :$installed = False,
    Bool :$all-phases = False,
    Bool :$no-color = False,
    Bool :$debug = False,
    Bool :$help = False,
) {
    if $help || $command eq 'help' {
        print USAGE;
        exit 0;
    }

    my %overrides;
    %overrides<scan><path>      = $path if $path;
    %overrides<scan><installed> = True  if $installed;
    %overrides<report><format>  = $format if $format;
    %overrides<report><width>   = $width.Int if $width;
    %overrides<report><color>   = False if $no-color;
    %overrides<rank><top>       = $top.Int if $top;

    my $cfg = load-config($config // '', %overrides);
    $USE-COLOR = ?$cfg.get('report.color');
    $WIDTH     = $cfg.get('report.width').Int;

    if $command eq 'about' {
        say cmd-about();
        exit 0;
    }

    # Discovery. A relative default path is resolved against this program's
    # own directory, so `modinfo list` works from anywhere.
    my Dist @dists;
    if $cfg.get('scan.installed') {
        @dists = scan-installed();
    }
    else {
        my $dir = $cfg.get('scan.path').Str;
        my $io  = $dir.IO.is-absolute ?? $dir.IO
                                      !! ($dir.IO.d ?? $dir.IO !! $*PROGRAM.parent.add($dir));
        @dists = scan-path($io);
    }

    unless @dists {
        note 'no distributions found — use --path=DIR or --installed';
        exit 2;
    }

    my $g = Graph.build(@dists, all => $all-phases);

    if $debug {
        note Dump(@dists.head, :indent(2));
    }

    given $command {
        when 'list'  { say cmd-list($g) }
        when 'graph' { say cmd-graph($g) }
        when 'rank'  { say cmd-rank($g, $cfg.get('rank.top').Int) }
        when 'show'  {
            die 'show needs a distribution name' unless $target;
            say cmd-show($g, resolve($g, ~$target));
        }
        when 'deps' {
            die 'deps needs a distribution name' unless $target;
            say cmd-deps($g, resolve($g, ~$target));
        }
        when 'rdeps' {
            die 'rdeps needs a distribution name' unless $target;
            say cmd-deps($g, resolve($g, ~$target), :reverse);
        }
        when 'check' {
            my @which = $target ?? [resolve($g, ~$target)] !! @dists;
            my ($text, $errors) = cmd-check($g, @which);
            say $text;
            exit $errors ?? 1 !! 0;
        }
        when 'export' {
            say cmd-export($g, ($format // 'json'), ($out // ''));
        }
        default {
            note "unknown command: $command";
            print USAGE;
            exit 2;
        }
    }
}
