=begin pod

=head1 JSON::Native::Raku

The pure-Raku half of JSON::Native: a JSON parser and serialiser with no
dependencies, which JSON::Native loads only when its native paths stand aside.
It can also be used on its own, by full name — it exports nothing, so that
loading it at run time can never put its subs over a program's own:

    need JSON::Native::Raku;

    my $data = JSON::Native::Raku::from-json('{"a": [1, 2.5, true, null]}');
    say JSON::Native::Raku::to-json($data, :!pretty);

Its contract is JSON::Fast's, exactly — not "parses JSON and writes JSON" but
"returns what JSON::Fast returns and writes what JSON::Fast writes", down to
the byte, because programs already depend on that text. The behaviour below is
a port of JSON::Fast 0.21.0, written in plain Raku (no C<nqp::>) so that every
engine runs the same code:

=item C<from-json(Str() $text, :$immutable, :$allow-jsonc)> — integer token →
C<Int>, decimal → C<Rat>, exponent form → C<Num> (each the token's own
C<.Numeric>), C<true>/C<false> → C<Bool>, C<null> → C<Any>, objects and arrays
→ C<Hash>/C<Array>, or C<Map>/C<List> under C<:immutable>. Content after the
document raises C<X::JSON::Native::AdditionalContent>.

=item C<to-json(\obj, :$pretty = True, :$spacing = 2, :$sorted-keys,
:$enums-as-value, :$level = 0)> — NaN and Inf are C<null> unless
C<$*JSON_NAN_INF_SUPPORT> is true.

=end pod

# X::JSON::Native::AdditionalContent is declared OUTSIDE the module, as
# JSON::Fast declares its X::JSON::AdditionalContent: an exception class wants
# its X:: name, not JSON::Native::Raku::X::…. It is NOT the same class as
# JSON::Fast's, and could not be — two compilation units declaring one global
# class is a redeclaration error the moment a program loads both.
class X::JSON::Native::AdditionalContent is Exception {
    has $.parsed;
    has $.parsed-length;
    has $.rest-position;

    method message {
        "JSON Input contained additional text after the document (parsed $.parsed-length chars, next non-whitespace lives at $.rest-position)"
    }
}

module JSON::Native::Raku {

    # ---- to-json ------------------------------------------------------------

    my sub to-surrogate-pair(Int $ord) {
        my int $base   = $ord - 0x10000;
        my int $top    = ($base +& 0b1_1111_1111_1100_0000_0000) +> 10;
        my int $bottom = $base +& 0b11_1111_1111;
        Q/\u/ ~ (0xD800 + $top).base(16) ~ Q/\u/ ~ (0xDC00 + $bottom).base(16)
    }

    # JSON::Fast escapes the string's NFD codepoints, not its graphemes, and
    # that is observable: an astral character whose decomposition differs
    # (U+1D15E is U+1D157 U+1D165) becomes two surrogate pairs, and a quote
    # carrying a combining mark — one grapheme — still has its quote escaped.
    # So the slow path walks .NFD exactly as JSON::Fast does. The fast path is
    # the common case made cheap, and it is only taken where the two cannot
    # differ: every grapheme is one codepoint (chars == codes) and none of
    # them is a control, a quote, a backslash or astral.
    my sub str-escape(Str $text) {
        return $text
          if $text.chars == $text.codes
          && !($text ~~ / <[\x[0]..\x[1F] \x[22] \x[5C] \x[10000]..\x[10FFFF]]> /);

        my int @out;
        for $text.NFD -> int $code {
            if $code <= 31 {
                if    $code == 10 { @out.append: 92, 110 }   # \n
                elsif $code == 13 { @out.append: 92, 114 }   # \r
                elsif $code ==  9 { @out.append: 92, 116 }   # \t
                else              { @out.append: |$code.fmt(Q/\u%04x/).ords }
            }
            elsif $code == 34      { @out.append: 92, 34 }   # \"
            elsif $code == 92      { @out.append: 92, 92 }   # \\
            elsif $code >= 0x10000 { @out.append: |to-surrogate-pair($code).ords }
            else                   { @out.push: $code }
        }
        # .NFC.Str: the text rebuilt from codepoints must come back composed,
        # as JSON::Fast's strfromcodes does, and mutsu's .chrs does not
        @out.chrs.NFC.Str
    }

