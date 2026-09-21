// Size + comment ratchet for core/: no new oversized file, no listed file growing, no directory
// getting more comment-heavy. The budget is tests/sizeBudget.json, read at RUNTIME.
// Core is codec-free, so this carries a minimal fixed-shape JSON reader of its own (CMake's
// string(JSON) needs 3.19 - the project pins 3.16). Paths resolve from the repo root.
// Print the tree's numbers: core/build/stencil_tests -tc="*budget: measured*" --no-skip
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

// ── minimal JSON reader (this file only; the budget's shape is fixed) ─────────
struct JsonReader {
  std::string s;
  std::size_t i = 0;

  explicit JsonReader(std::string text) : s(std::move(text)) {}
  void ws() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
  bool eat(char c) {
    ws();
    if (i >= s.size() || s[i] != c) return false;
    ++i;
    return true;
  }
  std::string str() {
    std::string out;
    if (!eat('"')) return out;
    for (; i < s.size() && s[i] != '"'; ++i) {
      if (s[i] == '\\' && i + 1 < s.size()) ++i;
      out += s[i];
    }
    ++i;
    return out;
  }
  int num() {
    ws();
    const std::size_t start = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-')) ++i;
    return i > start ? std::stoi(s.substr(start, i - start)) : 0;
  }
};

struct Budget {
  int maxNewFileLines = 0;
  int maxFilesPerDir = 0;
  std::map<std::string, int> files;
  std::map<std::string, int> dirs;
  std::map<std::string, int> commentPct;
  std::map<std::string, std::string> exceptions;
};

Budget loadBudget(const fs::path& path) {
  std::ifstream in(path);
  REQUIRE_MESSAGE(in.good(), "missing budget file: " << path.string());
  std::stringstream buf;
  buf << in.rdbuf();
  JsonReader r(buf.str());
  Budget b;
  REQUIRE(r.eat('{'));
  while (!r.eat('}')) {
    const std::string key = r.str();
    REQUIRE(r.eat(':'));
    if (key == "maxNewFileLines") b.maxNewFileLines = r.num();
    else if (key == "maxFilesPerDir") b.maxFilesPerDir = r.num();
    else if (key == "files" || key == "commentPct" || key == "exceptions" || key == "dirs") {
      REQUIRE(r.eat('{'));
      while (!r.eat('}')) {
        const std::string entry = r.str();
        REQUIRE(r.eat(':'));
        if (key == "exceptions") b.exceptions[entry] = r.str();
        else if (key == "files") b.files[entry] = r.num();
        else if (key == "dirs") b.dirs[entry] = r.num();
        else b.commentPct[entry] = r.num();
        r.eat(',');
      }
    } else {
      r.str();  // "_doc"
    }
    r.eat(',');
  }
  REQUIRE(b.maxNewFileLines > 0);
  return b;
}

// ── the tree ─────────────────────────────────────────────────────────────────
fs::path repoRoot() {
  fs::path p = fs::current_path();
  for (int hops = 0; hops < 16; ++hops) {
    if (fs::exists(p / "CLAUDE.md")) return p;
    if (!p.has_parent_path() || p.parent_path() == p) break;
    p = p.parent_path();
  }
  return {};
}

