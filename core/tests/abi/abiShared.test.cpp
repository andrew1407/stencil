// The exports whose bodies live once in abi/shared.inc and are emitted into both
// extern "C" ABIs. Each case calls the wasm spelling and the CLI spelling and
// asserts they agree, so a future edit to one surface cannot silently fork.
#include "doctest.h"
#include "cliApi.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
  const char* stencil_pageFormats(void);
  int stencil_formulaValidate(const char*, int);
  double stencil_formulaApply(const char*, int, double, int);
  void stencil_applyContourRGBA(std::uint8_t*, int, int);
  int stencil_parseDuration(const char*, long long*);

  int stencil_scriptParse(const char*, int);
  void stencil_scriptDestroy(int);
  int stencil_scriptErrorCount(int);
  int stencil_scriptDiagCount(int);
  const char* stencil_scriptDiagAt(int, int, int*, int*, int*, int*, const char**);
  int stencil_scriptTokenCount(int);
  int stencil_scriptTokenAt(int, int, int*, int*, int*, int*);
  int stencil_scriptBlockCount(int);
  const char* stencil_scriptBlockAt(int, int, int*, int*, int*, int*);
  int stencil_scriptOpCount(int);
  int stencil_scriptOpAt(int, int, int*, int*, int*, int*, int*, int*, int*);
  const char* stencil_scriptOpStr(int, int, int);
  int stencil_scriptOpTokCount(int, int);
  const char* stencil_scriptOpTok(int, int, int);
  int stencil_scriptOpNum(int, int, int, double*);
  int stencil_scriptOpResolve(int, int, double, double, double, double, double*, int);
  const char* stencil_scriptDump(int);
}

namespace {

  // Every reader in abi/scriptShared.inc, so one transcript drives both spellings. The two
  // surfaces own separate handle tables, so the handle itself is never compared.
  struct ScriptAbi {
    int (*parse)(const char*, int);
    void (*destroy)(int);
    int (*errorCount)(int);
    int (*diagCount)(int);
    const char* (*diagAt)(int, int, int*, int*, int*, int*, const char**);
    int (*tokenCount)(int);
    int (*tokenAt)(int, int, int*, int*, int*, int*);
    int (*blockCount)(int);
    const char* (*blockAt)(int, int, int*, int*, int*, int*);
    int (*opCount)(int);
    int (*opAt)(int, int, int*, int*, int*, int*, int*, int*, int*);
    const char* (*opStr)(int, int, int);
    int (*opTokCount)(int, int);
    const char* (*opTok)(int, int, int);
    int (*opNum)(int, int, int, double*);
    int (*opResolve)(int, int, double, double, double, double, double*, int);
    const char* (*dump)(int);
  };

  const ScriptAbi WASM_SCRIPT = {
      stencil_scriptParse, stencil_scriptDestroy, stencil_scriptErrorCount,
      stencil_scriptDiagCount, stencil_scriptDiagAt, stencil_scriptTokenCount,
      stencil_scriptTokenAt, stencil_scriptBlockCount, stencil_scriptBlockAt,
      stencil_scriptOpCount, stencil_scriptOpAt, stencil_scriptOpStr, stencil_scriptOpTokCount,
      stencil_scriptOpTok, stencil_scriptOpNum, stencil_scriptOpResolve, stencil_scriptDump};

  const ScriptAbi CLI_SCRIPT = {
      stencil_cli_scriptParse, stencil_cli_scriptDestroy, stencil_cli_scriptErrorCount,
      stencil_cli_scriptDiagCount, stencil_cli_scriptDiagAt, stencil_cli_scriptTokenCount,
      stencil_cli_scriptTokenAt, stencil_cli_scriptBlockCount, stencil_cli_scriptBlockAt,
      stencil_cli_scriptOpCount, stencil_cli_scriptOpAt, stencil_cli_scriptOpStr,
      stencil_cli_scriptOpTokCount, stencil_cli_scriptOpTok, stencil_cli_scriptOpNum,
      stencil_cli_scriptOpResolve, stencil_cli_scriptDump};

  std::string text(const char* s) { return s ? std::string(s) : std::string("<null>"); }

