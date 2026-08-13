// The Rust guide's example: compile a grammar from a .raku file, parse text,
// and move the results into Rust. From a checkout (BUILD = a directory
// holding librakupp):
//
//   RAKUPP_LIB_DIR=$BUILD cargo run --example shopping
//
// (run from bindings/rust, or add --manifest-path bindings/rust/Cargo.toml)

use rakulang::{Error, Grammar, Tree};

fn main() -> Result<(), Error> {
    let grammar = concat!(env!("CARGO_MANIFEST_DIR"), "/../examples/shopping.raku");
    let g = Grammar::from_file(grammar, "Shopping", "ShoppingActions")?;

    // parse returns Ok(None) if the grammar does not match
    let m = g.parse("milk=2\nbread=1\neggs=12\n", "")?.expect("the list matches");

    let items = m.get("item");
    println!("{} items", items.len()?);
    for i in 0..items.len()? {
        // lazy: one engine call per leaf
        let item = items.at(i);
        println!("{} x {}", item.get("name").str()?, item.get("qty").int()?);
    }

    // ShoppingActions made this
    if let Tree::Int(total) = m.made()? {
        println!("total, computed in Raku: {}", total);
    }

    println!("as plain Rust data: {:?}", m.tree()?);

    // parse gives Ok(None) on a non-match; parse_strict diagnoses it instead
    if let Err(Error::Parse { line, col, rule, .. }) = g.parse_strict("milk=2\nbread=lots\n", "") {
        println!("line {} column {} while trying <{}>", line, col, rule);
    }

    Ok(())
} // m drops here — the rooted engine value unroots itself
