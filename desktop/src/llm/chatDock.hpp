#pragma once
#include <QDockWidget>
#include <QImage>
#include <QList>
#include <QColor>
#include <QPointer>
#include <QString>
#include <QVector>

#include "opPlan.hpp"
#include "pillSplitter.hpp"   // the shared composer resize grip
#include <functional>

class QAction;
class QFrame;
class QCloseEvent;
class QHideEvent;
class QLabel;
class QMoveEvent;
class QPalette;
class QDragEnterEvent;
class QDropEvent;
class QTimer;
class QVariantAnimation;
class QMimeData;
class QPlainTextEdit;
class MainWindowGuiTest;  // QtTest e2e (tests/mainWindow.gui.cpp)
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSplitter;
class QToolButton;
class QVBoxLayout;
class QWidget;

// AI-assistant chat panel — the desktop counterpart of the browser's chat panel
// in the LLM plan (llm-contract.md; Phase 3). A QDockWidget that, unlike
// the deliberately pinned SelectionPanel, is dockable on ALL four sides AND
// free-floating (drag-move + resize) via the native dock features; MainWindow
// docks it LEFT by default (browser parity). The dock is pure UI: transcript
// over a user-resizable input area (QSplitter), attach buttons + a single
// settings gear with a status dot grouped right of the input, busy indicator.
// Provider details live on the gear's rich tooltip (styled by the app-wide
// QToolTip QSS), not a label. MainWindow owns the chat logic (history,
// LlmClient, plan execution, reachability probes) and drives it through the
// slots below. Clipboard paste into the input and drag-drop onto the dock
// attach images/videos (mediaLoader's suffix sniffers route them).
namespace stencil::gui {

  struct Palette;  // support/theme.hpp

  // Empty-state suggestion chips (browser parity): prompt pills in a wrapping
  // flow layout, SHARED by the dock and the context menu's assistant panel so
  // the two empty states stay identical. `onPick` gets the prompt text —
  // callers PREFILL their composer with it and never send.
  QWidget* makeSuggestionChips(QWidget* parent, std::function<void(QString)> onPick);
  // Re-tint a chip block for the current theme (call from the owner's restyle).
  void styleSuggestionChips(QWidget* chips, const Palette& pal);

  // Status colours shared by the gear reachability badges and the chat
  // completion toast: the same fixed green/red the inline-rename ✓/✗ icons use
  // (styleActionIcons).
  extern const char* const kChatStatusOkColor;
  extern const char* const kChatStatusBadColor;

  // Filled accent composer button (send / attach / gear), shared by the dock
  // and the context menu's assistant panel so the two composers read
  // identically: 30 px box, 20 px glyph, picked up by the theme's
  // QToolButton[chatAccent] rules.
  QToolButton* makeChatAccentButton(QWidget* parent, const QString& tooltip);

