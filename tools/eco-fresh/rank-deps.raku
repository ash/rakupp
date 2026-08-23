#!/usr/bin/env rakupp
# rank-deps.raku — count reverse dependencies over the REA index: how many
# dists (latest version of each) name a module in depends / build-depends /
# test-depends. The ranked names are what a sweep store gets seeded with
# (`rakupp install --no-test`), so a later `rakupp test X` finds every common
# dependency already installed and spends its whole budget on X's own suite.
#
#   rakupp rank-deps.raku ~/.raku/rakupp-install/rea-meta.json --min=2 > seed.tsv
#
# Output: name TAB count, most-depended-on first. Same line-wise read as
# pick-fresh.raku: the index is one dist-object per line, and only the latest
# release of each dist gets its deps counted — a 2015 release's dependency
# list is history, not load.

sub field($line, $key) {
    $line ~~ /'"' $key '":"' (<-["]>+) '"'/ ?? ~$0 !! ''
}

# The engine's own JSON parser, same spelling as tools/install.raku: the
# native Rakupp::Internals::JSON when this is rakupp, Rakudo's otherwise.
my $have-own-json = ?(try { ::('Rakupp::Internals::JSON').from-json('1') === 1 });
sub json-decode(Str $text) {
    $have-own-json
        ?? ::('Rakupp::Internals::JSON').from-json($text)
        !! Rakudo::Internals::JSON.from-json($text)
}

# A depends value is an array of strings or hashes, or a phase hash
# ({runtime => [...], test => [...]}) — a fifth of the ecosystem writes the
# latter. An entry hash names its module under <name>; a string may carry
# adverbs (JSON::Fast:ver<0.17+>) and only the part before the colon is the
# name.
sub count-deps($v, %count) {
    return unless $v.defined;
    if $v ~~ Positional {
        count-deps($_, %count) for @$v;
    }
    elsif $v ~~ Associative {
        if $v<name>.defined {
            count-one(~$v<name>, %count);
        }
        else {
            for <runtime build test> -> $phase {
                count-deps($v{$phase}, %count) if $v{$phase}.defined;
            }
        }
    }
    elsif $v ~~ Str {
        count-one($v, %count);
    }
}

sub count-one(Str $s, %count) {
    # "JSON::Fast:ver<0.17+>:auth<zef:timo>" — the name is everything before
    # the first SINGLE colon; a bare split on ':' would eat the '::'. And a
    # dep that is not a Raku dist at all (ssl:from<native>, Foo:from<Perl5>)
    # is nothing a store can be seeded with.
    return if $s ~~ /':from<' <-[>]>+ '>'/ && $s !~~ /':from<raku>'/;
    my $name = $s ~~ /^ ( [ <-[:\s]>+ | '::' ]+ ) / ?? ~$0 !! '';
    return unless $name.chars;
    return if $name eq 'Rakudo' || $name eq 'perl6' || $name eq 'nqp';
    %count{$name}++;
}

sub MAIN($rea, Int :$min = 2) {
    # pass 1: the latest line of each dist, kept as TEXT — json-decode runs
    # only over the ~2.5k survivors, not all 15k dist-versions
    my %latest;
    for $rea.IO.lines -> $line {
        next unless $line.starts-with('{');
        my $date = field($line, 'release-date');
        my $dist = field($line, 'dist');
        my $name = $dist ~~ /^ (.+?) ':ver<'/ ?? ~$0 !! field($line, 'name');
        next unless $name && $date;
        my $have = %latest{$name};
        if !$have.defined || $date gt $have<date> {
            %latest{$name} = { :$date, :$line };
        }
    }
    note "{%latest.elems} dists at latest version";

    # pass 2: decode those lines and count who gets named
    my %count;
    for %latest.values -> %e {
        # the index is a JSON array printed one object per line, so every
        # line but the last drags the array's comma along
        my %m = try json-decode(%e<line>.trim.subst(/ ',' $ /, ''));
        next unless %m;
        count-deps(%m{$_}, %count) for <depends build-depends test-depends>;
    }
    my @ranked = %count.pairs.grep(*.value >= $min)
                             .sort({ $^b.value <=> $^a.value || $^a.key leg $^b.key });
    note "{+@ranked} modules named by at least $min dists";
    say "{.key}\t{.value}" for @ranked;
}
