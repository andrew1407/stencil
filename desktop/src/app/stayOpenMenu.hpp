#pragma once

#include <QMenu>
#include <QPointer>
#include <QSet>

class QAbstractButton;

namespace stencil::gui {

  // A QMenu whose hosted checkbox/radio rows toggle WITHOUT closing the menu
  // (browser parity: the inline controls stay live). QMenu's own release handler
  // closes the popup over a QWidgetAction, so hosted-button clicks are intercepted.
  class StayOpenMenu : public QMenu {
    Q_OBJECT
   public:
    // Own constructors (not `using QMenu::QMenu;`) so every instance gets
    // WA_TranslucentBackground: theme.cpp styles QMenu with a rounded
    // border-radius, and without true window transparency a native popup's own
    // opaque backing paints through as square black wedges outside that rounded
    // shape — reading as a colder, blacker menu than the browser's DOM-rendered
    // (already-composited) rounded div ever shows.
    explicit StayOpenMenu(QWidget* parent = nullptr);
    explicit StayOpenMenu(const QString& title, QWidget* parent = nullptr);

    // Register a hosted widget with LIVE keyboard + mouse input (the assistant
    // chat row): real events are re-dispatched to the child under the cursor,
    // and keys (except Escape) go to `keyTarget` while it holds focus.
    void setInteractiveArea(QWidget* area, QWidget* keyTarget);
    QList<QWidget*> tabStops() const;   // hosted focusable controls, in row order

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
    void showEvent(QShowEvent* e) override;
    void actionEvent(QActionEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool enterControls();               // focus the chat input / first tab stop
    void focusStop(QWidget* w, Qt::FocusReason reason);   // setFocus + the radio filter
    bool walkTab(bool back);            // one Tab step over controls and plain rows
    bool eventFilter(QObject* watched, QEvent* event) override;
    QAction* firstRowAction() const;    // first plain row the keyboard can land on
    QList<QAction*> rowActions() const; // the plain rows, in order
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
    QSet<QWidget*> filteredStops_;  // hosted controls already given this filter
    bool entered_ = false;          // past the reveal: a second →, Tab, or the pointer
  };

}  // namespace stencil::gui
