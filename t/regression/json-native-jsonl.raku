# Regression: `use JSON::Native` (answered by the engine) hands out from-jsonl
# and to-jsonl beside from-json and to-json (docs/dev/plans/JSONL-PLAN.md,
# issue 268): the codec as routines whose fallback is the JSON::Native::Raku
# this rakupp bundles. The bundled copy, not an installed or -I one: those can
# only be older than the engine — a newer JSON::Native makes the engine step
# aside — and JSON::Native 0.1.0, installed for Rakudo on a development
# machine, has no from-jsonl, so every refused line raised "this call needs
# JSON::Native::Raku, which did not load", and an --exe binary embedded that
# copy. Raku++ only.

use Test;

unless $*RAKU.compiler.name eq 'Raku++' {
    plan skip-all => 'the engine answering a `use` is a Raku++ mechanism';
    exit;
}

plan 7;

sub run-code(Str $code, *@args) {
    my $p = run $*EXECUTABLE, |@args, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $out.chomp
}

# an OLDER JSON::Native::Raku on -I: from-json only, as 0.1.0's is
my $old = $*TMPDIR.add("jsonl-old-{$*PID}");
$old.add('JSON/Native').mkdir;
$old.add('JSON/Native/Raku.rakumod').spurt: q:to/END/;
    module JSON::Native::Raku {
        our sub from-json($t, *%) { die "the old copy" }
        our sub to-json($v, *%)   { die "the old copy" }
    }
    END
LEAVE { try $old.add('JSON/Native/Raku.rakumod').unlink; try $old.add('JSON/Native').rmdir;
        try $old.add('JSON').rmdir; try $old.rmdir }

is run-code(q[use JSON::Native; print json-backend, ' ', from-jsonl("1\n[2]".encode).raku, ' ', to-jsonl([{a => 1},]).raku]),
   'engine [1, [2]] "\{\"a\":1}\n"', 'use JSON::Native hands out from-jsonl and to-jsonl, the engine answering';

is run-code(q[use JSON::Native <immutable sorted-keys pretty>; print from-jsonl("[1]").raku, ' ', to-jsonl([{b => 1, a => 2},]).raku]),
   '((1,),) "\{\"a\":2,\"b\":1}\n"', 'JSON::Fast\'s import options reach them, and <pretty> leaves to-jsonl one value a line';

is run-code(q[use JSON::Native <from-jsonl>; print from-jsonl("1").raku, ' ', (try EVAL 'to-jsonl([1])') // 'none']),
   '[1] none', 'they import by name';

is run-code(q[use Data::Native <json>; print from-jsonl("1\n2").raku, to-jsonl([3]).raku]),
   '[1, 2]"3\n"', 'use Data::Native <json> hands them out too';

is run-code(q{use JSON::Native; try from-jsonl("1\n["); print $!.^name, ' ', $!.line}),
   'X::JSON::Native::Line 2', 'a refused line is numbered by the fallback';

is run-code(q{use Data::Native <json>; try from-jsonl("1\n["); print $!.^name, ' ', $!.line}, '-I', $old.Str),
   'X::JSON::Native::Line 2', 'by the BUNDLED fallback, with an older JSON::Native::Raku on -I';

is run-code(q[need JSON::Native::Raku; print so &JSON::Native::Raku::from-jsonl], '-I', $old.Str),
   'False', 'while a program that needs JSON::Native::Raku itself still gets the -I copy';
