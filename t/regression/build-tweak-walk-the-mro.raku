# Regression: the construction protocol runs the BUILD of EVERY class in the
# MRO — least-derived first, each exactly once — and then walks it again for
# TWEAK, as Rakudo's BUILDALL does. rakupp used to run only the MOST-derived
# one, so a parent that initialised its own attributes never ran.
#
# Reported as issue #72: `fez login` answered "FATAL: Failed to login: unknown
# error" on a login that had in fact succeeded. fez's Fez::Types is
# `class auth-response is api-response`; the child's BUILD sets $!key and
# leaves $!success to the parent's, and with the parent BUILD skipped every
# API response read back as unsuccessful. `unknown error` is literally what
# that class answers for `.message` when `.success` is False.
#
# Each row below builds its OWN classes, so a fix that repairs one shape by
# breaking another cannot hide here, and every expectation records the ORDER
# and the COUNT — an engine that runs the right BUILDs the wrong number of
# times, or in the wrong order, fails rather than passes.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape: the parent's BUILD initialises the parent's attrs ---
{
    class api-response {
        has Bool $!success;
        has Str $!message;
        submethod BUILD(:$!success = False, :$!message = Nil) {}
        method success { $!success // False }
        method message { $!message // (self.success ?? '' !! 'unknown error') }
    }
    class auth-response is api-response {
        has $!key;
        submethod BUILD(:$!key, *@_, *%_) { nextsame }
        method key(--> Str) { $!key // '' }
    }
    my $r = auth-response.new(success => True, key => 'K123', message => 'ok');
    ck($r.success, True,    'the parent BUILD ran, so the inherited attribute is set');
    ck($r.key,     'K123',  'and the child BUILD still set its own');
    ck($r.message, 'ok',    'the parent BUILD bound every attribute it declares');

    my $bad = auth-response.new(success => False, message => 'nope');
    ck($bad.message, 'nope', 'a genuinely failed response keeps its own message');
    ck($bad.key,     '',     'and has no key');
}

# --- order and count: least-derived first, exactly once each ----------------
{
    my @log;
    class OP     { submethod BUILD(*%_) { @log.push('OP')  } }
    class OC is OP { submethod BUILD(*%_) { @log.push('OC') } }
    class OD is OC { submethod BUILD(*%_) { @log.push('OD') } }
    OD.new;
    ck(@log, ['OP', 'OC', 'OD'], 'a three-deep chain builds parent-first, once each');
}

# --- `nextsame` in a BUILD is a no-op, not a second run and not an error ----
{
    my @log;
    class NP       { submethod BUILD(*%_) { @log.push('NP') } }
    class NC is NP { submethod BUILD(*%_) { @log.push('NC'); nextsame } }
    NC.new;
    ck(@log, ['NP', 'NC'], 'nextsame inside BUILD does not re-run the ancestor');
}
{
    class RP       { submethod BUILD(*%_) {} }
    class RC is RP { submethod BUILD(*%_) { nextsame } }
    ck(RC.new.defined, True, 'nextsame inside BUILD is not X::NoDispatcher');
}

# --- multiple inheritance follows the REVERSED .^mro ------------------------
{
    my @log;
    class MA          { submethod BUILD(*%_) { @log.push('MA') } }
    class MB          { submethod BUILD(*%_) { @log.push('MB') } }
    class MC is MA is MB { submethod BUILD(*%_) { @log.push('MC') } }
    MC.new;
    ck(MC.^mro.map(*.^name).head(3).List, ('MC', 'MA', 'MB'),
       'the MRO of a two-parent class is C, A, B');
    ck(@log, ['MB', 'MA', 'MC'], 'and BUILD runs it reversed');
}

# --- a diamond builds the shared ancestor ONCE ------------------------------
{
    my @log;
    class DTop            { submethod BUILD(*%_) { @log.push('DTop') } }
    class DL is DTop      { submethod BUILD(*%_) { @log.push('DL')  } }
    class DR is DTop      { submethod BUILD(*%_) { @log.push('DR')  } }
    class DBot is DL is DR { submethod BUILD(*%_) { @log.push('DBot') } }
    DBot.new;
    ck(@log.grep('DTop').elems, 1, 'the shared ancestor of a diamond builds once');
    ck(@log.tail, 'DBot', 'and the most-derived class builds last');
}

# --- TWEAK walks the same way ----------------------------------------------
{
    my @log;
    class TP       { submethod TWEAK(*%_) { @log.push('TP') } }
    class TC is TP { submethod TWEAK(*%_) { @log.push('TC') } }
    TC.new;
    ck(@log, ['TP', 'TC'], 'TWEAK walks the MRO parent-first too');
}

# --- BUILD and TWEAK INTERLEAVE per class, they are not two separate walks ---
# An ancestor's TWEAK runs before a descendant's BUILD. Getting this wrong is
# the natural way to implement the walk (all the BUILDs, then all the TWEAKs),
# so the row is here to catch exactly that.
{
    my @log;
    class BP       { submethod BUILD(*%_) { @log.push('BP-build') };
                     submethod TWEAK(*%_) { @log.push('BP-tweak') }; }
    class BC is BP { submethod BUILD(*%_) { @log.push('BC-build') };
                     submethod TWEAK(*%_) { @log.push('BC-tweak') }; }
    BC.new;
    ck(@log, ['BP-build', 'BP-tweak', 'BC-build', 'BC-tweak'],
       'each class runs its own BUILD then its own TWEAK, parent first');
}

# --- …including when only one of the pair is declared at each level ---------
{
    my @log;
    class OnlyT           { submethod TWEAK(*%_) { @log.push('OnlyT-tweak') }; }
    class OnlyB is OnlyT  { submethod BUILD(*%_) { @log.push('OnlyB-build') }; }
    OnlyB.new;
    ck(@log, ['OnlyT-tweak', 'OnlyB-build'],
       "an ancestor's TWEAK precedes a descendant's BUILD");
}

# --- `is required` is judged after a class's own BUILD, before its own TWEAK -
{
    class ReqB { has $.need is required; submethod BUILD { $!need = 'by-build' }; }
    ck(ReqB.new.need, 'by-build', 'an attribute the class\'s own BUILD filled counts as supplied');
    class ReqT { has $.need is required; submethod TWEAK { $!need //= 'by-tweak' }; }
    ck((try { ReqT.new.need } // $!.^name), 'X::Attribute::Required',
       'but a TWEAK that would fill it comes too late');
}

# --- a child with NO BUILD of its own still gets the parent's --------------
{
    class IP       { has $!v; submethod BUILD(:$!v = 'dflt') {}; method v { $!v } }
    class IC is IP { }
    ck(IC.new(v => 'given').v, 'given', 'an inherited BUILD runs for a child that declares none');
    ck(IC.new.v, 'dflt', 'and its defaults still apply');
}

# --- a BUILD that dies still unwinds (the frame is popped on the way out) ---
{
    class EP       { submethod BUILD(*%_) { die 'boom' } }
    class EC is EP { submethod BUILD(*%_) { } }
    ck((try { EC.new; 'no-throw' } // $!.message), 'boom', 'a throwing ancestor BUILD propagates');
    # …and the engine is still usable afterwards
    class ZP       { has $!z; submethod BUILD(:$!z = 7) {}; method z { $!z } }
    class ZC is ZP { submethod BUILD(*%_) { } }
    ck(ZC.new.z, 7, 'and construction still works after one threw');
}

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails ?? 1 !! 0;
