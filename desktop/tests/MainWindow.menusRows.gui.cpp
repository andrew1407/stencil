// MainWindow GUI e2e — A context-menu ROW: the menu bar's own copies, hide-not-grey, and the hover shimmer.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"
#include "menuReveal.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Menu-bar coverage: every browser toolbar section is reachable from the menu bar. Walking the real
  // menu bar also proves the shared plain QActions were not moved OUT of the context menu.
  void menuBarExposesTheDrawAndStyleControls() {
    MainWindow win(nullptr, /*restoreLast=*/false);

    // Collect every action title reachable from the menu bar, submenus included.
    QSet<QString> titles;
    std::function<void(QMenu*)> walk = [&](QMenu* m) {
      for (QAction* a : m->actions()) {
        if (a->menu()) walk(a->menu());
        else if (!a->isSeparator()) titles.insert(a->text());
      }
    };
    for (QAction* top : win.menuBar()->actions())
      if (top->menu()) walk(top->menu());

    // The reported gap: instant line/rect, beside Start/Stop.
    QVERIFY(titles.contains("Start Drawing"));
    QVERIFY(titles.contains("Stop Drawing"));
    QVERIFY(titles.contains("Draw Line"));
    QVERIFY(titles.contains("Draw Rectangle"));

    // Line style (browser toolbar's Line Style select).
    QVERIFY(titles.contains("Solid"));
    QVERIFY(titles.contains("Dashed"));
    QVERIFY(titles.contains("Dotted"));

    // Image filter (browser toolbar's View section).
    QVERIFY(titles.contains("Cycle Image Filter"));
  }
  // A context-menu row whose action is unavailable is not in the menu at all, the desktop's version of
  // the browser's hide-not-disable (contextMenu.js syncState). The menu bar keeps its greyed rows.
  void contextMenuHidesUnavailableActionsInsteadOfGreyingThem() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // An image but NO lines: the menu needs an image to open at all, so the line-dependent rows are what
    // "unavailable" means here. A real right-click on the scroll area's viewport.
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);


    QSet<QString> rootTitles, layoutTitles;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      for (QAction* a : menu->actions())
        if (!a->isSeparator()) rootTitles.insert(a->text());
      if (QMenu* layout = openSubByKey(menu, "Image / Layout"))
        for (QAction* a : layout->actions())
          if (!a->isSeparator()) layoutTitles.insert(a->text());
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    // A row with a shortcut carries it appended as "\t<combo>", rendered natively, so a literal
    // platform-specific suffix is not reliable to match: startsWith throughout, as openSubByKey does.
    auto has = [](const QSet<QString>& set, const QString& prefix) {
      for (const QString& t : set) if (t.startsWith(prefix)) return true;
      return false;
    };

    QVERIFY2(has(rootTitles, "Fit to Window"), "the context menu never opened");
    QVERIFY2(!has(rootTitles, "Clear All Lines"), "Clear All Lines showed with no lines to clear");

    QVERIFY2(!layoutTitles.isEmpty(), "the Image / Layout submenu never opened");
    // Line-dependent rows are the ones missing here — nothing is drawn yet.
    QVERIFY2(!has(layoutTitles, "Copy Layout JSON"), "Copy Layout showed with no lines to copy");
    QVERIFY2(!has(layoutTitles, "Export Layout JSON"), "Download Layout showed with no lines to download");
    // "Copy Image"/"Download Image" are the SUBMENU-OPENER titles — a different, always-enabled QAction
    // than actCopyImage/actSaveImage, whose own text is the "Current" row nested inside.
    QVERIFY2(has(layoutTitles, "Copy Image"), "Copy Image hid with an image loaded");
    QVERIFY2(has(layoutTitles, "Download Image"), "Download Image hid with an image loaded");
    QVERIFY2(has(layoutTitles, "Paste Layout JSON"), "Paste Layout hid with an image loaded");
    // These two need neither an image nor lines — they always show.
    QVERIFY2(has(layoutTitles, "Paste (Image or Layout)"), "Paste Image needs no existing image");
    QVERIFY2(has(layoutTitles, "Import Layout JSON"), "Upload Layout needs no existing lines");
    beat();
  }
  // Browser parity: css/layout.css's ui-shimmer covers .ctx-item too (support/MenuShimmer.hpp is the
  // port) — the same left→right sweep every shimmered control gets, played on a context-menu ROW.
  void contextMenuRowShimmersOnHover() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    bool overlayFound = false, advanced = false;
    qreal p1 = -1.0, p2 = -1.0;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QWidget* overlay = menu->findChild<QWidget*>("menuShimmerOverlay");
      if (!overlay) { menu->close(); return; }
      overlayFound = true;
      QCOMPARE(overlay->geometry(), menu->rect());  // the band covers the whole menu

      QAction* fit = nullptr;
      QAction* other = nullptr;
      for (QAction* a : menu->actions()) {
        if (a->isSeparator()) continue;
        if (a->text().startsWith("Fit to Window")) fit = a;
        else if (!other) other = a;
      }
      if (!fit || !other) { menu->close(); return; }
      // A freshly opened QMenu can already be hovering its first row, so land on a KNOWN different row
      // first: sweep()'s own re-fire guard would no-op a same-row "hover".
      menu->setActiveAction(other);
      menu->setActiveAction(fit);
      p1 = overlay->property("sweepProgress").toReal();
      // Poll rather than a single timed sample: a QVariantAnimation ticks off QMenu::exec()'s own event
      // loop, which paces timers coarser, so the same 325ms sweep takes longer wall-clock here.
      for (int i = 0; i < 400 && !advanced; ++i) {
        QTest::qWait(15);
        p2 = overlay->property("sweepProgress").toReal();
        advanced = p2 > p1 && p1 >= 0.0;
      }
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    QVERIFY2(overlayFound, "no shimmer overlay on the context menu");
    QVERIFY2(advanced, qPrintable(QString("row shimmer did not advance (%1 -> %2)")
                                      .arg(p1)
                                      .arg(p2)));
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusRows.gui.moc"
