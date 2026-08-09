// NativeJsonFast.cpp — JSON::Fast, shipped inside the interpreter.
//
// The ecosystem's rank-1 module is also the one module interpreted execution
// cannot serve: its parser walks the text one grapheme at a time, and the
// 332 KB SPDX license file License::SPDX loads takes 52 s that way (~1 s on
// Rakudo, where the module is compiled). Every dist in the Test::META chain
// inherits that wall.
//
// The shim below is the module's OWN 0.19 source (Artistic-2.0, © the
// JSON::Fast contributors) with one substitution: the parse machinery is
// replaced by a call into rakupp's C++ JSON parser (Builtins.cpp), which
// reports trailing-content positions so X::JSON::AdditionalContent behaves
// exactly as upstream. to-json, the exception class and the EXPORT protocol
// (use JSON::Fast <immutable !pretty> …) are the module's own code, untouched.
//
// loadModule() takes this shim ahead of any disk copy; RAKUPP_JSON_FAST=0
// falls back to the disk module for one release.

namespace rakupp {

const char* rakuppJsonFastShimSource() {
    return R"RAKUPPJF(
=begin pod
=head1 JSON::Fast

A naive imperative JSON parser in pure Raku (but with direct access to C<nqp::> ops), to evaluate performance against C<JSON::Tiny>. It is a drop-in replacement for C<JSON::Tiny>’s from-json and to-json subs, but it offers a few extra features.

Currently it seems to be about 4x faster and uses up about a quarter of the RAM JSON::Tiny would use.

This module also includes a very fast to-json function that tony-o created and lizmat later completely refactored.

=head2 Exported subroutines

=head3 to-json

=for code
    my $*JSON_NAN_INF_SUPPORT = 1; # allow NaN, Inf, and -Inf to be serialized.
    say to-json [<my Raku data structure>];
    say to-json [<my Raku data structure>], :!pretty;
    say to-json [<my Raku data structure>], :spacing(4);

    enum Blerp <Hello Goodbye>;
    say to-json [Hello, Goodbye]; # ["Hello", "Goodbye"]
    say to-json [Hello, Goodbye], :enums-as-value; # [0, 1]

Encode a Raku data structure into JSON. Takes one positional argument, which
is a thing you want to encode into JSON. Takes these optional named arguments:

=head4 pretty

C<Bool>. Defaults to C<True>. Specifies whether the output should be "pretty",
human-readable JSON. When set to false, will output json in a single line.

=head4 spacing

C<Int>. Defaults to C<2>. Applies only when C<pretty> is C<True>.
Controls how much spacing there is between each nested level of the output.

=head4 sorted-keys

Specifies whether keys from objects should be sorted before serializing them
to a string or if C<$obj.keys> is good enough.  Defaults to C<False>.  Can
also be specified as a C<Callable> with the same type of argument that the
C<.sort> method accepts to provide alternate sorting methods.

=head4 enum-as-value

C<Bool>, defaults to C<False>.  Specifies whether C<enum>s should be json-ified
as their underlying values, instead of as the name of the C<enum>.

=head3 from-json

=for code
    my $x = from-json '["foo", "bar", {"ber": "bor"}]';
    say $x.perl;
    # outputs: $["foo", "bar", {:ber("bor")}]

Takes one positional argument that is coerced into a C<Str> type and represents
a JSON text to decode. Returns a Raku datastructure representing that JSON.

=head4 immutable

C<Bool>. Defaults to C<False>. Specifies whether C<Hash>es and C<Array>s should be
rendered as immutable datastructures instead (as C<Map> / C<List>.  Creating an
immutable data structures is mostly saving on memory usage, and a little bit on
CPU (typically around 5%).

This also has the side effect that elements from the returned structure can now
be iterated over directly because they are not containerized.

=head4 allow-jsonc

C<BOOL>.  Defaults to C<False>.  Specifies whether commmands adhering to the
L<JSONC standard|https://changelog.com/news/jsonc-is-a-superset-of-json-which-supports-comments-6LwR>
are allowed.

=for code
    my %hash := from-json "META6.json".IO.slurp, :immutable;
    say "Provides:";
    .say for %hash<provides>;

=head2 Additional features

=head3 Adapting defaults of "from-json"

In the C<use> statement, you can add the string C<"immutable"> to make the
default of the C<immutable> parameter to the C<from-json> subroutine C<True>,
rather than False.

=for code
    use JSON::Fast <immutable>;  # create immutable data structures by default

=head3 Adapting defaults of "to-json"

In the C<use> statement, you can add the strings C<"!pretty">,
C<"sorted-keys"> and/or C<"enums-as-value"> to change the associated
defaults of the C<to-json> subroutine.

=for code
    use JSON::FAST <!pretty sorted-keys enums-as-value>;

=head3 Strings containing multiple json pieces

When the document contains additional non-whitespace after the first
successfully parsed JSON object, JSON::Fast will throw the exception
C<X::JSON::AdditionalContent>. If you expect multiple objects, you
can catch that exception, retrieve the parse result from its
C<parsed> attribute, and remove the first C<rest-position> characters
off of the string and restart parsing from there.

=end pod

use nqp;

our class X::JSON::AdditionalContent is Exception is export {
    has $.parsed;
    has $.parsed-length;
    has $.rest-position;

    method message {
        "JSON Input contained additional text after the document (parsed $.parsed-length chars, next non-whitespace lives at $.rest-position)"
    }
}

module JSON::Fast:ver<0.19> {

    multi sub to-surrogate-pair(Int $ord) {
        my int $base   = $ord - 0x10000;
        my int $top    = $base +& 0b1_1111_1111_1100_0000_0000 +> 10;
        my int $bottom = $base +&               0b11_1111_1111;
        Q/\u/ ~ (0xD800 + $top).base(16) ~ Q/\u/ ~ (0xDC00 + $bottom).base(16);
    }

    multi sub to-surrogate-pair(Str $input) {
        to-surrogate-pair(nqp::ordat($input, 0));
    }

    my $tab := nqp::list_i(92,116); # \t
    my $lf  := nqp::list_i(92,110); # \n
    my $cr  := nqp::list_i(92,114); # \r
    my $qq  := nqp::list_i(92, 34); # \"
    my $bs  := nqp::list_i(92, 92); # \\

# Convert string to decomposed codepoints.  Run over that integer array
# and inject whatever is necessary, don't do anything if simple ascii.
# Then convert back to string and return that.
    sub str-escape(\text) {
        my $codes := text.NFD;
        my int $i = -1;

        nqp::while(
          nqp::islt_i(++$i,nqp::elems($codes)),
          nqp::if(
            nqp::isle_i((my int $code = nqp::atpos_i($codes,$i)),92)
              || nqp::isge_i($code,128),
            nqp::if(                                           # not ascii
              nqp::isle_i($code,31),
              nqp::if(                                          # control
                nqp::iseq_i($code,10),
                nqp::splice($codes,$lf,$i++,1),                  # \n
                nqp::if(
                  nqp::iseq_i($code,13),
                  nqp::splice($codes,$cr,$i++,1),                 # \r
                  nqp::if(
                    nqp::iseq_i($code,9),
                    nqp::splice($codes,$tab,$i++,1),               # \t
                    nqp::stmts(                                    # other control
                      nqp::splice($codes,$code.fmt(Q/\u%04x/).NFD,$i,1),
                      ($i = nqp::add_i($i,5))
                    )
                  )
                )
              ),
              nqp::if(                                          # not control
                nqp::iseq_i($code,34),
                nqp::splice($codes,$qq,$i++,1),                  # "
                nqp::if(
                  nqp::iseq_i($code,92),
                  nqp::splice($codes,$bs,$i++,1),                 # \
                  nqp::if(
                    nqp::isge_i($code,0x10000),
                    nqp::stmts(                                    # surrogates
                      nqp::splice(
                        $codes,
                        (my $surrogate := to-surrogate-pair($code.chr).NFD),
                        $i,
                        1
                      ),
                      ($i = nqp::sub_i(nqp::add_i($i,nqp::elems($surrogate)),1))
                    )
                  )
                )
              )
            )
          )
        );

        nqp::strfromcodes($codes)
    }

    our sub to-json(
      \obj,
      Bool :$pretty         = True,
      Int  :$level          = 0,
      int  :$spacing        = 2,
           :$sorted-keys    = False,
      Bool :$enums-as-value = False,
    ) {

        my str @out;
        my str $spaces = ' ' x $spacing;
        my str $comma  = ",\n" ~ $spaces x $level;

#-- helper subs from here, with visibility to the above lexicals

        sub pretty-positional(\positional --> Nil) {
            $comma = nqp::concat($comma,$spaces);
            nqp::push_s(@out,'[');
            nqp::push_s(@out,nqp::substr($comma,1));

            for positional.list {
                jsonify($_);
                nqp::push_s(@out,$comma);
            }
            nqp::pop_s(@out);  # lose last comma

            $comma = nqp::substr($comma,0,nqp::sub_i(nqp::chars($comma),$spacing));
            nqp::push_s(@out,nqp::substr($comma,1));
            nqp::push_s(@out,']');
        }

        sub pretty-associative(\associative --> Nil) {
            $comma = nqp::concat($comma,$spaces);
            nqp::push_s(@out,'{');
            nqp::push_s(@out,nqp::substr($comma,1));
            my \pairs := $sorted-keys
              ?? associative.sort($sorted-keys<> =:= True ?? *.key !! $sorted-keys)
              !! associative.list;

            for pairs {
                nqp::push_s(@out,'"');
                nqp::push_s(@out, str-escape(.key.Str));
                nqp::push_s(@out,'": ');
                jsonify(.value);
                nqp::push_s(@out,$comma);
            }
            nqp::pop_s(@out);  # lose last comma

            $comma = nqp::substr($comma,0,nqp::sub_i(nqp::chars($comma),$spacing));
            nqp::push_s(@out,nqp::substr($comma,1));
            nqp::push_s(@out,'}');
        }

        sub unpretty-positional(\positional --> Nil) {
            nqp::push_s(@out,'[');
            my int $before = nqp::elems(@out);
            for positional.list {
                jsonify($_);
                nqp::push_s(@out,",");
            }
            nqp::pop_s(@out) if nqp::elems(@out) > $before;  # lose last comma
            nqp::push_s(@out,']');
        }

        sub unpretty-associative(\associative --> Nil) {
            nqp::push_s(@out,'{');
            my \pairs := $sorted-keys
              ?? associative.sort($sorted-keys<> =:= True ?? *.key !! $sorted-keys)
              !! associative.list;

            my int $before = nqp::elems(@out);
            for pairs {
                nqp::push_s(@out, '"');
                nqp::push_s(@out, str-escape(.key.Str));
                nqp::push_s(@out,'":');
                jsonify(.value);
                nqp::push_s(@out,",");
            }
            nqp::pop_s(@out) if nqp::elems(@out) > $before;  # lose last comma
            nqp::push_s(@out,'}');
        }

        sub jsonify(\obj --> Nil) {

            with obj {

                # basic ones
                if nqp::istype($_, Bool) {
                    nqp::push_s(@out,obj ?? "true" !! "false");
                }
                elsif nqp::istype($_, IntStr) {
                    jsonify(.Int);
                }
                elsif nqp::istype($_, RatStr) {
                    jsonify(.Rat);
                }
                elsif nqp::istype($_, NumStr) {
                    jsonify(.Num);
                }
                elsif nqp::istype($_, Enumeration) {
                    if $enums-as-value {
                        jsonify(.value);
                    }
                    else {
                        nqp::push_s(@out,'"');
                        nqp::push_s(@out,str-escape(.key));
                        nqp::push_s(@out,'"');
                    }
                }
                # Str and Int go below Enumeration, because there
                # are both Str-typed enums and Int-typed enums
                elsif nqp::istype($_, Str) {
                    nqp::push_s(@out,'"');
                    nqp::push_s(@out,str-escape($_));
                    nqp::push_s(@out,'"');
                }

                # numeric ones
                elsif nqp::istype($_, Int) {
                    nqp::push_s(@out,.Str);
                }
                elsif nqp::istype($_, Rat) {
                    nqp::push_s(@out,.contains(".") ?? $_ !! "$_.0")
                      given .Str;
                }
                elsif nqp::istype($_, FatRat) {
                    nqp::push_s(@out,.contains(".") ?? $_ !! "$_.0")
                      given .Str;
                }
                elsif nqp::istype($_, Rational) {
                    nqp::push_s(@out,.contains(".") ?? $_ !! "$_.0")
                      given .Str;
                }
                elsif nqp::istype($_, Num) {
                    if nqp::isnanorinf($_) {
                        nqp::push_s(
                          @out,
                          $*JSON_NAN_INF_SUPPORT ?? obj.Str !! "null"
                        );
                    }
                    else {
                        nqp::push_s(@out,.contains("e") ?? $_ !! $_ ~ "e0")
                          given .Str;
                    }
                }

                # iterating ones
                elsif nqp::istype($_, Seq) {
                    jsonify(.cache);
                }
                elsif nqp::istype($_, Associative) {
                    $pretty
                      ?? pretty-associative($_)
                      !! unpretty-associative($_);
                }
                elsif nqp::istype($_, Positional) {
                    $pretty
                      ?? pretty-positional($_)
                      !! unpretty-positional($_);
                }

                # rarer ones
                elsif nqp::istype($_, Dateish) {
                    nqp::push_s(@out,qq/"$_"/);
                }
                elsif nqp::istype($_, Instant) {
                    nqp::push_s(@out,qq/"{.DateTime}"/);
                }
                elsif nqp::istype($_, Real) {
                    jsonify(.Bridge);
                }
                elsif nqp::istype($_, Version) {
                    jsonify(.Str);
                }

                # huh, what?
                else {
                    die "Don't know how to jsonify {.^name}";
                }
            }
            else {
                nqp::push_s(@out,'null');
            }
        }

#-- do the actual work

        jsonify(obj);
        nqp::join("",@out)
    }

    # The parse machinery of the original lives in rakupp's C++ core: the
    # builtin returns [parsed, parsed-length, rest-position, ok] with the two
    # positions in graphemes, so additional-content handling below is exactly
    # the original's. Everything else in this file IS the original 0.19 source.
    our sub from-json(Str() $text, :$immutable, :$allow-jsonc) {
        my \parts = rakupp-json-from-parts($text, so $immutable, so $allow-jsonc);
        X::JSON::AdditionalContent.new(
          parsed        => parts[0],
          parsed-length => parts[1],
          rest-position => parts[2],
        ).throw unless parts[3];
        parts[0]
    }
}
sub EXPORT(*@_) {
    my @huh;

    my $from-json-changed;
    my $immutable-default := False;

    my $to-json-changed;
    my $pretty-default         := True;
    my $sorted-keys-default    := False;
    my $enums-as-value-default := False;

    for @_ {
        when "immutable" {
            $immutable-default := True;
            $from-json-changed := True;
        }
        when "!pretty" {
            $pretty-default  := False;
            $to-json-changed := True;
        }
        when "sorted-keys" {
            $sorted-keys-default := True;
            $to-json-changed     := True;
        }
        when "enums-as-value" {
            $enums-as-value-default := True;
            $to-json-changed        := True;
        }
        when "pretty" | "!immutable" | "!sorted-keys" | "!enums-as-value" {
            # no action, these are the defaults
        }
        default {
            @huh.push: $_;
        }
    }

    die "Unrecognized strings in -use- statement: @huh[]"
      if @huh;

    my sub from-json-changed(Str() $text,
      :$immutable = $immutable-default,
    ) {
        JSON::Fast::from-json($text, :$immutable)
    }
    my sub to-json-changed(\obj,
      :$pretty         = $pretty-default,
      :$sorted-keys    = $sorted-keys-default,
      :$enums-as-value = $enums-as-value-default,
    ) {
        JSON::Fast::to-json(obj, :$pretty, :$sorted-keys, :$enums-as-value)
    }

    Map.new((
      '&from-json' => $from-json-changed
        ?? &from-json-changed
        !! &JSON::Fast::from-json,
      '&to-json' => $to-json-changed
        ?? &to-json-changed
        !! &JSON::Fast::to-json,
    ))
}

# vi:syntax=perl6
)RAKUPPJF";
}

} // namespace rakupp
