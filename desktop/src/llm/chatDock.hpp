#pragma once
#include <QDockWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QColor>
#include <QPointer>
#include <QString>
#include <QVector>

#include "opPlan.hpp"
#include "pillSplitter.hpp"   // the shared composer resize grip
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
class MainWindowGuiTest;  // QtTest e2e (tests/mainWindow.<area>.gui.cpp)
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSplitter;
class QToolButton;
class QVBoxLayout;
class QWidget;

// AI-assistant chat panel (llm-contract.md), the desktop counterpart of the browser's.
// Unlike the deliberately pinned SelectionPanel it is dockable on ALL four sides and
// free-floating; MainWindow docks it LEFT by default. Pure UI — MainWindow owns the history,
// the LlmClient, plan execution and the reachability probes, and drives this through the slots
// below. Clipboard paste into the input and drag-drop onto the dock attach images/videos.
namespace stencil::gui {

  // Every free function here is SHARED with the context menu's assistant panel, so the two
  // surfaces render the same transcript from the same code.

  // Browser parity: prompt pills in a wrapping flow layout. `onPick` gets the prompt text —
  // callers PREFILL their composer with it and never send.
  QWidget* makeSuggestionChips(QWidget* parent, std::function<void(QString)> onPick);
  void styleSuggestionChips(QWidget* chips, const Palette& pal);

  // The gear reachability badges and the completion toast share the fixed green/red the
  // inline-rename ✓/✗ icons use (styleActionIcons).
  extern const char* const kChatStatusOkColor;
  extern const char* const kChatStatusBadColor;

  // 30 px box, 20 px glyph, picked up by the theme's QToolButton[chatAccent] rules.
  QToolButton* makeChatAccentButton(QWidget* parent, const QString& tooltip);

