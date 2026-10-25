// The bulk token read of abi/scriptShared.inc in both spellings: the wasm export the browser's
// script editor reads (stencil_scriptTokens) and the CLI one pystencil reads answer the same
// quadruples, and each is the per-token reader's stream.
#include "doctest.h"
#include "cliApi.h"

#include <cstring>
#include <vector>

extern "C" {
  int stencil_scriptParse(const char*, int);
  void stencil_scriptDestroy(int);
  int stencil_scriptTokenCount(int);
  int stencil_scriptTokenAt(int, int, int*, int*, int*, int*);
  int stencil_scriptTokens(int, int*, int);
}

TEST_CASE("scriptTokens: the wasm and CLI spellings read the same stream as the per-token reader") {
  const char* src = "@source a.png:\n  @filter #ccc # tail\n  @line (0,0) (10,5)\n";
  const int len = static_cast<int>(std::strlen(src));
  const int wasm = stencil_scriptParse(src, len);
  const int cli = stencil_cli_scriptParse(src, len);
  REQUIRE((wasm > 0 && cli > 0));
  const int n = stencil_scriptTokenCount(wasm);
  REQUIRE(n > 5);
  REQUIRE(stencil_cli_scriptTokenCount(cli) == n);
  std::vector<int> a(static_cast<std::size_t>(4 * n), -1), b(a.size(), -1);
  CHECK(stencil_scriptTokens(wasm, a.data(), n) == n);
  CHECK(stencil_cli_scriptTokens(cli, b.data(), n) == n);
  CHECK(a == b);
  for (int i = 0; i < n; ++i) {
    int q[4] = {-1, -1, -1, -1};
    REQUIRE(stencil_scriptTokenAt(wasm, i, &q[0], &q[1], &q[2], &q[3]) == 1);
    for (int k = 0; k < 4; ++k) CHECK(a[static_cast<std::size_t>(4 * i + k)] == q[k]);
  }
  CHECK(stencil_scriptTokens(4242, a.data(), n) == -1);
  stencil_scriptDestroy(wasm);
  stencil_cli_scriptDestroy(cli);
}
