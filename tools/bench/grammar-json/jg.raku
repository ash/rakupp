# A full-scale JSON parser written as a pure Raku grammar — the workload for
# the grammar-speed campaign. NOT a replacement for Rakupp::JSON (the native
# codec stays the production path): this is the thing we PROFILE, so it is
# written the way a Raku programmer would write it — proto tokens for the
# value dispatch, actions building real Raku values — not micro-tuned.
#
# Full RFC 8259: every escape (\" \\ \/ \b \f \n \r \t \uXXXX), surrogate
# pairs, strict numbers (no leading zeros, no leading +), control characters
# rejected unescaped, objects/arrays/nesting unlimited.
#
# Runs unchanged under both engines — the comparison IS the measurement.
#
#   rakupp jg.raku check              # correctness self-test (33 cases)
#   rakupp jg.raku parse FILE [reps]  # time reps (default 3), report ms/parse
#   rakupp jg.raku canon FILE         # canonical serialization (cross-engine diff)

grammar JSONGrammar {
    token TOP { \s* <value> \s* }

    proto token value {*}
    token value:sym<object> { '{' \s* [ <pair>* % [ \s* ',' \s* ] ] \s* '}' }
    token value:sym<array>  { '[' \s* [ <value>* % [ \s* ',' \s* ] ] \s* ']' }
    token value:sym<string> { <string> }
    token value:sym<number> { <number> }
    token value:sym<true>   { 'true' }
    token value:sym<false>  { 'false' }
    token value:sym<null>   { 'null' }

    token pair { <string> \s* ':' \s* <value> }

    token string     { \x22 <str-part>* \x22 }
    token str-part   { <str-chars> | <str-escape> }
    token str-chars  { <-[ \x22 \\ \x00..\x1F ]>+ }
    token str-escape { '\\' [ <str-esc-char> | 'u' <str-esc-hex> ] }
    token str-esc-char { <[ \x22 \\ / b f n r t ]> }
    token str-esc-hex  { <[ 0..9 a..f A..F ]> ** 4 }

    token number { '-'? [ '0' | <[1..9]> \d* ] [ '.' \d+ ]? [ <[eE]> <[+-]>? \d+ ]? }
}

class JSONActions {
    method TOP($/) { make $<value>.made }

    method value:sym<object>($/) {
        my %h;
        %h{.made.key} = .made.value for $<pair>;
        make %h;
    }
    method value:sym<array>($/)  { make [ $<value>.map(*.made) ] }
    method value:sym<string>($/) { make $<string>.made }
    method value:sym<number>($/) { make $<number>.made }
    method value:sym<true>($/)   { make True }
    method value:sym<false>($/)  { make False }
    method value:sym<null>($/)   { make Any }

    method pair($/) { make $<string>.made => $<value>.made }

    my %esc = '"' => '"', '\\' => '\\', '/' => '/',
              'b' => "\b", 'f' => "\f", 'n' => "\n", 'r' => "\r", 't' => "\t";

    method string($/) {
        my $out = '';
        my $pending = -1;   # a decoded high surrogate waiting for its low half
        for $<str-part> -> $p {
            with $p<str-chars> {
                if $pending >= 0 { $out ~= chr(0xFFFD); $pending = -1 }
                $out ~= ~$_;
            }
            else {
                my $e = $p<str-escape>;
                with $e<str-esc-char> {
                    if $pending >= 0 { $out ~= chr(0xFFFD); $pending = -1 }
                    $out ~= %esc{~$_};
                }
                else {
                    my $cp = (~$e<str-esc-hex>).parse-base(16);
                    if $pending >= 0 {
                        if 0xDC00 <= $cp <= 0xDFFF {
                            $out ~= chr(0x10000 + (($pending - 0xD800) +< 10) + ($cp - 0xDC00));
                            $pending = -1;
                        }
                        else {
                            $out ~= chr(0xFFFD);
                            $pending = 0xD800 <= $cp <= 0xDBFF ?? $cp !! do { $out ~= chr($cp); -1 };
                        }
                    }
                    elsif 0xD800 <= $cp <= 0xDBFF { $pending = $cp }
                    elsif 0xDC00 <= $cp <= 0xDFFF { $out ~= chr(0xFFFD) }  # lone low surrogate
                    else { $out ~= chr($cp) }
                }
            }
        }
        if $pending >= 0 { $out ~= chr(0xFFFD) }
        make $out;
    }

    method number($/) {
        my $t = ~$/;
        if $t.contains('e') || $t.contains('E') { make $t.Num }
        elsif $t.contains('.')                  { make $t.Rat }
        else                                    { make $t.Int }
    }
}

