# The zero-dep sweep of the v2 battery: NativeHelpers::Array, Cro::Core,
# Date::Calendar::Strftime and Config each blocked on one general bug —
#
#   * `--> CArray` / `--> CArray[uint8]` return types: NativeCall's container
#     types were missing from isKnownTypeName, so calling any routine declared
#     with one died "Type 'CArray' is not declared".
#   * `@$<seg>`: the contextualizer took only the primary as its operand (the
#     rule that keeps `@$p[0]` as `(@$p)[0]`), which STRANDED the match-capture
#     subscript — it re-attached to the contextualized value as `@($/)<seg>`,
#     an associative index on an Array. The capture is part of the VARIABLE.
#   * `&%dispatch{$key}`: the code sigil on a hash/array ELEMENT parsed as an
#     operator in term position. The element is the callable; & adds nothing.
#   * nested `=begin comment` blocks of the SAME name: the skipper closed the
#     outer block at the first `=end comment`, so the rest of the outer block
#     parsed as code (Date::Names does this, taking Strftime with it).
#   * `token match:character-class`: the lexer's adverb-key scan stopped at the
#     hyphen, so the name split and the body was never lexed as a regex
#     (IO::Glob, taking Config with it).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# capture subscript under a contextualizer
grammar Segs { token TOP { <seg>+ }  token seg { '/' <chars> }  token chars { \w+ } }
class SegActs {
    method TOP($/) {
        my $r = '';
        $r ~= '/' ~ (.<chars> // 'X') for @$<seg>;
        make $r;
    }
}
check(Segs.parse('/a/bb', :actions(SegActs)).ast, '/a/bb', '@$<seg> iterates the capture list');
# the rule that made this subtle is preserved: with a NAMED variable the
# postfix binds to the CONTEXTUALIZED value (`@$p[1]` is `(@$p)[1]`)
my $p = [10, 20, 30];
check(@$p[1], 20, 'while @$p[i] still subscripts the contextualized array');

# & on an element
my %dispatch = double => sub ($x) { $x * 2 }, triple => sub ($x) { $x * 3 };
my $key = 'double';
my $fnc = &%dispatch{$key};
check($fnc(21), 42, '&%hash{$key} takes the element as the callable');
my @handlers = sub { 'first' }, sub { 'second' };
check(&@handlers[1](), 'second', '&@array[$i] too');

# nested same-name comment blocks
my $x = 1;
=begin comment
$x = 2;
    =begin comment
    $x = 3;
    =end comment
$x = 4;
=end comment
check($x, 1, 'nested =begin comment blocks skip as one region');

# hyphenated proto-candidate names lex as one declaration
grammar CC {
    token TOP { <cls> }
    proto token cls { * }
    token cls:character-class { '[' $<not> = [ "!"? ] $<body> = [ <-[ \] ]>+ ] ']' }
}
my $cc = CC.parse('[!abc]');
check(?$cc, True, 'token name:with-hyphens declares');
check(~$cc<cls><not>, '!', 'and its body lexed as a regex');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
