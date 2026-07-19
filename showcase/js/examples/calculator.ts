// A four-function calculator: tokenizer, recursive-descent parser, evaluator —
// a small language implemented inside the language js.raku is implementing.

type Token = { kind: string; value: string };

function tokenize(src: string): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === " ") {
      i++;
    } else if ((c >= "0" && c <= "9") || c === ".") {
      let num = "";
      while (i < src.length && ((src[i] >= "0" && src[i] <= "9") || src[i] === ".")) {
        num += src[i];
        i++;
      }
      tokens.push({ kind: "num", value: num });
    } else {
      tokens.push({ kind: "op", value: c });
      i++;
    }
  }
  return tokens;
}

class Parser {
  private pos: number = 0;

  constructor(private tokens: Token[]) {}

  private peek(): Token {
    return this.pos < this.tokens.length
      ? this.tokens[this.pos]
      : { kind: "eof", value: "" };
  }

  private eat(): Token {
    return this.tokens[this.pos++];
  }

  // expr := term (('+' | '-') term)*
  expr(): number {
    let value = this.term();
    while (this.peek().value === "+" || this.peek().value === "-") {
      const op = this.eat().value;
      const rhs = this.term();
      value = op === "+" ? value + rhs : value - rhs;
    }
    return value;
  }

  // term := factor (('*' | '/') factor)*
  private term(): number {
    let value = this.factor();
    while (this.peek().value === "*" || this.peek().value === "/") {
      const op = this.eat().value;
      const rhs = this.factor();
      value = op === "*" ? value * rhs : value / rhs;
    }
    return value;
  }

  // factor := number | '(' expr ')'
  private factor(): number {
    const t = this.peek();
    if (t.value === "(") {
      this.eat();
      const value = this.expr();
      this.eat(); // the closing ')'
      return value;
    }
    return Number(this.eat().value);
  }
}

function calc(src: string): number {
  return new Parser(tokenize(src)).expr();
}

const inputs = ["1 + 2 * 3", "(1 + 2) * 3", "10 / 4 - 1", "2 * (3 + 4) * 5", "100 - 5 * 5"];
for (const expr of inputs) {
  console.log(`${expr} = ${calc(expr)}`);
}
