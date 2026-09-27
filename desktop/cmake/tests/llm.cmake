# The assistant: the op-plan parse, the shared corpus walkers, the typed oracle, the provider
# wire, the client, the store and deep-link walkers, and the executor.

# The op-plan parser (src/llm/opPlan): the llm-contract.md §1-2 parse matrix.
stencil_headless_test(stencil_llmopplan_headless
  SOURCES tests/llm/plan/llmOpPlan.headless.cpp tests/llm/plan/llmOpPlanExtract.headless.cpp
    tests/llm/plan/llmOpPlanCrop.headless.cpp tests/llm/plan/llmOpPlanShapes.headless.cpp
    tests/llm/plan/llmOpPlanPage.headless.cpp tests/llm/plan/llmOpPlanSettings.headless.cpp
    tests/llm/plan/llmOpPlanProjects.headless.cpp tests/llm/plan/llmOpPlanRows.headless.cpp
    tests/llm/plan/llmOpPlanImages.headless.cpp tests/llm/plan/llmOpPlanAsk.headless.cpp
    ${STENCIL_OPPLAN_SOURCES} ${STENCIL_OPREGISTRY_SOURCES}
    ${STENCIL_OPSCHEMA_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Core)

# The shared fixture corpus + the desktop override map, for the walkers below.
set(STENCIL_FIXTURE_WALKER_DEFS
  "STENCIL_CORPUS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../browser/js/config\""
  "STENCIL_OVERRIDES_JSON=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtureOverrides.json\"")

# Every shared opPlan fixture through the real parseOpPlan, with the desktop's known divergences.
stencil_headless_test(stencil_opplanfixtures_headless
  SOURCES tests/llm/plan/opPlanFixtures.headless.cpp ${STENCIL_OPPLAN_SOURCES} ${STENCIL_OPREGISTRY_SOURCES}
    ${STENCIL_OPSCHEMA_SOURCES} resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Core)

# The typed op-plan oracle: parseOpPlan's whole typed result over the corpus and the
# adversarial inputs, pinned in tests/pins/opPlanOracle.json (STENCIL_UPDATE_ORACLE=1).
stencil_headless_test(stencil_opplanoracle_headless
  SOURCES tests/llm/plan/opPlanOracle.headless.cpp ${STENCIL_OPPLAN_SOURCES} ${STENCIL_OPREGISTRY_SOURCES}
    ${STENCIL_OPSCHEMA_SOURCES} resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
    "STENCIL_ORACLE_JSON=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/pins/opPlanOracle.json\""
  LIBS stencil_core Qt6::Core)

# The shared §6 wire and sanitizer vectors through the real LlmClient over a mock transport.
# LlmClient.cpp is #included by the test TU, not compiled here; its sibling TUs are.
stencil_headless_test(stencil_llmwirefixtures_headless
  SOURCES tests/llm/client/llmWireFixtures.headless.cpp tests/llm/client/llmWireFixturesWalk.headless.cpp src/llm/client/LlmClientProbe.cpp
    src/llm/client/LlmClientChat.cpp src/llm/client/LlmClientAnthropic.cpp ${STENCIL_OPPLAN_SOURCES} ${STENCIL_OPREGISTRY_SOURCES}
    ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES} src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}
    src/io/deferredWrite.cpp resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Network)

# The LLM client (src/llm/client): the §6 wire mappings through a mock transport.
stencil_headless_test(stencil_llmclient_headless
  SOURCES tests/llm/client/LlmClient.headless.cpp tests/llm/client/LlmClientOllama.headless.cpp
    tests/llm/client/LlmClientServer.headless.cpp tests/llm/client/LlmClientProbe.headless.cpp
    tests/llm/client/LlmClientModels.headless.cpp tests/llm/client/LlmClientRegistry.headless.cpp
    tests/llm/client/LlmClientAssembly.headless.cpp tests/llm/client/LlmClientPrompt.headless.cpp
    tests/llm/client/LlmClientAnthropic.headless.cpp src/llm/client/LlmClient.cpp src/llm/client/LlmClientProbe.cpp
    src/llm/client/LlmClientChat.cpp src/llm/client/LlmClientAnthropic.cpp ${STENCIL_OPPLAN_SOURCES}
    ${STENCIL_OPREGISTRY_SOURCES} ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES}
    src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}
    src/io/deferredWrite.cpp resources/app.qrc
  LIBS stencil_core Qt6::Network)

# The anthropic session key's holder: the canon TTL, the expiry timer, forget and the quit wipe.
stencil_headless_test(stencil_sessionkey_headless
  SOURCES tests/llm/client/sessionKey.headless.cpp src/llm/client/SessionKey.cpp resources/app.qrc
  LIBS Qt6::Core)

# The shared chatDoc / layout / stencilProject fixtures through the real io/fileStore.
stencil_headless_test(stencil_storefixtures_headless
  SOURCES tests/io/storeFixtures.headless.cpp tests/io/storeFixturesWalkers.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Widgets)

# The shared telegramStart vectors through the real codec (the desktop only builds payloads).
stencil_headless_test(stencil_deeplinkfixtures_headless
  SOURCES tests/io/deepLinkFixtures.headless.cpp src/io/deepLink.cpp ${STENCIL_SERVERCLIENT_SOURCES}
  DEFS ${STENCIL_FIXTURE_WALKER_DEFS}
  LIBS stencil_core Qt6::Network)

# The plan executor (src/llm/plan/executor) against a canvas target seeded with the PNG fixture.
stencil_headless_test(stencil_llmexecutor_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/llm/plan/executor/llmExecutor.headless.cpp tests/llm/plan/executor/llmExecutorPlan.headless.cpp
    tests/llm/plan/executor/llmExecutorCoords.headless.cpp tests/llm/plan/executor/llmExecutorSettings.headless.cpp
    tests/llm/plan/executor/llmExecutorFiles.headless.cpp tests/llm/plan/executor/llmExecutorProject.headless.cpp
    tests/llm/plan/executor/llmExecutorImages.headless.cpp tests/llm/plan/executor/llmExecutorHistory.headless.cpp
    tests/llm/plan/executor/llmExecutorAccent.headless.cpp tests/llm/plan/executor/llmExecutorRows.headless.cpp
    tests/llm/plan/executor/llmExecutorAwait.headless.cpp
    ${STENCIL_OPPLAN_SOURCES} ${STENCIL_OPREGISTRY_SOURCES}
    ${STENCIL_OPSCHEMA_SOURCES} ${STENCIL_PLANEXECUTOR_SOURCES} ${STENCIL_CANVAS_SOURCES}
    src/canvas/overlay/IdleCard.cpp ${STENCIL_THEME_SOURCES} resources/app.qrc
  DEFS "STENCIL_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\""
  LIBS stencil_core Qt6::Widgets)
