#!/usr/bin/env raku
# A quine: this program prints its own source exactly and reads nothing
# from disk. The whole source is stored once in the string $s, and printf
# feeds $s back into itself, filling in the quote marks and the text.
my $s = '#!/usr/bin/env raku
# A quine: this program prints its own source exactly and reads nothing
# from disk. The whole source is stored once in the string $s, and printf
# feeds $s back into itself, filling in the quote marks and the text.
my $s = %c%s%c;
printf $s, 39, $s, 39;
';
printf $s, 39, $s, 39;
