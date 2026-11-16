# Regression (issue 267): the engine's JSON::Native READS an IO::Path, and an
# IO::Handle to its end, leaving the handle open. `from-json("doc.json".IO)`
# handed the path to the Raku fallback, whose Str() parsed the NAME, and the
# error pointed at the `d` of "doc.json". from-jsonl took a path the same way.
# The codec now reads the argument in place, so a hand-over to the fallback —
# for a bad document — gets the contents: a handle cannot be read twice.
# Anything that is not IO still binds through Str().

use Test;

my &engine = try &::('rakupp-from-jsonl');
unless &engine {
    plan skip-all => 'the engine codec is a Raku++ builtin';
    exit;
}

plan 12;

my $dir = $*TMPDIR.add("json-reads-paths-{$*PID}");
$dir.mkdir;
my $doc  = $dir.add('doc.json');   $doc.spurt: '{"ok":true,"n":[1,2.5]}';
my $jl   = $dir.add('doc.jsonl');  $jl.spurt: "1\n[2]\n";
my $bad  = $dir.add('bad.json');   $bad.spurt: '[1,';
my $badl = $dir.add('bad.jsonl');  $badl.spurt: "1\n[2,\n";
LEAVE { try .unlink for $doc, $jl, $bad, $badl; try $dir.rmdir }

{
    use JSON::Native;
    is-deeply from-json($doc), {ok => True, n => [1, 2.5]}, 'use JSON::Native: from-json reads an IO::Path';
    my $h = $doc.open;
    is from-json($h)<ok>, True, 'and an IO::Handle';
    ok $h.opened, 'which it leaves open';
    $h.close;
    is-deeply from-jsonl($jl), [1, [2]], 'from-jsonl reads an IO::Path';
    $h = $jl.open;
    is-deeply from-jsonl($h), [1, [2]], 'and an IO::Handle';
    $h.close;

    my $e = do { try from-json($bad); $! };
    ok $e.defined && !$e.message.contains('bad.json'), 'a bad file is refused for its contents, not its name';
    $h = $badl.open;
    $e = do { try from-jsonl($h); $! };
    is $e.?line, 2, 'a refused line from a handle is numbered on what the handle gave';
    $h.close;
    is from-json(42), 42, 'anything else still binds through Str()';
}

is &::('rakupp-from-json')($doc)<ok>, True, 'the bare rakupp-from-json reads a path too';
is-deeply &::('rakupp-from-jsonl')($jl), [1, [2]], 'and rakupp-from-jsonl';
my &codec = &::('rakupp-json-codec')('from-json', sub (|c) { 'FALLBACK ' ~ c.list[0].^name });
is codec($bad), 'FALLBACK Str', 'the codec hands the fallback the contents, not the path';
{
    use Data::Native <json>;
    is from-json($doc)<ok>, True, 'use Data::Native <json>: from-json reads a path';
}
