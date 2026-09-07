#!/usr/bin/env raku
# `rakupp doc SYMBOL ...` — offline lookup of a builtin, method, operator or
# syntax form from the terminal (go doc, perldoc -f, pydoc). A Raku program
# shipped beside the binary and dispatched by it, like install.raku.
#
# The content is guide/REFERENCE.md — every entry there was executed on rakupp
# and shows its real output — with FEATURES.md as the second source. Nothing is
# indexed ahead of time: the files are small, and a scan per lookup keeps the
# tool honest about what the docs say TODAY.
#
# Where the docs are: a checkout keeps them in docs/guide beside tools/; an
# installed layout puts this script in libexec/rakupp and the two files in
# share/rakupp/docs. RAKUPP_DOCS=DIR overrides both.

sub docs-dir() {
    return %*ENV<RAKUPP_DOCS>.IO if %*ENV<RAKUPP_DOCS>;
    my $here = $*PROGRAM.IO.resolve.parent;
    for $here.parent.add('docs/guide'), $here.parent.parent.add('share/rakupp/docs') -> $d {
        return $d if $d.add('REFERENCE.md').e;
    }
    return IO::Path;
}

# One hit: where it is (heading trail), and the line itself.
class Hit { has $.file; has @.trail; has $.line; has $.kind }

# Scan one markdown file for lines that mention the symbol: table rows whose
# FIRST cell names it (those carry the meaning column), lines of code blocks,
# and prose lines with it in inline code. Word-bounded for identifiers; a
# symbol made of punctuation (`»`, `<=>`, `Z`) matches as a substring.
sub scan(IO::Path $f, Str $sym) {
    my @hits;
    my @trail;            # the current ## / ### heading path
    my $in-code = False;
    my $wordy = so $sym ~~ /^ <[\w\-]>+ $/;
    my $re = $wordy ?? rx/ <|w> $sym <|w> / !! rx/ $sym /;
    for $f.lines.kv -> $n, $l {
        if $l ~~ /^ '```' / { $in-code = !$in-code; next }
        if !$in-code && $l ~~ /^ ('#'+) \s+ (.*) $/ {
            my $level = $0.chars;
            next if $level == 1;                 # the file title is not a location
            @trail = @trail[^($level - 2)];
            @trail[$level - 2] = ~$1;
            next;
        }
        next unless $l ~~ $re;
        my $kind;
        if $l ~~ /^ \s* '|' / {
            # a table row: the symbol must be in the first cell to count as
            # an entry (a mention in the "example" column is not one)
            my $first = $l.split('|')[1] // '';
            next unless $first ~~ $re;
            next if $first ~~ /^ \s* '-'+ \s* $/;   # the header separator
            $kind = 'table';
        }
        elsif $in-code { $kind = 'code' }
        elsif $l ~~ / '`' <-[`]>* $sym <-[`]>* '`' / { $kind = 'prose' }
        else { next }
        @hits.push: Hit.new(:file($f.basename), :trail(@trail.grep(*.defined).Array), :line($l.trim), :$kind);
    }
    @hits
}

sub show(@hits, Int $max) {
    my $last = '';
    my $shown = 0;
    for @hits -> $h {
        last if $shown >= $max;
        my $where = $h.file ~ ' › ' ~ $h.trail.join(' › ');
        if $where ne $last { say ($shown ?? "\n" !! '') ~ $where; $last = $where }
        say '    ' ~ $h.line;
        $shown++;
    }
    if @hits > $max { say "\n… {@hits - $max} more (--all shows every hit)" }
}

# `rakupp doc :i` used to print the usage instead of the entry. A Raku adverb is
# exactly the kind of thing a reader reaches for `doc` to explain, but MAIN's
# argument parser reads a leading colon as a NAMED argument, finds no `:i`
# parameter, and gives up on the whole dispatch. Protecting the token here and
# unwrapping it in MAIN costs two lines and keeps every other spelling as it was.
# (A user-defined `ARGS-TO-CAPTURE` would be the tidy way; Rakudo honours one and
# Raku++ ignores it, so this survives both.)
constant ADVERB = "\0adverb\0";
@*ARGS = @*ARGS.map({ .starts-with(':') ?? ADVERB ~ $_ !! $_ });

#| Look a Raku symbol up in the language reference: a builtin, a method, an operator, a syntax form
sub MAIN(
    *@symbols,          #= what to look up: trim, .comb, Z, <=>, gather, MAIN …
    Bool :$all,         #= every hit, not the first dozen
    Bool :$code,        #= code examples only (skip the tables and the prose)
) {
    unless @symbols { note "Usage: rakupp doc SYMBOL ...   (e.g. rakupp doc trim, rakupp doc '<=>', rakupp doc gather)"; exit 2 }
    my $dir = docs-dir();
    unless $dir.defined {
        note "rakupp doc: cannot find REFERENCE.md (looked beside this checkout and in share/rakupp/docs; RAKUPP_DOCS=DIR overrides)";
        exit 1;
    }
    my $missing = 0;
    for @symbols.map(*.subst(/^ $(ADVERB) /, '')) -> $raw {
        my $sym = $raw.subst(/^ '.' /, '');          # `.trim` is the method spelling of `trim`
        my @hits;
        for 'REFERENCE.md', 'FEATURES.md' -> $name {
            my $f = $dir.add($name);
            next unless $f.e;
            @hits.append: scan($f, $sym);
        }
        @hits .= grep(*.kind eq 'code') if $code;
        # tables first (they carry the meaning), then code, then prose —
        # REFERENCE before FEATURES within each
        my %rank = table => 0, code => 1, prose => 2;
        @hits = @hits.sort({ %rank{.kind}, .file ne 'REFERENCE.md' });
        if !@hits {
            say "$sym: nothing in REFERENCE.md or FEATURES.md";
            $missing++;
            next;
        }
        say "== $sym  ({@hits.elems} hit{@hits == 1 ?? '' !! 's'})" if @symbols > 1;
        show(@hits, $all ?? @hits.elems !! 12);
        say '' if @symbols > 1;
    }
    exit($missing == @symbols ?? 1 !! 0);
}
