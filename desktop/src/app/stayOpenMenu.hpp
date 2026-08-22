#pragma once

#include <QMenu>
#include <QPointer>

class QAbstractButton;

namespace stencil::gui {

  // A QMenu whose hosted checkbox/radio rows toggle WITHOUT closing the menu
  // (browser parity: the inline controls stay live). QMenu's own release handler
  // closes the popup over a QWidgetAction, so hosted-button clicks are intercepted.
  class StayOpenMenu : public QMenu {
    Q_OBJECT
   public:
    using QMenu::QMenu;

    // Register a hosted widget with LIVE keyboard + mouse input (the assistant
    // chat row): real events are re-dispatched to the child under the cursor,
    // and keys (except Escape) go to `keyTarget` while it holds focus.
    void setInteractiveArea(QWidget* area, QWidget* keyTarget);

   protected:
    QAbstractButton* toggleAt(const QPoint& p);
    QAction* checkableAt(const QPoint& p);
    QWidget* strictChildInArea(const QPoint& p) const;
    bool deliverToArea(QMouseEvent* e);
    void forward(QWidget* target, QMouseEvent* e);
    void mouseMoveEvent(QMouseEvent* e) override;
    void updateAreaHover(QMouseEvent* e);
    void clearAreaHover();
    void leaveEvent(QEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

   private:
    // Scoped true-while-alive flag for the re-dispatch guards.
    struct Latch {
      bool& flag;
      explicit Latch(bool& f) : flag(f) { flag = true; }
      ~Latch() { flag = false; }
    };
    QWidget* area_ = nullptr;       // hosted widget with live input (chat)
    QWidget* keyTarget_ = nullptr;  // where keystrokes go while it has focus
    QPointer<QWidget> pressTarget_; // drag owner inside the area (splitter…)
    QPointer<QWidget> hoverChild_;  // child currently sent a synthetic Enter
    bool redispatching_ = false;    // inside a mouse re-dispatch
    bool redispatchingKey_ = false; // inside a key re-dispatch
  };

}  // namespace stencil::gui
