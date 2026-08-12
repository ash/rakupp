# The Raku half of the grammar service (docs/dev/plans/GRAMMAR-PLAN.md, G0).
#
# A host binding rk_eval's this file once, then reaches everything below with
# rk_call. The subs keep their state in the mainline lexicals here — an
# interpreter is a session, so the compiled-grammar cache lives exactly as
# long as the host's interpreter does.
#
# Handles that cross the boundary:
#   grammar -> an Int id into the slot table. Nothing to root, nothing to leak.
#   match   -> the Match value itself. The HOST roots it (rk_root) and owns it.
#
# Every sub dies on misuse. Through the ABI a die becomes rk_error(), which a
# binding turns into its own exception type — that is the whole error contract
# in G0; parse-position diagnostics are G1.
#
# Grammar and actions are named EXPLICITLY and resolved as a trailing
# expression inside the same EVAL as the source, so they are lexical, fresh,
# and loud when missing. Not ::() — `(try ::('X')) // Any` can never work
# because a type object is undefined by nature, so `//` would always discard
# it. (::() on a missing name also used to manufacture a stub type; it throws
# X::NoSuchSymbol now, but the trailing-expression form stays the right tool.)

my %grammar-ids;    # cache key -> id: the same (source, name, actions) compiles once
my @grammar-slots;  # id -> { grammar => Type, actions => Type or Any }
my $compile-seq = 0; # every compile ATTEMPT gets a fresh wrapper package name
my %unnamed-seen;   # grammar ^name -> True for UNNAMED compiles, which have no
                    # wrapper package: a same-name recompile there would rebind
                    # every earlier handle to the new body, so it is refused

# Bumped when a sub here changes signature or meaning; a binding checks it
# right after loading so a shim/binding skew fails at startup, not mid-parse.
sub rk-shim-abi() { 1 }

# ---- compile ---------------------------------------------------------------
# $name:    the grammar's name in the source. May be '' when the source's LAST
#           statement is the grammar declaration — then the EVAL's own value
#           is taken instead.
# $actions: the actions class's name in the same source, or '' for none.
#           Needs $name (both ride the same trailing expression).
sub rk-grammar-compile(Str $source, Str $name, Str $actions) {
    my $key = $name ~ "\x[1]" ~ $actions ~ "\x[1]" ~ $source;
    return %grammar-ids{$key} if %grammar-ids{$key}:exists;

    my $g;
    my $a = Any;
    if $name eq '' {
        die "rk-grammar-compile: actions '$actions' needs the grammar's name too"
            if $actions ne '';
        $g = EVAL $source;
    }
    else {
        # Every named compile lives in its own wrapper package, so recompiling
        # an EDITED grammar under the same name can never collide with an
        # earlier compile — the engine refuses same-name redeclaration
        # (X::Redeclaration, as Rakudo does), and a held handle must keep the
        # body it was compiled from. The wrapper also means error messages
        # show the type as e.g. RKGRAMMAR0::Log; the sequence counter is
        # per-ATTEMPT, so a failed compile cannot poison the next one's name.
        my $pkg = 'RKGRAMMAR' ~ $compile-seq++;
        my $qn = $pkg ~ '::' ~ $name;
        my $qa = $actions eq '' ?? 'Any' !! $pkg ~ '::' ~ $actions;
        ($g, $a) = EVAL 'package ' ~ $pkg ~ " \{\n" ~ $source ~ ";\n\};\n"
                        ~ '((' ~ $qn ~ '), (' ~ $qa ~ '))';
    }
    die "rk-grammar-compile: { $name eq '' ?? "the source's last statement" !! "'$name'" } " ~
        "is not a type object (got { $g.raku })" if $g.defined;
    die "rk-grammar-compile: { $name eq '' ?? "the source's last statement" !! "'$name'" } " ~
        "is not a grammar — it has no parse method" unless $g.can('parse');
    die "rk-grammar-compile: actions '$actions' is not a type object" if $a.defined;
    if $name eq '' {
        my $tn = $g.^name;
        die "rk-grammar-compile: grammar '$tn' was already compiled from different source; "
            ~ "pass its name to compile isolated versions" if %unnamed-seen{$tn};
        %unnamed-seen{$tn} = True;
    }

    my $id = @grammar-slots.elems;
    @grammar-slots.push({ grammar => $g, actions => $a });
    %grammar-ids{$key} = $id;
    $id
}

