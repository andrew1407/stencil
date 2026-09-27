# The assistant: the plan's parse and validation, its executor, the dock and the flyout, and the
# LLM client.

set(STENCIL_OPPLAN_SOURCES
  src/llm/plan/opPlan.cpp                    # core's result document → the typed plan
  src/llm/plan/opPlanFields.cpp              # the per-op field filling
  src/llm/plan/opPlanParse.cpp)              # the actions, the variants and the ask card

set(STENCIL_OPREGISTRY_SOURCES
  src/llm/plan/opRegistry.cpp                # the table, the canon and the op names
  src/llm/plan/opRegistryBullets.cpp)        # the "Available ops" prompt section

# The plan executor (llm/planExecutor.hpp) is three TUs: the action dispatch, the
# MainWindow plan target and the canvas plan target.
set(STENCIL_PLANEXECUTOR_SOURCES
  src/llm/plan/executor/planExecutor.cpp
  src/llm/plan/executor/planExecutorRun.cpp            # the run: in order, suspended by an await
  src/llm/plan/executor/planExecutorAwait.cpp          # connect / frame / openUrl / openFile
  src/llm/plan/executor/planExecutorImage.cpp          # crop / rotate / filter / layout / page / blank
  src/llm/plan/executor/planExecutorEdit.cpp           # history, image refs, save
  src/llm/plan/executor/planExecutorState.cpp          # the editor-settings ops
  src/llm/plan/executor/planExecutorOps.cpp
  src/llm/plan/executor/planExecutorCanvas.cpp)

# The op-plan validator is core/opplan's, reached through its model/ seam.
set(STENCIL_OPSCHEMA_SOURCES
  src/model/OpPlanSchema.cpp)

list(APPEND STENCIL_GUI_SOURCES
  src/llm/dock/ChatDock.cpp
  src/llm/dock/compose/ChatDockComposer.cpp
  src/llm/dock/ChatDockChrome.cpp
  src/llm/dock/chatDockShared.cpp
  src/llm/panel/chatMoreMenu.cpp
  src/llm/dock/ChatDockEvents.cpp
  src/llm/dock/compose/ChatDockDrag.cpp
  src/llm/dock/compose/ChatDockAttach.cpp
  src/llm/dock/ChatDockJumpPills.cpp
  src/llm/dock/compose/ChatDockCompose.cpp
  src/llm/dock/ChatDockTray.cpp
  src/llm/dock/card/ChatDockCard.cpp
  src/llm/dock/card/chatDockCardMenu.cpp
  src/llm/dock/card/ChatDockCardParts.cpp
  src/llm/dock/card/ChatDockBubbleWidth.cpp
  src/llm/dock/ChatDockPending.cpp
  src/llm/dock/ChatDockAppend.cpp
  src/llm/dock/ChatDockNotices.cpp
  src/llm/dock/card/ChatDockVariants.cpp
  src/llm/dock/ChatDockState.cpp
  src/llm/panel/chatWidgets.cpp
  src/llm/panel/chatBubbleTail.cpp
  src/llm/panel/chatWidgetsOverlays.cpp
  src/llm/panel/chatCardRenderer.cpp
  src/llm/panel/ChatMenuPanel.cpp
  src/llm/panel/ChatMenuPanelCompose.cpp
  src/llm/panel/ChatMenuPanelRows.cpp
  src/llm/panel/ChatMenuPanelState.cpp
  src/app/chat/planTarget/ChatPlanTarget.cpp
  src/app/chat/planTarget/ChatPlanTargetServer.cpp
  src/app/chat/planTarget/ChatPlanTargetProjects.cpp
  src/app/chat/planTarget/ChatPlanTargetAwait.cpp
  src/app/chat/planTarget/PlanAwait.cpp
  ${STENCIL_OPPLAN_SOURCES}
  ${STENCIL_OPREGISTRY_SOURCES}
  ${STENCIL_OPSCHEMA_SOURCES}
  src/llm/client/LlmClient.cpp
  src/llm/client/LlmClientProbe.cpp
  src/llm/client/LlmClientChat.cpp
  src/llm/client/LlmClientAnthropic.cpp
  src/llm/client/SessionKey.cpp
  ${STENCIL_PLANEXECUTOR_SOURCES}
  src/llm/client/QtLlmTransport.cpp)
