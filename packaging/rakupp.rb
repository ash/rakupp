# Homebrew formula for rakupp.
#
# Personal-tap install (once this lives in github.com/ash/homebrew-rakupp as
# Formula/rakupp.rb):
#
#     brew tap ash/rakupp
#     brew install rakupp
#
# Build straight from the main branch (no release needed):
#
#     brew install --HEAD ash/rakupp/rakupp
#
# Before publishing a stable (non-HEAD) version, cut a tag + GitHub release and
# fill in the url/sha256 below (see packaging/README.md).
class Rakupp < Formula
  desc "From-scratch Raku implementation in C++17 (interpreter + native compiler)"
  homepage "https://github.com/ash/rakupp"
  url "https://github.com/ash/rakupp/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "56e5fefab6afa38b389196c4badf6caa9a8afcfea5d0f864f8b1dfbad2824fd2"
  license "Artistic-2.0"
  head "https://github.com/ash/rakupp.git", branch: "main"

  depends_on "cmake" => :build

  def install
    # std_cmake_args sets CMAKE_INSTALL_PREFIX=#{prefix} and a Release build.
    # CMakeLists installs bin/rakupp, lib/librakupp_rt.a and include/rakupp/*.h;
    # findRuntime() resolves the symlinked binary back into the Cellar so `--exe`
    # locates the runtime library and headers with no extra setup.
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    assert_equal "3", shell_output("#{bin}/rakupp -e 'say 1 + 2'").strip
    assert_equal "1267650600228229401496703205376",
                 shell_output("#{bin}/rakupp -e 'say 2 ** 100'").strip
  end
end
