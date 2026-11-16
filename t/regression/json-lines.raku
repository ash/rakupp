# Regression: from-jsonl and to-jsonl, JSON Lines in JSON::Native
# (docs/dev/plans/JSONL-PLAN.md, issue 268). The pure-Raku pair in
# JSON::Native::Raku is the contract — from-json for each line, to-json(:!pretty)
# for each value — and every faster implementation is put through the same
# checks against it. Runs on Rakudo too, with -Irakulib.

use Test;
# this checkout's copy, by path: a bare `need` finds an installed JSON::Native
# first, which may predate from-jsonl (0.1.0 does)
use lib $?FILE.IO.parent(3).add('rakulib').Str;
need JSON::Native::Raku;

my &ref-from-json  = &JSON::Native::Raku::from-json;
my &ref-to-json    = &JSON::Native::Raku::to-json;

# name => (from-jsonl, to-jsonl): the reference, and on Raku++ the engine's
# codec routines, which hand what they do not cover to the reference
my @impls =
    'raku' => (&JSON::Native::Raku::from-jsonl, &JSON::Native::Raku::to-jsonl);
my &maker = try &::('rakupp-json-codec');
my $engine = so &maker && try &::('rakupp-from-jsonl');
@impls.push: 'engine' => (maker('from-jsonl', &JSON::Native::Raku::from-jsonl),
                          maker('to-jsonl',   &JSON::Native::Raku::to-jsonl)) if $engine;

# What a block raised, or Nil.
sub caught(&code) { try code(); $! }

plan @impls + 2;

for @impls -> (:key($name), :value(($from, $to))) {
    subtest $name, {
        # ---- from-jsonl: lines --------------------------------------------
        is-deeply $from("\{\"a\":1}\n[1,2]\n\"x\"\n"), [{a => 1}, [1, 2], 'x'],
            'one value a line';
        is-deeply $from('{"a":1}' ~ "\n" ~ '[true,null]'), [{a => 1}, [True, Any]],
            'a last line with no newline is a line';
        is-deeply $from("1\r\n2\r\n"), [1, 2], 'CRLF';
        is-deeply $from("1\r2\n3"), [1, 2, 3], 'a lone CR ends a line, as .lines has it';
        is-deeply $from("1\n\n2\n  \t \n3\n\n"), [1, 2, 3], 'blank and space-or-tab lines yield nothing';
        is-deeply $from('  {"a" : [ 1 ] }  '), [{a => [1]},], 'whitespace around a value is the value\'s';
        is-deeply $from(''), [], 'an empty window';
        is-deeply $from("\n\n"), [], 'a window of blank lines';
        is-deeply $from("0\n-1.5\n1e3\n\"\"\nfalse\n"), [0, -1.5, 1e3, '', False],
            'any JSON value, not only objects';
        is $from("1.5")[0].WHAT, Rat, 'decimals are Rat, as from-json has them';
        is $from("123456789012345678901234567890")[0], 123456789012345678901234567890,
            'big integers are exact';

        # ---- from-jsonl: Blob ----------------------------------------------
        is-deeply $from("\{\"k\":\"ж\"}\n[1]\n".encode), [{k => 'ж'}, [1]], 'a Blob is UTF-8';
        is-deeply $from(Buf.new("2\n3".encode)), [2, 3], 'a Buf too';
        my $decomposed = Blob.new(0x22, 0x65, 0xCC, 0x81, 0x22);   # "e" + U+0301
        is $from($decomposed)[0], "\x[E9]", 'a Blob\'s text is composed, as .decode composes it';
        is $from('"é"')[0], "\x[E9]", 'and so is a \u escape\'s';
        dies-ok { $from(Blob.new(0x22, 0xFF, 0x22)) }, 'malformed UTF-8 is refused';

        # ---- from-jsonl: :immutable ----------------------------------------
        my $imm = $from("\{\"a\":[1]}\n[2]", :immutable);
        isa-ok $imm, List, ':immutable gives a List';
        nok $imm ~~ Array, 'not an Array';
        isa-ok $imm[0], Map, 'of Maps';
        nok $imm[0] ~~ Hash, 'not Hashes';
        isa-ok $imm[1], List, 'and Lists';
        isa-ok $from("1"), Array, 'without it, an Array';

        # ---- from-jsonl: errors ----------------------------------------------
        my $e = caught({ $from("1\n\n[1,\n2") });
        isa-ok $e, X::JSON::Native::Line, 'a refused line raises X::JSON::Native::Line';
        is $e.line, 3, 'naming its line, blank lines counted';
        ok $e.exception ~~ Exception, 'carrying what from-json raised';
        ok $e.message.starts-with('line 3: '), 'and saying the line first';
        $e = caught({ $from("1 2") });
        is $e.line, 1, 'two values on one line are refused';
        isa-ok $e.exception, X::JSON::Native::AdditionalContent, 'as content after the document';
        $e = caught({ $from("1\n\x[A0]\n") });
        is $e.line, 2, 'a line of U+00A0 is not blank';
        dies-ok { $from("1", :allow-jsonc) }, 'an adverb it does not take is refused';

        # ---- to-jsonl -------------------------------------------------------
        is $to([{a => 1}, [1, 'x'], Any, 2.5]), qq<\{"a":1}\n[1,"x"]\nnull\n2.5\n>,
            'one compact value a line, each ending in a newline';
        is $to([]), '', 'an empty list is an empty string';
        is $to(['a' ~ "\n" ~ 'b']), Q<"a\nb"> ~ "\n", 'a newline in a string is escaped, so a value is one line';
        is $to([{b => 1, a => 2},], :sorted-keys), qq<\{"a":2,"b":1}\n>, ':sorted-keys';
        is $to([Order::Less], :enums-as-value), "-1\n", ':enums-as-value';
        is $to([1], :!pretty), "1\n", ':!pretty is what it does anyway';
        dies-ok { $to([1], :pretty) }, ':pretty is refused';
        dies-ok { $to([1], :spacing(4)) }, 'so is :spacing';

        # ---- the two together, against the definitions ----------------------
        my @docs = {a => [1, 2.5, True, Any], 'ключ' => 'знач'}, [], {}, 'str"q\\b',
                   -0.125, 1e-10, [[[]]], {x => {y => {z => 'deep'}}}, "\t\x[1F600]";
        my $text = $to(@docs);
        is $text, @docs.map({ ref-to-json($_, :!pretty) ~ "\n" }).join,
            'to-jsonl is to-json(:!pretty) for each value';
        is-deeply $from($text), [$text.lines.map({ ref-from-json($_) })],
            'from-jsonl is from-json for each line';
        is-deeply $from($text.encode), $from($text), 'from a Blob as from a Str';
    }
}

