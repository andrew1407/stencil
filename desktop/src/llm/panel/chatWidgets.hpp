#pragma once

// Shared chat helper widgets, used by both chat surfaces (dock + context-menu panel).

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
    void setSide(bool right);
    bool isRight() const { return right; }

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QColor fill, border;
    bool right = true;
  };

  // Parented to the card's OWN parent (outside its clip), linked both ways with the card.
  void placeChatBubbleTail(QFrame* card, const QColor& fill, const QColor& border, bool right);
  // Reposition only; called after applyChatBubbleWidths moves the cards.
  void repositionChatBubbleTails(QWidget* transcript);

  // A card's resolved isChatBubbleOnRight() (bool QVariant); objectName() alone stopped answering it
  // once "Swap message sides" could put either role on either side.
  inline constexpr const char* CHAT_ON_RIGHT_PROPERTY = "chatOnRight";

  // `avoidGlobal` (global coords, null ⇒ none) is furniture the button shifts clear of, or hides.
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal = QRect());

  // Card-arrival dust (browser surface/motion.js chatIn): a finer grid than a list row.
  inline constexpr int CHAT_SCATTER_COLS = 32;
  inline constexpr int CHAT_SCATTER_ROWS = 16;
  // One frame: the callers' scrollToBottom() is a singleShot(0), so a 0 ms hop measures too early.
  inline constexpr int CHAT_GATHER_SETTLE_MS = 16;
  // A surface shown this very turn has no width yet; past the budget the card simply appears.
  inline constexpr int CHAT_GATHER_TRIES = 6;

  // The dust layer is unclipped by the scroller, so a card past an edge would fly motes outside.
  bool chatCardFullyInViewport(QWidget* card, QScrollArea* scroll);

  // `settle` is the one exit every bail-out takes, so a card is never stranded invisible.
  void gatherChatCardIn(QWidget* card, QVBoxLayout* layout, QScrollArea* scroll,
                        QWidget* host, int cols, int rows, std::function<void()> settle,
                        std::function<void()> onFlight = nullptr,
                        int tries = CHAT_GATHER_TRIES, QSize lastSize = QSize());

  // Drops the snapshot when the card leaves the viewport or resizes (browser surface/motion.js trackDust).
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
    QTimer timer;
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
    bool shown() const { return more && more->isVisible(); }
    void scheduleHide();

    QPointer<QFrame> card;
    QPointer<QToolButton> more;
    QScrollArea* scroll = nullptr;
    std::function<void()> moved;
    std::function<QRect()> avoidRect;
  };

}  // namespace stencil::gui
