#pragma once

// Shared chat helper widgets, split out of chatDock.cpp: the attachment
// HoverPreview, the in-flight TypingDots, the per-card "⋯" ChatCardMore watcher
// (+ its placement helper), and the card-arrival dust machinery. Used by both
// chat surfaces (dock + context-menu panel).

#include <QApplication>
#include <QCursor>
#include <QFrame>
#include <QGraphicsEffect>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>

namespace stencil::gui {

  class DisintegrateOverlay;   // support/disintegrateOverlay.hpp (the arrival's cloud)

  // A QLabel for UNTRUSTED text — model output, or a dropped filename (the Qt
  // spelling of the browser's textContent-only rule).
  QLabel* makePlainLabel(const QString& text, QWidget* parent);

  // Theme-provided muted text (palette PlaceholderText, not a hardcoded hex).
  void applyMutedText(QLabel* label);

  // Error text in the theme's --danger (browser .chat-msg-error).
  void applyDangerText(QLabel* label, const QColor& danger);

  // The small bold role caption every transcript card starts with.
  QLabel* makeRoleLabel(const QString& role, QWidget* card);

  // A message bubble's tail (browser .chat-msg-user::before/::after parity): a
  // small triangular flag hanging from the bubble's BOTTOM edge, at the corner
  // facing the panel centre — painted rather than QSS'd, since Qt stylesheets
  // have no clip-path/border-triangle equivalent. `right` picks which side it
  // attaches to (true = the bubble's right edge, the user side at rest);
  // `setColors` takes the SAME fill/border the card's own QSS uses, so the
  // tail reads as part of the bubble, not a separate shape.
  class ChatBubbleTail : public QWidget {
    Q_OBJECT
   public:
    explicit ChatBubbleTail(QWidget* parent);
    void setColors(const QColor& fill, const QColor& border);
    void setSide(bool right);
    bool isRight() const { return right_; }

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QColor fill_, border_;
    bool right_ = true;
  };

  // Attach (or move) `card`'s tail beside its bottom corner — right for a user
  // bubble, left otherwise (swappable: `right` is the caller's CURRENT resolved
  // side, not the raw role). Creates one on first call (parented to the card's
  // OWN parent, like ChatCardMore's "…" — outside the card's clip) and reuses it
  // after, linked BOTH ways (card→tail, tail→card) so a later reposition-only pass
  // needs no colour/side recomputation.
  void placeChatBubbleTail(QFrame* card, const QColor& fill, const QColor& border, bool right);
  // Re-place every tail already attached under `transcript` at its card's CURRENT
  // geometry, without touching colour or side — called after
  // applyChatBubbleWidths resizes/repositions the cards themselves, so a tail
  // never lags a bubble that just moved.
  void repositionChatBubbleTails(QWidget* transcript);

  // Where fillChatCard/setChatSwapSides (chatCardRenderer.cpp, chatMenuPanel.cpp)
  // stash a card's own chatBubbleOnRight() result (a bool QVariant) — placeChatCardMore
  // below reads it back to park the "…" trigger on whichever side the tail is NOT,
  // since objectName() alone (chatCardUser or not) stopped answering that once
  // "Swap message sides" could put either role on either side.
  inline constexpr const char* kChatOnRightProperty = "chatOnRight";