  std::string transcript(const ScriptAbi& abi, const std::string& src) {
    const int h = abi.parse(src.data(), static_cast<int>(src.size()));
    std::ostringstream out;
    out << "handle " << (h > 0) << " errors " << abi.errorCount(h) << " diags "
        << abi.diagCount(h) << " tokens " << abi.tokenCount(h) << " blocks "
        << abi.blockCount(h) << " ops " << abi.opCount(h) << "\n";
    for (int i = 0; i < abi.diagCount(h); ++i) {
      int sev = -1, line = -1, col = -1, len = -1;
      const char* code = nullptr;
      const char* msg = abi.diagAt(h, i, &sev, &line, &col, &len, &code);
      out << "diag " << sev << ' ' << line << ' ' << col << ' ' << len << ' ' << text(code)
          << ' ' << text(msg) << "\n";
    }
    for (int i = 0; i < abi.tokenCount(h); ++i) {
      int kind = -1, line = -1, col = -1, len = -1;
      out << "token " << abi.tokenAt(h, i, &kind, &line, &col, &len) << ' ' << kind << ' '
          << line << ' ' << col << ' ' << len << "\n";
    }
    for (int i = 0; i < abi.blockCount(h); ++i) {
      int kind = -1, frame = -1, start = -1, count = -1;
      out << "block " << text(abi.blockAt(h, i, &kind, &frame, &start, &count)) << ' ' << kind
          << ' ' << frame << ' ' << start << ' ' << count << "\n";
    }
    for (int i = 0; i < abi.opCount(h); ++i) {
      int kind = -1, block = -1, edit = -1, line = -1, col = -1, strs = -1, nums = -1;
      out << "op " << abi.opAt(h, i, &kind, &block, &edit, &line, &col, &strs, &nums) << ' '
          << kind << ' ' << block << ' ' << edit << ' ' << line << ' ' << col << "\n";
      for (int k = 0; k < strs; ++k) out << "  str " << text(abi.opStr(h, i, k)) << "\n";
      for (int k = 0; k < abi.opTokCount(h, i); ++k)
        out << "  tok " << text(abi.opTok(h, i, k)) << "\n";
      for (int k = 0; k < nums; ++k) {
        double v = 0.0;
        out << "  num " << abi.opNum(h, i, k, &v) << ' ' << v << "\n";
      }
      double px[64] = {0.0};
      const int n = abi.opResolve(h, i, 800.0, 600.0, 40.0, 40.0, px, 64);
      out << "  resolve " << n;
      for (int k = 0; k < n && k < 64; ++k) out << ' ' << px[k];
      out << "\n  tooSmall " << abi.opResolve(h, i, 800.0, 600.0, 40.0, 40.0, px, 0) << "\n";
    }
    out << "dump\n" << text(abi.dump(h));
    abi.destroy(h);
    return out.str();
  }

  // Handle 4242 was never created: every reader answers its neutral value, none dereferences.
  std::string neutral(const ScriptAbi& abi) {
    std::ostringstream out;
    out << abi.errorCount(4242) << ' ' << abi.diagCount(4242) << ' ' << abi.tokenCount(4242)
        << ' ' << abi.blockCount(4242) << ' ' << abi.opCount(4242) << ' '
        << abi.opTokCount(4242, 0) << ' '
        << abi.opAt(4242, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)
        << ' ' << abi.tokenAt(4242, 0, nullptr, nullptr, nullptr, nullptr) << ' '
        << abi.opNum(4242, 0, 0, nullptr) << ' '
        << abi.opResolve(4242, 0, 1.0, 1.0, 1.0, 1.0, nullptr, 0) << ' ' << text(abi.dump(4242))
        << ' ' << text(abi.opStr(4242, 0, 0)) << ' ' << text(abi.opTok(4242, 0, 0)) << ' '
        << text(abi.diagAt(4242, 0, nullptr, nullptr, nullptr, nullptr, nullptr)) << ' '
        << text(abi.blockAt(4242, 0, nullptr, nullptr, nullptr, nullptr));
    abi.destroy(4242);
    return out.str();
  }

}  // namespace

TEST_CASE("shared ABI: pageFormats is one list on both surfaces") {
  CHECK(std::string(stencil_pageFormats()) == std::string(stencil_cli_pageFormats()));
  CHECK(std::string(stencil_pageFormats()).find("A4") != std::string::npos);
}

