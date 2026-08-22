#pragma once

#include <QWidget>
#include <QList>
#include <QStringList>
#include <functional>

#include "chatDock.hpp"  // shared chat-card helpers + ProviderStatus

class QFrame;
class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace stencil::gui {

  class PillSplitter;
  struct Palette;

  // Most recent messages replayed (contract §7) — and the cap on the panel's
  // mirrored transcript rows. Shared with MainWindow's chatHistory_/mirror log.
  inline constexpr int kChatHistoryBound = 32;

  // Assistant chat hosted INSIDE the canvas context menu (browser .ctx-assist
  // parity): a tall scrolling transcript over a composer. NOT a second chat
  // implementation — MainWindow drives the same onChatSend/onChatReply pipeline
  // the dock uses and mirrors each line here through its chatMirror* helpers,
  // rendered by the dock's own fillChatCard/chatCardStyleSheet.
  class ChatMenuPanel : public QWidget {
    Q_OBJECT
   public:
    ChatMenuPanel(QWidget* parent, std::function<void(QString)> onSend,
                  std::function<void()> onStop, std::function<void()> onAttach,
                  std::function<void()> onSettings, std::function<void(QString)> onRetry);

    QWidget* input() const;

    // ── transcript (MainWindow mirrors the dock's lines here) ──
    void appendRow(const QString& role, const QString& text, bool muted,
                   const QString& retryText = QString(), bool pending = false,
                   const QStringList& notes = {});
    void appendLateNote(const QString& text);
    void showPending();
    void clearPending();
    void markStopped(const QString& retryText = QString());
    void clearRows();
    void setBusy(bool on);
    void setProviderStatus(const QString& richTooltip, ChatDock::ProviderStatus status);
    void restyle(const Palette& pal);

   protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    static QLabel* bodyOf(QFrame* card);
    void addRetry(QFrame* card, const QString& retryText);
    ChatCardMenuHooks menuHooks();
    bool dissolveRow(QFrame* l);
    void updateSendEnabled();
    void submit();
    void scrollToBottom();

    std::function<void(QString)> onSend_;
    std::function<void()> onStop_;
    std::function<void()> onAttach_;
    std::function<void()> onSettings_;
    PillSplitter* splitter_ = nullptr;  // transcript over the resizable composer
    QScrollArea* scroll_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* rows_ = nullptr;
    QPlainTextEdit* input_ = nullptr;
    QToolButton* send_ = nullptr;
    QToolButton* attach_ = nullptr;
    QToolButton* gear_ = nullptr;
    QLabel* statusDot_ = nullptr;
    QList<QFrame*> rowsAdded_;
    QWidget* suggest_ = nullptr;  // empty-state chips
    QFrame* pending_ = nullptr;
    bool busy_ = false;
    QColor danger_;   // error-card text (the dock's --danger)
    // …and the tones the shared row menu / "⋯" are built from.
    QColor text_, chip_, border_, accent_, muted_;
    std::function<void(QString)> onRetry_;   // resend a failed/stopped turn
  };

  // MainWindow stores the panel as a plain QWidget* member — this types it back.
  inline ChatMenuPanel* asChatMenu(QWidget* w) { return static_cast<ChatMenuPanel*>(w); }

}  // namespace stencil::gui
