#!/usr/bin/env rakupp
# Generates src/raku_docs_index_gen.cpp — the docs.raku.org index `--doc-links`
# resolves names against.
#
#   rakupp tools/gen-raku-docs-index.raku searchData.json > src/raku_docs_index_gen.cpp
#   rakupp tools/gen-raku-docs-index.raku --fetch       > src/raku_docs_index_gen.cpp
#
# The input is the search index the docs site itself loads,
# https://docs.raku.org/assets/searchData.json: one entry per page, per
# composite routine/syntax page, per heading and per X<> index entry, each
# with the URL the site links it at. That URL is the point. A section anchor
# cannot be derived from a name — on the Str page `uc` is `#routine_uc` and
# `contains` is `#method_contains`, and an operator heading that names two
# spellings is anchored on both (`#infix_!=,_infix_≠`) — so a link that was
# not copied from the site is a link that may point at the top of the page.
#
# --fetch downloads the index with curl; nothing else here touches the network,
# and nothing at all does at run time: the output is committed, and
# `rakupp --doc-links` answers from it offline. Rerun this when the docs site
# gains or renames pages; the diff of the generated file is the list of what
# moved, one row per line.
#
# What is kept (a fifth of the 1.4 MB input):
#   T  a type page            key: the type's name             info: its summary
#   M  a routine on a type    key: "Type name"                 info: method / routine / sub / trait …
#   R  a composite routine    key: the routine's name          info: the types that define it
#   S  a composite syntax     key: the entry's name            info: the pages it gathers
#   O  an operator heading    key: "infix +", "prefix -" …     (one row per spelling a heading names)
#   H  a language heading     key: "page:Heading"              (from the pages listed in @LANG-PAGES)
#   L  a language page        key: its slug                    info: its title
# Headings of the Perl/Python/Ruby migration guides, the glossary, the FAQ and
# the traps page are left out: they explain other languages, or explain Raku
# by contrast, and are never the page a construct is documented on.

use Data::Native <json>;

my @LANG-PAGES = <
    operators variables syntax control functions signatures objects classtut
    quoting regexes grammars phasers statement-prefixes typesystem traits terms
    subscripts containers concurrency exceptions packages hashmap list contexts
    setbagmix numerics pod iterating structures nativetypes
>;
my %LANG-PAGE = @LANG-PAGES.map({ "/language/$_" => True });

sub MAIN(Str $input?, Bool :$fetch, Str :$date = Date.today.Str) {
    my $json;
    if $fetch {
        my $p = run 'curl', '-sSfL', 'https://docs.raku.org/assets/searchData.json', :out;
        $json = $p.out.slurp(:close);
        die "curl failed" unless $p.exitcode == 0 && $json;
    }
    elsif $input {
        $json = $input.IO.slurp;
    }
    else {
        note "usage: rakupp tools/gen-raku-docs-index.raku (searchData.json | --fetch) [--date=YYYY-MM-DD]";
        exit 2;
    }
    my @entries = |from-json($json);

    my %seen;
    my @rows;
    sub row(Str $kind, Str $key, Str $url, Str $info = '') {
        return if %seen{"$kind\t$key\t$url"}++;
        @rows.push: [$kind, $key, $url, $info];
    }
    # "type/IO/Path" -> "IO::Path"; a language page keeps its path
    sub source-name(Str $s) {
        $s.starts-with('type/') ?? $s.substr(5).subst('/', '::', :g) !! $s
    }
    sub plain(Str $s) { $s.subst(/'<' <-[>]>* '>'/, '', :g).trim }

    for @entries -> %e {
        my $cat   = %e<category> // '';
        my $value = %e<value> // '';
        my $url   = %e<url> // '';
        my $info  = plain(%e<info> // '');
        next unless $url.starts-with('/');

        if $cat eq 'Class' | 'Role' | 'Enum' | 'Module' | 'Package' {
            # "class Str", "role Cool", "enum Bool", "Subset UInt"
            my $name = $value.words.tail // next;
            row 'T', $name, $url, $info;
        }
        elsif $cat eq 'Language' {
            my $slug = $url.substr('/language/'.chars);
            row 'L', $slug, $url, $value if $url.starts-with('/language/');
        }
        elsif $cat eq 'Composite' {
            my @from = ($info ~~ /^ 'From:' \s* (.*) $/ ?? ~$0 !! '').split(/\s*','\s*/, :skip-empty).map(&source-name);
            if $url.starts-with('/routine/') { row 'R', $value, $url, @from.unique.join(' ') }
            elsif $url.starts-with('/syntax/') { row 'S', $value, $url, @from.unique.join(' ') }
        }
        elsif $cat eq 'Heading' {
            my ($page, $anchor) = $url.split('#', 2);
            next unless $anchor;
            if $page.starts-with('/type/') {
                # "method push" on /type/Array#method_push; a heading that is not
                # a routine ("Methods", "Operators") is not a lookup target
                if $value ~~ /^ (method|routine|sub|submethod|trait|infix|prefix|postfix|circumfix|postcircumfix|term|attribute) \s+ (.+) $/ {
                    my $type = source-name($page.substr(1));
                    row 'M', "$type $1", $url, ~$0;
                }
            }
            elsif %LANG-PAGE{$page} {
                my $slug = $page.substr('/language/'.chars);
                if $slug eq 'operators' && $value ~~ /^ (infix|prefix|postfix|circumfix|postcircumfix|methodop|listop|term) \s/ {
                    # "infix !=, infix ≠" names two spellings on one heading,
                    # and so does "methodop ». / methodop >>."
                    for $value.split(/[',' | \s '/'] \s+ <?before [infix|prefix|postfix|circumfix|postcircumfix|methodop|listop|term] \s>/) -> $one {
                        row 'O', $one.trim, $url;
                    }
                }
                else {
                    row 'H', "$slug:$value", $url;
                }
            }
        }
    }

    @rows .= sort({ .[0], .[1], .[2] });
    my %count; %count{.[0]}++ for @rows;

    say q:to/HEAD/.chomp;
    // GENERATED by tools/gen-raku-docs-index.raku — do not edit; rerun the tool.
    HEAD
    say "// Source: https://docs.raku.org/assets/searchData.json, $date ({+@entries} entries; {+@rows} kept:";
    say "// " ~ <T M R S O H L>.map({ "$_ {%count{$_} // 0}" }).join(', ') ~ ').';
    say q:to/BODY/.chomp;
    //
    // The index `rakupp --doc-links` resolves names against: one row per page,
    // routine, operator or heading a construct can be documented at, with the
    // URL docs.raku.org itself links it at (relative to the site). See the tool
    // for what each kind holds and what was left out.
    #include "DocLinks.h"

    namespace rakupp {
    namespace doclinks {

    const IndexRow kIndex[] = {
    BODY
    for @rows -> [$k, $key, $url, $info] {
        say "    \{'$k', {cstr($key)}, {cstr($url)}, {cstr($info)}},";
    }
    say "};";
    say "const size_t kIndexSize = sizeof kIndex / sizeof kIndex[0];";
    say "const char kIndexDate[] = \"$date\";";
    say "";
    say "} // namespace doclinks";
    say "} // namespace rakupp";
}

# A C++ string literal. `?` is escaped so no `??x` can read as a trigraph to a
# compiler old enough to have them; the text is otherwise UTF-8 as written
# (MSVC builds with /utf-8).
sub cstr(Str $s) {
    '"' ~ $s.trans(['\\', '"', '?', "\n", "\t"] => ['\\\\', '\\"', '\\?', '\\n', '\\t']) ~ '"'
}
