// Headless check of the LLM chat client (src/llm/llmClient) — the three wire
// mappings of llm-contract.md §6 driven through a MockTransport that
// captures url/headers/body and answers with canned responses (no network),
// plus the error paths: HTTP/transport failures, llmDisabled, and the
// stopReason max_tokens / refusal outcomes that must surface as typed errors
// and never be parsed as plans — and the §13 op-registry pins: the registered
// op-name set against the contract's desktop surface, per-entry flags against
// the opPlan helpers, one key phrase per bullet, capability exclusion, the
// forbidden-ops list, the prompt censor, and the byte-stability of the
// assembled prompt against the qrc prose canon + hand-embedded bullet copies.
#include "llmClientParts.hpp"

#include <QCoreApplication>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  llmclient::checkOllamaAndOpenAi();
  llmclient::checkStencilServer();
  llmclient::checkProbe();
  llmclient::checkModelsAndErrors();
  llmclient::checkOpRegistry();
  llmclient::checkPromptAssembly();
  llmclient::checkPromptShape();

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}