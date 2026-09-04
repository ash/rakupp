#!/usr/bin/env raku
# Rank builtins and methods by how many corpus programs call them — the order
# in which they enter the --target=js runtime (TRANSPILE-PLAN.md, P1: "the
# order is not a guess"). Walks `rakupp --ast` output, so it counts what the
# parser saw, not what a grep would.
#
#   rakupp tools/js/rank-builtins.raku                 # t/regression + examples + showcases
#   rakupp tools/js/rank-builtins.raku examples        # a directory or files
#   rakupp tools/js/rank-builtins.raku --missing       # only names the JS runtime lacks
#
# Output: two tables (subs, methods), "programs  name", most-used first.

my $ROOT = $*PROGRAM.parent.parent.parent;
my $missing-only = so @*ARGS.grep('--missing');
my @args = @*ARGS.grep(* ne '--missing');
my @dirs = @args ?? @args.map(*.IO)
                 !! ($ROOT.add('t/regression'), $ROOT.add('examples'), $ROOT.add('showcase'));
my @files;
for @dirs -> $d {
    if $d.d {
        @files.append: $d.dir.grep({ .extension eq 'raku' }).sort(*.Str);
        @files.append: $d.dir.grep(*.d).map({ .dir.grep({ .extension eq 'raku' }).Slip }).sort(*.Str);
    }
    elsif $d.f { @files.push: $d }
}

my (%subs, %methods);
for @files -> $f {
    my $p = run $*EXECUTABLE.Str, '--ast', $f.Str, :out, :err;
    my $ast = $p.out.slurp(:close);
    next if $p.exitcode != 0;
    my (%s, %m);
    for $ast.lines {
        if / ^ \s* 'Call ' (<-[\s(]>+) / { %s{~$0} = 1 }
        elsif / ^ \s* 'MethodCall ' <[.!?]>? (<-[\s(]>+) / { %m{~$0} = 1 }
    }
    %subs{$_}++ for %s.keys;
    %methods{$_}++ for %m.keys;
}

# what the runtime answers today: the emitter's builtin table and the method tables
my %have-sub;
my %have-method;
if $missing-only {
    my $cpp = $ROOT.add('src/codegen/Js.cpp').slurp;
    my $block = $cpp.substr($cpp.index('const std::set<string> kBuiltins = {'));
    $block = $block.substr(0, $block.index('};'));
    %have-sub{$_} = 1 for $block.comb(/ '"' <-["]>+ '"' /).map(*.substr(1, *-1));
    my $rt = $ROOT.add('src/js-rt').dir(:test(/\.js$/)).map(*.slurp).join;
    # method tables are `name: (s…` or `'na-me': (s…` entries
    %have-method{$_} = 1 for $rt.comb(/ [ ^^ | <[{,]> ] \s* [ "'" (<-[']>+) "'" | (<[\w-]>+) ] \s* ':' \s* '(' /).map({ .comb(/ <[\w-]>+ /).head });
}

sub table($title, %count, %have) {
    say "# $title (programs calling it)";
    for %count.sort({ -.value, .key }) -> $p {
        next if $missing-only && %have{$p.key};
        say sprintf("%5d  %s", $p.value, $p.key);
    }
    say "";
}
table('subs', %subs, %have-sub);
table('methods', %methods, %have-method);