# The engine answers every window the reference would accept by itself: its
# routines, given a fallback that only counts, never call it on these.
subtest 'the engine answers on its own', sub {
    unless $engine { plan skip-all => 'from-jsonl is a Raku++ builtin'; return }
    my $asked = 0;
    my &from = maker('from-jsonl', sub (|) { $asked++; Nil });
    my &to   = maker('to-jsonl',   sub (|) { $asked++; Nil });
    from($_) for "\{\"a\":1}\n[1,2]\n", "1\r\n2\r3", "\n \t\n", '', "\"ж\"\n".encode,
                 Blob.new(0x22, 0x65, 0xCC, 0x81, 0x22), Buf.new("1\n".encode), "[1]\n";
    from("\{\"a\":[1]}", :immutable);
    to([{a => 1}, [1, 'x'], Any, 2.5]);
    to([{b => 1, a => 2},], :sorted-keys);
    to([]);
    is $asked, 0, 'not once';
    from("1\n["); to([1], :pretty); from("1", :allow-jsonc); to([Date.new(2026, 11, 16)]);
    to([Order::Less], :enums-as-value);   # as to-json's codec hands it over
    is $asked, 5, 'and hands over what it refuses, the whole call';
    is &from.name, 'from-jsonl', 'the routine is named for what it does';
}

# The bare primitives have nothing behind them, so they raise — X::AdHoc,
# saying where, as rakupp-from-json does.
subtest 'the bare primitives', sub {
    unless $engine { plan skip-all => 'from-jsonl is a Raku++ builtin'; return }
    my &from = &::('rakupp-from-jsonl');
    my &to   = &::('rakupp-to-jsonl');
    is-deeply from("1\n\n[2]"), [1, [2]], 'parse';
    is to([1, [2]]), "1\n[2]\n", 'and write';
    is caught({ from("1\n\n[1,\n2") }).message, 'from-jsonl: malformed JSON at line 3, column 4',
        'a malformed line, by line and column';
    is caught({ from("1 2") }).message, 'from-jsonl: trailing content after the value at line 1, column 3',
        'content after the value';
    like caught({ from(Blob.new(0x22, 0xFF, 0x22)) }).message, /Malformed/, 'malformed UTF-8, as .decode says it';
    like caught({ to([1], :pretty) }).message, /^ 'to-jsonl: '/, ':pretty';
}
