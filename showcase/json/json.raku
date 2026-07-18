#!/usr/bin/env raku
# A JSON parser and formatter. A Raku grammar (plus an actions class) turns JSON
# text into native Raku values — objects become Hashes, arrays Arrays, scalars
# Str/Int/Num/Bool — and a serializer turns them back into JSON, pretty-printed
# or minified. A small `jq`-style path can pull one value out. Where the Lisp
# and Forth showcases implement whole languages, this one is the everyday job a
# grammar is for: read a data format, do something, write it back.
#
#   build/rakupp showcase/json/json.raku showcase/json/sample.json      # pretty-print
#   build/rakupp showcase/json/json.raku --compact sample.json          # minify
#   build/rakupp showcase/json/json.raku --query='.users[0].name' sample.json
#   cat data.json | build/rakupp showcase/json/json.raku                # from stdin
#
# Object keys are emitted sorted, so the output is stable (handy for diffs).

# ---------- grammar -----------------------------------------------------
grammar JSON {
    rule  TOP               { <value> }
    proto rule value        {*}
    rule  value:sym<object> { '{' <pairs> '}' }
    rule  value:sym<array>  { '[' <items> ']' }
    rule  pairs             { <pair>* % ',' }
    rule  items             { <value>* % ',' }
    rule  pair              { <string> ':' <value> }
    token value:sym<string> { <string> }
    token value:sym<number> { '-'? \d+ [ '.' \d+ ]? [ <[eE]> <[+-]>? \d+ ]? }
    token value:sym<true>   { 'true' }
    token value:sym<false>  { 'false' }
    token value:sym<null>   { 'null' }
    token string            { '"' $<chars>=[ <-["\\]> | '\\' . ]* '"' }
}

# JSON null needs to be distinct from "key missing"; use a unique sentinel.
class JsonNull { method gist { 'null' } }
my \NULL = JsonNull.new;

class Actions {
    method TOP($/)               { make $<value>.made }
    method value:sym<object>($/) { make $<pairs><pair>.map({ .made }).hash }
    method value:sym<array>($/)  { make $<items><value>.map({ .made }).Array }
    method value:sym<string>($/) { make $<string>.made }
    method value:sym<number>($/) {
        my $s = ~$/;
        make ($s.contains('.') || $s.lc.contains('e')) ?? $s.Num !! $s.Int;
    }
    method value:sym<true>($/)   { make True }
    method value:sym<false>($/)  { make False }
    method value:sym<null>($/)   { make NULL }
    method pair($/)              { make $<string>.made => $<value>.made }
    method string($/)           { make decode-escapes(~$<chars>) }
}

# A single left-to-right pass: sequential global substs would corrupt data (an
# escaped backslash \\ followed by a letter looks like another escape).
sub decode-escapes(Str $s --> Str) {
    my @c = $s.comb;
    my $out = '';
    my $i = 0;
    while $i < @c.elems {
        if @c[$i] eq '\\' && $i + 1 < @c.elems {
            given @c[$i + 1] {
                when '"'  { $out ~= '"';  $i += 2 }
                when '\\' { $out ~= '\\'; $i += 2 }
                when '/'  { $out ~= '/';  $i += 2 }
                when 'n'  { $out ~= "\n"; $i += 2 }
                when 't'  { $out ~= "\t"; $i += 2 }
                when 'r'  { $out ~= "\r"; $i += 2 }
                when 'b'  { $out ~= "\b"; $i += 2 }
                when 'f'  { $out ~= "\f"; $i += 2 }
                when 'u'  { $out ~= chr(:16(@c[$i + 2 .. $i + 5].join)); $i += 6 }
                default   { $out ~= @c[$i + 1]; $i += 2 }
            }
        }
        else { $out ~= @c[$i]; $i++ }
    }
    $out;
}

sub parse-json(Str $src) {
    my $m = JSON.parse($src.trim, actions => Actions.new);   # tolerate leading/trailing whitespace
    die "invalid JSON" unless $m;
    $m.made;
}

# ---------- serializer --------------------------------------------------
sub esc-string(Str $s --> Str) {
    my $out = $s.subst('\\', '\\\\', :g).subst('"', '\\"', :g)
               .subst("\n", '\\n', :g).subst("\t", '\\t', :g).subst("\r", '\\r', :g);
    '"' ~ $out ~ '"';
}

sub num-str($n --> Str) {
    return ~$n if $n ~~ Int;
    my $s = $n.Str;                       # a whole-valued Num prints without a dot
    $s.contains('.') || $s.lc.contains('e') ?? $s !! $s;
}

sub to-json($v, Bool :$pretty = True, Int :$level = 0 --> Str) {
    my $nl   = $pretty ?? "\n" !! '';
    my $pad  = $pretty ?? '  ' x ($level + 1) !! '';
    my $end  = $pretty ?? '  ' x $level !! '';
    my $csep = $pretty ?? ': ' !! ':';

    given $v {
        when JsonNull { 'null' }
        when Bool     { $v ?? 'true' !! 'false' }
        when Int | Num { num-str($v) }
        when Str      { esc-string($v) }
        when Positional {
            return '[]' unless $v.elems;
            my @items = $v.map({ $pad ~ to-json($_, :$pretty, :level($level + 1)) });
            '[' ~ $nl ~ @items.join(',' ~ $nl) ~ $nl ~ $end ~ ']';
        }
        when Associative {
            return '{}' unless $v.elems;
            my @items = $v.keys.sort.map(-> $k {
                $pad ~ esc-string($k) ~ $csep ~ to-json($v{$k}, :$pretty, :level($level + 1))
            });
            '{' ~ $nl ~ @items.join(',' ~ $nl) ~ $nl ~ $end ~ '}';
        }
        when !.defined { 'null' }
        default       { esc-string(~$v) }
    }
}

# ---------- jq-lite query ----------------------------------------------
# A path is a run of `.key` and `[index]` steps, e.g. .users[0].name
sub run-query($data is copy, Str $path) {
    my @steps = $path.comb(/ '.' <-[.\[]>+ | '[' \d+ ']' /);
    for @steps -> $step {
        if $step.starts-with('[') {
            my $i = ($step ~~ / (\d+) /) ?? (~$0).Int !! 0;   # digits between the brackets
            die "not an array at [$i]" unless $data ~~ Positional;
            $data = $data[$i];
        }
        else {
            my $key = $step.substr(1);
            die "no key '$key'" unless $data ~~ Associative && ($data{$key}:exists);
            $data = $data{$key};
        }
    }
    $data;
}

# ---------- driver ------------------------------------------------------
sub MAIN($file?, Bool :$compact = False, Str :$query) {
    my $src  = $file.defined ?? $file.IO.slurp !! $*IN.slurp;
    my $data = parse-json($src);
    $data = run-query($data, $query) if $query.defined;
    say to-json($data, :pretty(!$compact));
}
