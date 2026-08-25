# Regression: the defects found making the real DBIish (DBDish::SQLite,
# DBDish::mysql, DBDish::Pg) run — github.com/ash/rakupp issue #28. Every
# check here is engine-level and needs no database.
#
#   1. NativeCall: `is native` on a METHOD (the whole DBDish::mysql and Pg API),
#      a CPointer class as an `is rw` out-param (sqlite3_open's sqlite3**),
#      CStruct field types spelled through a `constant` alias, Buf/Blob type
#      relations and .of, CArray[Str] owning its strings, Pointer.of == void.
#   2. Lists and containers: the one-arg rule in a `[ … ]` composer, binding an
#      itemized array or a native handle to `@`, `has @.a = 1,2,3`, `[ v ]` over
#      a sigilless term (a term, not a reduce), a parenthesised pair as a
#      POSITIONAL argument.
#   3. Objects: writing a field through a subscript, a plain method as a
#      subscript base, a user container's STORE, `is required` filled by BUILD.
#   4. Grammars: positional list captures in the ENTRY rule, `||` ending an LTM
#      prefix, `.values` slipping a list-valued capture, `q:to` unescaping `\\`.
# Contract: exit 0 + last line PASS.
use NativeCall;

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# ---- 1. NativeCall -------------------------------------------------------
class SQ is repr('CPointer') {
    method sqlite3_libversion(SQ:U: --> Str) is native('sqlite3') { * }
}
# `is native` on a method dispatches to the C symbol, not to its `{*}` stub
check(so (try SQ.sqlite3_libversion) ~~ Str:D, 'True', 'native-method-dispatches');

constant my_bool = int8;
class Aliased is repr('CStruct') {
    has int32   $.a is rw;
    has my_bool $.b is rw;
    has my_bool $.c is rw;
}
# a `constant` alias for a native type sizes like the type it names
check(nativesizeof(Aliased), '8', 'cstruct-constant-alias-size');
check(Aliased.REPR,          'CStruct', 'repr-method');

check(so (Buf ~~ Blob), 'True',  'buf-does-blob');
check(so (Blob ~~ Buf), 'False', 'blob-is-not-a-buf');
check(Buf.of.^name,     'uint8', 'buf-of');
check(blob8.of.^name,   'uint8', 'blob8-of');
# (Rakudo spells these fully qualified: NativeCall::Types::void)
check(Pointer.of.^name.subst(/^ .* '::' /, ''), 'void', 'bare-pointer-of-is-void');

# a CArray[Str] owns the strings its slots point at
my $ca = CArray[Str].new;
$ca[0] = 'one'; $ca[1] = 'two'; $ca[2] = Str;
check("{$ca[0]}/{$ca[1]}/{$ca[2] // 'NULL'}", 'one/two/NULL', 'carray-str-owns-strings');
check(CArray[Str].new('a', 'bb')[1], 'bb', 'carray-str-from-new');

# a Buf built by .allocate is a native buffer: reference semantics, like Rakudo
my $b1 = Buf.allocate(4);
my $b2 = $b1;
$b2[0] = 9;
check($b1[0], '9', 'buf-is-a-reference');

# ---- 2. lists and containers --------------------------------------------
my @p = <p q>; my @r = <r s>;
check([@p].raku,     '["p", "q"]',              'array-composer-one-arg-spreads');
check([@p, @r].raku, '[["p", "q"], ["r", "s"]]', 'array-composer-two-args-nest');
check([1, @p].raku,  '[1, ["p", "q"]]',          'array-composer-keeps-later-at-var');

my $itemized = @p;                 # $[…] — what the slurpy one-arg rule hands over
my @bound := $itemized;
check(@bound.elems, '2', 'at-bind-de-itemizes');

my @nat := CArray[Str].new('x');
check(@nat.^name.subst(/^ .* '::' /, ''), 'CArray[Str]', 'at-bind-keeps-a-native-array');

