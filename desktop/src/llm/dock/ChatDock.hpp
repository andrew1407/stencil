#pragma once
#include <QDockWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QColor>
#include <QPointer>
#include <QString>
#include <QVector>

#include "chatCards.hpp"
#include "chatDockState.hpp"
#include "chatMoreMenu.hpp"   // the shared "…" rows, built once for both composers
#include "opPlan.hpp"
#include "PillSplitter.hpp"   // the shared composer resize grip
#include "../../support/theme/theme.hpp"   // Palette, cached for a swap-triggered re-style
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

// Assistant chat panel (llm-contract.md); browser twin: browser/js/ui/chat/chatPanel.js. Pure UI:
// MainWindow owns the history, the LlmClient, plan execution and the reachability probes.
namespace stencil::gui {

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
    bool getChatSwapSides() const { return chatSwapSides; }
    QSize floatingDefaultSize() const;
    // Compact = pinned beside its icon, but still draggable; only the bar's dblclick is dead.
    void setCompactPopover(bool on);
    void focusInput();
    // Alt-peek release treats un-sent composer text as engagement.
    bool hasComposerText() const;
    bool dragPollActive() const;
    bool getDragActive() const;
    // Offscreen has no cursor/button state; tests stub how the drag poll reads them.
    void setDragProbesForTest(std::function<QPoint()> cursorPos,
                              std::function<bool()> leftButtonDown);
    enum class ProviderStatus { UNKNOWN, OK, UNREACHABLE };
    void setProviderStatus(const QString& richTooltip, ProviderStatus status);
    void restyleIcons(const Palette& pal);

    const QList<QImage>& attachedImages() const { return cmp.images; }
    const QStringList& attachedImageNames() const { return cmp.imageNames; }
    QString attachedVideoPath() const { return cmp.videoPath; }
    bool useCurrentImage() const { return true; }
    void addAttachmentImage(const QImage& img, const QString& name = QString());
    void clearAttachments();
    // Public: the context menu's assistant panel stages into the same attachment state.
    void pickMedia();

   public:
    // The §11 question card; submitting emits sendRequested() and locks the card.
    void appendAsk(const stencil::llm::AskCard& ask, const QVector<QImage>& previews);

    // Hidden with the dock, so a window raised from the menu falls from above (modalReveal's rule).
    QToolButton* moreButton() const { return cmp.more; }

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
    void buildComposer();   // its own TU: ChatDockComposer.cpp
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
    void setClosing(bool on) { closing = on; }

   private:
    bool closing = false;
    void positionJumpButtons();
    void updateSendEnabled();
    void scrollToBottom();

    // Furniture in chatDockState.hpp; input and scroll stay named (the GUI e2e reads them).
    ChatTranscriptParts log;
    ChatTitleBarParts chrome;
    ChatComposerParts cmp;
    QScrollArea* scroll = nullptr;
    class ScrollReveal* reveal = nullptr;
    QColor accentCache, textCache, dangerCache, mutedCache, chipCache, borderCache;
    // setChatSwapSides re-issues chatCardStyleSheet against the last palette.
    Palette paletteCache;
    bool chatSwapSides = false;
    void updatePlacementState();
    // The title-drag events are CONSUMED: Qt's own (window-server) move swallows the release.
    bool manualDrag = false, manualDragging = false, compactPopover = false;
    QPoint manualGrabOffset;
    // ~16 ms poll: the native floating-window drag (macOS) swallows move/release events.
    QTimer* dragPoll = nullptr;
    QPoint dragStartCursor;
    bool dragMoved = false;
    bool dragActive = false;
    std::function<QPoint()> dragPosProbe;
    std::function<bool()> dragDownProbe;
    QPlainTextEdit* input = nullptr;
    ChatMoreActions moreRows;   // the "…" overflow's four rows
    QPointer<QWidget> lastAssistantCard;
    // Not derived from cmp.busy's visibility: turns run while the dock is hidden (context-menu chat).


    QElapsedTimer capToastAt;
    bool stickToBottom = true;
    QWidget* pendingCard = nullptr;
    QLabel* pendingRole = nullptr;
    QLabel* pendingBody = nullptr;
    QWidget* pendingDots = nullptr;
  };

  void styleProviderStatusDot(QLabel* dot, QToolButton* gear,
                              const QString& richTooltip,
                              ChatDock::ProviderStatus status,
                              const QPalette& pal);

}  // namespace stencil::gui
