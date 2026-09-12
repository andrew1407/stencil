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
    bool isRight() const { return right_; }

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    QColor fill_, border_;
    bool right_ = true;
  };

  // Parented to the card's OWN parent (outside its clip), linked both ways with the card.
  void placeChatBubbleTail(QFrame* card, const QColor& fill, const QColor& border, bool right);
  // Reposition only; called after applyChatBubbleWidths moves the cards.
  void repositionChatBubbleTails(QWidget* transcript);

  // A card's resolved chatBubbleOnRight() (bool QVariant); objectName() alone stopped answering it
  // once "Swap message sides" could put either role on either side.
  inline constexpr const char* kChatOnRightProperty = "chatOnRight";

  // `avoidGlobal` (global coords, null ⇒ none) is furniture the button shifts clear of, or hides.
  void placeChatCardMore(QFrame* card, QToolButton* more, QScrollArea* scroll,
                         const QRect& avoidGlobal = QRect());

  // Card-arrival dust (browser motion.js chatIn): a finer grid than a list row.
  inline constexpr int kChatScatterCols = 32;
  inline constexpr int kChatScatterRows = 16;
  // One frame: the callers' scrollToBottom() is a singleShot(0), so a 0 ms hop measures too early.
  inline constexpr int kChatGatherSettleMs = 16;
  // A surface shown this very turn has no width yet; past the budget the card simply appears.
  inline constexpr int kChatGatherTries = 6;

  // The dust layer is unclipped by the scroller, so a card past an edge would fly motes outside.
  bool chatCardFullyInViewport(QWidget* card, QScrollArea* scroll);

  // `settle` is the one exit every bail-out takes, so a card is never stranded invisible.
  void gatherChatCardIn(QWidget* card, QVBoxLayout* layout, QScrollArea* scroll,
                        QWidget* host, int cols, int rows, std::function<void()> settle,
                        std::function<void()> onFlight = nullptr,
                        int tries = kChatGatherTries, QSize lastSize = QSize());

  // Drops the snapshot when the card leaves the viewport or resizes (browser motion.js trackDust).
  void trackChatCardDust(QWidget* card, DisintegrateOverlay* overlay, QScrollArea* scroll,
                         std::function<void()> settle);

  // Hover preview for a thumbnail (browser chatView.js wireThumbPreview): a top-level tooltip
  // window clamped to a fraction of the screen, like the browser's vw/vh.
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

  // Browser .chat-typing (chatView.js typingDots).
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
    bool shown() const { return more_ && more_->isVisible(); }
    void scheduleHide();

    QPointer<QFrame> card_;
    QPointer<QToolButton> more_;
    QScrollArea* scroll_ = nullptr;
    std::function<void()> moved_;
    std::function<QRect()> avoidRect_;
  };

}  // namespace stencil::gui
