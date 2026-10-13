# Regression: `use JSON::Native` is answered by the engine — no module file is
# read for it, installed or not — and what it hands out is EXACT: the codec as
# routines whose fallback is JSON::Native::Raku (found installed, on -I, or
# else bundled in this binary), so a call the codec does not cover is answered
# as JSON::Fast answers it. JSON::Native is a drop-in for JSON::Fast, so its
# import list takes JSON::Fast's options too. Raku++ only.

use Test;

unless $*RAKU.compiler.name eq 'Raku++' {
    plan skip-all => 'the engine answering a `use` is a Raku++ mechanism';
    exit;
}

plan 11;

sub run-with(Str $code, *%env) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err, :env(%(%*ENV, %env, :RAKUPP_TRACE<1>));
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($out.chomp, $err)
}

{
    my ($out, $err) = run-with q[use JSON::Native; print json-backend, ' ', json-backend(:write), ' ', to-json(from-json('{"a":[1,2.5,1e3]}'), :!pretty)];
    is $out, 'engine engine {"a":[1,2.5,1000e0]}', 'the engine answers both directions';
    nok $err.contains('[Load] JSON::Native'), 'and reads no file of the distribution to do it';
}

{
    my ($out, $err) = run-with q[use JSON::Native; print to-json(Date.new(2026, 10, 13)), ' ', (try from-json('[1] x')) // $!.^name];
    is $out, '"2026-10-13" X::JSON::Native::AdditionalContent', 'what the codec does not cover is answered by the fallback';
    ok $err.contains('[Load] JSON::Native::Raku'), 'which is loaded only then';
}

is run-with(q[use JSON::Native <!pretty sorted-keys>; print to-json({b => 1, a => 2})])[0],
   '{"a":2,"b":1}', 'JSON::Fast\'s import options set the defaults';
is run-with(q[use JSON::Native <immutable>; print from-json('[1]').^name, ' ', from-json('{}').^name])[0],
   'List Map', '<immutable> too';
is run-with(q[use JSON::Native <!pretty>; print to-json([1], :pretty)])[0],
   "[\n  1\n]", 'and an adverb at the call wins over the default';
is run-with(q[use JSON::Native <from-json>; print from-json('[1]').elems, ' ', (try EVAL 'to-json(1)') // 'no to-json'])[0],
   '1 no to-json', 'a list of names imports those names';
ok run-with(q[use JSON::Native <bogus>])[1].contains("no such name 'bogus'"), 'an unknown name is refused';

{
    my ($out, $err) = run-with q[use JSON::Native; print json-backend], JSON_NATIVE_BACKEND => 'raku';
    ok $err.contains('[Load] JSON::Native') || $out ne 'engine',
       'JSON_NATIVE_BACKEND naming another path leaves `use` to the distribution';
}

is run-with(q[use JSON::Native; print to-json("a\n\x[301]", :!pretty)])[0], "\"a\\\x[144]\"",
   'the codec is the fixed one: an escape followed by a combining mark composes';
