// Headless check of the LLM chat client (src/llm/llmClient): the three wire mappings of
// llm-contract.md §6 through a MockTransport (no network), the error paths — HTTP/transport
// failure, llmDisabled, stopReason max_tokens and refusal — as typed errors that are never
// parsed as plans, and the §13 op-registry pins: names, flags, bullets, capability
// exclusion, forbidden ops, the prompt censor and prompt byte-stability.
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