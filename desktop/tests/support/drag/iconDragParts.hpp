// Shared ground of the iconDrag suite (iconDrag.headless.cpp, iconDragSources.headless.cpp):
// the synthetic pointer and the section the main body hands its fixture to.
#pragma once
#include "iconDrag.hpp"
#include "uiTimings.hpp"
#include "AppTooltip.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include "../check.hpp"

using namespace stencil::support;

namespace iconDragTest {
  inline void mouse(QWidget* w, QEvent::Type type, const QPoint& local, Qt::MouseButtons held) {
    const Qt::MouseButton b = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, QPointF(local), QPointF(w->mapToGlobal(local)), b, held, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
  }
  inline void press(QWidget* w, const QPoint& at) { mouse(w, QEvent::MouseButtonPress, at, Qt::LeftButton); }
  inline void drag(QWidget* w, const QPoint& at) { mouse(w, QEvent::MouseMove, at, Qt::LeftButton); }
  inline void release(QWidget* w, const QPoint& at) { mouse(w, QEvent::MouseButtonRelease, at, Qt::NoButton); }

  // The ghost faces and the sources that are no button.
  void sourceCases(QApplication& app, QWidget& window, const QPoint& in, int slop, const IconDragHooks& hooks,
                   QStringList& log);
}  // namespace iconDragTest

using namespace iconDragTest;
