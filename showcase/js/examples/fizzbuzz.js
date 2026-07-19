// The classic, with a twist: rules live in data, not in an if-chain.
const rules = [
  { div: 15, word: "FizzBuzz" },
  { div: 3, word: "Fizz" },
  { div: 5, word: "Buzz" },
];

for (let i = 1; i <= 20; i++) {
  const rule = rules.find(r => i % r.div === 0);
  console.log(rule ? rule.word : i);
}
