# A hash, known to be one only because it is subscripted with { }.
my text = 'the cat and the hat and the bat';
my freq;
freq{$_}++ for text.words;
for freq.sort({ -.value, .key }) -> p { say p.key, ' ', p.value }
