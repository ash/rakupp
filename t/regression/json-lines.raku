# Regression: from-jsonl and to-jsonl, JSON Lines in JSON::Native
# (docs/dev/plans/JSONL-PLAN.md, issue 268). The pure-Raku pair in
# JSON::Native::Raku is the contract — from-json for each line, to-json(:!pretty)
# for each value — and every faster implementation is put through the same
# checks against it. Runs on Rakudo too, with -Irakulib.

use Test;
need JSON::Native::Raku;

my &ref-from-json  = &JSON::Native::Raku::from-json;
my &ref-to-json    = &JSON::Native::Raku::to-json;

# name => (from-jsonl, to-jsonl). Phase 1 adds the engine's.
my @impls =
    'raku' => (&JSON::Native::Raku::from-jsonl, &JSON::Native::Raku::to-jsonl);

# What a block raised, or Nil.
sub caught(&code) { try code(); $! }

plan +@impls;

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
