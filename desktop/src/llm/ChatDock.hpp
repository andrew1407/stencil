#pragma once
#include <QDockWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QColor>
#include <QPointer>
#include <QString>
#include <QVector>

#include "chatMoreMenu.hpp"   // the shared "…" rows, built once for both composers
#include "opPlan.hpp"
#include "PillSplitter.hpp"   // the shared composer resize grip
#include "../support/theme.hpp"   // Palette, cached for a swap-triggered re-style
#include <functional>

class QAction;
class QFrame;
class QCloseEvent;
class QHideEvent;
class QLabel;
class QLayout;
class QMoveEvent;
class QPalette;
class QDragEnterEvent;
class QDropEvent;
class QTimer;
class QVariantAnimation;
class QMimeData;
class QPlainTextEdit;
class MainWindowGuiTest;  // QtTest e2e (tests/MainWindow.<area>.gui.cpp)
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSplitter;
class QToolButton;
class QVBoxLayout;
class QWidget;

// Assistant chat panel (llm-contract.md); browser twin: browser/js/ui/chatPanel.js. Pure UI:
// MainWindow owns the history, the LlmClient, plan execution and the reachability probes.
namespace stencil::gui {

  // `onPick` gets the prompt text — callers prefill the composer, never send.
  QWidget* makeSuggestionChips(QWidget* parent, int gap, std::function<void(QString)> onPick);
  void styleSuggestionChips(QWidget* chips, const Palette& pal);

  extern const char* const CHAT_STATUS_OK_COLOR;
  extern const char* const CHAT_STATUS_BAD_COLOR;

  QToolButton* makeChatAccentButton(QWidget* parent, const QString& tooltip);

