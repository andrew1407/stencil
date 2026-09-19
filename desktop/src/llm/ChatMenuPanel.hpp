#pragma once

#include <QWidget>
#include <QList>
#include <QRect>
#include <QStringList>
#include <functional>

#include "ChatDock.hpp"  // shared chat-card helpers + ProviderStatus
#include "chatMoreMenu.hpp"   // the "…" rows, the dock's own builder

class QAction;
class QFrame;
class QHBoxLayout;
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
  inline constexpr int CHAT_HISTORY_BOUND = 32;

  // Assistant chat hosted INSIDE the canvas context menu (browser .ctx-assist). NOT a second chat -
  // MainWindow drives the same onChatSend/onChatReply pipeline the dock uses and mirrors each line.
  class ChatMenuPanel : public QWidget {
    Q_OBJECT
   public:
    ChatMenuPanel(QWidget* parent, std::function<void(QString)> onSend,
                  std::function<void()> onStop, std::function<void()> onAttach,
                  // `onSettings` receives the clicked control's GLOBAL rect, captured before this panel's popup
                  // starts closing, so the settings dialog still flies from the click.
                  std::function<void(QRect)> onSettings, std::function<void(QString)> onRetry);

    QWidget* input() const;

    // `configure` adds a "Configure provider" action beside Retry (browser unreachable-card parity);
    // it re-uses the panel's own settings callback, the one the gear already opens through.
    void appendRow(const QString& role, const QString& text, bool muted,
                   const QString& retryText = QString(), bool pending = false,
                   const QStringList& notes = {}, bool configure = false);
    void appendLateNote(const QString& text);
    void showPending();
    void clearPending();
    void markStopped(const QString& retryText = QString());
    void clearRows();
    void setBusy(bool on);
    void setProviderStatus(const QString& richTooltip, ChatDock::ProviderStatus status);
    void restyle(const Palette& pal);
    // "Swap message sides": set from either "…" menu and rendered by both, so a mirrored
    // turn matches. Re-skins every existing row in place, same as the dock.
    void setChatSwapSides(bool on);

   signals:
    // This flyout's own toggle; the owner persists it and mirrors it onto the dock.
    void chatSwapSidesChanged(bool swapped);

   protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    static QLabel* bodyOf(QFrame* card);
    void addRetry(QFrame* card, const QString& retryText);
    void addConfigure(QFrame* card);
    ChatCardMenuHooks menuHooks();
    // Send inline, everything else behind the "…" (browser js/ui/chatComposer.js).
    void buildComposerActions(QWidget* host, QHBoxLayout* btnRow);
    // A mirrored row's arrival: the shared gatherChatCardIn machinery (chatWidgets.hpp)
    // behind this panel's own veil/settle.
    void gatherRow(QFrame* card);
    bool dissolveRow(QFrame* l);
    void updateSendEnabled();
    void submit();
    void scrollToBottom();

    std::function<void(QString)> onSend_;
    std::function<void()> onStop_;
    std::function<void()> onAttach_;
    std::function<void(QRect)> onSettings_;
    PillSplitter* splitter_ = nullptr;  // transcript over the resizable composer
    QScrollArea* scroll_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* rows_ = nullptr;
    QPlainTextEdit* input_ = nullptr;
    QToolButton* send_ = nullptr;
    QToolButton* more_ = nullptr;      // the "…" overflow, and what the status dot rides on
    ChatMoreActions moreRows_;         // the "…" overflow's rows, minus Clear history
    QLabel* statusDot_ = nullptr;
    QList<QFrame*> rowsAdded_;
    QWidget* suggest_ = nullptr;  // empty-state chips
    QFrame* pending_ = nullptr;
    bool busy_ = false;
    QColor danger_;   // error-card text (the dock's --danger)
    // …and the tones the shared row menu / "⋯" are built from.
    QColor text_, chip_, border_, accent_, muted_;
    bool chatSwapSides_ = false;   // mirrors ChatDock::chatSwapSides_ (MainWindow keeps them in step)
    // The last Palette restyle() ran with - setChatSwapSides re-issues the shared card stylesheet
    // (chatCardStyleSheet) against it, since the flattened tail corner is keyed off chatSwapSides_ too.
    Palette paletteCache_;
    std::function<void(QString)> onRetry_;   // resend a failed/stopped turn
  };

  // MainWindow stores the panel as a plain QWidget* member — this types it back.
  inline ChatMenuPanel* asChatMenu(QWidget* w) { return static_cast<ChatMenuPanel*>(w); }

}  // namespace stencil::gui
