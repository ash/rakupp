# BROKE: leg 22 — X::Backslash::UnrecognizedSequence for unassigned alpha
# escapes also threw for \c, whose bracket forms (\c[LINE FEED]) are
# processed AFTER the escape switch; three S02-literals files went GONE
# (char-by-name.t, char-by-number.t, quoting.t — caught by gate av1).
# FIXED: same leg — c/x/o are exempt in the unrecognized-escape check.
die '\c[NAME] broken'  unless "\c[LINE FEED]" eq "\n";
die '\x41 broken'      unless "\x41" eq 'A';
die '\o101 broken'     unless "\o101" eq 'A';
die '\u must still be an error' unless (try EVAL q["\u"]) === Nil;
say 'PASS';
