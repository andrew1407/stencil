// MainWindow GUI e2e — A removed row blanking and holding its slot, and the batch bar gliding the rows up.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"
#include "../src/support/filterFade.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Removing project rows plays the scatter over an EMPTY slot. The overlay animates a
  // SNAPSHOT, and the list kept painting the ORIGINAL row underneath it — so the removal
  // was never actually seen. Pins the fixed sequence: the real row blanks the instant the
  // removal starts, its slot stays open while the dust falls, and the item leaves the
  // list only once the scatter has played (browser parity: leaveThenRemove +
  // beginRemoval in projectsModal.js). Driven through Clear All — the removal path that
  // keeps the dialog open while the animation runs.
  void projectRemovalBlanksTheRowAndHoldsItsSlot() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "doomed-row");
    QVERIFY(!id.isEmpty());

    bool sawRow = false, blankedAtOnce = false, slotHeld = false, goneAfter = false;
    QTimer::singleShot(0, [&] {
      const auto bailOut = [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
      };
      QDialog* dlg = nullptr;
      QListWidget* list = nullptr;
      for (int i = 0; i < 200 && !list; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
        if (!list) QTest::qWait(10);
      }
      if (!list) { bailOut(); return; }
      QListWidgetItem* item = nullptr;
      for (int i = 0; i < list->count() && !item; ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) item = list->item(i);
      if (!item) { bailOut(); return; }
      sawRow = true;
      list->scrollToItem(item);
      settleLayout(list, 30);
      const QRect r = list->visualItemRect(item);
      const int rowsBefore = list->count();
      const QImage before = list->viewport()->grab(r).toImage();

      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!clearBtn) { bailOut(); return; }
      // click() is synchronous (like trigger()): dismissModal's 0-timer must first fire
      // INSIDE the confirm's nested loop, not during a QTest::mouseClick event pump —
      // there its qWait poll gets buried under the confirm's loop and deadlocks.
      dismissModal("OK");   // the in-dialog styled confirm
      clearBtn->click();

      // The row is still IN the list (slot held open, same height) but paints as blank.
      const QImage after = list->viewport()->grab(r).toImage();
      slotHeld = list->count() == rowsBefore && list->visualItemRect(item).height() == r.height();
      bool uniform = !after.isNull();
      const QRgb base = uniform ? after.pixel(1, 1) : 0;
      for (int y = 0; y < after.height() && uniform; ++y)
        for (int x = 0; x < after.width() && uniform; ++x)
          if (after.pixel(x, y) != base) uniform = false;
      blankedAtOnce = uniform && after != before;

      // …and the item is gone once the scatter has played out.
      const auto rowPresent = [&] {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole).toString() == id) return true;
        return false;
      };
      settle([&] { return !(rowPresent()); }, 3000);
      goneAfter = !rowPresent();
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(blankedAtOnce, "the original row kept painting under the scatter");
    QVERIFY2(slotHeld, "the row's slot collapsed before the scatter finished");
    QVERIFY2(goneAfter, "the doomed row never left the list");
    beat();
  }

  // Nothing open here, so the pinned "Temporary (unsaved)" row is listed above the saved
  // projects — and the batch bar (it hosts Select all) is up because there are rows to
  // select. Removing every project takes both away, and the row underneath must GLIDE up
  // into the space, not be dropped into it: the strip used to lose its height the frame
  // its last control was hidden, and the layout's own spacing went in one more frame
  // after that ("it should smoothly move"). Pins the whole close as a
  // continuous slide: no single frame moves the row more than a few pixels.
  void closingTheBatchBarGlidesTheRowsUp() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    const auto motion = withMotion();   // the slide below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    QVERIFY(!win.addImageProjectEntry(img, "one").isEmpty());
    QVERIFY(!win.addImageProjectEntry(img, "two").isEmpty());

    bool sawPinned = false, barWasUp = false, dustedOnOpen = false;
    int biggestStep = 0, travelled = 0;
    QTimer::singleShot(0, [&] {
      const auto bailOut = [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) d->reject();
      };
      QDialog* dlg = nullptr;
      QListWidget* list = nullptr;
      for (int i = 0; i < 200 && !list; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg) list = dlg->findChild<QListWidget*>("projectsList");
        if (!list) QTest::qWait(10);
      }
      if (!list) { bailOut(); return; }
      QWidget* selectAll = dlg->findChild<QPushButton*>("projectsSelectAll");
      QWidget* bar = selectAll ? selectAll->parentWidget() : nullptr;
      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!bar || !clearBtn) { bailOut(); return; }
      // The pinned row's top edge, in screen coordinates — what the eye follows.
      const auto pinnedTop = [&] {
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole + 11).toBool())
            return list->viewport()->mapToGlobal(list->visualItemRect(list->item(i)).topLeft()).y();
        return -1;
      };
      sawPinned = pinnedTop() >= 0;
      // OPENING is not an arrival: the dialog rebuilds more than once on its way up.
      settle([] { return false; }, stencil::gui::ROW_ARRIVE_DELAY_MS + 120);
      dustedOnOpen = !dlg->findChildren<QWidget*>(
          QString::fromLatin1(stencil::gui::FILTER_DUST_OBJECT_NAME)).isEmpty();
      barWasUp = bar->isVisible() && bar->height() > 0;
      if (!sawPinned || !barWasUp) { bailOut(); return; }
      const int from = pinnedTop();
      dismissModal("OK");
      clearBtn->click();
      int last = from;
      for (int i = 0; i < 70 && bar->isVisible(); ++i) {
        QTest::qWait(16);
        const int now = pinnedTop();
        if (now < 0) continue;   // mid-rebuild
        biggestStep = std::max(biggestStep, std::abs(now - last));
        last = now;
      }
      travelled = from - last;
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawPinned, "the pinned row was not listed with the saved projects");
    QVERIFY2(!dustedOnOpen, "no row may form out of particles just because the dialog opened");
    QVERIFY2(barWasUp, "the batch bar was not up over the selectable rows");
    QVERIFY2(travelled > 20, QString("the rows never moved up (%1px)").arg(travelled).toLatin1());
    QVERIFY2(biggestStep <= 20,
             QString("the bar's close dropped the rows %1px in one frame — not a glide")
                 .arg(biggestStep).toLatin1());
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsRows.gui.moc"
