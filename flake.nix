{
  # The Nix twin of the Guix packaging (.guix/, PR #6), and the answer to
  # issue #5: NixOS cannot run the generic prebuilt Linux binary (it has no
  # global ELF interpreter), so NixOS users build from source through this
  # flake instead:
  #
  #   nix run github:ash/rakupp -- -e 'say 42'    # run without installing
  #   nix profile install github:ash/rakupp       # put rakupp on PATH
  #   nix build                                   # from a checkout → ./result/bin/rakupp
  #
  description = "Raku++ — a Raku language interpreter and compiler written from scratch in C++17";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems f;
    in {
      packages = forAll (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          rakupp = pkgs.stdenv.mkDerivation {
            pname = "rakupp";
            # Keep in step with project(RakuPP VERSION …) in CMakeLists.txt
            # (docs/dev/RELEASING.md step 1 lists every place the version lives).
            version = "3.24.0";
            src = self;
            nativeBuildInputs = [ pkgs.cmake ];
            cmakeBuildType = "Release";
            # Nix toolchains default to PIE, like Guix — without PIC in the
            # runtime archive, binaries produced by `rakupp --exe` fail to link.
            cmakeFlags = [ "-DCMAKE_POSITION_INDEPENDENT_CODE=ON" ];
            doCheck = false;
            meta = {
              description = "Raku language interpreter and compiler written from scratch in C++17";
              homepage = "https://github.com/ash/rakupp";
              license = nixpkgs.lib.licenses.artistic2;
              mainProgram = "rakupp";
            };
          };
        in {
          inherit rakupp;
          default = rakupp;
        });

      devShells = forAll (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in { default = pkgs.mkShell { packages = [ pkgs.cmake ]; }; });
    };
}
