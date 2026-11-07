#pragma once
// The chat helper widgets both chat surfaces share (the dock and the context-menu panel): the bubble tail, the
// card photograph, its pour and ghost entrances and their clocks. Browser twin: ui/motion/control/chatFx.js and
// the chat row modules under ui/chat/.
#include "../../support/tune/motionTable.hpp"
#include "../../support/vsync/FramePulse.hpp"

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
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

  class DisintegrateOverlay;

  // A QLabel for UNTRUSTED text — the Qt spelling of the browser's textContent-only rule.
  QLabel* makePlainLabel(const QString& text, QWidget* parent);

  void applyMutedText(QLabel* label);

  void applyDangerText(QLabel* label, const QColor& danger);

  QLabel* makeRoleLabel(const QString& role, QWidget* card);

  // Browser .chat-msg-user::before/::after parity; painted, since QSS has no clip-path.
  class ChatBubbleTail : public QWidget {
    Q_OBJECT
   public:
    explicit ChatBubbleTail(QWidget* parent);
    void setColors(const QColor& fill, const QColor& border);
    void setSide(bool isOnRight);
    bool isRight() const { return isOnRight; }

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QColor fill, border;
    bool isOnRight = true;
  };

  // Parented to the card's OWN parent (outside its clip), linked both ways with the card.
  void placeChatBubbleTail(QFrame* card, const QColor& fill, const QColor& border, bool isOnRight);
  // Reposition only; called after applyChatBubbleWidths moves the cards.
  void repositionChatBubbleTails(QWidget* transcript);

  // A card's resolved isChatBubbleOnRight() (bool QVariant); objectName() alone stopped answering it
  // once "Swap message sides" could put either role on either side.
  inline constexpr const char* CHAT_ON_RIGHT_PROPERTY = "chatOnRight";

  // `avoidGlobal` (global coords, null ⇒ none) is furniture the button shifts clear of, or hides.
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal = QRect());

  // Card-arrival dust (browser control/chatFx.js chatIn): a finer grid than a list row.
  inline constexpr int CHAT_SCATTER_COLS = 32;
  inline constexpr int CHAT_SCATTER_ROWS = 16;
  // One frame: the callers' scrollToBottom() is a singleShot(0), so a 0 ms hop measures too early.
  inline constexpr int CHAT_GATHER_SETTLE_MS = 16;
  // A surface shown this very turn has no width yet; past the budget the card simply appears.
  inline constexpr int CHAT_GATHER_TRIES = 6;

  // The dust layer is unclipped by the scroller, so a card past an edge would fly motes outside.
  bool chatCardFullyInViewport(QWidget* card, QScrollArea* scroll);

  // A card's entrance without particles, ms (common/config/motion.json CHAT_SIDE_MS / CHAT_SLIDE_MS): slid in from
  // its side, or grown from its tail corner.
  inline int chatSideMs() { return support::motionTableMs("CHAT_SIDE_MS", 420); }
  inline int chatGrowMs() { return support::motionTableMs("CHAT_SLIDE_MS", 320); }
  // The card's bubble alone on clear pixels, with its entrance veil lifted for the photograph.
  QPixmap chatCardShot(QWidget* card);
  // Whether `card` sits against the right side of `seen` rather than the left: its tail's and its motes' side.
  inline bool isOnRightSide(const QRect& seen, const QRect& card) { return (seen.right() - card.right()) <= (card.left() - seen.left()); }
  // Where Sliding pours a card from and back into (GLOBAL): a strip on the edge of `seen` (the transcript's view)
  // on the card's side, level with its tail (browser chatFx.js chatTailHome).
  QRect chatTailHome(const QRect& seen, const QRect& at);
  // Sliding's entrance: poured out of its tail home, clipped to the transcript; false when it cannot fly.
  bool pourCardIn(QWidget* card, QScrollArea* scroll, std::function<void()> done);
  void ghostCardIn(QWidget* card, bool isFromRight, bool shouldSlide, std::function<void()> done);

  // `settle` is the one exit every bail-out takes, so a card is never stranded invisible.
  void gatherChatCardIn(QWidget* card, QVBoxLayout* layout, QScrollArea* scroll,
                        QWidget* host, int cols, int rows, std::function<void()> settle,
                        std::function<void()> onFlight = nullptr,
                        int tries = CHAT_GATHER_TRIES, QSize lastSize = QSize());

  // Drops the snapshot when the card leaves the viewport or resizes (browser control/chatFx.js trackDust).
  void trackChatCardDust(QWidget* card, DisintegrateOverlay* overlay, QScrollArea* scroll,
                         std::function<void()> settle);

  // Hover preview for a thumbnail (browser chat/view.js wireThumbPreview): a top-level tooltip
  // window clamped to a fraction of the screen, like the browser's vw/vh.
  constexpr int PREVIEW_EDGE = 220;
  constexpr double PREVIEW_SCREEN_W = 0.25;
  constexpr double PREVIEW_SCREEN_H = 0.20;
  class HoverPreview : public QObject {
    Q_OBJECT
   public:
    HoverPreview(QLabel* thumb, QImage full, QString caption);

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    void hide();
    void show();

    QPointer<QLabel> thumb;
    QImage full;
    QString caption;
    QPointer<QWidget> popup;
  };

  // Browser .chat-typing (chat/view.js typingDots).
  class TypingDots : public QWidget {
    Q_OBJECT
   public:
    explicit TypingDots(QWidget* parent);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    static constexpr int DOT_SPAN = 4;
    static constexpr int DOT_GAP = 3;
    static constexpr int LIFT = 3;
    static constexpr int STEPS = 20;
    static constexpr int DOT_STEP_MS = 60;   // one phase every 60 ms on the frame clock, the browser's typing cadence
    support::FrameTicker ticker;
    QElapsedTimer stepClock;
    int phase = 0;
  };

  // One card's "⋯": hover reveal, placement, and a grace period to cross the gap. Owned by the card.
  class ChatCardMore : public QObject {
    Q_OBJECT
   public:
    ChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                 std::function<void()> moved, std::function<QRect()> avoidRect = nullptr);
    void place();

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

   private:
    bool isShown() const { return more && more->isVisible(); }
    void scheduleHide();

    QPointer<QFrame> card;
    QPointer<QToolButton> more;
    QScrollArea* scroll = nullptr;
    std::function<void()> moved;
    std::function<QRect()> avoidRect;
  };

}  // namespace stencil::gui
