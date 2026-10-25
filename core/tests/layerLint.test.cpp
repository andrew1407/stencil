// Core's layer order (core/ARCHITECTURE.md Layers): a group includes only groups to its left.
// Includes are bare, so each one is resolved to the folder holding that file; a crossing
// within one column is a frozen allowance that only shrinks.
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

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

  // Column of each group; the root's ABI units (wasm*Api.cpp, cliApi.*) sit right of abi/.
  const std::map<std::string, int> COLUMN{
      {"", 0},       {"geometry", 1}, {"color", 1},  {"parse", 1},  {"json", 1},
      {"raster", 2}, {"page", 2},     {"format", 2}, {"state", 2},  {"script", 2},
      {"opplan", 2}, {"abi", 3}};
  int column(const std::string& group, const std::string& file) {
    if (group.empty() && (file.rfind("wasm", 0) == 0 || file.rfind("cliApi", 0) == 0)) return 4;
    const auto hit = COLUMN.find(group);
    return hit == COLUMN.end() ? -1 : hit->second;
  }

  // `includer -> included`, both `group/file`: same-column crossings the tree holds today.
  const std::set<std::string> ALLOWANCE{
      "format/tooltipRows.hpp -> page/pageMetrics.hpp",
      "json/jsNumber.cpp -> parse/decimal.hpp",
      "json/jsonScan.cpp -> parse/decimal.hpp",
      "parse/cropSpec.hpp -> geometry/cropGeometry.hpp",
  };

  bool skipped(const std::string& top) { return top == "tests" || top == "third_party" || top.rfind("build", 0) == 0; }
}  // namespace

TEST_CASE("core includes point left: a group never reaches one in its own column or to its right") {
  const fs::path root = coreRoot();
  REQUIRE(fs::exists(root / "CMakeLists.txt"));
  std::map<std::string, std::string> groupOf;   // basename -> group folder ("" = core/ root)
  std::map<fs::path, std::string> files;
  for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
    const fs::path rel = fs::relative(it->path(), root);
    const std::string top = rel.begin()->string();
    if (it->is_directory()) {
      if (skipped(top)) it.disable_recursion_pending();
      continue;
    }
    const std::string ext = it->path().extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".inc") continue;
    const std::string group = rel.has_parent_path() ? top : "";
    groupOf.emplace(it->path().filename().string(), group);
    files.emplace(it->path(), group);
  }
  const std::regex include(R"re(^\s*#\s*include\s+"([^"]+)")re");
  std::set<std::string> crossings;
  std::size_t edges = 0;
  for (const auto& file : files) {
    const fs::path& path = file.first;
    const std::string& group = file.second;
    const std::string name = path.filename().string();
    REQUIRE_MESSAGE(column(group, name) >= 0, group << " has no column: add it to COLUMN and the doc");
    std::ifstream in(path, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
      std::smatch m;
      if (!std::regex_search(line, m, include)) continue;
      const std::string target = fs::path(m[1].str()).filename().string();
      const auto hit = groupOf.find(target);
      if (hit == groupOf.end() || hit->second == group) continue;
      ++edges;
      if (column(hit->second, target) >= column(group, name))
        crossings.insert((group.empty() ? "." : group) + "/" + name + " -> " + (hit->second.empty() ? "." : hit->second) + "/" + target);
    }
  }
  CHECK(edges >= 100);
  for (const std::string& c : crossings) CHECK_MESSAGE(ALLOWANCE.count(c), "a new layer crossing: " << c);
  for (const std::string& a : ALLOWANCE) CHECK_MESSAGE(crossings.count(a), "gone — drop it from ALLOWANCE: " << a);
}