  enum class ChatCardKind { BUBBLE, ERROR, MUTED };
  QLabel* fillChatCard(QFrame* card, QVBoxLayout* lay, const QString& role,
                       const QString& text, ChatCardKind kind, const QColor& danger);
  // Pure: `swapped` flips the side, `user` alone decides it at rest.
  inline bool isChatBubbleOnRight(bool user, bool swapped) { return swapped ? !user : user; }
  // OPAQUE, flattened over `pageBg` (the surface the cards sit ON, not `chip`); false ⇒ no tail.
  bool chatBubbleColorsFor(const QString& objectName, const QColor& accent, const QColor& chip,
                           const QColor& border, const QColor& danger, const QColor& pageBg,
                           QColor& fillOut, QColor& borderOut);
  void applyChatBubbleSide(QFrame* card, QLayout* layout, bool right, const QColor& accent,
                           const QColor& chip, const QColor& border, const QColor& danger,
                           const QColor& pageBg);
  void applyChatSwapToCards(QWidget* transcript, QLayout* layout, bool swapped,
                            const QColor& accent, const QColor& chip, const QColor& border,
                            const QColor& danger, const QColor& pageBg);
  // Browser chatView.js chatRowMenuItems; only these hooks differ per surface.
  struct ChatCardMenuHooks {
    QWidget* owner = nullptr;
    QScrollArea* scroll = nullptr;
    std::function<void(const QString&)> insertIntoPrompt;
    std::function<void(QFrame*, const QString&)> resend;
    std::function<bool()> busy;
    // A dock mid-close is still visible for its slide; a menu opened then has nowhere to live.
    std::function<bool()> leaving;
    std::function<void()> moreMoved;
    // Global-coords furniture the "…" must not sit under; re-asked on every placement.
    std::function<QRect()> avoidRect;
    QColor text, chip, border, accent, muted;
  };
  void installChatCardMenu(QFrame* card, const ChatCardMenuHooks& hooks);
  // The default arg lives on the one declaration in chatWidgets.hpp — repeating it is a redefinition.
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal);
  QWidget* makeChatTypingDots(QWidget* parent);
  QToolButton* addChatRetryButton(QVBoxLayout* lay, const QColor& glyph,
                                  std::function<void()> onClick);
  QPushButton* addChatConfigureCta(QVBoxLayout* lay, const QColor& accent,
                                   std::function<void(QPushButton*)> onClick);
  // One bubble per turn, notes inside it, never extra rows.
  QLabel* addChatCardNote(QVBoxLayout* lay, const QString& text);

  // A word-wrapped QLabel clips its own last line unless its real height is reserved.
  void applyChatBubbleWidths(QWidget* transcript, QScrollArea* scroll);
  // Keyed on the whole sheet: a card's own local QSS shifts its wrapped label's height.
  QString chatCardStyleSheet(const Palette& pal, bool swapped);

  class ChatDock : public QDockWidget {
    Q_OBJECT
    friend class ::MainWindowGuiTest;
   public:
    explicit ChatDock(QWidget* parent = nullptr);

    // `images` are what the USER attached; the working image rides every turn (§7).
    void appendUser(const QString& text, const QList<QImage>& images = {});
    void appendAssistant(const QString& text, const QStringList& warnings = {},
                         const QStringList& notes = {});
    void appendError(const QString& text, const QString& retryText = QString());
    void appendExpiredSession(const QString& text, const QString& host,
                              const QString& retryText = QString());
    // The "Configure provider" dialog flies FROM that button, not from the "…" trigger.
    void appendUnreachable(const QString& text, const QString& retryText = QString());
    void addRetryButton(QVBoxLayout* lay, const QString& retryText);
    void appendNote(const QString& text);
    void appendLateNote(const QString& text);
    void warnAttachmentCap();
    void appendNotice(const QString& text);
    void showPending();
    void clearPending();
    void markPendingStopped(const QString& stoppedText = QString());
    struct VariantCard {
      QString label;
      QImage image;
      QString projectId;
    };
    void appendVariants(const QVector<VariantCard>& variants);
    // The model-side history lives in MainWindow, which clears it on clearRequested.
    void clearConversation();

    void setBusy(bool on);
    bool isBusy() const;
    // Idempotent; called from the dock's own toggle and from the saved setting at boot.
    void setChatSwapSides(bool on);
    bool chatSwapSides() const { return chatSwapSides_; }
    QSize floatingDefaultSize() const;
    // Pinned beside its icon while compact: the title bar drags nothing.
    void setCompactPopover(bool on);
    void focusInput();
    // Alt-peek release treats un-sent composer text as engagement.
    bool hasComposerText() const;
    bool dragPollActive() const;
    bool dragActive() const;
    // Offscreen has no cursor/button state; tests stub how the drag poll reads them.
    void setDragProbesForTest(std::function<QPoint()> cursorPos,
                              std::function<bool()> leftButtonDown);
    enum class ProviderStatus { UNKNOWN, OK, UNREACHABLE };
    void setProviderStatus(const QString& richTooltip, ProviderStatus status);
    void restyleIcons(const Palette& pal);

    const QList<QImage>& attachedImages() const { return images_; }
    const QStringList& attachedImageNames() const { return imageNames_; }
    QString attachedVideoPath() const { return videoPath_; }
    bool useCurrentImage() const { return true; }
    void addAttachmentImage(const QImage& img, const QString& name = QString());
    void clearAttachments();
    // Public: the context menu's assistant panel stages into the same attachment state.
    void pickMedia();

   public:
    // The §11 question card; submitting emits sendRequested() and locks the card.
    void appendAsk(const stencil::llm::AskCard& ask, const QVector<QImage>& previews);

    // Hidden with the dock, so a window raised from the menu falls from above (modalReveal's rule).
    QToolButton* moreButton() const { return more_; }

   signals:
    void sendRequested(const QString& text);
    void retryRequested(const QString& text);
    // Enter never stops — it is ignored while busy.
    void stopRequested();
    void videoAttached(const QString& path);
    void videoDetached();
    void openVariantRequested(const QString& projectId);
    // A note the dock posted ITSELF; the owner mirrors it onto the menu panel.
    void notePosted(const QString& text);
    void toastRequested(const QString& text);
    void lateNotePosted(const QString& text);
    void settingsRequested();
    void clearRequested();
    void chatSwapSidesChanged(bool swapped);
    void reconnectRequested(const QString& host);
    // `anchor` is the CTA button, for the reveal's origin (it stays on screen through the click).
    void configureProviderRequested(QWidget* anchor);
    // The owner hides through the ANIMATED path; the dock never hides itself.
    void closeRequested();
    void dockRequested(Qt::DockWidgetArea area);
    void floatToggleRequested();
    void titleDragStarted();
    void titleDragMoved(const QPoint& globalPos);
    void titleDragFinished(const QPoint& globalPos);
    void titleDragCanceled();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    // The dock swallows drops that miss the composer, so none reaches the window's drop zone.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

   private:
    void submit();
    void onSendClicked();
    void buildTitleBar();
    void startDragPoll();
    void pollDrag();
    void cancelDragPoll();
    // Native dock-drop is locked out during a title drag so only the zone overlay can dock.
    void setNativeDockingSuppressed(bool on);
    bool attachFromMimeData(const QMimeData* mime);
    bool canAttachMime(const QMimeData* mime) const;
    void showDropCue(bool on);
    void buildSuggestions();
    QVBoxLayout* appendTranscriptCard(int spacing);
    // Each card owns its animation, so overlapping appends never interfere.
    void animateCardIn(QWidget* card, QVBoxLayout* lay);
    void startCardEntrance(QWidget* card, QVBoxLayout* lay);
    enum class CardKind { BUBBLE, ERROR, MUTED };
    QVBoxLayout* appendCard(const QString& role, const QString& text, CardKind kind);
    // Installed on the card AND its labels: a selectable QLabel pops Qt's own menu otherwise.
    void installCardMenu(QFrame* card);
    ChatCardMenuHooks cardMenuHooks();
    bool transcriptHasCards() const;
    void syncMoreMenuItems();
    void refreshAttachmentTray();
    void applyBubbleWidths();
    void updateJumpButtons();
    QRect jumpPillsGlobalRect() const;
    void revalidateMoreButtons();

   public:
    // Set by the owner while this dock animates OUT.
    void setClosing(bool on) { closing_ = on; }

   private:
    bool closing_ = false;
    void positionJumpButtons();
    void updateSendEnabled();
    void scrollToBottom();

    PillSplitter* splitter_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QToolButton* jumpTop_ = nullptr;
    QToolButton* jumpBottom_ = nullptr;
    class ScrollReveal* reveal_ = nullptr;
    QWidget* transcript_ = nullptr;
    QVBoxLayout* transcriptLayout_ = nullptr;
    QWidget* titleBar_ = nullptr;
    QLabel* headerIcon_ = nullptr;
    QLabel* headerTitle_ = nullptr;
    QToolButton* clearBtn_ = nullptr;
    QToolButton* floatBtn_ = nullptr;
    QToolButton* closeBtn_ = nullptr;
    QList<QToolButton*> dockBtns_;
    QColor accentCache_, textCache_, dangerCache_, mutedCache_;
    QColor chipCache_, borderCache_;
    // setChatSwapSides re-issues chatCardStyleSheet against the last palette.
    Palette paletteCache_;
    bool chatSwapSides_ = false;
    void updatePlacementState();
    // The title-drag events are CONSUMED: Qt's own (window-server) move swallows the release.
    bool manualDrag_ = false, manualDragging_ = false, compactPopover_ = false;
    QPoint manualGrabOffset_;
    // ~16 ms poll: the native floating-window drag (macOS) swallows move/release events.
    QTimer* dragPoll_ = nullptr;
    QPoint dragStartCursor_;
    bool dragMoved_ = false;
    bool dragActive_ = false;
    std::function<QPoint()> dragPosProbe_;
    std::function<bool()> dragDownProbe_;
    QWidget* inputArea_ = nullptr;
    QWidget* dropCue_ = nullptr;
    QLabel* dropCueIcon_ = nullptr;
    QLabel* dropCueText_ = nullptr;
    QVariantAnimation* dropCueAnim_ = nullptr;
    QWidget* suggest_ = nullptr;
    QPlainTextEdit* input_ = nullptr;
    QToolButton* send_ = nullptr;
    QToolButton* attach_ = nullptr;
    QToolButton* gear_ = nullptr;
    QToolButton* more_ = nullptr;
    ChatMoreActions moreRows_;   // the "…" overflow's four rows
    QLabel* statusDot_ = nullptr;
    QPointer<QWidget> lastAssistantCard_;
    QWidget* attachTray_ = nullptr;
    QProgressBar* busy_ = nullptr;
    // Not derived from busy_'s visibility: turns run while the dock is hidden (context-menu chat).
    bool busyFlag_ = false;

    QList<QImage> images_;

    QElapsedTimer capToastAt_;
    QStringList imageNames_;
    QString videoPath_;
    bool stickToBottom_ = true;
    QWidget* pendingCard_ = nullptr;
    QLabel* pendingRole_ = nullptr;
    QLabel* pendingBody_ = nullptr;
    QWidget* pendingDots_ = nullptr;
  };

  void styleProviderStatusDot(QLabel* dot, QToolButton* gear,
                              const QString& richTooltip,
                              ChatDock::ProviderStatus status,
                              const QPalette& pal);

}  // namespace stencil::gui
