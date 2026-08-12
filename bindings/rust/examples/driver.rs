// The Rust half of the byte-identical gate: the same walk as driver.py,
// through the rakulang crate. Run by grammar-smoke.raku; outputs are
// compared byte for byte.
//
//   RAKUPP_LIB_DIR=$BUILD cargo run --example driver <grammar> <input>

use rakulang::{Error, Grammar, Tree};

fn canon(t: &Tree) -> String {
    match t {
        Tree::List(v) => format!("[{}]", v.iter().map(canon).collect::<Vec<_>>().join(",")),
        Tree::Map(m) => format!( // BTreeMap iterates key-sorted, matching the twins
            "{{{}}}",
            m.iter().map(|(k, v)| format!("{}:{}", k, canon(v))).collect::<Vec<_>>().join(",")),
        Tree::Null => String::new(),
        Tree::Bool(b) => if *b { "True".into() } else { "False".into() },
        Tree::Int(i) => i.to_string(),
        Tree::Num(n) => n.to_string(),
        Tree::Str(s) => s.clone(),
    }
}

fn raku_bool(b: bool) -> &'static str {
    if b { "True" } else { "False" }
}

fn main() -> Result<(), Error> {
    let args: Vec<String> = std::env::args().collect();
    let (grammar_file, input_file) = (&args[1], &args[2]);

    let g = Grammar::from_file(grammar_file, "Log", "LogActions")?;
    let input = std::fs::read_to_string(input_file).expect("input");
    let m = g.parse(&input, "")?.expect("gate: the log corpus did not parse");

    let mut out = String::new();
    let lines = m.get("line");
    let n = lines.len()?;
    out += &format!("lines {}\n", n);
    out += &format!("islist {}\n", raku_bool(lines.is_list()?));

    for i in 0..n {
        out += &format!("{} {}\n", lines.at(i).get("ip").str()?, lines.at(i).get("status").str()?);
    }

    out += &format!("made {}\n", canon(&m.get("line").at(0).get("size").made()?));
    out += &format!("req.str {}\n", m.get("line").at(42).get("req").str()?);
    out += &format!("size.int {}\n", m.get("line").at(42).get("size").int()?);
    out += &format!("missing {}\n", raku_bool(m.get("nope").truthy()?));
    out += &format!("tree {}\n", canon(&m.get("line").at(999).tree()?));

    let one = g.parse("7.7.7.7 - - [x] \"GET / HTTP/1.1\" 200 5\n", "line")?.expect("rule parse");
    out += &format!("rule-parse {}\n", one.get("status").str()?);

    out += &format!("failed-parse {}\n",
                    if g.parse("this is not a log line", "")?.is_some() { "Match" } else { "None" });

    match g.parse_strict("this is not a log line", "") {
        Err(Error::Parse { line, col, rule, .. }) =>
            out += &format!("diag line {} col {} rule {}\n", line, col, rule),
        _ => out += "diag none\n",
    }

    print!("{}", out);
    Ok(())
}
