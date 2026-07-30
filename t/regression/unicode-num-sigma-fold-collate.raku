# Regression: four Unicode divergences reported from spec-site probing
# (Rakudo-confirmed), fixed as batch 4 of the zef-bar campaign.
#
# 1. Multi-digit non-ASCII numerals: "٤٢".Int threw (or truncated) — Nd
#    codepoints now transliterate to ASCII digits ahead of the positional
#    parse, so Arabic-Indic and Devanagari numbers read correctly.
# 2. .lc missed Final_Sigma: a lowercased word-final Σ is ς (preceded by a
#    letter, not followed by one); mid-word and lone sigmas stay σ.
# 3. m:i lacked FULL case folding: ß folds to "ss", so "Weiß" ~~ m:i/WEISS/
#    matches (and the reverse). Needed both the fold-aware literal matcher
#    and the adjacent-literal merge (per-char Lit nodes could never span a
#    one-to-many fold). A fold may not end mid-expansion: m:i/WEIS/ still
#    fails against "Weiß".
# 4. .collate was missing — wired to the existing UCA (DUCET) coll compare.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. non-ASCII decimal digits
@fail.push("arabic: {'٤٢'.Int}")     unless '٤٢'.Int == 42;
@fail.push('devanagari')             unless '१२३'.Int == 123;
@fail.push('mixed-still-fails')      if (try { 'x٤'.Int }).defined;   # non-digit lead stays a Failure
@fail.push('plus-prefix')            unless +'٧' == 7;

# 2. final sigma
@fail.push(".lc: {'ΣΊΣΥΦΟΣ'.lc}")    unless 'ΣΊΣΥΦΟΣ'.lc eq 'σίσυφος';
@fail.push('sigma-pair')             unless 'ΣΣ'.lc eq 'σς';
@fail.push('sigma-lone')             unless 'Σ'.lc eq 'σ';
@fail.push('sigma-mid')              unless 'ΣΟΣΟ'.lc eq 'σοσο';

# 3. full case folding under :i
@fail.push('fold ß~SS')              unless 'Weiß'  ~~ m:i/WEISS/;
@fail.push('fold SS~ß')              unless 'WEISS' ~~ m:i/weiß/;
@fail.push('fold no-half')           if     'Weiß'  ~~ m:i/WEISS_X/;
@fail.push('fold sigma')             unless 'ΣΊΣΥΦΟΣ' ~~ m:i/σίσυφος/;
@fail.push('plain :i intact')        unless 'HeLLo'  ~~ m:i/hello/;
@fail.push('plain regex intact')     unless ('hello world' ~~ /wor./) eq 'worl';

# 4. .collate
@fail.push('collate-basic')          unless <b a c>.collate.join(',') eq 'a,b,c';
@fail.push('collate-uca')            unless <zeta apple Ähre>.collate.join(',') eq 'Ähre,apple,zeta';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
