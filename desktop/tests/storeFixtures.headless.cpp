// Walks the shared persistence conformance corpora against the real desktop io/fileStore:
// llm/fixtures/chatDoc (§12.1 chat documents), fixtures/layout (the envelope plus tolerant line
// parsing) and fixtures/stencilProject (.stencil files). The corpora pin the BROWSER reference, and
// measured desktop differences live in tests/fixtureOverrides.json. Structurally unwalkable here:
// chatDoc's savedAt tolerance, and layout's expectKeyOrder, expectSanitized and expectLinesSameRef.
#include "storeFixturesParts.hpp"


int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  int overridden = 0;
  overridden += walkChatDoc();
  overridden += walkLayout();
  overridden += walkStencilProject("valid.json");
  overridden += walkStencilProject("invalid.json");
  std::printf("local overrides applied: %d\n", overridden);

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
