#pragma once
// Driving a toolbar icon's drag on the live window, for the MainWindow drag suites: presses, moves
// and releases in GLOBAL coordinates sent to the icon as its pointer grab would deliver them, and a
// catch for the dialog a drop opens.
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>

namespace stencil::guitest {

  inline void iconMouse(QWidget* icon, QEvent::Type type, const QPoint& global) {
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(icon->mapFromGlobal(global)), QPointF(global), button, held, Qt::NoModifier);
    QApplication::sendEvent(icon, &e);
  }

  inline QPoint iconCentre(const QWidget* w) { return w->mapToGlobal(w->rect().center()); }

  // Pressed on its centre and carried past the slop; the drag is live after this.
  inline void liftIcon(QWidget* icon) {
    iconMouse(icon, QEvent::MouseButtonPress, iconCentre(icon));
    iconMouse(icon, QEvent::MouseMove, iconCentre(icon) + QPoint(0, 40));
  }

  // Released at `at`; the drop or the cancel runs on the turn this lets go by.
  inline void dropIcon(QWidget* icon, const QPoint& at) {
    iconMouse(icon, QEvent::MouseMove, at);
    iconMouse(icon, QEvent::MouseButtonRelease, at);
    QApplication::processEvents();
  }

  // Lifted, carried to `to`, released at `at`.
  inline void dragIcon(QWidget* icon, const QPoint& to, const QPoint& at) {
    liftIcon(icon);
    iconMouse(icon, QEvent::MouseMove, to);
    dropIcon(icon, at);
  }

  // Armed before a gesture that may open a dialog: the first modal's frame is noted and it is closed.
  struct ModalSeen {
    QRect frame;
    bool seen = false;
  };
  inline void catchModal(ModalSeen& seen, int capMs = 3000) {
    auto* poll = new QTimer(qApp);
    poll->setInterval(5);
    QElapsedTimer clock;
    clock.start();
    QObject::connect(poll, &QTimer::timeout, poll, [poll, &seen, clock, capMs] {
      auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (dlg) {
        seen.frame = dlg->frameGeometry();
        seen.seen = true;
      }
      if (dlg || clock.elapsed() > capMs) {
        poll->stop();
        poll->deleteLater();
      }
      if (dlg) dlg->reject();
    });
    poll->start();
  }

  inline int dropGlows(const QWidget& win, const char* name) {
    int n = 0;
    for (QWidget* g : win.findChildren<QWidget*>(QLatin1String(name))) n += g->isVisible() ? 1 : 0;
    return n;
  }

}  // namespace stencil::guitest
