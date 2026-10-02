# Issue #116: Sparky's web UI answered 500 on its first page — "Type check
# failed in assignment to $!else; expected Cro::WebApp::Template::AST::Node
# but got YAMLish::Node". Three faults stood between Sparky and its page:
#
#   1. A module's UNEXPORTED type was published to GLOBAL under its short
#      name. `role Node` inside `unit module YAMLish` is YAMLish::Node, and
#      nothing else; published as `Node`, it replaced the `Node` that Cro's
#      AST module exported, and Cro's template builder — loaded first — typed
#      its variables with YAMLish's role from then on. (Exported types kept
#      their flag only on a cold load: the AST cache dropped `is export` on a
#      class, so the second run is part of the check.)
#   2. `Cursor`, the setting's old name for Match, was unknown, so Cro's
#      `has Cursor $.cursor` refused the Match its syntax error carries.
#   3. Proto dispatch ran a candidate's code while RANKING: two prefix-model
#      gaps (a `:my` in a candidate, the built-in <ident>) sent Cro's `arg`
#      proto to the probe, which ran by-pos's `|| <.panic>` on `:$m`.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# 1. two modules, one short name: the importer keeps the one it imported
{
    my $dir = $*TMPDIR.add("issue116-{$*PID}");
    $dir.mkdir;
    $dir.add('I116Yaml.rakumod').spurt: q:to/END/;
        unit module I116Yaml;
        role Node { }
        class Thing does Node { }
        END
    $dir.add('I116Ast.rakumod').spurt: q:to/END/;
        unit module I116Ast;
        role Node is export { }
        my class Cond does Node is export { has Node $.else; }
        my class Lit is export { }
        END
    $dir.add('I116Builder.rakumod').spurt: q:to/END/;
        use I116Ast;
        class I116Builder {
            method build() {
                my Node $else-part = Nil;
                (Node.^name, $else-part.^name, Cond.new(else => $else-part).else.^name,
                 Lit.^name).join(' ')
            }
        }
        END
    my $code = 'use I116Builder; use I116Yaml; say I116Builder.build';
    my $want = 'I116Ast::Node I116Ast::Node I116Ast::Node I116Ast::Lit';
    for <cold cached> -> $run {
        my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
        my $out = $p.out.slurp(:close).trim;
        my $err = $p.err.slurp(:close);
        check $out, $want, "$run run: Node resolves to the imported role{$err ?? " ($err.lines.head())" !! ''}";
    }
    # the unexported role is reachable by its qualified name
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', 'use I116Yaml; say I116Yaml::Node.^name', :out, :err);
    check $p.out.slurp(:close).trim, 'I116Yaml::Node', 'unexported role by its long name';
    $p.err.slurp(:close);
    for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
    $dir.rmdir;
}

# 2. Cursor is Match
{
    check Cursor.^name, 'Match', 'Cursor.^name';
    my class HoldsCursor { has Cursor $.c }
    check (try HoldsCursor.new(c => 'a' ~~ /a/).c.Str) // $!.^name, 'a', 'a Cursor attribute holds a Match';
}

# 3. proto dispatch never runs a candidate's code to rank it
{
    grammar Args {
        token TOP { '<.' <deref> '>' }
        token identifier { <.ident> [ <[-']> <.ident> ]* }
        token deref { <deref-item>+ % '.' }
        proto token deref-item { * }
        token deref-item:sym<method> { <identifier> <arglist> }
        token deref-item:sym<smart> { <.identifier> }
        token arglist { '(' \s* <arg>* % [\s* ',' \s*] \s* ')' \h* }
        proto token arg { * }
        token arg:by-pos { <expression> }
        token arg:by-name {
            :my $negated = False;
            ':'
            [
            | $<var-name>=['$' <identifier>]
            | [ $<negated>='!' { $negated = True } ]? <identifier> [ '(' ~ ')' <expression> ]?
            ]
        }
        rule expression { <!before ')'> [ <term> || { die "panicked" } ] }
        proto token term { * }
        token term:sym<integer> { '-'? \d+ }
        token term:sym<variable> { '$' <.identifier> }
    }
    for '<.f($m)>', '<.f(:$m)>', '<.f(:m(1))>', '<.f(1, :!x)>' -> $src {
        check (try { Args.parse($src) ?? 'parsed' !! 'no match' }) // $!.message, 'parsed', "Args.parse('$src')";
    }
    # `:my` does not end a declarative prefix; a bare block does
    grammar MyPrefix {
        proto token a {*}
        token a:sym<y> { 'ab' }
        token a:sym<x> { :my $q = 1; 'abc' }
    }
    grammar CodePrefix {
        proto token a {*}
        token a:sym<y> { 'ab' }
        token a:sym<x> { {} 'abc' }
    }
    check MyPrefix.subparse('abc', :rule<a>).Str, 'abc', ':my keeps the prefix going';
    check CodePrefix.subparse('abc', :rule<a>).Str, 'ab', 'a code block ends the prefix';
    # <ident> and <alnum> rank by what they match
    grammar Builtins {
        proto token t {*}
        token t:sym<short> { 'x' }
        token t:sym<ident> { :my $n; 'x' <ident> }
        token t:sym<alnum> { :my $n; 'x' <alnum> <alnum> }
    }
    check Builtins.subparse('xyz-', :rule<t>).Str, 'xyz', '<ident> outranks a shorter literal';
    check Builtins.subparse('x1z', :rule<t>).Str, 'x1z', '<alnum> ranks one character each';
}

if @fail { .say for @fail; say 'FAIL' }
else { say 'PASS' }
