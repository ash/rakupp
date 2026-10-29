# Helper for t/regression/tagged-code-constant-export.raku: code constants
# and a code variable exported under tags, beside a sub for comparison.
unit module RakuppTaggedCodeConst;

sub drop-spaces($x) { "ds($x)" }

sub tagged-sub() is export(:shortcuts) { 'tagged sub' }
constant &sp is export(:shortcuts, :ALL) = &drop-spaces;
constant &pFive is export(:extra) = { 5 };
constant &always is export(:MANDATORY) = { 'always' };
constant &plain is export = { 'plain' };
our &counter is export(:extra) = { 'counter' };
