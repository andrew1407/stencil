#include "doctest.h"
#include "logoBanner.hpp"

#include <cstring>
#include <string>
#include <vector>

extern "C" {
  int stencil_cli_logoBanner(const char* mark, int compact, int boxed, char* out, int cap);
  int stencil_logoBanner(const char* mark, int compact, int boxed, char* out, int cap);
}

using namespace stencil::core;

// The rows the CLI's banner prints today (cli/src/app/logo/mark.zig), byte for byte.
TEST_CASE("logoBanner: the S raster matches the CLI's 14x6 and 10x5 marks") {
  const std::vector<std::string> full = {
      "   ●───────●  ", " ●╱           ", "  ╲●──────●   ", "           ╲● ", "            ╱ ", "  ●────────●  "};
  CHECK(rasteriseMark(LogoMark::S, false) == full);
  const std::vector<std::string> small = {
      "  ●─────● ", " ●        ", "  ●────●  ", "        ● ", " ●─────●  "};
  CHECK(rasteriseMark(LogoMark::S, true) == small);
}

TEST_CASE("logoBanner: the P and M marks keep their stems straight") {
  const auto p = rasteriseMark(LogoMark::P, false);
  REQUIRE(p.size() == 6);
  const std::vector<std::string> pRows = {
      "   ●──────●   ", "   │       ╲● ", "   ●──────●╱  ", "   │          ", "   │          ", "   ●          "};
  CHECK(p == pRows);
  const std::vector<std::string> mRows = {
      "  ●        ●  ", "  │╲      ╱│  ", "  │ ╲╲  ╱╱ │  ", "  │   ●●   │  ", "  │        │  ", "  ●        ●  "};
  CHECK(rasteriseMark(LogoMark::M, false) == mRows);
}

TEST_CASE("logoBanner: the boxed banner is the rounded panel around the mark") {
  const BannerLayout l = logoBannerLayout(false);
  CHECK(l.panelWidth == 16);
  CHECK(l.labelRow == 3);
  const auto rows = logoBannerRows(LogoMark::S, false);
  REQUIRE(rows.size() == 10);
  CHECK(rows[0] == "   ╭──────────────╮");
  CHECK(rows[1] == "  ╱                ╲");
  CHECK(rows[2] == "  │    ●───────●   │");
  CHECK(rows[8] == "  ╲                ╱");
  CHECK(rows[9] == "   ╰──────────────╯");
  const auto compact = logoBannerRows(LogoMark::S, true);
  REQUIRE(compact.size() == 7);
  CHECK(compact[0] == "    ╭────────────╮");
  CHECK(compact[1] == "    │   ●─────●  │");
  CHECK(compact[6] == "    ╰────────────╯");
}

TEST_CASE("logoBanner ABI: both spellings hand back the rows, sized first") {
  const int needed = stencil_cli_logoBanner("S", 0, 0, nullptr, 0);
  CHECK(needed > 0);
  std::string buf(static_cast<std::size_t>(needed), '\0');
  CHECK(stencil_logoBanner("S", 0, 0, buf.data(), needed) == needed);
  CHECK(std::strcmp(buf.c_str(), "   ●───────●  \n ●╱           \n  ╲●──────●   \n           ╲● \n            ╱ \n  ●────────●  ") == 0);
  char small[4] = {'x', 'x', 'x', 'x'};
  CHECK(stencil_cli_logoBanner("S", 1, 1, small, sizeof small) == stencil_cli_logoBanner("S", 1, 1, nullptr, 0));
  CHECK(small[0] == 'x');
  CHECK(stencil_cli_logoBanner("Q", 0, 1, nullptr, 0) == 0);
  CHECK(stencil_cli_logoBanner(nullptr, 0, 1, nullptr, 0) == 0);
}
