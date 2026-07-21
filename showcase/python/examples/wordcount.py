# Dicts, sorting by (count desc, word asc), f-strings, str methods.
text = "the quick brown fox the lazy dog the fox jumps the dog"
counts = {}
for word in text.split():
    counts[word] = counts.get(word, 0) + 1

for word in sorted(counts, key=lambda w: (-counts[w], w)):
    print(f"{word:<6} {counts[word]}")
