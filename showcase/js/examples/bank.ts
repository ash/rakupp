// TypeScript: everything here that tsc would erase, js.raku erases too —
// and enums, the one TS construct with runtime behaviour, actually run.

enum TxKind { Deposit, Withdrawal }

interface Tx {
  kind: TxKind;
  amount: number;
}

type Report = { balance: number; count: number };

class Account {
  protected txs: Tx[] = [];

  constructor(public owner: string, protected balance: number = 0) {}

  deposit(amount: number): void {
    this.balance += amount;
    this.txs.push({ kind: TxKind.Deposit, amount: amount });
  }

  withdraw(amount: number): void {
    if (amount > this.balance) {
      throw new Error(`insufficient funds: have ${this.balance}, want ${amount}`);
    }
    this.balance -= amount;
    this.txs.push({ kind: TxKind.Withdrawal, amount: amount });
  }

  report(): Report {
    return { balance: this.balance, count: this.txs.length };
  }
}

class SavingsAccount extends Account {
  constructor(owner: string, private rate: number) {
    super(owner, 0);
  }
  addInterest(): void {
    this.deposit(this.balance * this.rate);
  }
  report(): Report {
    const base = super.report();
    return { balance: Math.round(base.balance * 100) / 100, count: base.count };
  }
}

function describe<T>(label: string, value: T): T {
  console.log(label + ":", value);
  return value;
}

const acct = new SavingsAccount("Ada", 0.05);
acct.deposit(100);
acct.deposit(150);
acct.withdraw(30);
acct.addInterest();

describe<Report>("Ada's account", acct.report());
console.log("owner:", acct.owner, "| kind names:", TxKind[0], TxKind[1]);

try {
  acct.withdraw(1000000);
} catch (e) {
  console.log("caught:", (e as { message: string }).message);
}
