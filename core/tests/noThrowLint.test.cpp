// The wasm build has no exceptions: a throw there aborts the whole module. So core's own code
// (tests and third_party aside) never spells try, catch or throw, nor calls the library's
// throwing helpers — .at(), std::stoi/stod and friends, std::regex. Comments may mention them.
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
  namespace fs = std::filesystem;

  fs::path coreRoot() {
    fs::path dir = fs::current_path();
    for (int i = 0; i < 16; ++i) {
      if (fs::exists(dir / "CLAUDE.md")) return dir / "core";
      if (!dir.has_parent_path() || dir.parent_path() == dir) break;
      dir = dir.parent_path();
    }
    return {};
  }

  // The source with its comments and string/char literals blanked out.
  std::string codeOnly(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
      const char c = src[i];
      if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
        while (i < src.size() && src[i] != '\n') ++i;
        out.push_back('\n');
      } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
        const std::size_t end = src.find("*/", i + 2);
        i = end == std::string::npos ? src.size() : end + 1;
        out.push_back(' ');
      } else if (c == '"' || c == '\'') {
        out.push_back(' ');
        for (++i; i < src.size() && src[i] != c; ++i)
          if (src[i] == '\\') ++i;
      } else {
        out.push_back(c);
      }
    }
    return out;
  }

  bool wordChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

  // Whole-word hits of `word` (a leading "." or "::" is part of the needle when given).
  bool spells(const std::string& code, const std::string& word) {
    for (std::size_t at = code.find(word); at != std::string::npos; at = code.find(word, at + 1)) {
      const bool before = at == 0 || !wordChar(code[at - 1]) || !wordChar(word[0]);
      const std::size_t end = at + word.size();
      const bool after = end >= code.size() || !wordChar(code[end]) || !wordChar(word.back());
      if (before && after) return true;
    }
    return false;
  }
}  // namespace

TEST_CASE("core never throws: no try/catch/throw, no throwing library helpers") {
  const fs::path root = coreRoot();
  REQUIRE(fs::exists(root / "CMakeLists.txt"));
  const std::vector<std::string> banned{"try", "catch", "throw", ".at(", "std::stoi", "std::stol", "std::stoll",
                                        "std::stoul", "std::stof", "std::stod", "std::stold", "std::regex"};
  std::size_t scanned = 0;
  for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
    const std::string top = fs::relative(it->path(), root).begin()->string();
    if (it->is_directory() && (top == "tests" || top == "third_party" || top.rfind("build", 0) == 0)) {
      it.disable_recursion_pending();
      continue;
    }
    const std::string ext = it->path().extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".inc") continue;
    std::ifstream in(it->path(), std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string code = codeOnly(buf.str());
    ++scanned;
    for (const std::string& word : banned)
      CHECK_MESSAGE(!spells(code, word), fs::relative(it->path(), root).string() << " spells " << word);
  }
  CHECK(scanned >= 100);
}

// Qt #defines these words, so an identifier spelled with one vanishes in any Qt file that
// includes Qt first — a struct then lays out differently from the core that built it.
TEST_CASE("core headers never spell a Qt keyword macro") {
  const fs::path root = coreRoot();
  REQUIRE(fs::exists(root / "CMakeLists.txt"));
  const std::vector<std::string> qtWords{"slots", "signals", "emit", "foreach", "forever"};
  std::size_t scanned = 0;
  for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
    const std::string top = fs::relative(it->path(), root).begin()->string();
    if (it->is_directory() && (top == "tests" || top == "third_party" || top.rfind("build", 0) == 0)) {
      it.disable_recursion_pending();
      continue;
    }
    const std::string ext = it->path().extension().string();
    if (ext != ".hpp" && ext != ".h") continue;
    std::ifstream in(it->path(), std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string code = codeOnly(buf.str());
    ++scanned;
    for (const std::string& word : qtWords)
      CHECK_MESSAGE(!spells(code, word), fs::relative(it->path(), root).string() << " spells " << word);
  }
  CHECK(scanned >= 50);
}