# Canonical serialization for cross-engine byte comparison: sorted keys,
# minimal escaping, integers as-is, Rat via exact decimal, Num via %.15g.
sub canon($v) {
    given $v {
        when Hash  { '{' ~ $v.keys.sort.map({ canon-str($_) ~ ':' ~ canon($v{$_}) }).join(',') ~ '}' }
        when Array { '[' ~ $v.map({ canon($_) }).join(',') ~ ']' }
        when Bool  { $v ?? 'true' !! 'false' }
        when Str   { canon-str($v) }
        when Int   { ~$v }
        when Rat   { rat-dec($v) }   # NOT $v.base(10): unimplemented in rakupp (campaign finding #1)
        when Num   { sprintf '%.15g', $v }
        default    { 'null' }
    }
}
# Exact decimal for a Rat whose denominator divides a power of ten (always
# true for JSON-parsed decimals). Pure arithmetic — runs on both engines.
sub rat-dec($r) {
    my $neg = $r < 0;
    my $x = $r.abs;
    my $k = 0;
    while $x != $x.floor and $k < 40 {
        $x = $x * 10;
        $k++;
    }
    my $s = ~$x.floor;
    $s = '0' x ($k + 1 - $s.chars) ~ $s if $s.chars <= $k;
    my $out = $k == 0 ?? $s !! $s.substr(0, $s.chars - $k) ~ '.' ~ $s.substr($s.chars - $k);
    ($neg ?? '-' !! '') ~ $out
}

sub canon-str($s) {
    my $out = '"';
    for $s.comb -> $c {
        my $o = $c.ord;
        if    $c eq '"'  { $out ~= '\\"' }
        elsif $c eq '\\' { $out ~= '\\\\' }
        elsif $o < 0x20  {
            $out ~= $o == 0x08 ?? '\\b' !! $o == 0x09 ?? '\\t' !! $o == 0x0A ?? '\\n'
                 !! $o == 0x0C ?? '\\f' !! $o == 0x0D ?? '\\r' !! sprintf('\\u%04x', $o);
        }
        else { $out ~= $c }
    }
    $out ~ '"'
}

sub jg-parse(Str $text, Bool :$raw = False) {
    my $m = $raw ?? JSONGrammar.parse($text)
                 !! JSONGrammar.parse($text, :actions(JSONActions.new));
    $m ?? ($raw ?? $m !! $m.made, True) !! (Any, False)
}

my @self-tests =
    Q<{}>                     => Q<{}>,
    Q<[]>                     => Q<[]>,
    Q<null>                   => Q<null>,
    Q<true>                   => Q<true>,
    Q<false>                  => Q<false>,
    Q<0>                      => Q<0>,
    Q<-1>                     => Q<-1>,
    Q<42>                     => Q<42>,
    Q<3.25>                   => Q<3.25>,
    Q<-0.5>                   => Q<-0.5>,
    Q<1e3>                    => Q<1000>,
    Q<1.5E-2>                 => Q<0.015>,
    Q<"hi">                   => Q<"hi">,
    Q<"a\"b">                 => Q<"a\"b">,
    Q<"a\\b">                 => Q<"a\\b">,
    Q<"a\/b">                 => Q<"a/b">,
    Q<"tab\there">            => Q["tab\there"],
    Q<"nl\nend">              => Q["nl\nend"],
    Q<"A">               => Q<"A">,
    Q<"é">               => Q<"é">,
    Q<"中文">         => Q<"中文">,
    Q<"😀">         => Q<"😀">,
    Q<[1,2,3]>                => Q<[1,2,3]>,
    Q<[ 1 , 2 ]>              => Q<[1,2]>,
    Q<{"a":1,"b":[true,null]}>  => Q<{"a":1,"b":[true,null]}>,
    Q<{"b":2,"a":1}>          => Q<{"a":1,"b":2}>,
    Q<[[[[[1]]]]]>            => Q<[[[[[1]]]]]>,
    Q<{"x":{"y":{"z":[]}}}>   => Q<{"x":{"y":{"z":[]}}}>,
    ;
my @must-fail = Q<01>, Q<+1>, Q<1.>, Q<.5>, Q<{a:1}>, Q<[1,]>, Q<"un
terminated">, Q<["ctrl	raw"]x>, Q<[1 2]>, Q<{"a"}>;

# --raw: parse WITHOUT the actions class — the timing gap vs a normal parse
# is the action-invocation + action-body share (the Match tree still builds).
sub MAIN(Str $mode = 'check', Str $file?, Int $reps = 3, Bool :$raw = False) {
    if $mode eq 'check' {
        my $fails = 0;
        for @self-tests -> $t {
            my ($v, $ok) = jg-parse($t.key);
            my $got = $ok ?? canon($v) !! 'PARSE-FAIL';
            if $got ne $t.value {
                say "not ok - {$t.key.substr(0, 40)} → $got (expected {$t.value})";
                $fails++;
            }
        }
        for @must-fail -> $bad {
            my ($v, $ok) = jg-parse($bad);
            if $ok {
                say "not ok - accepted invalid: {$bad.substr(0, 40)}";
                $fails++;
            }
        }
        say $fails == 0
            ?? "PASS — {+@self-tests} good + {+@must-fail} bad cases"
            !! "FAIL — $fails case(s)";
        exit($fails ?? 1 !! 0);
    }
    my $text = $file.IO.slurp;
    if $mode eq 'parse' {
        my @t;
        my $v;
        for ^$reps {
            my $t0 = now;
            my ($got, $ok) = jg-parse($text, :$raw);
            die "parse failed on $file" unless $ok;
            @t.push(((now - $t0) * 1000).Int);
            $v = $got;
        }
        say "$file\t{$text.chars} chars\t{@t.min} ms{$raw ?? ' (raw)' !! ''}  (runs: {@t.join(',')})";
    }
    elsif $mode eq 'canon' {
        my ($v, $ok) = jg-parse($text);
        die "parse failed on $file" unless $ok;
        say canon($v);
    }
}