class HasList { has @.things = 1, 2, 3; has %.pairs = (a => 1), (b => 2); }
check(HasList.new.things.elems, '3', 'has-array-default-takes-the-list');
check(HasList.new.pairs.elems,  '2', 'has-hash-default-takes-the-list');

sub sigilless(\v) { [ v ].elems }   # `[ v ]` is a one-element array, not a reduce
check(sigilless('s'), '1', 'bracket-over-a-sigilless-term');
check(([max] 1, 5, 3), '5', 'bracket-reduce-still-reduces');

sub argshape(*@pos, *%named) { "{+@pos}/{+%named}" }
my $x = 1;
check(argshape(:$x),   '0/1', 'colonpair-is-named');
check(argshape((:$x)), '1/0', 'parenthesised-pair-is-positional');
my %h = a => 1;
%h.push((b => 2));
check(%h.elems, '2', 'hash-push-of-a-parenthesised-pair');

# ---- 3. objects ----------------------------------------------------------
class Holder {
    has @!slots;
    has %!map;
    submethod BUILD { @!slots = 1, 2, 3; %!map = (k => 'v') }
    method slots { @!slots }
    method map   { %!map }
}
my $h = Holder.new;
$h.slots[1] = 99;                  # through a plain METHOD, not an accessor
$h.map<n> = 'w';
check($h.slots.join(','), '1,99,3', 'write-through-a-plain-method');
check($h.map<n>,          'w',      'write-into-a-hash-a-method-answers');

class Cell is repr('CStruct') { has int64 $.v is rw }
my $cells = CArray[int64].new(0, 0);
check(($cells[1] = 7), '7', 'carray-element-assign');

role Store does Associative {
    has %!kept handles <AT-KEY EXISTS-KEY>;
    method STORE(::?CLASS:D: \what) { %!kept<last> = what; self }
    method last { %!kept<last> }
}
class WithStore { has %.box is Store }
my $ws = WithStore.new;
$ws.box = 'through-STORE';
check($ws.box.last, 'through-STORE', 'user-container-store');
my %alias := $ws.box;
check(%alias.last, 'through-STORE', 'associative-object-binds-to-a-percent-var');

class BuiltReq { has $.made is required; submethod BUILD { $!made = 'by-BUILD' } }
check(BuiltReq.new.made, 'by-BUILD', 'is-required-satisfied-by-BUILD');
class DfltReq { has $.d is required = 7 }
check(((try { DfltReq.new; 'no-throw' }) // $!.^name), 'X::Attribute::Required',
      'a-default-does-not-excuse-is-required');

# ---- 4. grammars ---------------------------------------------------------
grammar Toks {
    token normal      { <-[?]>+ }
    token placeholder { '?' }
    token TOP         { ^ ( <normal> | <placeholder> )* $ }
}
class TokActions {
    has $.n = 0;
    method normal($/)      { make ~$/ }
    method placeholder($/) { make '$' ~ ++$!n }
    method TOP($/)         { make $0.flatmap({ .values[0].ast }).join }
}
Toks.parse('a?b?c', :actions(TokActions.new));
check($/.ast, 'a$1b$2c', 'entry-rule-positional-list-captures');

grammar Escaped {
    rule TOP           { ^ <quoted> $ }
    rule quoted        { '"' $<value>=( [<-[\\"]> || '\"' || '\\\\']* ) '"' }
    rule unquoted      { <-["]>+ }
    rule either        { <quoted> | <unquoted> }
}
check(so Escaped.parse(Q["a\"b"]), 'True', 'ltm-prefix-ends-at-a-first-match-alt');

check(('abc' ~~ /(\w)*/).values.join(','), 'a,b,c', 'values-slips-a-list-capture');

my $heredoc = q:to/END/;
a\\b
END
check($heredoc.chars, '4', 'q-heredoc-unescapes-a-doubled-backslash');   # a \ b \n

# -------------------------------------------------------------------------
if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
