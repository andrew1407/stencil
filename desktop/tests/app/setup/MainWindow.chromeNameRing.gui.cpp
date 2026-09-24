// MainWindow GUI e2e — The title's hover ring, chips surviving a cut-short slide, popup rows, and toasts.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The project name is a TITLE at rest — no box — that RINGS in the accent under the pointer. The
  // ring must live in that field's OWN sheet (applyProjectNameStyle), so it is watched in PIXELS.
  void projectNameTitleRingsOnHoverOnly() {
    MainWindow win(nullptr, false);
    win.resize(1400, 860);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    const QString id = win.addImageProjectEntry(img, "name-row");
    QVERIFY(!id.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(id, false));
    QVERIFY(win.nameBar.field && win.nameBar.edit && win.nameBar.colorBtn);
    QTRY_VERIFY(win.nameBar.colorBtn->isVisible());   // the chips slide in after the bind
    settleLayout(&win, 300);

    // The chips: the browser's box, and its 4px gaps either side.
    QCOMPARE(win.nameBar.edit->size(), QSize(stencil::gui::NAME_CHIP_BOX,
                                                 stencil::gui::NAME_CHIP_BOX));
    QCOMPARE(win.nameBar.colorBtn->size(), win.nameBar.edit->size());
    const QRect f(win.nameBar.field->mapTo(&win, QPoint(0, 0)), win.nameBar.field->size());
    const QRect e(win.nameBar.edit->mapTo(&win, QPoint(0, 0)), win.nameBar.edit->size());
    const QRect c(win.nameBar.colorBtn->mapTo(&win, QPoint(0, 0)), win.nameBar.colorBtn->size());
    // 8px of air either side — at 4 the chips sat right against the field's edge (user
    // report, with a picture). Browser twin: .project-name-field's `gap`.
    QCOMPARE(e.left() - f.right() - 1, 8);
    QCOMPARE(c.left() - e.right() - 1, 8);

    // …and the ring itself, top edge of the field: nothing at rest, the accent on hover.
    const auto edge = [&] {
      const QImage im = win.grab(f).toImage();
      return im.pixelColor(im.width() / 2, 1);
    };
    const QColor accent = stencil::gui::accentPrimary(win.settings.accentColor);
    const QColor rest = edge();
    // The ring is the accent at the shared 45% (the browser's two stacked layers come to the same on
    // screen), so the edge lands between the ground and the accent, never the flat accent.
    const auto near = [](const QColor& a, const QColor& b, int tol) {
      return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()) < tol;
    };
    const QColor blend(qRound(0.45 * accent.red() + 0.55 * rest.red()),
                       qRound(0.45 * accent.green() + 0.55 * rest.green()),
                       qRound(0.45 * accent.blue() + 0.55 * rest.blue()));
    QVERIFY2(!near(rest, accent, 60), "the title wears the ring at rest");
    win.nameBar.field->setAttribute(Qt::WA_UnderMouse, true);
    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), win.nameBar.field->mapToGlobal(QPointF(5, 5)));
    QApplication::sendEvent(win.nameBar.field, &enter);
    win.nameBar.field->update();
    QTest::qWait(150);
    const QColor hovered = edge();
    QVERIFY2(!near(hovered, rest, 24), "no ring appeared under the pointer");
    QVERIFY2(near(hovered, blend, 40),
             qPrintable(QString("the ring is not the shared 45%% accent: %1 (wanted ~%2)")
                            .arg(hovered.name(), blend.name())));
    win.nameBar.field->setAttribute(Qt::WA_UnderMouse, false);
    beat();
  }

  // The rename ✓/✗ slide their slots open by animating maximumWidth, so the layout's own cap is
  // parked while that runs (controlReveal parkMaxWidth); read off the live value it ratchets down.
  void nameChipsSurviveRenamesCutShortMidSlide() {
    MainWindow win(nullptr, false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    settle([&] { return win.canvas->hasImage(); }, 500);
    const auto motion = withMotion();   // the slide is the whole point here
    const int box = win.nameBar.accept->maximumWidth();
    QVERIFY(box > 20);
    for (int i = 0; i < 6; ++i) {   // in and straight back out, mid-slide every time
      win.enterNameEdit();
      QTest::qWait(60);
      win.cancelProjectName();
      QTest::qWait(60);
    }
    win.enterNameEdit();
    QTRY_COMPARE(win.nameBar.accept->width(), box);
    QCOMPARE(win.nameBar.cancel->width(), box);
    QVERIFY2(!win.nameBar.accept->icon().isNull(), "the tick lost its glyph");
    win.cancelProjectName();
  }

  // A select popup's rows hover like every other item in the app: the glass sweep plus the browser's
  // 2px ease right. The slide WRAPS the popup's existing delegate, so a custom painter keeps it.
  void selectPopupRowsSweepAndSlideOnHover() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("popup gestures need the offscreen platform");
    const auto motion = withMotion();   // the slide below IS the thing under test
    MainWindow win(nullptr, false);
    win.resize(1500, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    settle([&] { return win.canvas->hasImage(); }, 500);
    stencil::gui::SearchComboBox* combo = nullptr;
    for (QComboBox* c : win.findChildren<QComboBox*>())
      if (c->isVisible() && c->count() > 2) {
        combo = dynamic_cast<stencil::gui::SearchComboBox*>(c);
        if (combo) break;
      }
    QVERIFY2(combo, "no themed select on the toolbar");
    combo->showPopup();
    settle([&] { return combo->popupList() != nullptr; }, 500);
    QListView* list = combo->popupList();
    QVERIFY(list);
    QCOMPARE(list->itemDelegate()->objectName(), QStringLiteral("stencilSlidingRows"));
    // The sweep lives on the viewport, as the projects list's does.
    QVERIFY2(!list->viewport()->findChildren<QWidget*>().isEmpty(),
             "no shimmer overlay over the popup's rows");

    const QRect row = list->visualRect(list->model()->index(1, 0));
    const auto pointAt = [&](const QPoint& at) {
      QMouseEvent mv(QEvent::MouseMove, QPointF(at),
                     list->viewport()->mapToGlobal(QPointF(at)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(list->viewport(), &mv);
    };
    QCOMPARE(list->property("rowSlidePx").toInt(), 0);   // nothing hovered yet
    pointAt(row.center());
    QTRY_COMPARE_WITH_TIMEOUT(list->property("rowSlidePx").toInt(), 2, 1500);

    // …and it settles back the moment the pointer is off the rows.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(list->viewport(), &leave);
    QTRY_COMPARE_WITH_TIMEOUT(list->property("rowSlidePx").toInt(), 0, 1500);
    combo->hidePopup();
    beat();
  }

  // Toasts stack, so the stack is capped: a new arrival retires the oldest instead of piling a column
  // of identical messages up the side of the canvas.
  void toastStackIsCappedAtThree() {
    QWidget host;
    host.resize(600, 420);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::Notifications toasts(&host);

    // Read the stack TOP-DOWN by geometry: findChildren order is not creation order here, because
    // reflow() raise()s each toast and raise() moves a widget to the end of its parent's child list.
    const auto stackTopDown = [&host] {
      QList<QLabel*> live = host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly);
      std::sort(live.begin(), live.end(),
                [](QLabel* a, QLabel* b) { return a->y() < b->y(); });
      QStringList out;
      // text() is markup wrapping the level glyph; the plain message rides alongside it.
      for (QLabel* l : live) out << l->property("stencilToastText").toString();
      return out;
    };

    for (int i = 1; i <= 6; ++i) toasts.info(QString("Toast %1").arg(i));
    // The retired ones play their exit first, so wait for the stack to settle rather
    // than asserting on the frame the sixth arrived in.
    QTRY_COMPARE(stackTopDown().size(), stencil::gui::ToastStack::MAX_VISIBLE);
    // The OLDEST three went; the newest is lowest, where the next one will appear.
    QCOMPARE(stackTopDown(), QStringList({"Toast 4", "Toast 5", "Toast 6"}));

    // Stacked bottom-left, and none of them ran off the top of the host — which is what
    // an uncapped stack eventually does.
    for (QLabel* l : host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly)) {
      QCOMPARE(l->x(), 6);   // Notifications.cpp LEFT_MARGIN
      QVERIFY(l->y() >= 8);
      QVERIFY(l->geometry().bottom() <= host.height());
    }
    beat();
  }

  void repeatedToastReplacesAStillLeavingOne() {
    QWidget host;
    host.resize(600, 420);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::Notifications toasts(&host);
    toasts.show("Saved", stencil::gui::Notifications::Level::SUCCESS, /*msec=*/50);
    QTest::qWait(70);   // its life timer fires -> dismiss() -> mid-way through the 160ms fadeOut
    toasts.show("Saved", stencil::gui::Notifications::Level::SUCCESS, /*msec=*/3000);
    QTest::qWait(10);
    const auto ts = host.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly);
    QCOMPARE(ts.size(), 1);
    QCOMPARE(ts.first()->property("stencilToastText").toString(), QString("Saved"));
    QVERIFY2(!ts.first()->property("stencilToastLeaving").toBool(),
             "the survivor is the stale leaving one, not the fresh arrival");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeNameRing.gui.moc"
