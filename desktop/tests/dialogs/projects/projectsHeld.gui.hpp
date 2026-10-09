#pragma once
// Shared ground for the Projects window's suites: the window they open it over, the modal window
// and the parts a held row reaches, the cursor the header polls, and an answer to the close
// question that never mistakes a window's Close pill for its own button.
#include "../../MainWindow.gui.hpp"
#include "../../../src/dialogs/projects/list/ReorderableListWidget.hpp"

#include <QPointer>
#include <QToolButton>
#include <memory>

namespace stencil::guitest::held {

  using stencil::gui::ReorderableListWidget;

  // Shown at the size the Projects window's rows were tuned against.
  inline bool showForProjects(MainWindow& win) {
    win.resize(1100, 800);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }

  // Whatever modal is up goes: a case that cannot find its row leaves no loop behind.
  inline void rejectModal() {
    if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
  }

  // The modal Projects window openProjects() raised, and what a held row reaches in it.
  struct ProjectsWindow {
    QDialog* dlg = nullptr;
    ReorderableListWidget* list = nullptr;
    QToolButton* more = nullptr;
    QPushButton* pill = nullptr;
  };

  inline ProjectsWindow projectsWindow() {
    ProjectsWindow w;
    for (int i = 0; i < 200 && !w.list; ++i) {
      w.dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (w.dlg) w.list = static_cast<ReorderableListWidget*>(w.dlg->findChild<QListWidget*>("projectsList"));
      if (!w.list) QTest::qWait(10);
    }
    if (w.dlg) {
      w.more = w.dlg->findChild<QToolButton*>("projectsDragMore");
      w.pill = w.dlg->findChild<QPushButton*>("modalClosePill");
    }
    return w;
  }

  inline QListWidgetItem* rowFor(QListWidget* list, const QString& id) {
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
    return nullptr;
  }

  // Where the held row is: the header polls the real cursor, as the drag-out zones do.
  inline void holdAt(const QPoint& global, int ms = 80) {
    QCursor::setPos(global);
    QTest::qWait(ms);
  }

  // The held row's menu on screen: the visible one carrying `text`.
  inline QMenu* shownMenuWith(QWidget* dlg, const QString& text) {
    for (QMenu* m : dlg->findChildren<QMenu*>())
      if (m->isVisible())
        for (QAction* a : m->actions())
          if (a->text() == text) return m;
    return nullptr;
  }

  struct Asked { QString title; QString message; bool danger = true; };

  // Answers the close question by its own button — never a window's Close pill, which every modal
  // header carries under the same word. A repeating timer, so it ticks inside the question's own loop.
  inline QPointer<QTimer> answerClose(const QString& button, Asked* asked = nullptr) {
    auto* t = new QTimer;
    t->setInterval(5);
    auto ticks = std::make_shared<int>(800);
    QObject::connect(t, &QTimer::timeout, t, [t, button, asked, ticks] {
      if (--*ticks <= 0) { t->deleteLater(); return; }
      QWidget* m = QApplication::activeModalWidget();
      if (!m || m->objectName() != QLatin1String("stencilConfirmModal")) return;
      if (asked) {
        asked->title = m->windowTitle();
        asked->danger = m->findChild<QPushButton*>(QStringLiteral("dangerButton")) != nullptr;
        for (QLabel* l : m->findChildren<QLabel*>())
          if (l->text().contains(QStringLiteral("stays saved"))) asked->message = l->text();
      }
      for (QPushButton* b : m->findChildren<QPushButton*>())
        if (b->objectName() != QLatin1String("modalClosePill") && b->text() == button) {
          t->stop();
          t->deleteLater();
          b->click();
          return;
        }
    });
    t->start();
    return t;
  }

}  // namespace stencil::guitest::held

using namespace stencil::guitest::held;