  // Browser .chat-msg parity: the role is carried by the card's object name — colour and side
  // — never by a caption, and the body is the full text, wrapped and selectable, never elided.
  enum class ChatCardKind { Bubble, Error, Muted };
  // Fills `lay` with the body label, tagged with the chatRole/chatBody properties mirrors and
  // tests read. Returns it, so the caller can extend the card (warnings, notes, thumbnails).
  QLabel* fillChatCard(QFrame* card, QVBoxLayout* lay, const QString& role,
                       const QString& text, ChatCardKind kind, const QColor& danger);
  // "Swap message sides" (Settings::chatSwapSides; browser/extension chatLayoutPrefs.js):
  // `swapped` flips the side, `user` alone decides it at rest — user right, everything else
  // left. Pure.
  inline bool chatBubbleOnRight(bool user, bool swapped) { return swapped ? !user : user; }
  // OPAQUE, flattened over `pageBg` (browser --msg-fill/--msg-border). `pageBg` is the surface
  // the cards sit ON, NOT `chip`, which is only the assistant bubble's own un-mixed fill.
  // false ⇒ no tail (Muted notices).
  bool chatBubbleColorsFor(const QString& objectName, const QColor& accent, const QColor& chip,
                           const QColor& border, const QColor& danger, const QColor& pageBg,
                           QColor& fillOut, QColor& borderOut);
  // Alignment in `layout`, the kChatOnRightProperty placeChatCardMore reads back, and the tail
  // (skipped for a tail-less card). Both surfaces run it on append AND on a swap re-skin.
  void applyChatBubbleSide(QFrame* card, QLayout* layout, bool right, const QColor& accent,
                           const QColor& chip, const QColor& border, const QColor& danger,
                           const QColor& pageBg);
  // The shared body of both surfaces' setChatSwapSides re-skin loop.
  void applyChatSwapToCards(QWidget* transcript, QLayout* layout, bool swapped,
                            const QColor& accent, const QColor& chip, const QColor& border,
                            const QColor& danger, const QColor& pageBg);
  // The hover "…" plus the right-click menu. Browser chatView.js chatRowMenuItems: EVERY
  // settled row, errors included, offers Copy message / Select all / Insert into prompt, and
  // user rows add Resend. Only these hooks differ per surface.
  struct ChatCardMenuHooks {
    QWidget* owner = nullptr;       // menu parent; also owns the hover timers
    QScrollArea* scroll = nullptr;  // the viewport the "…" must stay inside
    std::function<void(const QString&)> insertIntoPrompt;
    // Null ⇒ no Resend row; it is a user-bubble action (browser parity).
    std::function<void(QFrame*, const QString&)> resend;
    std::function<bool()> busy;     // greys Resend mid-turn
    // "Do not pop right now": a dock mid-close is still visible for the length of its slide,
    // and a menu opening out of a shrinking panel has nowhere to live once it lands.
    std::function<bool()> leaving;
    // Fired whenever a row's "…" is shown, hidden, moved or resized.
    std::function<void()> moreMoved;
    // Global-coords furniture the "…" must not sit under (the dock's jump pills): it shifts
    // clear, or hides when there is no room. Re-asked on every placement, so the caller's own
    // furniture can move independently.
    std::function<QRect()> avoidRect;
    QColor text, chip, border, accent, muted;   // theme tones for menu + button
  };
  void installChatCardMenu(QFrame* card, const ChatCardMenuHooks& hooks);
  // Parks the "…" against the VISIBLE SLICE of the card inside `scroll`'s viewport, so a row
  // clipped by the viewport edge still shows a reachable button. The default arg lives on the
  // one declaration in chatWidgets.hpp — repeating it here would be a redefinition.
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal);
  // The three bouncing dots an in-flight turn shows (a static "…" read as a hung request).
  QWidget* makeChatTypingDots(QWidget* parent);
  // Browser .chat-retry-cta: a neutral ghost glyph added to the card's own layout.
  QToolButton* addChatRetryButton(QVBoxLayout* lay, const QColor& glyph,
                                  std::function<void()> onClick);
  // Browser chatConfigureButton; `onClick` gets the button as the reveal's anchor.
  QPushButton* addChatConfigureCta(QVBoxLayout* lay, const QColor& accent,
                                   std::function<void(QPushButton*)> onClick);
  // A muted note line INSIDE a message card (warnings, executor notes, the late §3.2/§3.1
  // reports) — one bubble per turn, notes inside it, never extra rows.
  QLabel* addChatCardNote(QVBoxLayout* lay, const QString& text);

  // Caps the cards to a share of `scroll`'s viewport (browser .chat-msg max-width) and reserves
  // each wrapped label's real height — a word-wrapped QLabel clips its own last line otherwise.
  void applyChatBubbleWidths(QWidget* transcript, QScrollArea* scroll);
  // The QSS the card object names resolve against, applied by both hosts. `swapped` picks which
  // corner is flattened for the tail — the base 10 px radius left a notch against its straight
  // edges. Keyed on the whole sheet, not per card: a card's own local QSS shifts its wrapped
  // label's height, so a swap toggle re-issues all of it (see ChatDock::paletteCache_).
  QString chatCardStyleSheet(const Palette& pal, bool swapped);

  class ChatDock : public QDockWidget {
    Q_OBJECT
    friend class ::MainWindowGuiTest;   // QtTest e2e reaches the input box (tests/mainWindow.<area>.gui.cpp)
   public:
    explicit ChatDock(QWidget* parent = nullptr);

    // `images` are what the USER attached to this turn, never the working image, which rides
    // every turn (§7); they render as thumbnails inside the user's own bubble.
    void appendUser(const QString& text, const QList<QImage>& images = {});
    // `warnings` are the op-plan's unknown-op notices; `notes` are executor remarks about
    // SUCCESSFUL work. Both are muted lines inside the same bubble — one card per turn.
    void appendAssistant(const QString& text, const QStringList& warnings = {},
                         const QStringList& notes = {});
    // A non-empty `retryText` adds a one-click Retry emitting retryRequested(retryText) — the
    // same send path as typing it again.
    void appendError(const QString& text, const QString& retryText = QString());
    // Plus a labelled "Reconnect to <host>" action opening Connections. Distinct from an
    // ordinary failure, which offers only Resend.
    void appendExpiredSession(const QString& text, const QString& host,
                              const QString& retryText = QString());
    // Browser "unreachable" kind: a "Configure provider" action whose dialog flies FROM that
    // very button (configureProviderRequested), not from the "…" trigger.
    void appendUnreachable(const QString& text, const QString& retryText = QString());
    void addRetryButton(QVBoxLayout* lay, const QString& retryText);   // no-op without text
    void appendNote(const QString& text);   // muted informational card
    // A note about the LAST assistant reply: rides INSIDE that reply's bubble, since a separate
    // card reads as a second message.
    void appendLateNote(const QString& text);
    void warnAttachmentCap();   // one inline note when an attach exceeds the §7 image cap
    void appendNotice(const QString& text);   // a neutral note, not an error row
    // The in-flight "…" card: removed when the real reply lands, or converted in place to a
    // muted "Stopped." when the user aborts.
    void showPending();
    void clearPending();
    void markPendingStopped(const QString& stoppedText = QString());
    struct VariantCard {
      QString label;
      QImage image;
      QString projectId;  // project entry created for it ("" = none)
    };
    void appendVariants(const QVector<VariantCard>& variants);
    // Wipes every transcript card (a pending one included) and the attachment state, bringing
    // the empty state back. Provider settings and the working image are untouched, and the
    // model-side history lives in MainWindow, which clears it on clearRequested.
    void clearConversation();

    void setBusy(bool on);
    bool isBusy() const;
    // Re-skins every EXISTING card (alignment + tail side, never colour) and every card
    // appended after. Idempotent. MainWindow calls it from BOTH the dock's own toggle (via
    // chatSwapSidesChanged) and the saved setting at boot/restore.
    void setChatSwapSides(bool on);
    bool chatSwapSides() const { return chatSwapSides_; }
    // kFloatingSize floored by the minimum. Public so the owner's popover gesture can pin the
    // dock at this size next to the toolbar icon (MainWindow::openChatCompact).
    QSize floatingDefaultSize() const;
    void focusInput();   // the popover gesture opens ready to type
    // Un-sent text in the composer: the owner's Alt-peek release check treats that as
    // engagement, so a window is never yanked mid-typing.
    bool hasComposerText() const;
    bool dragPollActive() const;   // the zones overlay's liveness check
    // A title drag is live on EITHER path (event-driven or the poll seam) — what the zone
    // overlay's watchdog must gate on.
    bool dragActive() const;
    // The offscreen platform has no movable cursor or synthetic button state, so tests stub how
    // the drag poll reads them. Defaults to QCursor::pos / QGuiApplication::mouseButtons.
    void setDragProbesForTest(std::function<QPoint()> cursorPos,
                              std::function<bool()> leftButtonDown);
    // The dot on the settings gear; the rich tooltip carries provider/URL/model/status, which
    // MainWindow composes.
    enum class ProviderStatus { Unknown, Ok, Unreachable };
    void setProviderStatus(const QString& richTooltip, ProviderStatus status);
    // Button line-art plus the theme-tracking chrome (header sparkle/title, suggestion pills).
    // Called from MainWindow::applyTheme, like SelectionPanel::restyleIcons.
    void restyleIcons(const Palette& pal);

    // attachments (owned by the dock; MainWindow reads them on send)
    const QList<QImage>& attachedImages() const { return images_; }
    const QStringList& attachedImageNames() const { return imageNames_; }   // "" where unnamed
    QString attachedVideoPath() const { return videoPath_; }
    // The working image always rides along with a turn — the old composer eye toggle was
    // dropped with the 3-button row.
    bool useCurrentImage() const { return true; }
    // `name` is what the chip shows; empty falls back to the dimensions, since a clipboard
    // bitmap has no name.
    void addAttachmentImage(const QImage& img, const QString& name = QString());
    void clearAttachments();
    // Images AND videos, routed by suffix. Public because the context menu's assistant panel
    // reuses THIS picker, so both surfaces stage into the same attachment state.
    void pickMedia();

   public:
    // The turn's §11 question as a choice card under the reply: radios for a single-pick card,
    // checkboxes for a multi-pick one, each option showing the caller's preview image (empty =
    // label only). Submitting emits sendRequested() — the path typing the answer takes — and
    // locks the card.
    void appendAsk(const stencil::llm::AskCard& ask, const QVector<QImage>& previews);

    // The composer's "…" trigger. A window raised from inside that menu belongs to it — the
    // Settings item is gone by the time the window opens. Hidden with the dock, which is what
    // makes such a window fall from above when the chat isn't on screen (modalReveal's rule).
    QToolButton* moreButton() const { return more_; }

   signals:
    void sendRequested(const QString& text);
    // Re-send exactly this text; the owner guards busy.
    void retryRequested(const QString& text);
    // The send button clicked while a turn is in flight (STOP mode). Enter never stops — it is
    // ignored while busy.
    void stopRequested();
    // MainWindow extracts a preview frame / offers a server upload. The path is already
    // attachedVideoPath().
    void videoAttached(const QString& path);
    void videoDetached();   // the queued video chip was removed
    void openVariantRequested(const QString& projectId);
    // A note card the dock posted ITSELF, with no reply bubble to ride in: the owner mirrors it
    // onto the menu panel, so the panel shows what this transcript shows and nothing else.
    void notePosted(const QString& text);
    // A passing notice with no place in the transcript (the §7 attachment cap): the owner raises
    // it as an accent toast on its stack.
    void toastRequested(const QString& text);
    void lateNotePosted(const QString& text);   // went INTO the last assistant bubble
    void settingsRequested();
    // The title-bar trash: the dock already wiped its own transcript and attachments, so the
    // owner drops the per-conversation model state.
    void clearRequested();
    // The dock already re-skinned itself; the owner persists Settings::chatSwapSides and
    // propagates it to the mirror panel, so a change from either surface reaches both.
    void chatSwapSidesChanged(bool swapped);
    void reconnectRequested(const QString& host);   // the expired card's CTA
    // Unlike settingsRequested — the gear, reached through the "…" menu, which is closed by the
    // time the dialog opens — this CTA lives in the transcript and stays on screen through the
    // click, so the flight belongs to it. `anchor` is the button, for the reveal's origin.
    void configureProviderRequested(QWidget* anchor);
    // The title-bar X, and ANY close() on the dock. The owner answers by hiding it through the
    // ANIMATED path; the dock never hides itself, or the close blinks out.
    void closeRequested();
    // A title-bar placement button asked for a side (MainWindow docks it).
    void dockRequested(Qt::DockWidgetArea area);
    // MainWindow plays the animated transition — a raw setFloating() here would teleport it.
    void floatToggleRequested();
    void titleDragStarted();
    void titleDragMoved(const QPoint& globalPos);
    void titleDragFinished(const QPoint& globalPos);
    void titleDragCanceled();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    // The COMPOSER acts on a drop (eventFilter); these two make the DOCK swallow the ones that
    // miss it, so a drop onto the chat can never reach the window's own drop zone and offer to
    // open the image as a project (browser chatPanel.js parity).
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // A second drag trigger for when the native drag consumed the title-bar press.
    void moveEvent(QMoveEvent* event) override;
    void hideEvent(QHideEvent* event) override;  // never leak the drag poll
    void closeEvent(QCloseEvent* event) override;   // → closeRequested(), so the owner animates

   private:
    void submit();
    // STOP while busy, submit otherwise. Enter goes through submit directly, which bails while
    // busy — the single-turn guard.
    void onSendClicked();
    // Sparkle + accent title + float/close, replacing the native bar so there is no double
    // header; Qt's title drag still works through it.
    void buildTitleBar();
    void startDragPoll();   // lifecycle — see dragPoll_ below
    void pollDrag();
    void cancelDragPoll();
    // While a title drag runs, Qt's NATIVE dock-drop is locked out (allowedAreas =
    // NoDockWidgetArea) so only the zone overlay can dock; restored on every termination path.
    void setNativeDockingSuppressed(bool on);
    // Image data or image/video file URLs; true when anything was attached. The shared body of
    // clipboard paste and drag-drop.
    bool attachFromMimeData(const QMimeData* mime);
    bool canAttachMime(const QMimeData* mime) const;
    void showDropCue(bool on);      // the composer's "Drop to attach" cue + its icon animation
    void buildSuggestions();        // the empty state, built into the transcript column
    // Inserted above the bottom stretch, scroll deferred; the caller populates the layout.
    QVBoxLayout* appendTranscriptCard(int spacing);
    // Browser parity: the card's dust gathers into place out of a point off the side it sits
    // against — a toast arriving, not the Clear scatter reversed — while the card comes up
    // behind the motes over a ~6 px slide. Each card owns its animation, so overlapping appends
    // never interfere; reduced motion (or a failed grab) leaves the plain fade + slide.
    void animateCardIn(QWidget* card, QVBoxLayout* lay);
    // The second half, one event-loop turn later, once the caller has populated the card: this
    // dock's settle/slide around the SHARED gatherChatCardIn machinery (chatWidgets.hpp), which
    // owns the layout waits, the viewport gate and the per-frame snapshot tracking.
    void startCardEntrance(QWidget* card, QVBoxLayout* lay);
    // Error = danger wash + danger text, real turn failures only.
    enum class CardKind { Bubble, Error, Muted };
    QVBoxLayout* appendCard(const QString& role, const QString& text, CardKind kind);
    // EVERY settled card — user, assistant, note, error. Installed on the card AND its labels,
    // since a selectable QLabel would otherwise pop Qt's own menu; safe to call again after
    // late labels join the card.
    void installCardMenu(QFrame* card);
    ChatCardMenuHooks cardMenuHooks();
    // Is a real card on screen (empty state / stretch excluded)? Gates the deferred empty state.
    bool transcriptHasCards() const;
    // Show a … item only while it can act — inapplicable ones hide, they do not grey out.
    void syncMoreMenuItems();
    void refreshAttachmentTray();
    void applyBubbleWidths();
    // Browser .chat-jumps: visibility from the scrollbar's position, geometry from the viewport's.
    void updateJumpButtons();
    // The pills' united global box, null while neither is shown. Fed to placeChatCardMore as the
    // furniture a row's "…" must shift clear of, or hide rather than sit under.
    QRect jumpPillsGlobalRect() const;
    // After the pills move, show or hide. Cheap while no "…" is shown — a direct-children
    // lookup, with placement running only for visible buttons.
    void revalidateMoreButtons();

   public:
    // Set by the owner while this dock animates OUT: the row menu refuses to pop into a surface
    // that is about to be hidden.
    void setClosing(bool on) { closing_ = on; }

   private:
    bool closing_ = false;
    void positionJumpButtons();
    void updateSendEnabled();
    void scrollToBottom();

    PillSplitter* splitter_ = nullptr;   // transcript over the resizable input area
    QScrollArea* scroll_ = nullptr;
    QToolButton* jumpTop_ = nullptr;     // ⌃ to the beginning (overlay on scroll_)
    QToolButton* jumpBottom_ = nullptr;  // ⌄ to the latest message
    class ScrollReveal* reveal_ = nullptr;   // edge fade over the transcript (scrollReveal.hpp)
    QWidget* transcript_ = nullptr;
    QVBoxLayout* transcriptLayout_ = nullptr;
    QWidget* titleBar_ = nullptr;     // the branded title-bar widget
    QLabel* headerIcon_ = nullptr;    // sparkle glyph (accent-tinted)
    QLabel* headerTitle_ = nullptr;   // accent "Assistant" title (browser header parity)
    QToolButton* clearBtn_ = nullptr; // clear the conversation (trash ghost)
    QToolButton* floatBtn_ = nullptr; // float/dock toggle in the title bar
    QToolButton* closeBtn_ = nullptr; // close in the title bar
    QList<QToolButton*> dockBtns_;    // left/top/bottom/right chevrons (browser parity)
    QColor accentCache_, textCache_, dangerCache_, mutedCache_;  // for restyling placement state on the fly
    QColor chipCache_;   // active placement button's ground (browser --bg-container)
    QColor borderCache_; // themed hairline (browser --border-main)
    // The last Palette restyleIcons() ran with: setChatSwapSides re-issues chatCardStyleSheet
    // against it, since the flattened tail corner is keyed off chatSwapSides_ too.
    Palette paletteCache_;
    // Set at construction from the saved Settings::chatSwapSides.
    bool chatSwapSides_ = false;
    QAction* actSwapSides_ = nullptr;   // … menu item, between Clear and Settings
    // Highlights the button matching the CURRENT placement and makes it inert (browser
    // .chat-dock-btn-active): you cannot re-dock where you already are.
    void updatePlacementState();
    // Event-driven title drag: the press/move/release are CONSUMED so Qt never starts its own
    // (on macOS, window-server) move — that variant swallows the release, leaving the drop
    // undetectable and nothing docked.
    bool manualDrag_ = false;      // a left press landed on the bar itself
    bool manualDragging_ = false;  // past the threshold: the window follows us
    QPoint manualGrabOffset_;      // cursor → frame top-left while dragging
    // ~16 ms poll, because the native floating-window drag (macOS) swallows move/release
    // events: the drag is followed via QCursor::pos() + QGuiApplication::mouseButtons().
    QTimer* dragPoll_ = nullptr;
    QPoint dragStartCursor_;
    bool dragMoved_ = false;   // ≥4 px from the press = a drag, not a click
    bool dragActive_ = false;  // titleDragStarted emitted
    std::function<QPoint()> dragPosProbe_;  // test seams ("" = the real cursor)
    std::function<bool()> dragDownProbe_;
    QWidget* inputArea_ = nullptr;    // the composer — the drop target (see showDropCue)
    QWidget* dropCue_ = nullptr;      // "Drop to attach" overlay over the composer
    QLabel* dropCueIcon_ = nullptr;   // its bobbing glyph (re-tinted on theme change)
    QLabel* dropCueText_ = nullptr;
    QVariantAnimation* dropCueAnim_ = nullptr;
    QWidget* suggest_ = nullptr;      // the empty state's prompt chips, hidden once a card lands
    QPlainTextEdit* input_ = nullptr;
    // Exactly three composer buttons (browser parity): send, attach, gear.
    QToolButton* send_ = nullptr;
    QToolButton* attach_ = nullptr;       // images AND videos, one dialog
    QToolButton* gear_ = nullptr;         // settings target (now behind the … menu)
    QToolButton* more_ = nullptr;         // the … overflow: attach / clear / settings
    QAction* actAttach_ = nullptr;        // … menu items (mirror the hidden buttons)
    QAction* actClear_ = nullptr;
    QAction* actSettings_ = nullptr;
    QLabel* statusDot_ = nullptr;         // reachability dot next to the gear
    QPointer<QWidget> lastAssistantCard_;  // the newest reply bubble (late notes)
    // Browser .chat-attach-chip: one chip per queued image/video, with a thumbnail, a label
    // and a remove ×.
    QWidget* attachTray_ = nullptr;
    QProgressBar* busy_ = nullptr;
    // Deliberately not derived from busy_'s visibility: turns can run while the whole dock is
    // hidden, since the context-menu chat drives the same pipeline.
    bool busyFlag_ = false;

    QList<QImage> images_;

    QElapsedTimer capToastAt_;   // folds one batch's repeated cap hits into a single toast
    QStringList imageNames_;   // in lockstep with images_; "" = unnamed
    QString videoPath_;
    // True while the view sits at or near the bottom, so new content may auto-follow; scrolling
    // up releases it, sends re-arm it.
    bool stickToBottom_ = true;
    QWidget* pendingCard_ = nullptr;   // the in-flight "…" card; see showPending/clearPending
    QLabel* pendingRole_ = nullptr;
    QLabel* pendingBody_ = nullptr;
    QWidget* pendingDots_ = nullptr;   // the bouncing dots while a turn is in flight
  };

  // Colours the corner dot for the status (palette Mid when unknown) and puts the rich tooltip
  // on both the gear and the dot.
  void styleProviderStatusDot(QLabel* dot, QToolButton* gear,
                              const QString& richTooltip,
                              ChatDock::ProviderStatus status,
                              const QPalette& pal);

}  // namespace stencil::gui
