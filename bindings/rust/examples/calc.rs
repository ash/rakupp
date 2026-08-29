// The Rust guide's second example: Raku as a library, no grammar involved.
// Evaluate source, call subs with Rust values, read results back. From a
// checkout (BUILD = a directory holding librakupp):
//
//   RAKUPP_LIB_DIR=$BUILD cargo run --example calc
//
// (run from bindings/rust, or add --manifest-path bindings/rust/Cargo.toml)

use rakulang::{call, eval, Error, Tree};

// The examples print numbers and strings, never the Tree wrapper — a small
// helper keeps the formatting in one place.
fn show(t: &Tree) -> String {
    match t {
        Tree::Null => String::new(),
        Tree::Bool(b) => b.to_string(),
        Tree::Int(i) => i.to_string(),
        Tree::Num(n) => n.to_string(),
        Tree::Str(s) => s.clone(),
        Tree::List(l) => l.iter().map(show).collect::<Vec<_>>().join(" "),
        Tree::Map(_) => format!("{:?}", t),
    }
}

fn main() -> Result<(), Error> {
    // eval runs source in the mainline scope and keeps it, like the REPL.
    println!("2 + 2 = {}", show(&eval("2 + 2")?));

    // So loading a file of subs is just an eval — they stay callable below.
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../examples/calc.raku");
    eval(&std::fs::read_to_string(path).expect("calc.raku"))?;

    // call passes Rust values as arguments and returns a Tree.
    println!("area(3, 4) = {}", show(&call("area", &[3.into(), 4.into()])?));

    // a Raku list -> Tree::List
    println!("primes below 30: {}", show(&call("primes-below", &[30.into()])?));

    // a Vec<Tree> -> a Raku list; a Raku hash -> Tree::Map
    let nums: Vec<Tree> = [3, 1, 4, 1, 5, 9, 2, 6].iter().map(|&n| Tree::Int(n)).collect();
    let s = call("stats", &[nums.into()])?;
    if let Tree::Map(m) = &s {
        println!("stats: count={} sum={} mean={} max={}",
                 show(&m["count"]), show(&m["sum"]), show(&m["mean"]), show(&m["max"]));
    }

    let who: std::collections::BTreeMap<String, Tree> =
        [("name".to_string(), "Ada".into()), ("age".to_string(), 36.into())].into();
    println!("greet: {}", show(&call("greet", &[who.into()])?));

    // Raku integers do not overflow; past 64 bits one crosses as Tree::Str digits.
    println!("30! = {}", show(&call("factorial", &[30.into()])?));

    // A die inside Raku crosses as Error::Raku, the host's own error type.
    if let Err(Error::Raku(msg)) = call("checked-div", &[10.into(), 0.into()]) {
        println!("died: {}", msg);
    }

    Ok(())
}