  // ── Transcript card rendering, SHARED by the dock and the context menu's
  // assistant panel (browser .chat-msg parity). The role is carried by the
  // card's object name — colour + side — never by a caption; the body is the
  // full text, wrapped and selectable, never elided.
  enum class ChatCardKind { Bubble, Error, Muted };
  // Give `card` its role object name and fill `lay` with the body label (tagged
  // with the chatRole/chatBody properties mirrors and tests read). Returns the
  // label so the caller can extend the card (warnings, notes, thumbnails).
  QLabel* fillChatCard(QFrame* card, QVBoxLayout* lay, const QString& role,
                       const QString& text, ChatCardKind kind, const QColor& danger);
  // ── Per-message row menu: the hover "…" plus the right-click menu, SHARED by
  // the dock and the context menu's assistant panel (browser chatView.js
  // chatRowMenuItems: EVERY settled row — errors included — offers Copy message /
  // Select all / Insert into prompt, and user rows add Resend). Only these hooks
  // differ per surface.
  struct ChatCardMenuHooks {
    QWidget* owner = nullptr;       // menu parent; also owns the hover timers
    QScrollArea* scroll = nullptr;  // the viewport the "…" must stay inside
    std::function<void(const QString&)> insertIntoPrompt;
    // Null ⇒ no Resend row (it is a user-bubble action, browser parity).
    std::function<void(QFrame*, const QString&)> resend;
    std::function<bool()> busy;     // greys Resend mid-turn
    // "Do not pop right now": the surface is on its way out (a dock mid-close is
    // still visible for the length of its slide, and a menu opening out of a
    // shrinking panel has nowhere to live once it lands).
    std::function<bool()> leaving;
    // Fired whenever a row's "…" is shown, hidden, moved or resized, so a
    // surface with its own floating furniture (the dock's jump pills) can get
    // out of its way. Null where there is nothing to move.
    std::function<void()> moreMoved;
    QColor text, chip, border, accent, muted;   // theme tones for menu + button
  };
  void installChatCardMenu(QFrame* card, const ChatCardMenuHooks& hooks);
  // Park a card's "…" against the VISIBLE SLICE of the card inside `scroll`'s
  // viewport (the intersection), so a row clipped by the viewport edge still
  // shows a reachable button instead of one parked out of sight.
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll);
  // The three bouncing dots an in-flight turn shows. Shared so the context
  // menu's assistant panel animates exactly like the dock (its pending row used
  // to be a static "…", which read as a hung request).
  QWidget* makeChatTypingDots(QWidget* parent);
  // The error/stopped card's Resend control (browser .chat-retry-cta): a neutral
  // ghost glyph, added to the card's own layout. Shared so both surfaces offer it.
  QToolButton* addChatRetryButton(QVBoxLayout* lay, const QColor& glyph,
                                  std::function<void()> onClick);
  // A muted note line INSIDE a message card (warnings, executor notes, the late
  // §3.2/§3.1 reports). Shared so a mirrored card carries them exactly as the
  // dock's does — one bubble per turn, notes inside it, never extra rows.
  QLabel* addChatCardNote(QVBoxLayout* lay, const QString& text);

  // Cap the transcript cards in `transcript` to a share of `scroll`'s viewport
  // (browser .chat-msg max-width) and reserve each wrapped label's real height —
  // a word-wrapped QLabel clips its own last line otherwise. Run after appending
  // and on every viewport resize.
  void applyChatBubbleWidths(QWidget* transcript, QScrollArea* scroll);
  // The QSS the card object names above resolve against — both hosts apply it
  // in their own restyle, so a bubble looks the same wherever it is rendered.
  QString chatCardStyleSheet(const Palette& pal);

  class ChatDock : public QDockWidget {
    Q_OBJECT
    friend class ::MainWindowGuiTest;   // QtTest e2e reaches the input box (tests/mainWindow.gui.cpp)
   public:
    explicit ChatDock(QWidget* parent = nullptr);

    // ── transcript ──
    // `images` are what the USER attached to this turn (never the working image,
    // which rides every turn — contract §7); they render as thumbnails inside the
    // user's own bubble, mirroring the browser/extension .chat-attached strip.
    void appendUser(const QString& text, const QList<QImage>& images = {});
    // `warnings` are the op-plan's unknown-op notices, shown under the reply.
    // `notes` are executor remarks about SUCCESSFUL work (e.g. "Opened X in the
    // editor first") — muted lines inside the same bubble, one card per turn.
    void appendAssistant(const QString& text, const QStringList& warnings = {},
                         const QStringList& notes = {});
    // `retryText` non-empty adds a one-click "Retry" under the error that emits
    // retryRequested(retryText) — the same send path as typing it again.
    void appendError(const QString& text, const QString& retryText = QString());
    // An EXPIRED-session error card: the message plus a labelled "Reconnect to
    // <host>" action that opens Connections (browser parity). Distinct from an
    // ordinary failure, which offers only Resend.
    void appendExpiredSession(const QString& text, const QString& host,
                              const QString& retryText = QString());
    // Adds the shared one-click Retry to a card's layout (no-op without text).
    void addRetryButton(QVBoxLayout* lay, const QString& retryText);
    // A muted informational card (e.g. "text-only model — image not sent").
    void appendNote(const QString& text);
    // A note about the LAST assistant reply (a skipped action, a canceled clear):
    // rides inside that reply's bubble — a separate card read as a second message.
    void appendLateNote(const QString& text);
    // One inline note when an attach attempt exceeds the §7 image cap (browser parity).
    void warnAttachmentCap();
    // Muted informational card (e.g. "assistant turned off") — configure-style
    // guidance, not an error row.
    void appendNotice(const QString& text);
    // Pending "…" assistant card while a turn is in flight: shown on send,
    // removed when the real reply lands (clearPending) or converted in place
    // to a muted "Stopped." when the user aborts (markPendingStopped).
    void showPending();
    void clearPending();
    void markPendingStopped(const QString& stoppedText = QString());
    struct VariantCard {
      QString label;
      QImage image;
      QString projectId;  // project entry created for it ("" = none)
    };
    void appendVariants(const QVector<VariantCard>& variants);
    // Wipe the conversation UI: every transcript card (incl. a pending one) and
    // the attachment state, then the empty state (suggestion chips) comes back.
    // Provider settings and the working image are deliberately untouched. The
    // model-side history lives in MainWindow, which clears it on clearRequested.
    void clearConversation();

    // ── state ──
    void setBusy(bool on);
    bool isBusy() const;
    // The compact tear-off size every float adopts (kFloatingSize, floored by the
    // minimum) — public so the owner's popover gesture can pin the dock at this
    // size next to the toolbar icon (MainWindow::openChatCompact).
    QSize floatingDefaultSize() const;
    // Drop the caret into the composer (the popover gesture opens ready to type).
    void focusInput();
    // Un-sent text sits in the composer — the owner's Alt-peek release check
    // treats that as engagement (never yank a window mid-typing).
    bool hasComposerText() const;
    // True while the title-drag poll runs — the owner's zones overlay uses it
    // as its liveness check.
    bool dragPollActive() const;
    // A title drag is live on EITHER path (event-driven or the poll seam) —
    // what the zone overlay's watchdog must gate on.
    bool dragActive() const;
    // Test seam: the offscreen platform has no movable cursor / synthetic
    // button state, so tests stub how the drag poll reads them. Defaults to
    // QCursor::pos / QGuiApplication::mouseButtons.
    void setDragProbesForTest(std::function<QPoint()> cursorPos,
                              std::function<bool()> leftButtonDown);
    // Provider reachability shown as the dot on the settings gear; the rich
    // tooltip carries provider/URL/model/status (MainWindow composes it).
    enum class ProviderStatus { Unknown, Ok, Unreachable };
    void setProviderStatus(const QString& richTooltip, ProviderStatus status);
    // Re-tint the button line-art icons + the theme-tracking chrome (header
    // sparkle/title, suggestion pills). Called from MainWindow::applyTheme,
    // like SelectionPanel::restyleIcons, with the current theme palette.
    void restyleIcons(const Palette& pal);

    // ── attachments (owned by the dock; MainWindow reads them on send) ──
    const QList<QImage>& attachedImages() const { return images_; }
    // Filenames in lockstep with attachedImages() ("" where unnamed).
    const QStringList& attachedImageNames() const { return imageNames_; }
    QString attachedVideoPath() const { return videoPath_; }
    // The working image always rides along with a turn (browser parity — the
    // old composer eye toggle was dropped with the 3-button row).
    bool useCurrentImage() const { return true; }
    // `name` is what the chip shows (a filename); empty falls back to the
    // dimensions — a clipboard bitmap has no name to show.
    void addAttachmentImage(const QImage& img, const QString& name = QString());
    void clearAttachments();
    // The single attach dialog (images AND videos, routed by suffix). Public
    // because the context menu's assistant panel reuses THIS picker, so both
    // surfaces stage into the same attachment state.
    void pickMedia();

   public:
    // Render the turn's §11 question as a choice card under the reply: radios for a
    // single-pick card, checkboxes for a multi-pick one, each option showing the preview
    // image the caller rendered for it (empty = label only), then the optional free-text
    // row and a Submit that stays disabled until something is chosen. Submitting emits
    // sendRequested() — the same path typing the answer takes — and locks the card.
    void appendAsk(const stencil::llm::AskCard& ask, const QVector<QImage>& previews);

   signals:
    void sendRequested(const QString& text);
    // A failed turn's Retry button: re-send exactly this text (owner guards busy).
    void retryRequested(const QString& text);
    // The send button clicked while a turn is in flight (STOP mode): the
    // owner aborts the request; Enter never stops (it is ignored while busy).
    void stopRequested();
    // A video file was picked/pasted/dropped; MainWindow extracts a preview
    // frame / offers a server upload. The path is already attachedVideoPath().
    void videoAttached(const QString& path);
    // The user removed the queued video chip — the owner drops its video input.
    void videoDetached();
    void openVariantRequested(const QString& projectId);
    // A note card the dock posted ITSELF (the attachment cap, a late note with
    // no reply bubble to ride in): the owner mirrors it onto the menu panel, so
    // the panel shows what this transcript shows and nothing else.
    void notePosted(const QString& text);
    // A late note that went INTO the last assistant bubble — mirrored the same way.
    void lateNotePosted(const QString& text);
    void settingsRequested();
    // The title-bar trash button: the dock already wiped its own transcript /
    // attachments; the owner drops the per-conversation model state (history,
    // video input, encoded working-image cache).
    void clearRequested();
    // The expired card's CTA: open Connections for `host` so the user can sign in.
    void reconnectRequested(const QString& host);
    // The title-bar X — and ANY close() on the dock (a native affordance, a
    // shortcut, a programmatic call). The owner answers by hiding it through the
    // ANIMATED path; the dock never hides itself, or the close blinks out.
    void closeRequested();
    // Title-bar drag tracking for the drag dock zones (floating only): the
    // owner shows edge drop bands during the drag and docks on release at the
    // last cursor position. Canceled = the dock hid mid-drag (just dismiss).
    // A title-bar placement button asked for a side (MainWindow docks it).
    void dockRequested(Qt::DockWidgetArea area);
    void titleDragStarted();
    void titleDragMoved(const QPoint& globalPos);
    void titleDragFinished(const QPoint& globalPos);
    void titleDragCanceled();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    // The COMPOSER acts on a drop (eventFilter); these two make the DOCK swallow the
    // ones that miss it, so a drop onto the chat can never reach the window's own drop
    // zone and offer to open the image as a project (browser chatPanel.js parity).
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    // A floating dock being dragged moves continuously — a second drag trigger
    // for when the native drag consumed the title-bar press.
    void moveEvent(QMoveEvent* event) override;
    void hideEvent(QHideEvent* event) override;  // never leak the drag poll
    // Converts a close into closeRequested() so the owner can animate it out.
    void closeEvent(QCloseEvent* event) override;

   private:
    void submit();
    // The send button's click: STOP while busy, submit otherwise (Enter goes
    // through submit directly, which bails while busy — the single-turn guard).
    void onSendClicked();
    // Branded title bar (sparkle + accent title + float/close) replacing the
    // native one — no double header; Qt's title drag still works through it.
    void buildTitleBar();
    // Drag-poll lifecycle (see the member comment).
    void startDragPoll();
    void pollDrag();
    void cancelDragPoll();
    // While a title drag runs, Qt's NATIVE dock-drop is locked out
    // (allowedAreas = NoDockWidgetArea) so only the zone overlay can dock;
    // restored on every poll-termination path.
    void setNativeDockingSuppressed(bool on);
    // Attach whatever the mime payload carries (image data, image/video file
    // URLs). Returns true when anything was attached — the shared body of
    // clipboard paste and drag-drop.
    bool attachFromMimeData(const QMimeData* mime);
    // Is this payload attachable (raw image data, or a local image/video file)?
    bool canAttachMime(const QMimeData* mime) const;
    // Show/hide the composer's "Drop to attach" cue (and run its icon animation).
    void showDropCue(bool on);
    // Empty-state suggestion chips (caption + prefill pills), built into the
    // transcript column ahead of the stretch.
    void buildSuggestions();
    // New framed transcript card (inserted above the bottom stretch, scroll
    // deferred); the caller populates the returned layout.
    QVBoxLayout* appendTranscriptCard(int spacing);
    // Subtle appear motion for a freshly inserted card (browser parity): ~140 ms
    // fade with a ~6 px upward slide. Each card owns its own animation (parented
    // to it, DeleteWhenStopped), so overlapping appends never interfere and a
    // card deleted mid-flight takes its animation with it.
    void animateCardIn(QWidget* card, QVBoxLayout* lay);
    // Card treatment: Bubble = user/assistant tone, Error = danger wash + danger
    // text (real turn failures only), Muted = quiet informational card.
    enum class CardKind { Bubble, Error, Muted };
    QVBoxLayout* appendCard(const QString& role, const QString& text, CardKind kind);
    // Transcript row menu (EVERY settled card — user, assistant, note, error):
    // the shared installChatCardMenu with this dock's hooks. Installed on the
    // card AND its labels (a selectable QLabel would otherwise pop Qt's own
    // menu); safe to call again after late labels join the card.
    void installCardMenu(QFrame* card);
    // This surface's hooks for the shared row menu (composer, resend, tones).
    ChatCardMenuHooks cardMenuHooks();
    // Is a real transcript card on screen (empty state / stretch excluded)? Gates the
    // deferred empty state after a clear.
    bool transcriptHasCards() const;
    void refreshAttachmentTray();
    // Cap transcript bubbles to a share of the viewport (browser max-width).
    void applyBubbleWidths();
    // Jump pills over the transcript's bottom edge (browser .chat-jumps parity):
    // visibility from the scrollbar's position, geometry from the viewport's.
    void updateJumpButtons();
    // True while a row's "…" overlaps the jump pills (they yield to it).
    bool jumpPillsBlocked() const;

   public:
    // Set by the owner while this dock is animating OUT: the row menu refuses to
    // pop into a surface that is about to be hidden.
    void setClosing(bool on) { closing_ = on; }

   private:
    bool closing_ = false;
    void positionJumpButtons();
    void updateSendEnabled();
    void scrollToBottom();

    PillSplitter* splitter_ = nullptr;   // transcript over the resizable input area
    QScrollArea* scroll_ = nullptr;
    QToolButton* jumpTop_ = nullptr;     // ⌃ jump to the very beginning (overlay on scroll_)
    QToolButton* jumpBottom_ = nullptr;  // ⌄ jump to the latest message
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
    // Highlight the button matching the CURRENT placement and make it inert
    // (browser .chat-dock-btn-active): you can't re-dock where you already are.
    void updatePlacementState();
    // Event-driven title drag: the title-bar press/move/release are CONSUMED so
    // Qt never starts its own (on macOS: window-server) move — that variant
    // swallows the release, leaving the drop undetectable and nothing docked.
    bool manualDrag_ = false;      // a left press landed on the bar itself
    bool manualDragging_ = false;  // past the threshold: the window follows us
    QPoint manualGrabOffset_;      // cursor → frame top-left while dragging
    // Poll-driven title-drag tracking (~16 ms): the native floating-window
    // drag (macOS) swallows move/release events, so the drag is followed via
    // QCursor::pos() + QGuiApplication::mouseButtons(), not event delivery.
    QTimer* dragPoll_ = nullptr;
    QPoint dragStartCursor_;
    bool dragMoved_ = false;   // ≥4 px from the press = a drag, not a click
    bool dragActive_ = false;  // titleDragStarted emitted
    std::function<QPoint()> dragPosProbe_;  // test seams ("" = the real cursor)
    std::function<bool()> dragDownProbe_;
    // Empty-state suggestion chips (caption + prefill pills); hidden once the
    // first transcript card lands.
    QWidget* inputArea_ = nullptr;    // the composer — the drop target (see showDropCue)
    QWidget* dropCue_ = nullptr;      // "Drop to attach" overlay over the composer
    QLabel* dropCueIcon_ = nullptr;   // its bobbing glyph (re-tinted on theme change)
    QLabel* dropCueText_ = nullptr;
    QVariantAnimation* dropCueAnim_ = nullptr;
    QWidget* suggest_ = nullptr;      // the empty state: the prompt chips
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
    // Pending-attachment chips (browser .chat-attach-chip parity): one chip per
    // queued image/video with a thumbnail, a label and a remove ×.
    QWidget* attachTray_ = nullptr;
    QProgressBar* busy_ = nullptr;
    // The in-flight flag itself. Deliberately not derived from busy_'s
    // visibility: turns can run while the whole dock is hidden (the context-menu
    // chat drives the same pipeline).
    bool busyFlag_ = false;

    QList<QImage> images_;
    QStringList imageNames_;   // in lockstep with images_; "" = unnamed
    QString videoPath_;
    // Chat stickiness: true while the view sits at (or near) the bottom, so new
    // content may auto-follow; scrolling up releases it, sends re-arm it.
    bool stickToBottom_ = true;
    // The in-flight "…" card (null when none); see showPending/clearPending.
    QWidget* pendingCard_ = nullptr;
    QLabel* pendingRole_ = nullptr;
    QLabel* pendingBody_ = nullptr;
    QWidget* pendingDots_ = nullptr;   // the bouncing dots while a turn is in flight
  };

  // Gear reachability badge, shared by the dock and the context menu's
  // assistant panel: colour the corner dot for the status (palette Mid when
  // unknown) and put the rich tooltip on both the gear and the dot.
  void styleProviderStatusDot(QLabel* dot, QToolButton* gear,
                              const QString& richTooltip,
                              ChatDock::ProviderStatus status,
                              const QPalette& pal);

}  // namespace stencil::gui