// Scope: .cpp/.hpp/.h under core/, minus vendored headers and build output.
std::vector<fs::path> scopedFiles(const fs::path& root) {
  std::vector<fs::path> out;
  for (fs::recursive_directory_iterator it(root / "core"), end; it != end; ++it) {
    const std::string name = it->path().filename().string();
    if (it->is_directory()) {
      if (name == "third_party" || name.rfind("build", 0) == 0) it.disable_recursion_pending();
      continue;
    }
    const std::string ext = it->path().extension().string();
    if (ext == ".cpp" || ext == ".hpp" || ext == ".h") out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

struct Counts { int total = 0; int comment = 0; int files = 0; };

// A comment line opens with // or /*, or sits inside a block comment. String and char
// literals are skipped so a // or /* inside one never flips the state.
Counts countFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buf;
  buf << in.rdbuf();
  const std::string src = buf.str();
  Counts c;
  bool inBlock = false;
  for (std::size_t i = 0; i < src.size();) {
    const std::size_t nl = src.find('\n', i);
    const std::size_t end = (nl == std::string::npos) ? src.size() : nl;
    ++c.total;
    std::size_t j = i;
    while (j < end && std::isspace(static_cast<unsigned char>(src[j]))) ++j;
    if (inBlock || (j + 1 < end && src[j] == '/' && (src[j + 1] == '/' || src[j + 1] == '*')))
      ++c.comment;
    for (std::size_t k = i; k < end; ++k) {
      if (inBlock) {
        if (src[k] == '*' && k + 1 < end && src[k + 1] == '/') { inBlock = false; ++k; }
        continue;
      }
      if (src[k] == '"' || src[k] == '\'') {
        const char quote = src[k];
        for (++k; k < end; ++k) {
          if (src[k] == '\\') { ++k; continue; }
          if (src[k] == quote) break;
        }
        continue;
      }
      if (src[k] == '/' && k + 1 < end) {
        if (src[k + 1] == '/') break;
        if (src[k + 1] == '*') { inBlock = true; ++k; }
      }
    }
    if (nl == std::string::npos) break;
    i = nl + 1;
  }
  return c;
}

std::string rel(const fs::path& root, const fs::path& p) {
  return fs::relative(p, root).generic_string();
}

}  // namespace

TEST_CASE("size budget: no core file exceeds its recorded line count") {
  const fs::path root = repoRoot();
  REQUIRE_MESSAGE(!root.empty(), "could not locate the repo root (no CLAUDE.md above cwd)");
  const Budget b = loadBudget(root / "core/tests/sizeBudget.json");

  for (const fs::path& p : scopedFiles(root)) {
    const std::string name = rel(root, p);
    if (b.exceptions.count(name)) continue;  // generated / byte-pinned twins only
    const int lines = countFile(p).total;
    const auto listed = b.files.find(name);
    if (listed == b.files.end()) {
      CHECK_MESSAGE(lines <= b.maxNewFileLines,
                    name << " is " << lines << " lines (> " << b.maxNewFileLines
                         << "): split it, or record it in core/tests/sizeBudget.json");
      continue;
    }
    CHECK_MESSAGE(lines <= listed->second,
                  name << " grew to " << lines << " lines (budget " << listed->second << ")");
    if (lines * 10 < listed->second * 9)
      MESSAGE(name << " shrank to " << lines << " (budget " << listed->second
                   << ") — ratchet the budget down");
  }
}

TEST_CASE("size budget: comment share and folder fan-out per core directory") {
  const fs::path root = repoRoot();
  REQUIRE(!root.empty());
  const Budget b = loadBudget(root / "core/tests/sizeBudget.json");

  std::map<std::string, Counts> byDir;  // files directly in a dir, not its subdirs
  for (const fs::path& p : scopedFiles(root)) {
    const Counts c = countFile(p);
    Counts& d = byDir[rel(root, p.parent_path())];
    d.total += c.total;
    d.comment += c.comment;
    // A header and its .cpp are one module to the reader, so they count once.
    if (p.extension() != ".cpp" || !fs::exists(fs::path(p).replace_extension(".hpp"))) ++d.files;
  }
  for (const auto& e : byDir) {
    const auto f = b.dirs.find(e.first);
    const int cap = f == b.dirs.end() ? b.maxFilesPerDir : f->second;
    CHECK_MESSAGE(e.second.files <= cap, e.first << " holds " << e.second.files
                  << " (cap " << cap << ") — split it");
  }
  for (const auto& entry : b.commentPct) {
    const auto found = byDir.find(entry.first);
    REQUIRE_MESSAGE(found != byDir.end(), "budget lists an empty dir: " << entry.first);
    const int pct = found->second.comment * 100 / found->second.total;
    CHECK_MESSAGE(pct <= entry.second, entry.first << " is " << pct << "% (budget "
                  << entry.second << "%)");
  }
}

TEST_CASE("size budget: measured tree numbers" * doctest::skip()) {
  const fs::path root = repoRoot();
  REQUIRE(!root.empty());
  for (const fs::path& p : scopedFiles(root)) {
    const Counts c = countFile(p);
    MESSAGE("  \"" << rel(root, p) << "\": " << c.total << ",");
  }
}
