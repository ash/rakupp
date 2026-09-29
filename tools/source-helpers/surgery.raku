#!/usr/bin/env rakupp
# Cut Builtins.cpp's two giant functions into pieces, before the file is split.
#
#   rakupp tools/source-helpers/surgery.raku IN OUT
#
# Its output is what builtins.plan splits. The new functions need their
# declarations in Interpreter.h (next to methodCallPart2 and registerBuiltins).
#
# methodCallInner: its last arms move into new ordered segments, the way
#   methodCallPart2/3/Tail were split out before — each takes (inv, m, args,
#   rwArgs) and returns nullopt for "not handled here". A cut is only legal where
#   the code below it uses none of methodCallInner's own locals.
# registerBuiltins: each piece ends by calling the next, so the registrations run
#   in exactly the original order.
#
# The cuts are given by the TEXT of the line they fall before, not a line number,
# so the script can be re-run on a newer Builtins.cpp.

my @mci-cuts = (
    # segment name, the first line it takes (a substring of it)
    ['methodCallPart1b', 'if (inv.t == VT::Type && (inv.s == "Pointer" || inv.s.rfind("Pointer[", 0) == 0) &&'],
    ['methodCallPart1c', 'if (inv.t == VT::Hash && inv.hashKind == "Cancellation" && m == "can")'],
);
my $mci-end = '// Segment continues in MethodCallPart2.cpp — same ordered chain.';

my @reg-cuts = (
    ['registerBuiltinsPart2', '// (the EVAL moves the current line into its own text: a failure is reported'],
    ['registerBuiltinsPart3', '// the sub forms delegate to the methods so they share the char-based'],
    ['registerBuiltinsPart4', '// :16("2e") radix conversion — the value\'s digits parsed in the given base'],
    ['registerBuiltinsPart5', '// `sleep-until $instant` sleeps until that moment and answers whether it'],
);
# locals of registerBuiltins that a later piece still uses: re-declared there
my %reg-locals = valAllomorph => 'auto valAllomorph = [](const Value& v) -> Value { return rakupp::valAllomorph(v); };';

sub MAIN(Str $in, Str $out) {
    my @l = $in.IO.lines;

    # ---- locate a function's body: its header line .. the closing `}` in column 0
    sub body(Str $head) {
        my $a = @l.first({ .starts-with($head) }, :k) // die "no function starting '$head'";
        my $z = ($a ^.. @l.end).first({ @l[$_] eq '}' }) // die "no end for '$head'";
        ($a, $z)
    }
    sub find(Str $text, Int $from, Int $to) {
        my @hit = ($from .. $to).grep({ @l[$_].trim-leading.starts-with($text) });
        die "cut text found {+@hit} times, want once: $text" unless @hit == 1;
        # a cut takes the comment block just above its line with it
        my $c = @hit[0];
        $c-- while @l[$c - 1] ~~ /^ \s+ '//'/;
        $c
    }

    my @outl;
    my ($ma, $mz) = body('Value Interpreter::methodCallInner(');
    my $mend = find($mci-end, $ma, $mz);
    my @mc = @mci-cuts.map({ [.[0], find(.[1], $ma, $mend)] });
    my ($ra, $rz) = body('void Interpreter::registerBuiltins()');
    my @rc = @reg-cuts.map({ [.[0], find(.[1], $ra, $rz)] });

    # methodCallInner up to the first cut, then the calls into the new segments
    @outl.append: @l[^@mc[0][1]];
    @outl.push: '    // Segments continue in ' ~ @mc.map({ .[0] }).join(' and ')
              ~ ' (MethodCallPart1b.cpp, MethodCallPart1c.cpp) — same ordered chain.';
    @outl.push: "    if (auto r = {.[0]}(inv, m, args, rwArgs)) return std::move(*r);" for @mc;
    @outl.append: @l[$mend .. $mz];
    # the segments themselves
    for @mc.kv -> $k, $seg {
        my $from = $seg[1];
        my $to = $k < @mc.end ?? @mc[$k + 1][1] - 1 !! $mend - 1;
        @outl.push: '';
        @outl.push: "// Segment {$seg[0].subst('methodCallPart', '')} of the method-dispatch chain, split out of methodCallInner for";
        @outl.push: "// compile time. The chain is ORDER-SENSITIVE (an earlier arm shadows a later";
        @outl.push: "// one), so these arms run after the ones above and before "
                  ~ ($k < @mc.end ?? @mc[$k + 1][0] !! 'methodCallPart2') ~ '. nullopt = "not handled here".';
        @outl.push: "std::optional<Value> Interpreter::{$seg[0]}(const Value& inv, const MName& m, ValueList& args,";
        @outl.push: ' ' x "std::optional<Value> Interpreter::{$seg[0]}(".chars ~ 'const std::vector<ExprPtr>* rwArgs) {';
        @outl.append: @l[$from .. $to];
        @outl.push: '    return std::nullopt;   // not handled here — fall through to the next segment';
        @outl.push: '}';
    }

    # everything between methodCallInner and registerBuiltins
    @outl.append: @l[$mz ^..^ $ra];

    # registerBuiltins, in pieces
    my @starts = ($ra, |@rc.map(*.[1]));
    for @starts.kv -> $k, $s {
        my $e = $k < @starts.end ?? @starts[$k + 1] - 1 !! $rz - 1;
        if $k == 0 {
            @outl.append: @l[$s .. $e];
        }
        else {
            my $name = @rc[$k - 1][0];
            @outl.push: '';
            @outl.push: "// registerBuiltins, continued. Split for compile time: each piece ends by";
            @outl.push: "// calling the next, so the registrations run in the original order (a later";
            @outl.push: "// one of the same name still replaces an earlier one).";
            @outl.push: "void Interpreter::{$name}() \{";
            @outl.push: '    auto& B = builtins_;';
            my $body = @l[$s .. $e].join("\n");
            for %reg-locals.kv -> $n, $decl {
                @outl.push: "    $decl" if $body ~~ / <!after ['::' | \w]> $n <|w> /;   # unqualified uses only
            }
            @outl.append: @l[$s .. $e];
        }
        if $k < @starts.end {
            @outl.push: "    {@rc[$k][0]}();";
            @outl.push: '}';
        }
    }
    @outl.push: '}';
    @outl.append: @l[$rz ^.. @l.end];
    spurt $out, @outl.join("\n") ~ "\n";
    note "methodCallInner: cut at lines {@mc.map({ .[1] + 1 }).join(', ')}; registerBuiltins: cut at {@rc.map({ .[1] + 1 }).join(', ')}";
}