  // Park a card's "⋯" beside the bottom corner of its VISIBLE SLICE (see
  // chatWidgets.cpp for the placement rules). `avoidGlobal` (global/screen coords,
  // a null QRect ⇒ nothing to avoid) is furniture the button must not sit under — the
  // dock's jump pills: it shifts clear of them, or hides where there is no room to
  // (browser/extension parity — the pills are the higher-priority control and stay put).
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal = QRect());

  // Card-arrival dust, shared by both chat surfaces (browser motion.js chatIn)
  // A message arriving or leaving is the surface's main event, so it plays on a finer grid
  // than a list row. On the way in these only cap overSurface's own grid.
  inline constexpr int kChatScatterCols = 32;
  inline constexpr int kChatScatterRows = 16;
  // One frame between arrival hops: the callers' own scrollToBottom() is a
  // singleShot(0), so a 0ms hop would measure the box before that scroll landed.
  inline constexpr int kChatGatherSettleMs = 16;
  // How many hops a card may wait for the layout to give it a real box — a surface
  // shown this very turn has no width yet; past the budget the card simply appears.
  inline constexpr int kChatGatherTries = 6;

  // Is `card` wholly inside `scroll`'s viewport? The dust layer lives on the window
  // and the scroller does not clip it, so a card hanging past either edge would fly
  // its motes over whatever sits outside it. Taller than the viewport ⇒ false.
  bool chatCardFullyInViewport(QWidget* card, QScrollArea* scroll);

  // One arrival attempt per event-loop turn: wait (bounded by `tries`) for `layout` to
  // give `card` a stable box inside `scroll`'s viewport, then fly its dust into place over
  // `host` while the card hides behind the motes. `settle` writes the resting state and is
  // the one exit every bail-out takes, so a card is never stranded invisible. `onFlight`
  // runs the moment the dust launches — the dock hangs its margin slide off it.
  void gatherChatCardIn(QWidget* card, QVBoxLayout* layout, QScrollArea* scroll,
                        QWidget* host, int cols, int rows, std::function<void()> settle,
                        std::function<void()> onFlight = nullptr,
                        int tries = kChatGatherTries, QSize lastSize = QSize());

  // Keep a flying snapshot pinned to the card it photographed, and drop it the moment
  // the card leaves the viewport or changes size (a stale copy reads as one message
  // drawn over its neighbour — browser motion.js trackDust). The overlay deletes
  // itself when its animation ends, so `!overlay` is exactly "the flight is over".
  void trackChatCardDust(QWidget* card, DisintegrateOverlay* overlay, QScrollArea* scroll,
                         std::function<void()> settle);

  // Hover preview for a small attachment thumbnail
  // Tray chips show 28px and message bubbles 160px — too small to tell two
  // screenshots apart, so hovering pops the picture up at a readable size next to
  // it (browser chatView.js wireThumbPreview / extension chatUi.js parity). The
  // popup is a top-level tooltip window, so the dock's own clipping can't cut it.
  // A GLANCE, not a lightbox (browser .chat-thumb-preview / the projects modal's row
  // zoom): the picture is clamped so the conversation underneath stays readable. The
  // ceiling is also bounded by a fraction of the screen, like the browser's vw/vh.
  constexpr int kPreviewEdge = 220;
  constexpr double kPreviewScreenW = 0.25;
  constexpr double kPreviewScreenH = 0.20;
  class HoverPreview : public QObject {
    Q_OBJECT
   public:
    HoverPreview(QLabel* thumb, QImage full, QString caption);

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    void hide();
    void show();

    QPointer<QLabel> thumb_;
    QImage full_;
    QString caption_;
    QPointer<QWidget> popup_;
  };

  // Three dots bouncing in sequence while a turn is in flight — the Qt spelling of
  // the browser's .chat-typing (chatView.js typingDots) and the extension panel's.
  class TypingDots : public QWidget {
    Q_OBJECT
   public:
    explicit TypingDots(QWidget* parent);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    static constexpr int kDotSpan = 4;
    static constexpr int kDotGap = 3;
    static constexpr int kLift = 3;
    static constexpr int kSteps = 20;
    QTimer timer_;
    int phase_ = 0;
  };

  // One card's "⋯": hover reveal, placement (against the visible slice above),
  // and the grace period that lets the cursor cross the gap to the button.
  // Owned by the card, so it dies with it. Shared by both chat surfaces —
  // installChatCardMenu attaches one per card.
  class ChatCardMore : public QObject {
    Q_OBJECT
   public:
    ChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                 std::function<void()> moved, std::function<QRect()> avoidRect = nullptr);
    void place();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    bool shown() const { return more_ && more_->isVisible(); }
    void scheduleHide();

    QPointer<QFrame> card_;
    QPointer<QToolButton> more_;
    QScrollArea* scroll_ = nullptr;
    std::function<void()> moved_;
    std::function<QRect()> avoidRect_;   // global rect to shift/hide clear of; null ⇒ none
  };

}  // namespace stencil::gui
