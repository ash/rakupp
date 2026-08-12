// Linking: RAKUPP_LIB_DIR names the directory holding librakupp (a build
// directory or an install's lib/). Without it, the system linker paths are
// tried — right for an installed library, loud and clear when there is none.
fn main() {
    println!("cargo:rerun-if-env-changed=RAKUPP_LIB_DIR");
    if let Ok(dir) = std::env::var("RAKUPP_LIB_DIR") {
        println!("cargo:rustc-link-search=native={}", dir);
        // the run-time search path too, so `cargo run`/`cargo test` work
        // without LD_LIBRARY_PATH gymnastics
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir);
    }
    println!("cargo:rustc-link-lib=dylib=rakupp");
}