TEST_CASE("shared ABI: formula validate/apply agree on both surfaces") {
  const int x = static_cast<int>('x');
  for (const char* expr : {"x + 1", "x +", "", "x ** 2", "x / 0"}) {
    CHECK(stencil_formulaValidate(expr, x) == stencil_cli_validateFormula(expr, x));
    const double w = stencil_formulaApply(expr, x, 3.0, 1);
    const double c = stencil_cli_applyFormula(expr, x, 3.0, 1);
    CHECK(((w == c) || (w != w && c != c)));  // NaN == NaN is false; treat both-NaN as agreeing
  }
  // A null expr is the empty (identity) expression, not a crash, on both.
  CHECK(stencil_formulaValidate(nullptr, x) == 1);
  CHECK(stencil_cli_validateFormula(nullptr, x) == 1);
  CHECK(stencil_formulaApply(nullptr, x, 7.0, 1) == doctest::Approx(7.0));
  CHECK(stencil_cli_applyFormula(nullptr, x, 7.0, 1) == doctest::Approx(7.0));
}

TEST_CASE("shared ABI: applyContour writes the same pixels on both surfaces") {
  std::vector<std::uint8_t> a(4 * 4 * 4, 0), b;
  for (std::size_t i = 0; i < a.size(); i += 4) {
    a[i] = static_cast<std::uint8_t>(i);
    a[i + 1] = 40;
    a[i + 2] = 200;
    a[i + 3] = 255;
  }
  b = a;
  stencil_applyContourRGBA(a.data(), 4, 4);
  stencil_cli_applyContour(b.data(), 4, 4);
  CHECK(a == b);
}

TEST_CASE("shared ABI: parseDuration returns int64 ms on both surfaces") {
  struct Case { const char* spec; bool ok; long long ms; };
  const long long day = 24LL * 60 * 60 * 1000;
  const Case cases[] = {
      {"days 23", true, 23 * day},
      {"fortnight", true, 14 * day},
      {"month", true, 30 * day},
      {"3 weeks", true, 21 * day},
      {"off", true, 0},
      {"banana", false, 0},
      {"days 0", false, 0},
      // The parser's ceiling is Number.isSafeInteger (2^53-1), so the widest
      // accepted value still round-trips exactly through a JS number.
      {"days 100000000", true, 100000000LL * day},
      {"days 200000000", false, 0},
  };
  for (const Case& c : cases) {
    long long w = -1, cli = -1;
    CHECK(stencil_parseDuration(c.spec, &w) == (c.ok ? 1 : 0));
    CHECK(stencil_cli_parseDuration(c.spec, &cli) == (c.ok ? 1 : 0));
    if (!c.ok) continue;
    CHECK(w == c.ms);
    CHECK(cli == c.ms);
    CHECK(w <= 9007199254740991LL);
  }
  // A null out pointer is tolerated by both (the caller only wanted validity).
  CHECK(stencil_parseDuration("week", nullptr) == 1);
  CHECK(stencil_cli_parseDuration("week", nullptr) == 1);
}

TEST_CASE("shared ABI: the script engine reads back identically on both surfaces") {
  const char* sources[] = {
      "@source a.png:\n  @crop x1=10% x2=-10%\n  @rect (10,10) (100,80)\n  @save out.png\n",
      "@use line red dashed 3, point blue 6\n@line (0,0) (2cm,3cm)\n@undo\n",
      "@stencil box @1:\n  @rect (0,0) (@1,@1)\n@use stencil box 50\n@filter sepia\n",
      "@nope\n@crop\n@line (\n",
      "",
  };
  for (const char* src : sources)
    CHECK(transcript(WASM_SCRIPT, src) == transcript(CLI_SCRIPT, src));
  // Not a vacuous comparison: the first script reads back as a whole op stream and a dump.
  const std::string first = transcript(WASM_SCRIPT, sources[0]);
  CHECK(first.find("ops 4") != std::string::npos);
  CHECK(first.find("\ndump\nblock 0 file \"a.png\"\n  op open") != std::string::npos);
}

TEST_CASE("shared ABI: an unknown script handle is neutral on both surfaces") {
  CHECK(stencil_scriptParse(nullptr, 0) == stencil_cli_scriptParse(nullptr, 0));
  const std::string both = neutral(WASM_SCRIPT);
  CHECK(both == neutral(CLI_SCRIPT));
  CHECK(both.rfind("-1 -1 -1 -1 -1 -1 0 0 0 -1 <null>", 0) == 0);
}