# ---- parse -----------------------------------------------------------------
# Returns the Match, or Any when the parse fails — G1 turns that Any into
# a position and a rule name. A fresh actions instance per parse, so action
# state never leaks between inputs.
sub rk-grammar-parse(Int $id, Str $input, Str $rule) {
    my $slot = @grammar-slots[$id] // die "rk-grammar-parse: no grammar #$id";
    my $g = $slot<grammar>;
    my $m = do
        if $slot<actions>.^name ne 'Any' {
            $rule eq '' ?? $g.parse($input, :actions($slot<actions>.new))
                        !! $g.parse($input, :rule($rule), :actions($slot<actions>.new));
        }
        else {
            $rule eq '' ?? $g.parse($input)
                        !! $g.parse($input, :rule($rule));
        };
    $m // Any
}

# ---- lazy access -----------------------------------------------------------
# One call per LEAF, however deep: the host accumulates ['entry', 3, 'key']
# and asks once. Str steps are named captures, Int steps are positional
# captures or quantifier-list indexes — the same distinction Raku's own
# $m<x> / $m[0] makes.
#
# Ops on the resolved node:
#   str int num bool  - the node coerced; str/int/num die on a missing node,
#                       bool answers False instead (that is how a host asks
#                       "is it there?")
#   elems             - list length; 1 for a bare node, 0 for a missing one
#   islist            - is this a quantified capture (a list of matches)?
#                       Iteration needs the distinction: [0] on a bare Match
#                       is its first POSITIONAL capture, never the Match itself
#   made              - the actions' .made value (Any when none)
#   match             - the node itself, for the host to root
#   tree              - eager conversion, see rk-match-tree
sub rk-match-walk($m, @path, Str $op) {
    my $cur = $m;
    for @path -> $step {
        $cur = $step ~~ Int ?? $cur[$step] !! $cur{$step};
    }
    given $op {
        when 'str'   { $cur.defined ?? ~$cur    !! die "rk-match-walk: nothing at [{@path.join(' ')}]" }
        when 'int'   { $cur.defined ?? $cur.Int !! die "rk-match-walk: nothing at [{@path.join(' ')}]" }
        when 'num'   { $cur.defined ?? $cur.Num !! die "rk-match-walk: nothing at [{@path.join(' ')}]" }
        when 'bool'  { $cur.defined && ?$cur }
        when 'elems' { !$cur.defined ?? 0 !! ($cur ~~ Positional ?? $cur.elems !! 1) }
        when 'islist' { ?($cur.defined && $cur ~~ Positional) }
        when 'made'  { $cur.defined ?? $cur.made !! Any }
        when 'match' { $cur // Any }
        when 'tree'  { rk-match-tree($cur) }
        default      { die "rk-match-walk: unknown op '$op'" }
    }
}

# ---- parse-failure diagnostics (G1) ----------------------------------------
# After rk-grammar-parse returned Any, this answers WHERE and WHAT: the
# furthest position a named rule failed at (the engine's highwater), as
# { pos, line, col, rule } — line/col are 1-based and computed from the same
# input string the host parsed. Any when the engine has no diagnosis (or the
# last parse on this thread succeeded). Rule-grained: the position is where
# the deepest failing RULE started, not the exact character (G1's documented
# grain).
sub rk-grammar-diagnosis(Str $input) {
    my $d = rakupp-parse-diagnosis();
    return Any unless $d.defined;
    my $pos = $d<pos>;
    my $before = $input.substr(0, $pos min $input.chars);
    my $nl = $before.rindex("\n");
    {
        pos  => $pos,
        line => 1 + $before.comb("\n").elems,
        col  => $pos - ($nl // -1),
        rule => $d<rule>,
    }
}

# ---- eager conversion ------------------------------------------------------
# The 3×-the-parse conversion, opt-in by design (the measurements are in
# GRAMMAR-PLAN.md). Shape: a node with no captures is its text; a node with
# captures is a hash — named captures under their names, positional under
# '0', '1', …; a quantified capture is a list. The node's own text is NOT
# kept once it has children; ask the lazy path for that.
sub rk-match-tree($m) {
    return Any unless $m.defined;
    return $m.map({ rk-match-tree($_) }).Array if $m ~~ Positional;
    return $m unless $m ~~ Match;
    my %named = $m.hash;
    my @pos   = $m.list;
    return ~$m if !%named.elems && !@pos.elems;
    my %out;
    for %named.kv -> $k, $v { %out{$k}  = rk-match-tree($v) }
    for @pos.kv   -> $i, $v { %out{~$i} = rk-match-tree($v) }
    %out
}
