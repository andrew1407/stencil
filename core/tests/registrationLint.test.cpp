// Every core/tests/**/*.test.cpp is compiled into stencil_tests: a file missing from the
// add_executable(stencil_tests …) block in core/CMakeLists.txt would never run, and a big folder
// (tests/abi, tests/raster) could drop out and still clear the registry floor in testMain.cpp.
#include "doctest.h"

#include <filesystem>
#include <fstream>
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

  // The tests/… entries of the stencil_tests block, which ends at the first line that is only ")".
  std::set<std::string> listedTests(const std::string& cmake) {
    std::set<std::string> listed;
    const std::size_t start = cmake.find("add_executable(stencil_tests");
    if (start == std::string::npos) return listed;
    std::istringstream lines(cmake.substr(start));
    std::string line;
    std::getline(lines, line);
    while (std::getline(lines, line)) {
      const std::size_t first = line.find_first_not_of(" \t");
      if (first == std::string::npos || line[first] == '#') continue;
      const std::string entry = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
      if (entry == ")") break;
      if (entry.rfind("tests/", 0) == 0) listed.insert(entry);
    }
    return listed;
  }
}  // namespace

TEST_CASE("every core test file is registered in stencil_tests, and every entry exists") {
  const fs::path root = coreRoot();
  REQUIRE(fs::exists(root / "CMakeLists.txt"));
  std::ifstream in(root / "CMakeLists.txt", std::ios::binary);
  std::stringstream buf;
  buf << in.rdbuf();
  const std::set<std::string> listed = listedTests(buf.str());
  REQUIRE(listed.size() >= 50);

  std::size_t found = 0;
  for (auto it = fs::recursive_directory_iterator(root / "tests"); it != fs::recursive_directory_iterator(); ++it) {
    const std::string name = it->path().filename().string();
    if (!it->is_regular_file() || name.size() < 9 || name.compare(name.size() - 9, 9, ".test.cpp") != 0) continue;
    ++found;
    const std::string rel = fs::relative(it->path(), root).generic_string();
    CHECK_MESSAGE(listed.count(rel) == 1, rel << " is not in the stencil_tests block of core/CMakeLists.txt");
  }
  CHECK(found >= 50);
  for (const std::string& entry : listed)
    CHECK_MESSAGE(fs::exists(root / entry), "core/CMakeLists.txt lists " << entry << ", which does not exist");
}
