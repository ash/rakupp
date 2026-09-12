#!/usr/bin/env raku
# The ORACLE half of the `--rakuast` tree dump (RAKUAST-PLAN Part III).
#
#     raku tools/rakuast-oracle-dump.raku [--attrs] FILE
#
# Prints Rakudo's own RakuAST tree for FILE in the serialization `rakupp
# --rakuast FILE` prints, so comparing the two engines is a shell `diff` and
# polishing the view is: pick a file, diff, fix the first differing line, repeat.
#
# THE SERIALIZATION
#   * one node per line, two spaces of indent per depth;
#   * the class name with `RakuAST::` stripped;
#   * children in `visit-children` order — which is source order;
#   * with `--attrs`, the node's syntax-bearing scalar attributes follow as
#     ` key=value`, sorted by key: strings .raku-quoted, Ints plain, Bools
#     True/False.
#
# WHY `visit-children` AND NOT THE ATTRIBUTES. The first version of this tool
# walked every attribute holding a node, sorted by name, on the grounds that
# name-anchored order is deterministic on both engines without either side
# reproducing the other's traversal. That was wrong, and measurably so: a
# RakuAST node is not a tree node. It links UPWARD and sideways — a declaration
# knows its containing block, a statement list knows its comp unit, a resolver
# hangs off the unit — so an attribute walk drags the whole graph in and the
# dump for a 12-line program came to 45 nodes, of which 19 were the same handful
# of blocks reached from underneath. `visit-children` is the syntactic children,
# in source order, and it is what the plan asked for.
#
# WHICH ATTRIBUTES COUNT under `--attrs`. Rakudo's nodes carry compiler state
# beside syntax — `$!sorries`, `$!resolution`, `$!parse-performed` — and a view
# must not reproduce any of it. The plan's rule is the TWO-POSITION TEST: an
# attribute belongs only if the same statement parsed from a different line and
# column carries the same value. This tool applies it directly rather than
# keeping a table: it parses the source twice, once with two blank lines in
# front, walks both trees in lockstep and emits only the attributes that agree.
#
# The test does not in fact do the job, which is why attributes are off by
# default here and in `--rakuast`: most of Rakudo's compiler state is POSITION-
# INVARIANT — `begin-performed`, `parse-performed`, `sunk`, `okifnil`,
# `meta-object-produced` and two dozen more are the same in both parses — so the
# test keeps them, and an attribute-level number would be dominated by state a
# view is not supposed to have. Shape is the honest measure until a better rule
# exists; `--attrs` prints the rest for anyone wanting to look.

use experimental :rakuast;

sub type-of(Mu $v) { (try $v.^name) // Str }   # NQP-level values (SCRef, BOOTHash) answer nothing
sub scalar-ish(Mu $v) {
    my $t = type-of($v);
    return False unless $t.defined;
    # DEFINED scalars only. An attribute holding a bare type object is an UNSET
    # compiler slot (`fatal=Bool`, `sorries=Mu`), and it survives the
    # two-position test precisely because it is the same nothing in both parses
    # — so the test alone does not exclude it and this does.
    return False unless (try $v.defined) // False;
    $t eq any(<Str Int Bool Num Rat IntStr>)
}
sub render-value(Mu $v) {
    my $t = type-of($v);
    return "?" unless $t.defined;
    return ($v ?? "True" !! "False") if $t eq "Bool";
    return ((try $v.raku) // "?")   if $t eq "Str";
    return ((try ~$v) // "?")       if $t eq any(<Int Num Rat IntStr>);
    $t
}
sub is-node(Mu $v) {
    my $t = type-of($v);
    $t.defined && $t.starts-with("RakuAST::") && ((try $v.defined) // False)
}

# The attributes of $n as (name => value) with the `$!` stripped, sorted by name.
sub attrs(Mu $n) {
    return () unless $n.defined;
    my @out;
    for $n.^attributes -> $a {
        my $name = $a.name.substr(2);       # `$!foo` -> `foo`
        my $v = try $a.get_value($n);
        @out.push($name => $v);
        CATCH { default { } }
    }
    @out.sort(*.key)
}

# The syntactic children, in order.
sub kids(Mu $n) {
    my @k;
    try $n.visit-children(-> Mu $c { @k.push($c) if is-node($c) });
    @k
}

# The RESOLUTION APPARATUS, skipped with its subtree. Rakudo attaches a
# `VarDeclaration::Implicit::*` for a block's topic and its `$/`, `$!`, `$_`,
# and in a `:compunit` tree a `Type::Setting` + `Declaration::External::*` pair
# for every built-in name the program mentions — what Rakudo needs to COMPILE
# the program rather than what the program SAYS. A view of the syntax will never
# build them, so counting them would make the published percentage a measure of
# that decision rather than of fidelity.
sub apparatus(Str $cls) {
    $cls.starts-with("Origin")
      || $cls.starts-with("IMPL::")
      || $cls.starts-with("VarDeclaration::Implicit")
      || $cls.starts-with("Declaration::External")
      || $cls eq "Type::Setting"
}

sub dump(Mu $a, Mu $b, $depth, @lines) {
    return if $depth > 60;
    my $cls = $a.^name.subst("RakuAST::", "");
    return if apparatus($cls);

    my @scalars;
    if $*ATTRS {
        my %bv = ($b.defined ?? attrs($b).list !! ()).map({ .key => .value });
        for attrs($a) -> $p {
            next unless scalar-ish($p.value);
            # The two-position test: keep it only if the second parse agrees.
            next unless (try render-value(%bv{$p.key})) eqv (try render-value($p.value));
            @scalars.push(" {$p.key}=" ~ render-value($p.value));
        }
    }
    @lines.push('  ' x $depth ~ $cls ~ @scalars.join);

    my @bk = kids($b);
    for kids($a).kv -> $i, $c {
        dump($c, (@bk[$i] // Nil), $depth + 1, @lines);
    }
}

my $attrs = so @*ARGS.grep("--attrs");
my $file = @*ARGS.first({ !.starts-with("--") }) // die "usage: rakuast-oracle-dump.raku [--attrs] FILE";
my $src  = slurp $file;
my $a = $src.AST;
# The second parse is ONLY for the two-position test, so it only happens under
# `--attrs`. It is not free: `.AST` compiles in the caller's scope the way EVAL
# does, so a program that declares a package declares it HERE — and parsing the
# same source twice in one process made eighteen of the corpus's fifty-nine
# programs die with "Redeclaration of symbol 'JSON'", which looked exactly like
# the oracle refusing the program and was the tool refusing itself.
my $b = $attrs ?? ("\n\n" ~ $src).AST !! Nil;
my @lines;
my $*ATTRS = $attrs;
dump($a, $b, 0, @lines);
.say for @lines;
