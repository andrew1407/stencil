// The op-plan exports of abi/opplanShared.inc, called under both spellings: the wasm one
// (compiled, never exported) and the CLI one must agree on every schema and every result,
// and a stale, forged or null handle must come back neutral, never crash.
#include "doctest.h"
#include "cliApi.h"

#include "../opplan/opplanCorpus.hpp"

#include <string>
#include <thread>
#include <vector>

extern "C" {
  int stencil_opplanSchemaCreate(const char*, int, const char*, const char*);
  const char* stencil_opplanSchemaError(int);
  const char* stencil_opplanSchemaEntries(int);
  void stencil_opplanSchemaDestroy(int);
  int stencil_opplanParse(int, const char*, int);
  int stencil_opplanStatus(int);
  const char* stencil_opplanJson(int);
  void stencil_opplanDestroy(int);
}

namespace {
  struct OpplanAbi {
    int (*create)(const char*, int, const char*, const char*);
    const char* (*error)(int);
    const char* (*entries)(int);
    void (*destroySchema)(int);
    int (*parse)(int, const char*, int);
    int (*status)(int);
    const char* (*json)(int);
    void (*destroy)(int);
  };

  const OpplanAbi WASM_OPPLAN{stencil_opplanSchemaCreate, stencil_opplanSchemaError, stencil_opplanSchemaEntries,
                              stencil_opplanSchemaDestroy, stencil_opplanParse, stencil_opplanStatus,
                              stencil_opplanJson, stencil_opplanDestroy};
  const OpplanAbi CLI_OPPLAN{stencil_cli_opplanSchemaCreate, stencil_cli_opplanSchemaError,
                             stencil_cli_opplanSchemaEntries, stencil_cli_opplanSchemaDestroy, stencil_cli_opplanParse,
                             stencil_cli_opplanStatus, stencil_cli_opplanJson, stencil_cli_opplanDestroy};

  // One surface's transcript: its entries, then each reply's status and result JSON.
  std::string transcript(const OpplanAbi& abi, const std::string& registry, const char* surface,
                         const std::vector<std::string>& replies) {
    const int s = abi.create(registry.data(), static_cast<int>(registry.size()), surface, nullptr);
    std::string out = std::string(abi.error(s)) + "\n" + abi.entries(s) + "\n";
    for (const std::string& r : replies) {
      const int p = abi.parse(s, r.data(), static_cast<int>(r.size()));
      out += std::to_string(abi.status(p)) + " " + abi.json(p) + "\n";
      abi.destroy(p);
    }
    abi.destroySchema(s);
    return out;
  }
}  // namespace

TEST_CASE("opplan ABI: both spellings agree on every surface") {
  const std::string registry = opplanCorpus::registryText();
  const std::vector<std::string> replies{
      "just chatting",
      R"({"reply":"x","actions":[{"op":"rotate","dir":"left"},{"op":"llm"}]})",
      R"({"reply":"x","actions":[{"op":"blank","color":"notacolor"}]})",
      R"({"reply":"x","variants":[{"actions":[{"op":"clear"}]}],"ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}]}})",
  };
  for (const char* surface : {"browser", "desktop", "cli", "pystencil", "bot", "mcp"}) {
    const std::string wasm = transcript(WASM_OPPLAN, registry, surface, replies);
    CHECK_MESSAGE(wasm == transcript(CLI_OPPLAN, registry, surface, replies), surface);
    CHECK(wasm.find("\"status\":\"chatOnly\"") != std::string::npos);
  }
}

TEST_CASE("opplan ABI: stale, forged and null handles are neutral") {
  const std::string registry = opplanCorpus::registryText();
  for (const OpplanAbi* abi : {&WASM_OPPLAN, &CLI_OPPLAN}) {
    CHECK(abi->create(nullptr, 0, "cli", nullptr) == 0);
    CHECK(abi->create(registry.data(), -1, "cli", nullptr) == 0);
    CHECK(abi->error(987654) == nullptr);
    CHECK(abi->entries(987654) == nullptr);
    CHECK(abi->parse(987654, "x", 1) == 0);
    CHECK(abi->status(987654) == -1);
    CHECK(abi->json(987654) == nullptr);
    const int bad = abi->create("{", 1, "cli", nullptr);
    CHECK(std::string(abi->error(bad)).size() > 0);
    CHECK(abi->entries(bad) == nullptr);
    CHECK(abi->parse(bad, "{}", 2) == 0);
    abi->destroySchema(bad);
    const int s = abi->create(registry.data(), static_cast<int>(registry.size()), "cli", nullptr);
    CHECK(abi->parse(s, nullptr, 0) == 0);
    const int p = abi->parse(s, "{}", 2);
    CHECK(abi->status(p) == 0);
    abi->destroy(p);
    CHECK(abi->status(p) == -1);
    abi->destroySchema(s);
    CHECK(abi->error(s) == nullptr);
  }
}

TEST_CASE("opplan ABI: the handle tables survive concurrent callers") {
  const std::string registry = opplanCorpus::registryText();
  const int s = stencil_cli_opplanSchemaCreate(registry.data(), static_cast<int>(registry.size()), "cli", nullptr);
  std::vector<std::thread> workers;
  std::vector<int> ok(4, 0);
  for (int t = 0; t < 4; ++t)
    workers.emplace_back([&, t] {
      for (int i = 0; i < 50; ++i) {
        const std::string reply = R"({"reply":"x","actions":[{"op":"rotate","dir":"right","times":)" + std::to_string(1 + i % 3) + "}]}";
        const int p = stencil_cli_opplanParse(s, reply.data(), static_cast<int>(reply.size()));
        ok[t] += stencil_cli_opplanStatus(p) == 0 ? 1 : 0;
        stencil_cli_opplanDestroy(p);
      }
    });
  for (std::thread& w : workers) w.join();
  for (int n : ok) CHECK(n == 50);
  stencil_cli_opplanSchemaDestroy(s);
}
