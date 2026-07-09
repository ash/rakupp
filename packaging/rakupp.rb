class Rakupp < Formula
  desc "From-scratch Raku implementation in C++17 (interpreter + native compiler)"
  homepage "https://github.com/ash/rakupp"
  version "0.1.0"
  license "Artistic-2.0"

  head "https://github.com/ash/rakupp.git", branch: "main" do
    depends_on "cmake" => :build
  end

  on_macos do
    on_arm do
      # Prebuilt binary — no compiler or Command Line Tools needed.
      url "https://github.com/ash/rakupp/releases/download/v0.1.0/rakupp-0.1.0-arm64-macos.tar.gz"
      sha256 "61834ad92b6c48935cb65609da128e48e00aa5156cd762581127e116ff9c8de7"
    end
    on_intel do
      # Built from source.
      url "https://github.com/ash/rakupp/archive/refs/tags/v0.1.0.tar.gz"
      sha256 "56e5fefab6afa38b389196c4badf6caa9a8afcfea5d0f864f8b1dfbad2824fd2"
      depends_on "cmake" => :build
    end
  end

  def install
    if File.exist?("bin/rakupp")
      # Prebuilt binary tarball: bin/, lib/librakupp_rt.a, include/rakupp/*.
      bin.install "bin/rakupp"
      lib.install "lib/librakupp_rt.a"
      (include/"rakupp").install Dir["include/rakupp/*"]
    else
      # Source build. findRuntime() resolves the symlinked binary back into the
      # Cellar, so `--exe` locates the runtime library and headers automatically.
      system "cmake", "-S", ".", "-B", "build", *std_cmake_args
      system "cmake", "--build", "build"
      system "cmake", "--install", "build"
    end
  end

  test do
    assert_equal "3", shell_output("#{bin}/rakupp -e 'say 1 + 2'").strip
    assert_equal "1267650600228229401496703205376",
                 shell_output("#{bin}/rakupp -e 'say 2 ** 100'").strip
  end
end
