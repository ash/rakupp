# Regression: the general fixes behind HTTP::UserAgent's zef suite
# (2026-08-05, session 3 continuation):
#   1. `is` with a JUNCTION as the GOT argument autothreads
#      (`is any(@names), 'a'` collapses per the junction's kind).
#   2. A hash composer may contain a HYPER method call: the dot in `>>.trim`
#      calls on the term before the marker, not on the topic —
#      `{ a => 1, b => $s.split(',')>>.trim }` is a Hash, not a Block.
#   3. `.subst(:g, 'x', 'y')` — named adverbs are position-independent; the
#      pattern is the first POSITIONAL argument.
#   4. "\x41"/"\x[263a]"/"\o[17]" decode inside DOUBLE-quoted regex spans
#      (single-quoted spans still keep the backslash).
#   5. Actions fire for completed subrules even when the OVERALL parse fails
#      (Rakudo fires during the match and does not unfire) — and `$<child>.made`
#      is visible to parent actions in that replay.
#   6. quoteMetaRx escapes every ASCII non-alphanumeric — an interpolated
#      `$file` holding `?r=1&r=2` must not become a regex CONJUNCTION.
#   7. `$.attr = v` enforces the attribute's declared type, subsets included,
#      reporting the QUALIFIED name (`has RequestMethod $.m is rw` rejects
#      a value outside the subset).
#   8. A phaser keyword with a TIGHT paren is CALL syntax: `POST($u, |%n)`
#      inside a module calls the user's `sub POST`, it is not a POST phaser
#      echoing its statement's value.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. junction got in `is` — probe via the same comparison Test.is does
{
    my @names = <a b>;
    @fail.push('junction-got') unless ?(any(@names) eq 'a');
}

# 2. hyper method call inside a hash composer
{
    my $s = "x, y";
    my $h = { a => 1, b => $s.split(',')>>.trim };
    @fail.push('composer-hyper') unless $h ~~ Hash && $h<b>[1] eq 'y';
}

# 3. leading :g adverb on subst
{
    @fail.push('subst-adverb-first') unless 'aQbQc'.subst(:g, 'Q', '*') eq 'a*b*c';
    @fail.push('subst-adverb-last') unless 'aQbQc'.subst('Q', '*', :g) eq 'a*b*c';
}

# 4. \x escapes in quoted regex spans
{
    @fail.push('qx20') unless ' ' ~~ /"\x20"/;
    @fail.push('qx41-mid') unless 'ab' ~~ /"a\x62"/;
    @fail.push('qx-bracket') unless 'A' ~~ /"\x[41]"/;
    @fail.push('sq-literal') if 'a' ~~ /'\x61'/;   # single quotes: no escapes
    grammar MT {
        token TOP { <sp> 'x' }
        token sp { "\x20" }
    }
    @fail.push('grammar-x20') unless MT.parse(' x');
}

# 5. actions fire on a failed parse, bottom-up mades included
{
    my @log;
    grammar G {
        token TOP { [ <message-header> \n ]* }
        token message-header { $<field-name>=[ <-[:]>+ ] ':' <field-value> }
        token field-value { \h* $<content>=[ \S+ ] }
    }
    my class A {
        method field-value($/) { make ~$<content> }
        method message-header($/) { @log.push(~$<field-name> => $<field-value>.made) }
    }
    my $m = G.parse('ETag: xyz', :actions(A));   # no trailing \n -> parse fails
    @fail.push('failed-parse-nil') if $m.defined;
    @fail.push('failed-parse-action') unless @log == 1 && @log[0].key eq 'ETag';
    @fail.push('failed-parse-made') unless @log[0].value eq 'xyz';
}

# 6. interpolated metachars stay literal
{
    my $file = '/cat/b.a?r=1&r=2';
    @fail.push('interp-amp') unless "POST /cat/b.a?r=1&r=2 HTTP/1.1" ~~ /^POST\s$file/;
}

# 7. subset-typed rw attribute rejects out-of-subset values
{
    my subset Meth of Str where any(<GET POST>);
    my class R {
        has Meth $.m is rw;
        method set($v) { $.m = $v.uc }
    }
    my $r = R.new(m => 'GET');
    $r.set('post');
    @fail.push('subset-attr-ok') unless $r.m eq 'POST';
    my $died = False;
    { $r.set('nope'); CATCH { default { $died = True } } }
    @fail.push('subset-attr-reject') unless $died;
}

# 8. phaser keyword with tight paren is a call
{
    my sub POST($u, *%n) { "called:$u" }
    my $r = POST('u1', x => 1);
    @fail.push('post-call') unless $r eq 'called:u1';
}

if @fail {
    say "FAILED: @fail[]";
    say "FAIL";
    exit 1;
}
say "PASS";
