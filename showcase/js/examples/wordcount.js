// String and object work: count words, then report the top five.
const text =
  "the quick brown fox jumps over the lazy dog " +
  "the dog barks and the fox runs the fox wins";

const counts = {};
for (const word of text.split(" ")) {
  counts[word] = (counts[word] ?? 0) + 1;
}

const top = Object.entries(counts)
  .sort((a, b) => b[1] - a[1])
  .slice(0, 5);

console.log("total words:", text.split(" ").length);
console.log("unique words:", Object.keys(counts).length);
for (const [word, n] of top) {
  console.log(word.padEnd(8), "#".repeat(n), n);
}
