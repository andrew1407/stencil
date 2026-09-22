// Drift guard for the numbers the core shares with a browser JS twin: the project refresh
// presets and the .stc caps. Both are read out of the canonical .js here, so an edit there
// that never reached C++ fails natively instead of only in the self-skipping wasm run.
#include "doctest.h"

#include "ProjectsStore.hpp"
#include "text.hpp"
#include "types.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
  double stencil_projects_periodMs(const char*);
}

namespace {
  namespace fs = std::filesystem;
  using stencil::core::ProjectsStore;

  fs::path repoRoot() {
    fs::path p = fs::current_path();
    for (int hops = 0; hops < 16; ++hops) {
      if (fs::exists(p / "CLAUDE.md")) return p;
      if (!p.has_parent_path() || p.parent_path() == p) break;
      p = p.parent_path();
    }
    return {};
  }

  std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
  }

  // Trimmed, and without any trailing line comment: the twins annotate their constants.
  std::string trim(const std::string& s) {
    const std::size_t comment = s.find("//");
    return std::string(stencil::core::trimAscii(
        comment == std::string::npos ? s : s.substr(0, comment)));
  }

  using Names = std::map<std::string, long long>;

  // `7 * 24 * 60 * 60 * 1000`, where a factor may be a name read earlier in the same file.
  // 0 means "not a product of known factors" — no constant this guards is ever 0.
  long long product(const std::string& expr, const Names& names) {
    long long out = 1;
    for (std::size_t i = 0; i <= expr.size();) {
      const std::size_t star = expr.find('*', i);
      const std::string term =
          trim(expr.substr(i, star == std::string::npos ? std::string::npos : star - i));
      if (term.empty()) return 0;
      if (std::isdigit(static_cast<unsigned char>(term[0]))) {
        out *= std::stoll(term);
      } else {
        const auto at = names.find(term);
        if (at == names.end()) return 0;
        out *= at->second;
      }
      if (star == std::string::npos) break;
      i = star + 1;
    }
    return out;
  }

  bool couldBeProduct(const std::string& s) {
    for (char c : s) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '*' || c == ' ')
        continue;
      return false;
    }
    return !s.empty();
  }

  // Every `[export ]const NAME = <product>;` line, in file order; anything else is skipped.
  Names constsOf(const std::string& src) {
    Names out;
    std::istringstream in(src);
    for (std::string raw; std::getline(in, raw);) {
      std::string line = trim(raw);
      if (line.rfind("export ", 0) == 0) line = line.substr(7);
      if (line.rfind("const ", 0) != 0) continue;
      const std::size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string name = trim(line.substr(6, eq - 6));
      std::string value = trim(line.substr(eq + 1));
      if (!value.empty() && value.back() == ';') value.pop_back();
      value = trim(value);
      if (!couldBeProduct(value)) continue;
      const long long v = product(value, out);
      if (v != 0) out[name] = v;
    }
    return out;
  }

  // The `{ key: <product>, … }` rows of one `const <name> = Object.freeze({ … })`, in order.
  std::vector<std::pair<std::string, long long>> rowsOf(const std::string& src,
                                                        const std::string& name,
                                                        const Names& names) {
    std::vector<std::pair<std::string, long long>> rows;
    const std::size_t at = src.find("const " + name + " = ");
    if (at == std::string::npos) return rows;
    const std::size_t open = src.find('{', at);
    const std::size_t close = src.find('}', open);
    if (open == std::string::npos || close == std::string::npos) return rows;
    std::istringstream in(src.substr(open + 1, close - open - 1));
    for (std::string raw; std::getline(in, raw);) {
      const std::size_t colon = raw.find(':');
      if (colon == std::string::npos) continue;
      std::string key = trim(raw.substr(0, colon));
      if (key.size() >= 2 && (key.front() == '\'' || key.front() == '"'))
        key = key.substr(1, key.size() - 2);
      std::string value = trim(raw.substr(colon + 1));
      if (!value.empty() && value.back() == ',') value.pop_back();
      rows.emplace_back(key, product(trim(value), names));
    }
    return rows;
  }

}  // namespace

TEST_CASE("drift: the refresh presets match browser js/core/project/meta/projectPeriods.js") {
  const fs::path root = repoRoot();
  REQUIRE_MESSAGE(!root.empty(), "could not locate the repo root (no CLAUDE.md above cwd)");
  const std::string src = readFile(root / "browser/js/core/project/meta/projectPeriods.js");
  REQUIRE_MESSAGE(!src.empty(), "the canonical projectPeriods.js moved or is unreadable");

  const Names js = constsOf(src);
  REQUIRE(js.count("DAY_MS") == 1);
  CHECK(js.at("DAY_MS") == ProjectsStore::DAY_MS);
  CHECK(js.at("EXPIRY_MS") == ProjectsStore::EXPIRY_MS);
  CHECK(js.at("WARN_MS") == ProjectsStore::WARN_MS);
  CHECK(src.find("DEFAULT_PERIOD = '" + std::string(ProjectsStore::DEFAULT_PERIOD) + "'") !=
        std::string::npos);

  const auto rows = rowsOf(src, "PERIOD_MS", js);
  REQUIRE(rows.size() == ProjectsStore::PERIOD_MS.size());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    CHECK_MESSAGE(rows[i].first == ProjectsStore::PERIOD_MS[i].word,
                  "PERIOD_MS row " << i << " is '" << rows[i].first << "' in the JS twin");
    // Read back over the ABI rather than mirrored here.
    CHECK_MESSAGE(stencil_projects_periodMs(rows[i].first.c_str()) ==
                      static_cast<double>(rows[i].second),
                  rows[i].first << " drifted from the JS twin");
  }
  CHECK(stencil_projects_periodMs("nosuchperiod") ==
        static_cast<double>(ProjectsStore::EXPIRY_MS));
}

TEST_CASE("drift: the .stc caps match browser js/core/script/types.js") {
  const fs::path root = repoRoot();
  REQUIRE(!root.empty());
  const std::string src = readFile(root / "browser/js/core/script/types.js");
  REQUIRE_MESSAGE(!src.empty(), "the canonical script types.js moved or is unreadable");

  const Names js = constsOf(src);
  using namespace stencil::core::script;
  const std::vector<std::pair<std::string, long long>> caps = {
      {"MAX_LINES", MAX_LINES},
      {"MAX_TOKENS", MAX_TOKENS},
      {"MAX_OPS", MAX_OPS},
      {"MAX_BLOCKS", MAX_BLOCKS},
      {"MAX_TEMPLATES", MAX_TEMPLATES},
      {"MAX_TEMPLATE_DEPTH", MAX_TEMPLATE_DEPTH},
      {"MAX_TEMPLATE_EXPANSIONS", MAX_TEMPLATE_EXPANSIONS},
      {"MAX_POINTS_PER_LINE", MAX_POINTS_PER_LINE},
      {"MAX_SOURCE_CHARS", MAX_SOURCE_CHARS},
  };
  for (const auto& cap : caps) {
    REQUIRE_MESSAGE(js.count(cap.first) == 1, "types.js no longer declares " << cap.first);
    CHECK_MESSAGE(js.at(cap.first) == cap.second, cap.first << " drifted from the JS twin");
  }
  std::size_t declared = 0;
  for (const auto& entry : js)
    if (entry.first.rfind("MAX_", 0) == 0) ++declared;
  CHECK_MESSAGE(declared == caps.size(), "types.js declares a cap core/script/types.hpp lacks");
}
