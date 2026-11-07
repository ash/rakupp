# Regression: `rakupp --doc-links` links each construct of a program to the
# page on docs.raku.org that documents it (src/DocLinks.cpp).
#
# What goes wrong here goes wrong quietly: a method linked to the wrong type's
# page, an operator read as the wrong kind (prefix `-` as infix), a token the
# highlighter cut in pieces that the linker then misread — `<=>` arrived as
# `<`, `=`, `>` and was linked as an assignment, `->` and `++` were not linked
# at all, before plain spans were joined. Each case pins the label and the URL
# of one construct; the sweep at the end holds every link's offsets to the
# source bytes over examples/.
#
# `--doc-links` is rakupp's own mode (Rakudo has none), so the cases run here
# only.
#
# Contract: exit 0 + last line PASS.
my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
my $pp = $*RAKU.compiler.name eq 'Raku++';

if $pp {
    sub field(Str $line, Str $name) {
        return Nil unless $line ~~ / '"' $name '": "' ( [ <-["\\]> | '\\' . ]* ) '"' /;
        (~$0).subst(/ '\\' (.) /, { given ~$0 { when 'n' { "\n" }; when 't' { "\t" }; when 'r' { "\r" }; default { ~$0 } } }, :g)
    }
    sub number(Str $line, Str $name) {
        $line ~~ / '"' $name '": ' (\d+) / ?? +$0 !! Nil
    }
    sub doc-links(*@args) {
        my $p = run($*EXECUTABLE, |@args, :out, :err);
        my $out = $p.out.slurp(:close);
        $p.err.slurp(:close);
        $out, $p.exitcode
    }
    # one hash per ref of `--doc-links --json`, in source order
    sub refs(*@args) {
        my ($out) = doc-links('--doc-links', '--json', |@args);
        $out.lines.grep(*.contains('"text": ')).map(-> $l {
            %( :line($l), text => field($l, 'text'), group => field($l, 'group'),
               label => field($l, 'label'), url => field($l, 'url'),
               start => number($l, 'start'), end => number($l, 'end'),
               from => number($l, 'from'), to => number($l, 'to') )
        }).list
    }

    # code, the linked text (and which occurrence of it), label, URL tail
    my @cases =
        [q{my @w = 'raku is fun'.words;},       'words', 0, 'Str.words',          '/type/Str#routine_words'],
        [q{my @w = 1, 2; say @w.sort;},          'sort',  0, 'Array.sort',         '/type/List#routine_sort'],
        [q{my Int $n = 5; say $n.is-prime;},     'is-prime', 0, 'Int.is-prime',    '/type/Int#routine_is-prime'],
        [q{my $t = "a b".words.Bag; say $t.total;}, 'total', 0, 'Bag.total',       '/type/Baggy#method_total'],
        [q{sub f($s) { $s.uc }},                 'uc',    0, '.uc',                '/routine/uc'],
        [q{class P { has $.x }; say P.new(x => 1);}, 'new', 0, 'P.new',            '/type/Mu#method_new'],
        [q{my $a = 1; $a++;},                    '++',    0, 'postfix ++',         '/language/operators#postfix_++'],
        [q{my $a = 1; say -$a - 1;},             '-',     0, 'prefix -',           '/language/operators#prefix_-'],
        [q{my $a = 1; say -$a - 1;},             '-',     1, 'infix -',            '/language/operators#infix_-'],
        [q{say 1 <=> 2;},                        '<=>',   0, 'infix <=>',          '/language/operators#infix_%3C=%3E'],
        [q{my $a = 1; $a += 3;},                 '+=',    0, 'assignment +=',      '/language/operators#Assignment_operators'],
        [q{say 1 ?? 2 !! 3;},                    '??',    0, 'infix ?? !!',        '/language/operators#infix_%3F%3F_!!'],
        [q{my @a = 1, 2;},                       '=',     0, 'list assignment =',  '/language/operators#infix_=_(list_assignment)'],
        [q{my $s = 1;},                          '=',     0, 'item assignment =',  '/language/operators#infix_=_(item_assignment)'],
        [q{say [+] 1, 2;},                       '[+]',   0, 'reduction [+]',      '/language/operators#Reduction_operators'],
        [q{say (1, 2) Z+ (3, 4);},               'Z',     0, 'zip metaoperator Z', '/syntax/Z%20%28zip%20metaoperator%29'],
        [q{sub f(*@a) { +@a }},                  '*',     0, 'slurpy *@',          '/language/signatures#Flattening_slurpy:_*@'],
        [q{sub f(*@a) { +@a }},                  '+',     0, 'prefix +',           '/language/operators#prefix_+'],
        [q{for 1..3 -> $x { }},                  '->',    0, '->',                 '/syntax/-%3E'],
        [q{for 1..3 -> $x { }},                  '..',    0, 'infix ..',           '/language/operators#infix_%2E%2E'],
        [q{say $*PROGRAM-NAME;},                 '$*PROGRAM-NAME', 0, '$*PROGRAM-NAME', '/language/variables#$*PROGRAM-NAME'],
        [q{say 'abc' ~~ m:i/ \d+ /;},            ':i',    0, ':ignorecase',        '/syntax/%3Aignorecase'],
        [q{say 'abc' ~~ m:i/ \d+ /;},            '\d',    0, '\d',                 '/syntax/%5Cd'],
        [q{use Test; plan 1; ok 1;},             'plan',  0, 'plan',               '/type/Test#sub_plan'],
        [q{given 1 { when 1 { say 1 } }},        'given', 0, 'given',              '/syntax/given'],
        [q{say "a" x 3;},                        'x',     0, 'infix x',            '/language/operators#infix_x'],
        [q{say 'hi';},                           q{'hi'}, 0, 'q (single quotes)',  '/syntax/q'],
        # the parser keeps `»+«` as `>>+<<`; `pi` and `done` are bare terms, not calls
        [q{my @a = 1, 2; say @a »+« @a;},        '»+«',   0, 'hyper »+«',          '/language/operators#Hyper_operators'],
        [q{say pi;},                             'pi',    0, 'pi',                 '/language/terms#term_pi'],
        [q{react { whenever Promise.in(0) { done } }}, 'done', 0, 'done',          '/type/independent-routines#sub_done'],
        [q{sub MAIN() { }},                      'MAIN',  0, 'sub MAIN',           '/language/create-cli'],
        [q{say $*IN;},                           '$*IN',  0, '$*IN',               '/language/variables#Special_filehandles:_STDIN,_STDOUT_and_STDERR'],
        # a chain written over several lines keeps its types
        ["my @x = (1..5)\n    .grep(* > 1)\n    .sum;", 'sum', 0, 'Seq.sum',      '/type/Any#method_sum'];
    for @cases -> [$code, $text, $nth, $label, $tail] {
        my @hits = refs('-e', $code).grep(*<text> eq $text);
        my $r = @hits[$nth];
        check(($r // {})<label>, $label, "$code — label of {$text}#$nth");
        check(($r // {})<url>, "https://docs.raku.org$tail", "$code — url of {$text}#$nth");
    }

    # the program's own declarations link to their line, not to the docs
    {
        my @r = refs('-e', "class P \{ method m \{ 1 \} \}\nsay P.new.m;");
        my $m = @r.first(*<text> eq 'm');
        check(($m // {})<group>, 'local', 'own method: group');
        check(($m // {})<url>, '', 'own method: no url');
        check((($m // {})<line> // '').contains('"declLine": 1'), True, 'own method: declLine');
    }

    # a program that does not parse still gets the token layer
    {
        my ($out) = doc-links('--doc-links', '--json', '-e', 'say 1 +;');
        check($out.contains('"parsed": false'), True, 'parse error: parsed false');
        check($out.contains('"text": "say"'), True, 'parse error: say still linked');
    }

    # the formats
    {
        my ($text, $rc) = doc-links('--doc-links', '-e', 'say 1');
        check($rc, 0, 'text: exit 0');
        check(so $text.lines.head.contains('pages on docs.raku.org for'), True, 'text: summary line');
        check(so $text.lines.grep(/^ '  say' \s+ '1' \s+ 'https://docs.raku.org/type/independent-routines#sub_say' $/), True,
              'text: the say row');
        my ($html) = doc-links('--doc-links', '--html', '-e', 'say 1');
        check(so $html.contains('<a class="dl" data-k="'), True, 'html: a link in the code');
        check(so $html.contains('href="https://docs.raku.org/type/independent-routines#sub_say"'), True, 'html: the say href');
        my ($ansi) = doc-links('--doc-links', '--ansi', '-e', 'say 1');
        check(so $ansi.contains("\e]8;;https://docs.raku.org/type/independent-routines#sub_say\e\\"), True, 'ansi: an OSC 8 link');
        my ($first) = doc-links('--ansi', '--doc-links', '-e', 'say 1');
        check($first, $ansi, 'ansi: --ansi before --doc-links is the same');
        my ($, $both) = doc-links('--doc-links', '--html', '--ansi', '-e', 'say 1');
        check($both, 4, 'html and ansi together: exit 4');
        my ($, $json-html) = doc-links('--doc-links', '--json', '--html', '-e', 'say 1');
        check($json-html, 4, 'json and html together: exit 4');
    }

    # `rakupp doc FILE` is `rakupp --doc-links FILE`
    {
        my $f = $*TMPDIR.add("doc-links-{$*PID}.raku");
        $f.spurt("say 'hi'.uc;\n");
        my ($a) = doc-links('doc', $f.Str);
        my ($b) = doc-links('--doc-links', $f.Str);
        check($a, $b, 'rakupp doc FILE');
        check(so $a.contains('Str.uc'), True, 'rakupp doc FILE: a link');
        $f.unlink;
    }

    # every link, over the examples: in order, not overlapping, and its offsets
    # are the bytes (and the UTF-16 units) of the text it links
    my $root = $?FILE.IO.parent.parent.parent;
    for $root.add('examples').dir.grep(*.extension eq 'raku').sort -> $file {
        my $buf = $file.slurp(:bin);
        my $str = $file.slurp;
        my @u16-at;    # UTF-16 offset of each byte offset that starts a character
        my $u = 0; my $b = 0;
        for $str.comb -> $g {
            for $g.NFC.list -> $cp {
                @u16-at[$b] = $u;
                $b += chr($cp).encode.bytes;
                $u += $cp > 0xFFFF ?? 2 !! 1;
            }
        }
        @u16-at[$b] = $u;
        my $end = 0;
        for refs($file.Str) -> %r {
            unless %r<start> >= $end && %r<end> > %r<start> {
                check(%r<start>, "≥ $end", "{$file.basename}: ref order at {%r<text>}"); last;
            }
            $end = %r<end>;
            my $bytes = $buf.subbuf(%r<start>, %r<end> - %r<start>).decode;
            unless $bytes eq %r<text> { check($bytes, %r<text>, "{$file.basename}: bytes of a ref"); last }
            unless (@u16-at[%r<start>] // -1) == %r<from> && (@u16-at[%r<end>] // -1) == %r<to> {
                check((%r<from>, %r<to>), (@u16-at[%r<start>], @u16-at[%r<end>]), "{$file.basename}: UTF-16 span of {%r<text>}"); last;
            }
        }
    }
}

say $ok ?? 'PASS' !! 'FAIL';
exit($ok ?? 0 !! 1);
