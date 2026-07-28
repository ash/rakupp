# FAQ

Short, task-shaped answers to questions people actually ask — "how do I …",
"why does this print twice", "does Rakudo do the same".

These are not a tutorial and not a reference. [COOKBOOK.md](../COOKBOOK.md) has
runnable one-liners by topic; [REFERENCE.md](../REFERENCE.md) has every operator,
sub and method. This is for the questions in between, where the answer is a
paragraph and a caveat rather than a signature.

**Every snippet is run on both Raku++ and Rakudo and produces identical output.**
Where the two genuinely differ, the page says so and explains why — those
differences are the most useful thing here, since they are what a Rakudo user
trips over.

## Pages

- **[shell.md](shell.md)** — running external commands: `run` vs `shell`,
  capturing output, feeding input, exit codes, and what a `Proc` shows you.

## Adding one

Answer a question someone actually asked, in the shape they asked it. Verify
every snippet against both engines before it goes in — a FAQ that is wrong is
worse than no FAQ, because it is what people copy. If Raku++ and Rakudo differ,
say so plainly rather than quietly writing to whichever one is convenient.