    our sub to-json(
      \obj,
      Bool :$pretty         = True,
      Int  :$level          = 0,
      Int  :$spacing        = 2,
           :$sorted-keys    = False,
      Bool :$enums-as-value = False,
    ) {
        my str @out;
        my str $spaces = ' ' x $spacing;

        # Every string's escaped form, for the length of this call. A
        # document repeats its keys once per record, and a hash lookup is a
        # fraction of what deciding whether a string needs escaping costs.
        my %escaped;
        my sub escape(Str $s) { %escaped{$s} //= str-escape($s) }
        my str $comma  = ",\n" ~ $spaces x $level;

        # The pretty layout is JSON::Fast's, built the way it builds it: the
        # separator carries the next line's indentation, a container pushes
        # one and pops the trailing one, and so an EMPTY container comes out
        # as "[\n]" — odd, but it is the text programs have on disk.
        my sub pretty-positional(\positional --> Nil) {
            $comma = $comma ~ $spaces;
            @out.push: '[';
            @out.push: $comma.substr(1);
            for positional.list {
                jsonify($_);
                @out.push: $comma;
            }
            @out.pop;   # the last separator
            $comma = $comma.substr(0, $comma.chars - $spacing);
            @out.push: $comma.substr(1);
            @out.push: ']';
        }

        my sub pairs-of(\associative) {
            $sorted-keys
              ?? associative.sort($sorted-keys<> =:= True ?? *.key !! $sorted-keys)
              !! associative.list
        }

        my sub pretty-associative(\associative --> Nil) {
            $comma = $comma ~ $spaces;
            @out.push: '{';
            @out.push: $comma.substr(1);
            for pairs-of(associative) {
                @out.push: '"';
                @out.push: escape(.key.Str);
                @out.push: '": ';
                jsonify(.value);
                @out.push: $comma;
            }
            @out.pop;   # the last separator
            $comma = $comma.substr(0, $comma.chars - $spacing);
            @out.push: $comma.substr(1);
            @out.push: '}';
        }

        my sub unpretty-positional(\positional --> Nil) {
            @out.push: '[';
            my int $before = @out.elems;
            for positional.list {
                jsonify($_);
                @out.push: ',';
            }
            @out.pop if @out.elems > $before;
            @out.push: ']';
        }

        my sub unpretty-associative(\associative --> Nil) {
            @out.push: '{';
            my int $before = @out.elems;
            for pairs-of(associative) {
                @out.push: '"';
                @out.push: escape(.key.Str);
                @out.push: '":';
                jsonify(.value);
                @out.push: ',';
            }
            @out.pop if @out.elems > $before;
            @out.push: '}';
        }

        # The order of these tests is JSON::Fast's and it matters: Bool and
        # the allomorphs before Str, enums before Str and Int (an enum can be
        # either), Seq before Positional, Associative before Positional (a
        # Pair is both and is written as an object).
        my sub jsonify(\obj --> Nil) {
            with obj {
                if $_ ~~ Bool {
                    @out.push: $_ ?? 'true' !! 'false';
                }
                elsif $_ ~~ IntStr { jsonify(.Int) }
                elsif $_ ~~ RatStr { jsonify(.Rat) }
                elsif $_ ~~ NumStr { jsonify(.Num) }
                elsif $_ ~~ Enumeration {
                    if $enums-as-value {
                        jsonify(.value);
                    }
                    else {
                        @out.push: '"';
                        @out.push: escape(.key);
                        @out.push: '"';
                    }
                }
                elsif $_ ~~ Str {
                    @out.push: '"';
                    @out.push: escape($_);
                    @out.push: '"';
                }
                elsif $_ ~~ Int {
                    @out.push: .Str;
                }
                elsif $_ ~~ Rational {   # Rat, FatRat and any other Rational
                    my str $s = .Str;
                    @out.push: $s.contains('.') ?? $s !! $s ~ '.0';
                }
                elsif $_ ~~ Num {
                    if .isNaN || $_ == Inf || $_ == -Inf {
                        @out.push: $*JSON_NAN_INF_SUPPORT ?? .Str !! 'null';
                    }
                    else {
                        my str $s = .Str;
                        @out.push: $s.contains('e') ?? $s !! $s ~ 'e0';
                    }
                }
                elsif $_ ~~ Seq {
                    jsonify(.cache);
                }
                elsif $_ ~~ Associative {
                    $pretty ?? pretty-associative($_) !! unpretty-associative($_);
                }
                elsif $_ ~~ Positional {
                    $pretty ?? pretty-positional($_) !! unpretty-positional($_);
                }
                elsif $_ ~~ Dateish {
                    # quoted but NOT escaped, as JSON::Fast does — a date's
                    # text has nothing in it to escape
                    @out.push: qq/"$_"/;
                }
                elsif $_ ~~ Instant {
                    @out.push: qq/"{.DateTime}"/;
                }
                elsif $_ ~~ Real {
                    jsonify(.Bridge);
                }
                elsif $_ ~~ Version {
                    jsonify(.Str);
                }
                else {
                    die "Don't know how to jsonify {.^name}";
                }
            }
            else {
                @out.push: 'null';
            }
        }

        jsonify(obj);
        @out.join
    }

    # ---- from-json ----------------------------------------------------------
    #
    # Positions are GRAPHEME positions, as JSON::Fast's are, and every test of
    # "what is at this position" is spelled the way JSON::Fast spells it,
    # because there are two kinds and they disagree on a grapheme that carries
    # a combining mark. A test by ORD (JSON::Fast's nqp::ordat) sees the
    # grapheme's BASE codepoint, so a quote carrying U+0301 opens a string
    # and a space carrying one is whitespace; a test by `eq` against one
    # character (its nqp::eqat and nqp::index) compares the whole grapheme,
    # so the same quote does not close one. Which test sits where is copied,
    # not reasoned about.

    my constant CONTEXT-LENGTH = 5;
    my sub context-for-error-report(str $text, int $pos, int $startpos = -1) {
        my int $start-of-context = 0 max $pos - CONTEXT-LENGTH;
        my str $prematch  = $text.substr($start-of-context, $pos - $start-of-context);
        my str $postmatch = $pos < $text.chars ?? $text.substr($pos, CONTEXT-LENGTH) !! '';
        my int $has-postmatch = $postmatch.chars > 1;
        "Context: "
          ~ (($start-of-context == 0 ?? "<BOS>" !! "") ~ $prematch).raku.substr(1, *-1)
          ~ "→"
          ~ ($has-postmatch ?? $postmatch.substr(0, 1).raku.substr(1, *-1) ~ "←" !! "")
          ~ ($postmatch ~ ($postmatch.chars < CONTEXT-LENGTH ?? "<EOS>" !! "")).raku.substr($has-postmatch ?? 2 !! 1, *-1)
          ~ ($startpos != -1
              ?? " (corresponding start " ~ ($pos - $startpos) ~ " characters earlier, at $startpos)"
              !! "")
    }

    my sub char-at(str $text, int $pos) {
        $pos < $text.chars ?? $text.substr($pos, 1) !! ''
    }
    my sub die-end-in-comment(str $text, int $pos, int $startpos) {
        die "reached end of input inside comment\n" ~ context-for-error-report($text, $pos, $startpos);
    }
    my sub die-missing-object-key(str $text, int $pos, int $startpos) {
        die ($pos == $text.chars
          ?? "at end of input: expected a quoted string for an object key"
          !! "at $pos: json requires object keys to be strings") ~ "\n" ~ context-for-error-report($text, $pos, $startpos);
    }
    my sub die-unexpected-partitioner(str $text, int $pos, int $startpos) {
        die ($pos == $text.chars
          ?? "at end of input: expected a partitioner inside list of things"
          !! "at $pos, unexpected partitioner '{ char-at($text, $pos) }' inside list of things in an array") ~ "\n" ~ context-for-error-report($text, $pos, $startpos);
    }
    my sub die-missing-colon(str $text, int $pos, int $startpos) {
        die "expected to see a ':' after an object key at $pos\n" ~ context-for-error-report($text, $pos, $startpos);
    }
    my sub die-unexpected-end-of-object(str $text, int $pos, int $startpos) {
        die ($pos == $text.chars
          ?? "at end of input: unexpected end of object."
          !! "unexpected '{ char-at($text, $pos) }' in an object at $pos") ~ "\n" ~ context-for-error-report($text, $pos, $startpos);
    }
    my sub die-expected-object(str $text, int $pos, int $startpos) {
        die ($pos == $text.chars
          ?? "at end of input: expected a json object"
          !! "at $pos: expected a json object") ~ "\n" ~ context-for-error-report($text, $pos, $startpos);
    }
    my sub die-invalid-number(str $text, int $start, str $token) {
        die "at {$start + 1}: invalid number token $token\n" ~ context-for-error-report($text, $start + 1);
    }

    # escape letter's codepoint => the codepoint it stands for
    my %escapees = 34 => 34, 47 => 47, 92 => 92,                         # " / \
                   98 => 8, 102 => 12, 110 => 10, 114 => 13, 116 => 9;   # b f n r t

    # $s starts with the opening quote's grapheme. On NFD codepoints, as
    # JSON::Fast does it: raw TAB and LF are refused (other raw control
    # characters pass — JSON::Fast's rule, not RFC 8259's), a \u escape takes
    # exactly four hex digits, and a surrogate pair is joined.
    my sub unescape-string(str $s, int $pos) {
        # (.list: mutsu 2026.07 cannot fill a native array from a Uni directly)
        my int @codes = $s.NFD.list;
        my int $n = @codes.elems;
        my int $i = 1;   # past the quote itself
        my int @out;

        my sub fetch-codepoint() {
            my int $cp = 0;
            for ^4 {
                die "incomplete \\u sequence in string near $pos" if $i >= $n;
                my int $c = @codes[$i];
                $i = $i + 1;
                my int $d = 48 <= $c <= 57  ?? $c - 48
                         !! 97 <= $c <= 102 ?? $c - 87
                         !! 65 <= $c <= 70  ?? $c - 55
                         !! die "invalid hexadecimal char {$c.chr.raku} in \\u sequence at $pos";
                $cp = $cp * 16 + $d;
            }
            $cp
        }

        my int $c;
        my int $e;
        my int $cp;
        while $i < $n {
            $c = @codes[$i];
            $i = $i + 1;
            if $c == 92 {
                $e = $i < $n ?? @codes[$i] !! -1;
                $i = $i + 1;
                if $e == 117 {   # u
                    $cp = fetch-codepoint();
                    if 0xD800 <= $cp < 0xE000 {
                        if $i + 1 < $n && @codes[$i] == 92 && @codes[$i + 1] == 117 {
                            $i = $i + 2;
                            my int $low = fetch-codepoint();
                            die "improper low surrogate \\u{$low.base(16)} for high surrogate \\u{$cp.base(16)} near $pos"
                              unless $low >= 0xDC00;
                            $cp = ($cp - 0xD800) * 0x400 + 0x10000 + $low - 0xDC00;
                        }
                        else {
                            die "missing low surrogate for high surrogate \\u{$cp.base(16)} near $pos";
                        }
                    }
                    @out.push: $cp;
                }
                elsif %escapees{$e} -> $code {
                    @out.push: $code;
                }
                else {
                    die "unknown escape code found '\\{ $e < 0 ?? '' !! $e.chr }' found near $pos";
                }
            }
            elsif $c == 9 || $c == 10 {
                die "this kind of whitespace is not allowed in a string: '{ $c.chr.raku }' near $pos";
            }
            else {
                @out.push: $c;
            }
        }
        @out.chrs.NFC.Str   # composed; see str-escape
    }

    # A digit is any Unicode decimal digit, tested on the base codepoint, as
    # JSON::Fast's CCLASS_NUMERIC scan has it.
    my sub is-wide-digit(int $o --> Bool) {
        $o > 127 && $o.uniprop eq 'Nd'
    }

    # Four ASCII hex digits at $at, or -1.
    my sub hex4(str $s, int $at --> int) {
        return -1 if $at + 4 > $s.chars;
        my int $cp = 0;
        my int $i = $at;
        my int $c;
        while $i < $at + 4 {
            $c = $s.substr($i, 1).ord;
            if    48 <= $c <= 57  { $cp = $cp * 16 + $c - 48 }
            elsif 97 <= $c <= 102 { $cp = $cp * 16 + $c - 87 }
            elsif 65 <= $c <= 70  { $cp = $cp * 16 + $c - 55 }
            else                  { return -1 }
            $i = $i + 1;
        }
        $cp
    }

    my @escaped = Nil xx 128;
    @escaped[.key.ord] = .value for '"' => '"', '/' => '/', '\\' => '\\',
      b => "\b", f => "\f", n => "\n", r => "\r", t => "\t";

    # The common escaped string, decoded a run at a time between backslashes
    # rather than a codepoint at a time. Only for a document with no
    # multi-codepoint grapheme in it, where a backslash is always a backslash,
    # and only for a string with no raw TAB or LF; anything irregular — an
    # unknown escape, a short or non-ASCII \u, an unpaired surrogate —
    # answers Nil, and unescape-string then decodes the same text the way
    # JSON::Fast does and raises what JSON::Fast raises.
    my sub unescape-plain(str $s) {
        my @parts;
        my int $u = 0;   # a \u escape was decoded: the join must be put in NFC
        my int $p = 0;
        my int $len = $s.chars;
        my $b;
        my str $e;
        my int $cp;
        my int $low;
        loop {
            $b = $s.index('\\', $p);
            without $b {
                @parts.push: $s.substr($p) if $p < $len;
                # composed where an escape may have made a combining pair:
                # Rakudo's join composes on its own, mutsu's does not
                return $u ?? @parts.join.NFC.Str !! @parts.join;
            }
            @parts.push: $s.substr($p, $b - $p) if $b > $p;
            $e = $b + 1 < $len ?? $s.substr($b + 1, 1) !! '';
            if $e eq 'u' {
                $u = 1;
                $cp = hex4($s, $b + 2);
                return Nil if $cp < 0;
                $p = $b + 6;
                if 0xD800 <= $cp < 0xE000 {
                    return Nil unless $s.substr($p, 2) eq '\\u';
                    $low = hex4($s, $p + 2);
                    return Nil if $low < 0xDC00;
                    $cp = ($cp - 0xD800) * 0x400 + 0x10000 + $low - 0xDC00;
                    return Nil if $cp > 0x10FFFF;
                    $p = $p + 6;
                }
                @parts.push: $cp.chr;
            }
            elsif $e ne '' && $e.ord < 128 && @escaped[$e.ord] -> $c {
                @parts.push: $c;
                $p = $b + 2;
            }
            else {
                return Nil;
            }
        }
    }

    # The parser proper is a set of closures over ONE document — its text,
    # length and position — rather than subs threading `int $pos is rw`
    # through every call. Declarations sit outside loop bodies on purpose: a
    # block that declares nothing is flattened into its routine, one that
    # does is a frame entered per iteration.
    our sub from-json(Str() $text, :$immutable, :$allow-jsonc) {
        my str $t = $text;
        my int $n = $t.chars;
        my int $pos = 0;
        my int $imm = $immutable ?? 1 !! 0;

        # Skips whitespace (and, under :allow-jsonc, comments) and answers
        # the codepoint it stopped at — the base one, as nqp::ordat answers
        # it — or -1 at the end. Every structural test reads this, so the
        # character is fetched once rather than once to skip and once to test.
        my sub ws-ord(--> int) {
            # no `return` or `last` inside the loop: on Rakudo either one is
            # a control exception when the loop body is a block of its own
            my int $o = $pos < $n ?? $t.substr($pos, 1).ord !! -1;
            while $o == 32 || $o == 10 || $o == 9 || $o == 13 || $o == 47 && $allow-jsonc {
                if $o == 47 { nom-comment() } else { $pos = $pos + 1 }
                $o = $pos < $n ?? $t.substr($pos, 1).ord !! -1;
            }
            $o
        }

        # Called at a '/'. Where the comment ends is JSON::Fast's answer,
        # quirks included, because they are observable: a // comment that
        # runs to the end of the text, and a /* that never closes, both leave
        # the position one PAST the end — so after a document they read as
        # additional content — while a lone '/' at the very end is accepted.
        my sub nom-comment(--> Nil) {
            $pos = $pos + 1;
            my int $startpos = $pos;
            my int $next = $pos < $n ?? $t.substr($pos, 1).ord !! -1;
            if $next == 47 {   # //
                $pos = $pos + 1;
                $pos = $pos + 1 while $pos < $n && $t.substr($pos, 1).ord != 10;
                $pos = $pos + 1;   # past the newline — or past the end
            }
            elsif $next == 42 {   # /*
                # the first '/' tested is two past the '*': "/*/" is still open
                $pos = $pos + 2;
                $pos = $pos + 1
                  until $pos >= $n
                     || $t.substr($pos, 1).ord == 47 && $t.substr($pos - 1, 1).ord == 42;
                if $pos >= $n {
                    $pos = $n + 1;
                    die-end-in-comment($t, $n, $startpos);
                }
                $pos = $pos + 1;
            }
            elsif $next == -1 {
                die-end-in-comment($t, $pos, $startpos);
            }
            else {
                die-expected-object($t, $pos, $startpos);
            }
        }

        # Called with $pos just past the opening quote's grapheme. The closing
        # quote is the first '"' grapheme preceded by an even number of '\'
        # graphemes. A string with no backslash and no raw TAB or LF in it is
        # its own value — the common case, one substr — PROVIDED every
        # grapheme in it is a single codepoint and its opening quote was a
        # plain one. Anything else is decoded codepoint by codepoint from the
        # OPENING quote's grapheme on, which is where a combining mark on that
        # quote ends up: as the string's first character, as JSON::Fast has it.
        #
        # "Is there a backslash, TAB or LF in this string" is answered from
        # the position of the NEXT one in the document, found once and reused
        # until the parse passes it, rather than searched for per string.
        my int $plain = $t.codes == $n;   # no multi-codepoint grapheme anywhere
        my int $next-bs  = -1;
        my int $next-tab = -1;
        my int $next-nl  = -1;
        my sub next-at(str $c, int $from --> int) {
            my $i = $t.index($c, $from);
            $i.defined ?? $i !! $n
        }
        my sub parse-string() {
            my int $start = $pos;
            my $end = $t.index('"', $pos);
            my int $slashes;
            $next-bs = next-at('\\', $start) if $next-bs < $start;
            if $next-bs < ($end // $n) {
                # a backslash before the first quote: that quote may be escaped
                loop {
                    last without $end;
                    $slashes = 0;
                    $slashes = $slashes + 1 while $t.substr($end - $slashes - 1, 1) eq '\\';
                    last unless $slashes +& 1;
                    $end = $t.index('"', $end + 1);
                }
            }
            die "unexpected end of input in string\n"
              ~ context-for-error-report($t, $n, $start - 1)
                without $end;
            $pos = $end + 1;
            $next-tab = next-at("\t", $start) if $next-tab < $start;
            $next-nl  = next-at("\n", $start) if $next-nl  < $start;
            if $plain {
                return $t.substr($start, $end - $start)
                  if $next-bs >= $end && $next-tab >= $end && $next-nl >= $end;
                if $next-tab >= $end && $next-nl >= $end {
                    with unescape-plain($t.substr($start, $end - $start)) { return $_ }
                }
            }
            elsif $next-bs >= $end && $next-tab >= $end && $next-nl >= $end {
                my str $s = $t.substr($start, $end - $start);
                return $s if $s.codes == $s.chars && $t.substr($start - 1, 1) eq '"';
            }
            unescape-string($t.substr($start - 1, $end - $start + 1), $pos)
        }

        # The scan is JSON::Fast's: a sign or digit, digits, an optional
        # fraction, an optional exponent, nothing checked beyond that. The
        # token is then typed by its own .Numeric, which is what makes 1 an
        # Int, 1.5 a Rat and 1e0 a Num and what refuses a token that scanned
        # but is no number — EXCEPT for the two shapes nearly every document
        # is made of, an integer or a plain decimal of at most 18 ASCII
        # digits, which are built from the digits as they are scanned: the
        # same Int, and the same normalised Rat, without the general parse.
        my sub parse-numeric() {
            my int $start = $pos;
            my int $end = $pos + 1;
            my int $o = $t.substr($pos, 1).ord;
            my int $neg = 0;
            my int $acc = 0;
            my int $digits = 0;
            my int $fdigits = 0;
            my int $frac = 0;
            my int $simple = 1;
            if $o == 45 { $neg = 1 }
            elsif $o >= 48 && $o <= 57 { $acc = $o - 48; $digits = 1 }
            else { $simple = 0 }   # a non-ASCII digit
            $o = $end < $n ?? $t.substr($end, 1).ord !! -1;
            while $o >= 48 && $o <= 57 || $o > 127 && is-wide-digit($o) {
                if $o <= 57 {
                    $acc = $acc * 10 + $o - 48 if $digits < 18;
                    $digits = $digits + 1;
                }
                else { $simple = 0 }
                $end = $end + 1;
                $o = $end < $n ?? $t.substr($end, 1).ord !! -1;
            }
            if $o == 46 {   # .
                $frac = 1;
                $end = $end + 1;
                $o = $end < $n ?? $t.substr($end, 1).ord !! -1;
                while $o >= 48 && $o <= 57 || $o > 127 && is-wide-digit($o) {
                    if $o <= 57 {
                        $acc = $acc * 10 + $o - 48 if $digits + $fdigits < 18;
                        $fdigits = $fdigits + 1;
                    }
                    else { $simple = 0 }
                    $end = $end + 1;
                    $o = $end < $n ?? $t.substr($end, 1).ord !! -1;
                }
            }
            if $o == 101 || $o == 69 {   # e E
                $simple = 0;
                $end = $end + 1;
                my str $sign = char-at($t, $end);
                $end = $end + 1 if $sign eq '-' || $sign eq '+';
                $o = $end < $n ?? $t.substr($end, 1).ord !! -1;
                while $o >= 48 && $o <= 57 || $o > 127 && is-wide-digit($o) {
                    $end = $end + 1;
                    $o = $end < $n ?? $t.substr($end, 1).ord !! -1;
                }
            }
            if $simple && $digits && $digits + $fdigits <= 18 && (!$frac || $fdigits) {
                $pos = $end;
                $acc = -$acc if $neg;
                return $frac ?? Rat.new($acc, 10 ** $fdigits) !! $acc;
            }
            my str $token = $t.substr($start, $end - $start);
            my $result := $token.Numeric;
            if $result ~~ Failure {
                $result.so;   # handled: the die says what went wrong
                die-invalid-number($t, $start, $token);
            }
            $pos = $end;
            $result
        }

        my sub parse-obj() {
            my int $startpos = $pos;
            my int $o = ws-ord();
            if $o == 125 {   # }
                $pos = $pos + 1;
                return $imm ?? Map.new !! {};
            }
            my %result;
            my @pairs;
            my $key;
            # The value goes straight into its container rather than through a
            # variable rebound per pair: on Raku++ (5.3 and before) a `$v :=`
            # of a sub's native-int result leaves $v a native-int slot, and
            # every later `$v :=` was coerced to an integer — an array after
            # a number came back as its length.
            loop {
                die-missing-object-key($t, $pos, $startpos) unless $o == 34;   # "
                $pos = $pos + 1;
                $key = parse-string();
                die-missing-colon($t, $pos, $startpos) unless ws-ord() == 58;   # :
                $pos = $pos + 1;
                $imm ?? @pairs.push(Pair.new($key, parse-thing())) !! (%result{$key} = parse-thing());
                $o = ws-ord();
                if $o == 125 {
                    $pos = $pos + 1;
                    return $imm ?? Map.new(@pairs) !! %result;
                }
                die-unexpected-end-of-object($t, $pos, $startpos) unless $o == 44;   # ,
                $pos = $pos + 1;
                $o = ws-ord();
            }
        }

        my sub parse-array() {
            ws-ord();
            my int $startpos = $pos;
            if $pos < $n && $t.substr($pos, 1) eq ']' {
                $pos = $pos + 1;
                return $imm ?? List.new !! [];
            }
            my @result;
            my int $o;
            loop {
                @result.push: parse-thing();
                $o = ws-ord();
                if $o == 93 {   # ]
                    $pos = $pos + 1;
                    return $imm ?? @result.List !! @result;
                }
                die-unexpected-partitioner($t, $pos, $startpos) unless $o == 44;   # ,
                $pos = $pos + 1;
            }
        }

        my sub parse-thing() {
            my int $o = ws-ord();
            if    $o ==  34 { $pos = $pos + 1; parse-string() }   # "
            elsif $o == 123 { $pos = $pos + 1; parse-obj()    }   # {
            elsif $o ==  91 { $pos = $pos + 1; parse-array()  }   # [
            elsif $o == 45 || 48 <= $o <= 57 || is-wide-digit($o) { parse-numeric() }
            # substr + eq rather than .substr-eq: the same test, and the one
            # of the two that every engine answers without copying the text
            elsif $o == 116 && $t.substr($pos, 4) eq 'true'  { $pos = $pos + 4; True  }
            elsif $o == 102 && $t.substr($pos, 5) eq 'false' { $pos = $pos + 5; False }
            elsif $o == 110 && $t.substr($pos, 4) eq 'null'  { $pos = $pos + 4; Any   }
            else { die-expected-object($t, $pos, $pos) }
        }

        my $parsed := parse-thing();
        if $pos != $n {
            my int $parsed-length = $pos;
            try ws-ord();
            X::JSON::Native::AdditionalContent.new(
              :$parsed, :$parsed-length, rest-position => $pos
            ).throw unless $pos == $n;
        }
        $parsed
    }
}
