#include "doctest.h"

#include "cliApi.h"

#include <cstring>
#include <string>

// The extern "C" script surface, driven through its own prototypes. The wasm twin is
// emitted from the same abi/scriptShared.inc, so this also guards that body.
namespace {

  const char* kScript =
      "@source a.png:\n  @crop 10%\n  @rect (10,10) (100,80)\n  @save out.png\n";

  struct Handle {
    int h;
    explicit Handle(const std::string& src)
        : h(stencil_cli_scriptParse(src.data(), static_cast<int>(src.size()))) {}
    ~Handle() { stencil_cli_scriptDestroy(h); }
  };

}  // namespace

TEST_CASE("stencil_cli_scriptParse returns a handle and reports no errors for a good script") {
  Handle s{kScript};
  CHECK(s.h > 0);
  CHECK(stencil_cli_scriptErrorCount(s.h) == 0);
  CHECK(stencil_cli_scriptBlockCount(s.h) == 1);
  CHECK(stencil_cli_scriptOpCount(s.h) == 4);
  CHECK(stencil_cli_scriptTokenCount(s.h) > 0);
}

TEST_CASE("an unknown handle is refused, never dereferenced") {
  CHECK(stencil_cli_scriptErrorCount(4242) == -1);
  CHECK(stencil_cli_scriptOpCount(4242) == -1);
  CHECK(stencil_cli_scriptBlockCount(4242) == -1);
  CHECK(stencil_cli_scriptDiagCount(4242) == -1);
  CHECK(stencil_cli_scriptDump(4242) == nullptr);
  CHECK(stencil_cli_scriptBlockAt(4242, 0, nullptr, nullptr, nullptr, nullptr) == nullptr);
  CHECK(stencil_cli_scriptOpStr(4242, 0, 0) == nullptr);
  CHECK(stencil_cli_scriptTokenAt(4242, 0, nullptr, nullptr, nullptr, nullptr) == 0);
  stencil_cli_scriptDestroy(4242);  // no-op, not a crash
  CHECK(stencil_cli_scriptParse(nullptr, 0) == 0);
}

TEST_CASE("a block reports its spec, kind and op range") {
  Handle s{kScript};
  int kind = -1, frame = -1, opStart = -1, opCount = -1;
  const char* spec = stencil_cli_scriptBlockAt(s.h, 0, &kind, &frame, &opStart, &opCount);
  REQUIRE(spec != nullptr);
  CHECK(std::strcmp(spec, "a.png") == 0);
  CHECK(kind == 1);  // SourceKind::FILE
  CHECK(frame == 0);
  CHECK(opStart == 0);
  CHECK(opCount == 4);
}

TEST_CASE("an op reports its kind, edit number and payload counts") {
  Handle s{kScript};
  int kind = -1, block = -1, edit = -1, line = -1, col = -1, strs = -1, nums = -1;
  REQUIRE(stencil_cli_scriptOpAt(s.h, 1, &kind, &block, &edit, &line, &col, &strs, &nums) == 1);
  CHECK(kind == 2);  // OpKind::CROP
  CHECK(block == 0);
  CHECK(edit == 1);
  CHECK(line == 2);
  CHECK(nums == 1);

  double v = -1.0;
  CHECK(stencil_cli_scriptOpNum(s.h, 1, 0, &v) == 1);
  CHECK(v == doctest::Approx(0.0));            // album off
  CHECK(stencil_cli_scriptOpNum(s.h, 1, 9, &v) == 0);  // out of range
  CHECK(stencil_cli_scriptOpAt(s.h, 99, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr) == 0);
}

TEST_CASE("scriptOpResolve turns length tokens into pixels for the size it is given") {
  Handle s{kScript};
  double out[16];
  const int n = stencil_cli_scriptOpResolve(s.h, 1, 200, 100, 37.795, 37.795, out, 16);
  REQUIRE(n == 4);
  CHECK(out[0] == doctest::Approx(20));
  CHECK(out[2] == doctest::Approx(160));
  CHECK(out[3] == doctest::Approx(80));

  // The same crop against a different image resolves differently — nothing is cached.
  const int m = stencil_cli_scriptOpResolve(s.h, 1, 400, 200, 37.795, 37.795, out, 16);
  REQUIRE(m == 4);
  CHECK(out[2] == doctest::Approx(320));

  CHECK(stencil_cli_scriptOpResolve(s.h, 2, 200, 100, 37.795, 37.795, out, 2) == -2);
  CHECK(stencil_cli_scriptOpResolve(s.h, 99, 200, 100, 37.795, 37.795, out, 16) == -1);
  CHECK(stencil_cli_scriptOpResolve(4242, 0, 200, 100, 37.795, 37.795, out, 16) == -1);
}

TEST_CASE("a diagnostic crosses with its severity, span and stable code") {
  const std::string bad = "@source a.png:\n  @crp 10%\n";
  Handle s{bad};
  REQUIRE(stencil_cli_scriptErrorCount(s.h) == 1);
  REQUIRE(stencil_cli_scriptDiagCount(s.h) == 1);

  int sev = -1, line = -1, col = -1, len = -1;
  const char* code = nullptr;
  const char* msg = stencil_cli_scriptDiagAt(s.h, 0, &sev, &line, &col, &len, &code);
  REQUIRE(msg != nullptr);
  REQUIRE(code != nullptr);
  CHECK(sev == 0);
  CHECK(line == 2);
  CHECK(col == 3);
  CHECK(len == 4);
  CHECK(std::strcmp(code, "E_UNKNOWN_DIRECTIVE") == 0);
  CHECK(std::string(msg).find("@crop") != std::string::npos);
  CHECK(stencil_cli_scriptDiagAt(s.h, 9, nullptr, nullptr, nullptr, nullptr, nullptr) == nullptr);
}

TEST_CASE("handle-owned strings stay valid for the life of the handle") {
  const int h = stencil_cli_scriptParse(kScript, static_cast<int>(std::strlen(kScript)));
  const char* dump = stencil_cli_scriptDump(h);
  const char* target = stencil_cli_scriptOpStr(h, 3, 0);
  REQUIRE(dump != nullptr);
  REQUIRE(target != nullptr);
  const std::string dumpCopy = dump;

  // Parsing another script must not disturb the first handle's memory.
  const int other = stencil_cli_scriptParse("@filter bw\n", 11);
  CHECK(std::string(stencil_cli_scriptDump(h)) == dumpCopy);
  CHECK(std::strcmp(stencil_cli_scriptOpStr(h, 3, 0), "out.png") == 0);
  stencil_cli_scriptDestroy(other);
  CHECK(std::string(stencil_cli_scriptDump(h)) == dumpCopy);

  stencil_cli_scriptDestroy(h);
  CHECK(stencil_cli_scriptDump(h) == nullptr);
}

TEST_CASE("the token stream carries spans an editor can colour") {
  Handle s{kScript};
  const int n = stencil_cli_scriptTokenCount(s.h);
  REQUIRE(n > 0);
  int directives = 0;
  for (int i = 0; i < n; ++i) {
    int kind = -1, line = -1, col = -1, len = -1;
    REQUIRE(stencil_cli_scriptTokenAt(s.h, i, &kind, &line, &col, &len) == 1);
    CHECK(line >= 1);
    CHECK(col >= 1);
    CHECK(len >= 0);
    if (kind == 1) ++directives;  // TokenKind::DIRECTIVE
  }
  CHECK(directives == 4);
}
