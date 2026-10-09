#pragma once
#include "ChatDock.hpp"
#include "LlmClient.hpp"

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

class MainWindowGuiTest;
class QWidgetAction;

namespace stencil::llm {
  class PlanTarget;
  struct ExecResult;
  struct OpPlan;
}

namespace stencil::gui {

  class CanvasWidget;
  class MediaLoader;
  class Notifications;
  struct Settings;

  // One assistant conversation (llm-contract.md) across its two views, the dock and the menu
  // panel: the turn, the §7 history and attachments, the plan gate, the mirror and the §12 doc.
  // Editing, filing projects and storing the doc stay on MainWindow as hooks.
  class ChatSessionController : public QObject {
    Q_OBJECT
   public:
    struct Hooks {
      std::function<llm::LlmSettings()> llmSettings;
      std::function<void()> ensureClient;
      // The live editor a plan edits.
      std::function<std::unique_ptr<llm::PlanTarget>()> planTarget;
      // An editing plan on an empty canvas opens the turn's attachment first (§7).
      std::function<void(const QImage& image)> adoptAttachment;
      std::function<void()> planChanged;
      // A local project for a variant; the registry write waits for projectsAdded.
      std::function<QString(const QImage& image, const QString& name)> addVariantProject;
      std::function<void()> projectsAdded;
      std::function<void()> refreshActions;
      // §12: where the settled conversation is filed is the window's (project or server).
      std::function<void()> persist;
      std::function<void()> clearPersisted;
      std::function<void(const QString& path)> offerVideoUpload;
      // The dock's close slide is running: it still reports isVisible().
      std::function<bool()> dockClosing;
      std::function<void()> openChat;
      std::function<void(QRect anchorRect)> openSettings;
      std::function<void()> persistSettings;
    };

    ChatSessionController(QWidget* host, CanvasWidget* canvas, ChatDock* chatDock,
                          const QPointer<Notifications>& notify, Settings& settings,
                          std::unique_ptr<llm::LlmClient>& llmClient, Hooks hooks);

    void onChatSend(const QString& text);
    void onChatStop();
    void onChatClear();
    // Offered on BOTH surfaces, so the send path is a method rather than a lambda.
    void chatRetryTurn(const QString& text);
    void onChatVideoAttached(const QString& path);
    void refreshLlmStatus();
    // Parented to the WINDOW so the transcript survives the per-right-click menu rebuild.
    void ensureChatMenuPanel();
    void chatMirror(const QString& role, const QString& text, bool muted,
                    const QString& retryText = QString(), const QStringList& notes = {},
                    bool configure = false);
    void chatMirrorLateNote(const QString& text);
    QJsonObject buildActiveChatDoc() const;
    void restoreChatFromDoc(const QJsonObject& doc);

    // True for the whole of an op plan, which an op waiting on I/O spreads across event-loop turns.
    bool planRunning = false;
    QVector<llm::ChatMessage> chatHistory;
    QList<QImage> chatTurnAttachments;
    QStringList chatTurnAttachmentNames;
    int chatActiveAttachment = 0;
    QString chatVideoPath;
    int chatVideoFrames = 0;
    MediaLoader* chatMedia = nullptr;
    // §10 clearChat is deferred to chatTurnSettled.
    bool chatClearPending = false;
    // The ACTION is parented to the window, not the menu, so it outlives the per-right-click rebuild.
    QWidgetAction* chatMenuAction = nullptr;
    QWidget* chatMenuPanel = nullptr;
    QWidget* chatMenuInput = nullptr;

   private:
    friend class ::MainWindowGuiTest;
    void onChatReply(const llm::LlmReply& reply);
    // The rest of the turn, once the plan has answered.
    void onPlanDone(const llm::OpPlan& plan, llm::PlanTarget& target, const llm::ExecResult& res,
                    bool toastWanted);
    bool settleFailedChatReply(const llm::LlmReply& reply, bool toastWanted);
    void postChatReplyBubble(const llm::OpPlan& plan, bool hasWork, bool adoptAttachment);
    void renderChatAskCard(const llm::OpPlan& plan, llm::PlanTarget& target);
    void renderChatVariantCards(const llm::ExecResult& res);
    bool maybeContinueChat(const llm::OpPlan& plan);
    bool chatPlanLoadsWithoutTracing(const llm::OpPlan& plan) const;
    void flushHeldChatReply();
    void chatTurnSettled();
    void runDeferredChatClear();
    void resetChatState();
    QString chatSystemSuffix() const;
    void pushChatHistory(const llm::ChatMessage& m);
    // §7: last 32 messages; only this turn's images and the one before them ride along.
    QVector<llm::ChatMessage> wireChatMessages() const;
    // Browser closedToast parity: ~90 chars, click opens the chat.
    void showChatToast(const QString& text, bool success);
    // A dock mid-close counts as hidden: its slide keeps isVisible() true for 260 ms.
    bool chatSurfaceHidden() const;
    void chatError(const QString& text, const QString& toastError = QString());
    void chatUnreachable(const QString& text, const QString& toastError = QString());
    void chatMirrorPending(bool show);
    void chatMirrorStopped(const QString& retryText = QString());
    void chatMirrorBusy(bool on);
    void chatMirrorClear();
    static QString withChatWarnings(const QString& text, const QStringList& warnings);
    void chatLateNote(const QString& text);
    void chatNote(const QString& text);
    void chatMirrorProviderStatus(const QString& richTooltip, ChatDock::ProviderStatus status);

    QWidget* host;
    CanvasWidget* canvas;
    ChatDock* chatDock;
    // notify is made after the dock, and dies with the scroll viewport.
    const QPointer<Notifications>& notify;
    Settings& settings;
    std::unique_ptr<llm::LlmClient>& llmClient;
    Hooks h;

    struct MirrorRow {
      QString role;
      QString text;
      QString retryText;
      QStringList notes;
      bool muted = false;
    };
    QVector<MirrorRow> chatMirrorLog;
    // Short-TTL probe cache (browser chatSession cacheProbe), keyed by the effective settings.
    QString llmProbeKey;
    qint64 llmProbeAt = 0;
    llm::LlmProbeResult llmProbeCache;
    QString chatTextOnlyKey;
    QString chatLastPrompt;
    QByteArray chatImageDigest;
    llm::ChatImage chatImageEncoded;
    llm::ChatImage chatEdgeMapEncoded;
    QWidget* chatToast = nullptr;
    bool chatStopRequested = false;
    bool chatContinued = false;
    bool chatReplyHeld = false;
    QString chatHeldReply;
    QStringList chatHeldWarnings;
    QStringList chatHeldNotes;
  };

}  // namespace stencil::gui
