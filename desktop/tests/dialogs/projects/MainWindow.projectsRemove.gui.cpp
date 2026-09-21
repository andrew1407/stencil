// MainWindow GUI e2e — Removing the OPEN project pins a temporary row, and closing the dialog finalizes them.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/theme/filterFade.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Removing the OPEN project resets this window to a blank unsaved editor, so the emptied list shows
  // the pinned "Temporary (unsaved)" row, never "No projects yet". Driven through Clear All.
  void removingTheOpenProjectPinsTheTemporaryRow() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    const auto motion = withMotion();   // the arrival below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkMagenta);
    const QString id = win.addImageProjectEntry(img, "the-only-one");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));   // …and it is this window's OPEN project

    bool sawRow = false, tempPinned = false, noPlaceholder = true, landedWhereItArrived = false;
    bool arrivedVeiled = false, cloudInFlight = false, landedWhole = false;
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
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) sawRow = true;
      if (!sawRow) { bailOut(); return; }

      QPushButton* clearBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->text().startsWith("Clear All")) clearBtn = b;
      if (!clearBtn) { bailOut(); return; }
      dismissModal("OK");   // the in-dialog styled confirm (see the note above)
      clearBtn->click();

      // Once the dust has landed the list holds the pinned row and nothing else.
      const auto pinned = [&] {
        return list->count() == 1 && list->item(0)->data(Qt::UserRole + 11).toBool();
      };
      settle([&] { return pinned() && !dlg->findChildren<QWidget*>("stencilFilterDust").isEmpty(); }, 3000);
      tempPinned = pinned() && list->item(0)->text() == QStringLiteral("Temporary (unsaved)");
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == QStringLiteral("No projects yet")) noPlaceholder = false;
      // …and it ARRIVES: veiled behind its own motes with the filter's light cloud in flight, never the
      // removal's scatter — the rebuild answering a removal finds the list EMPTY, like an opening build.
      arrivedVeiled = list->item(0)->data(Qt::UserRole + 43).toDouble() == 0.0;
      cloudInFlight = !dlg->findChildren<QWidget*>("stencilFilterDust").isEmpty();
      for (int i = 0; i < 200 && list->item(0)->data(Qt::UserRole + 43).toDouble() < 1.0; ++i)
        QTest::qWait(10);
      landedWhole = list->item(0)->data(Qt::UserRole + 43).toDouble() >= 1.0;

      // …and it arrives WHERE IT BELONGS: the list's own top moves with the batch bar above it, so the
      // pinned row's screen position at arrival must already be its final one.
      if (tempPinned) {
        const auto rowTop = [&] {
          return list->viewport()->mapToGlobal(list->visualItemRect(list->item(0)).topLeft()).y();
        };
        const int atArrival = rowTop();
        for (int i = 0; i < 60; ++i) QTest::qWait(10);   // past the bar's out-flight
        landedWhereItArrived = rowTop() == atArrival;
      }
      bailOut();
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(tempPinned, "the emptied list never pinned the window's \"Temporary (unsaved)\" row");
    QVERIFY2(noPlaceholder, "the emptied list still read \"No projects yet\"");
    QVERIFY2(arrivedVeiled, "the pinned row was simply there — not veiled behind its own motes");
    QVERIFY2(cloudInFlight, "no arrival cloud played for the row the removal revealed");
    QVERIFY2(landedWhole, "the arriving row never came out from behind its veil");
    QVERIFY2(landedWhereItArrived, "the pinned row appeared off its final place and jumped");
    beat();
  }

  // Closing the dialog mid-scatter must not bring removed rows back: on done() the doomed rows are
  // finalized, and the close flight's ghost shows their slots as empty background.
  void closingProjectsDialogFinalizesRetiredRows() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("modal-dialog gestures need the offscreen platform");
    // ctest runs this suite with STENCIL_NO_ANIM=1, which turns the reveal/close
    // flights off entirely — but the close flight's ghost IS what this test pins.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "doomed-close-row");
    QVERIFY(!id.isEmpty());

    bool sawRow = false, finalized = false, ghostSeen = false, ghostClean = false;
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
      // Let the OPEN flight (dust or ghost) land and delete itself, so the one found
      // below is unambiguously the close flight's. Bounded wait for a loaded machine.
      QTest::qWait(50);
      const auto openFlightLive = [&] { return surfaceFlight(&win) || modalGhost(&win); };
      settle([&] { return !(openFlightLive()); }, 2500);
      if (openFlightLive()) { bailOut(); return; }
      // Where the row sits, in DIALOG coordinates — the ghost photographs the dialog.
      const QRect rowInDlg =
          QRect(list->viewport()->mapTo(dlg, list->visualItemRect(item).topLeft()),
                list->visualItemRect(item).size()).adjusted(4, 4, -4, -4);

      QPushButton* clearBtn = nullptr;
      QPushButton* closeBtn = nullptr;
      for (QPushButton* b : dlg->findChildren<QPushButton*>()) {
        if (b->text().startsWith("Clear All")) clearBtn = b;
        if (b->text() == "Close") closeBtn = b;
      }
      if (!clearBtn || !closeBtn) { bailOut(); return; }
      dismissModal("OK");
      clearBtn->click();     // rows doomed, scatter playing
      closeBtn->click();     // …and the dialog closed IMMEDIATELY, mid-scatter

      // Finalized on done(): the doomed row left the list at once, no DUST_MS wait.
      finalized = true;
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) finalized = false;

      // The close flight's SNAPSHOT must show the slot as bare background: a stale open-time picture, or a
      // barely started scatter, would still paint the row. Checked either way, dust or ghost.
      QPixmap shot;
      if (auto* fx = surfaceFlight(&win)) { ghostSeen = true; shot = fx->snapshot(); }
      else if (auto* g = modalGhost(&win)) { ghostSeen = true; shot = g->pixmap(); }
      if (!shot.isNull()) {
        const qreal dpr = shot.devicePixelRatio();
        const QImage gi = shot.toImage();
        const QRect strip(int(rowInDlg.x() * dpr), int(rowInDlg.y() * dpr),
                          int(rowInDlg.width() * dpr), int(rowInDlg.height() * dpr));
        if (gi.rect().contains(strip)) {
          const QRgb base = gi.pixel(strip.center());   // bare list background
          int off = 0;
          for (int y = strip.top(); y <= strip.bottom(); ++y)
            for (int x = strip.left(); x <= strip.right(); ++x)
              if (gi.pixel(x, y) != base) ++off;
          // A hair of frame anti-aliasing may cross the strip; a painted row (icon,
          // text, badges) is orders of magnitude more than 1% of it.
          ghostClean = off < strip.width() * strip.height() / 100;
        }
      }
      bailOut();   // belt and braces — Close already rejected the dialog
    });
    win.openProjects();
    QVERIFY2(sawRow, "the seeded project row never appeared in the dialog");
    QVERIFY2(finalized, "closing mid-scatter left the doomed row in the list");
    QVERIFY2(ghostSeen, "the close flight's ghost was not found");
    QVERIFY2(ghostClean, "the close ghost still painted the removed row");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.projectsRemove.gui.moc"
