// MainWindow GUI e2e — The gestures that open a row in the projects list.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"
#include "../src/support/filterFade.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Opening a project from the list is gesture-mapped (browser parity):
  //   single click          → confirm, then open in the CURRENT window
  //   double click          → open immediately, NO confirmation
  //   Ctrl/⌘ + single click → confirm, then open in a NEW window
  //   Ctrl/⌘ + double click → new window immediately, NO confirmation
  // The crux is that the single-click open is deferred by doubleClickInterval()
  // and cancelled by the double click, so the confirmation never flashes.
  void projectsListOpenGestures() {
    // Offscreen only (which is how ctest runs this suite). Driving a MODAL
    // dialog with synthetic clicks needs the window server to have activated
    // it; on a real desktop the clicks go nowhere and the flow deadlocks on the
    // still-open modal. Same class of limitation as the fullscreen edge-hover
    // test, which is likewise offscreen-only.
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A project to click on.
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::magenta);
    const QString id = win.addImageProjectEntry(img, "gesture-target");
    QVERIFY(!id.isEmpty());

    auto mainWindowCount = [] {
      int n = 0;
      for (QWidget* w : QApplication::topLevelWidgets())
        if (qobject_cast<MainWindow*>(w) && w->isVisible()) ++n;
      return n;
    };
    // Watches for the styled in-dialog open-confirm (modalChrome confirmModal —
    // it sits OVER the still-open projects dialog now) and answers it. Matched by
    // objectName: the projects dialog itself is a modal QDialog too.
    struct BoxWatch {
      bool seen = false;
      bool accept = true;
    };
    auto startWatch = [](BoxWatch* w) {
      auto* t = new QTimer;
      t->setInterval(5);
      QObject::connect(t, &QTimer::timeout, t, [w] {
        QWidget* m = QApplication::activeModalWidget();
        if (!m || m->objectName() != QLatin1String("stencilConfirmModal")) return;
        w->seen = true;
        const QLatin1String want = w->accept ? QLatin1String("Open") : QLatin1String("Cancel");
        for (QPushButton* b : m->findChildren<QPushButton*>())
          if (b->text() == want) { b->click(); return; }
      });
      t->start();
      return t;
    };
    // Perform a gesture on the first project row inside the (modal) dialog.
    auto gesture = [&win, &id](bool doubleClick, Qt::KeyboardModifiers mods) {
      QTimer::singleShot(0, [doubleClick, mods, id] {
        // Always leave a way out: if anything below fails to accept the modal,
        // reject it so the blocking openProjects() can return.
        const auto bailOut = [] {
          if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            d->reject();
        };
        QListWidget* list = nullptr;
        QWidget* opener = nullptr;   // the projects dialog: what an opening gesture leaves
        for (int i = 0; i < 200 && !list; ++i) {
          if (auto* dlg = qobject_cast<QDialog*>(opener = QApplication::activeModalWidget()))
            list = dlg->findChild<QListWidget*>("projectsList");
          if (!list) QTest::qWait(10);
        }
        if (!list) { bailOut(); return; }
        // OUR project's row specifically — the store may hold hundreds, and a
        // stale one could fail to load and mask the result.
        QListWidgetItem* item = nullptr;
        for (int i = 0; i < list->count() && !item; ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
        if (!item) { bailOut(); return; }
        list->scrollToItem(item);
        settleLayout(list, 30);
        // Aim right of the icon/kebab strips, at the row's text.
        const QRect r = list->visualItemRect(item);
        const QPoint hit(r.left() + r.width() / 2, r.center().y());
        if (doubleClick) {
          // Synthesised directly rather than via QTest::mouseDClick: that helper
          // waits internally between the events, and the dialog accepting
          // mid-sequence leaves it stuck.
          QWidget* vp = list->viewport();
          const QPointF gp = vp->mapToGlobal(hit);
          const auto send = [&](QEvent::Type t) {
            QMouseEvent e(t, QPointF(hit), gp, Qt::LeftButton,
                          t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          mods);
            QApplication::sendEvent(vp, &e);
          };
          send(QEvent::MouseButtonPress);
          send(QEvent::MouseButtonRelease);
          send(QEvent::MouseButtonDblClick);
          send(QEvent::MouseButtonRelease);
        } else {
          QTest::mouseClick(list->viewport(), Qt::LeftButton, mods, hit);
        }
        // Outlast the deferred open so "no dialog" is really observed, but stop early
        // once the gesture HAS opened something.
        settle([opener] { return QApplication::activeModalWidget() != opener; },
               QApplication::doubleClickInterval() + 250);
        bailOut();  // gesture did not open anything → don't hang the test
      });
      win.openProjects();
    };

    const int baseWindows = mainWindowCount();

    // ── 1. single click → confirmation, then opens HERE ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY2(w.seen, "single click did not ask for confirmation");
      QTRY_COMPARE(mainWindowCount(), baseWindows);  // same window
      QVERIFY(win.canvas_->hasImage());
    }

    // ── 2. single click, confirmation DECLINED → nothing opens ──
    win.canvas_->clearImage();
    {
      BoxWatch w;
      w.accept = false;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY(w.seen);
      QVERIFY2(!win.canvas_->hasImage(), "declining the confirmation still opened it");
      QCOMPARE(mainWindowCount(), baseWindows);
    }

    // ── 3. double click → NO confirmation, opens HERE ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(true, Qt::NoModifier);
      t->stop();
      delete t;
      QVERIFY2(!w.seen, "double click still raised a confirmation dialog");
      QTRY_VERIFY(win.canvas_->hasImage());
      QCOMPARE(mainWindowCount(), baseWindows);
    }

    // ── 4. ⌘ + single click → confirmation, then a NEW window ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(false, Qt::ControlModifier);
      t->stop();
      delete t;
      QVERIFY2(w.seen, "Ctrl/⌘ + single click did not ask for confirmation");
      QTRY_COMPARE(mainWindowCount(), baseWindows + 1);
    }

    // ── 5. ⌘ + double click → NO confirmation, another NEW window ──
    {
      BoxWatch w;
      QTimer* t = startWatch(&w);
      gesture(true, Qt::ControlModifier);
      t->stop();
      delete t;
      QVERIFY2(!w.seen, "Ctrl/⌘ + double click still raised a confirmation dialog");
      QTRY_COMPARE(mainWindowCount(), baseWindows + 2);
    }

    // Tidy up the windows this test opened.
    for (QWidget* wgt : QApplication::topLevelWidgets())
      if (qobject_cast<MainWindow*>(wgt) && wgt != &win) wgt->close();
    QTRY_COMPARE(mainWindowCount(), baseWindows);
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsGestures.gui.moc"
