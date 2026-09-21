// MainWindow GUI e2e — the crop size READ-OUT ("5040 x 3564 px · Album"): its line slides
// in and out with the Crop toggle, and its motes form and fall on the keyword chip's
// recipe, landing on that line. Browser twin: openImageModal.js syncCropDims.
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "../src/support/DisintegrateOverlay.hpp"
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>

using stencil::gui::OpenImageDialog;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The read-out's cloud lands ON the read-out while the column EASES into its new height: raised
  // before that ease, it is pinned where the line used to be (measured 19px off).
  void theReadOutsCloudLandsOnItWhileTheColumnEases() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    int dx = 999, dy = 999, steps = 0;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      QTest::keyClicks(page->findChild<QLineEdit*>(), guiTestImage());
      auto* pv = page->findChild<QPushButton*>();
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      settle([] { return false; }, 600);
      QCheckBox* crop = nullptr;
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { crop = c; break; }
      if (!crop) { dlg->reject(); return; }
      QLabel* dims = dlg->cropDims;
      int last = dlg->height();
      crop->setChecked(true);
      for (int i = 0; i < 14; ++i) {
        settle([] { return false; }, 30);
        if (dlg->height() != last) { ++steps; last = dlg->height(); }   // a real ease, not a jump
        const QRect l = QRect(dims->mapToGlobal(QPoint()), dims->size());
        for (QWidget* w : dlg->findChildren<QWidget*>(
                 QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)))
          if (qAbs(w->height() - l.height()) <= 6 && qAbs(w->width() - l.width()) <= 40) {
            // The LAST sighting, not the first: at raise time the two always agree, and the
            // defect is the cloud staying put while the easing column moves the line.
            const QRect c = QRect(w->mapToGlobal(QPoint()), w->size());
            dx = c.x() - l.x();
            dy = c.y() - l.y();
          }
      }
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(dx != 999, "the read-out never raised a cloud of its own");
    QVERIFY2(qAbs(dx) <= 2 && qAbs(dy) <= 2,
             qPrintable(QString("the cloud is (%1,%2) off the line it came from").arg(dx).arg(dy)));
    QVERIFY2(steps >= 3, qPrintable(QString("the column jumped in %1 step(s), not eased").arg(steps)));
  }

  // The read-out's cloud is a mesh of SPECKS sized from the box — the keyword chip's recipe and the
  // browser's reshapeGrid. A fixed budget shaped 3:1 gave a 13px line cells of 3.1 x 0.59.
  void theReadOutsCloudIsSpeckSizedForItsBox() {
    const auto motion = withMotion();
    // A LARGE picture, as the user's is: the read-out line is wide, so a fixed grid's cells
    // are at their most lopsided.
    const QString big = QDir::temp().filePath(QStringLiteral("stencil_oi_big.png"));
    QImage im(2400, 1700, QImage::Format_RGB32);
    im.fill(Qt::darkGreen);
    QVERIFY(im.save(big, "PNG"));
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    double cw = 0, ch = 0;
    int dx = 999, dy = 999;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      QTest::keyClicks(page->findChild<QLineEdit*>(), big);
      auto* pv = page->findChild<QPushButton*>();
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 6000);
      settle([] { return false; }, 900);   // the arrival's own cloud is gone by now
      QCheckBox* crop = nullptr;
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { crop = c; break; }
      if (!crop) { dlg->reject(); return; }
      QLabel* dims = dlg->cropDims;
      QWidget* host = dims->parentWidget();
      crop->setChecked(true);
      settle([] { return false; }, 120);
      for (QWidget* w : dlg->findChildren<QWidget*>(
               QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)))
        // No Q_OBJECT on the overlay; the object name is only ever set by its own ctor.
        if (auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w))
          if (qAbs(fx->width() - dims->width()) <= 40 && qAbs(fx->height() - dims->height()) <= 8) {
            cw = double(fx->width()) / fx->grid().width();
            ch = double(fx->height()) / fx->grid().height();
            dx = fx->x() - dims->mapTo(host, QPoint()).x();
            dy = fx->y() - dims->mapTo(host, QPoint()).y();
          }
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(cw > 0, "the read-out never raised a cloud over a big picture");
    // A speck: roughly square, and never thinner than a pixel — under that the mote's
    // radius falls below the paint threshold and most of the line never flies at all.
    QVERIFY2(cw >= 1.0 && ch >= 1.0,
             qPrintable(QString("cells are %1 x %2 px").arg(cw).arg(ch)));
    QVERIFY2(qMax(cw, ch) / qMin(cw, ch) <= 2.0,
             qPrintable(QString("cells are %1 x %2 px — slices, not specks").arg(cw).arg(ch)));
    QVERIFY2(qAbs(dx) <= 2 && qAbs(dy) <= 2,
             qPrintable(QString("the cloud is (%1,%2) off its line").arg(dx).arg(dy)));
  }

  // The size read-out ARRIVES and LEAVES with its own cloud (browser twin: syncCropDims):
  // it is the one thing a crop toggle adds to, or takes from, the preview column.
  void theCropReadOutFormsAndFallsWithItsOwnCloud() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    int cloudsOnTick = 0, cloudsOnUntick = 0;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      QTest::keyClicks(page->findChild<QLineEdit*>(), guiTestImage());
      auto* pv = page->findChild<QPushButton*>();
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      const auto clouds = [dlg] {
        return dlg->findChildren<QWidget*>(
                      QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)).size();
      };
      QCheckBox* crop = nullptr;
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { crop = c; break; }
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return clouds() > 0; }, 1500);   // the gather waits out the height ease
      cloudsOnTick = clouds();
      settle([&] { return clouds() == 0; }, 3000);  // …and clears itself
      crop->setChecked(false);
      settle([&] { return clouds() > 0; }, 1500);
      cloudsOnUntick = clouds();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(cloudsOnTick > 0, "ticking Crop must form the read-out out of particles");
    QVERIFY2(cloudsOnUntick > 0, "unticking it must scatter the read-out, not just hide it");
  }
  // The line SLIDES in and back out, so nothing under it is snapped by its height, and the
  // window returns to the height it left (browser twin: open-image-readout.spec.js).
  void theReadOutSlidesIntoTheColumnAndBackOut() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QList<int> inward, outward;
    int before = 0, after = 0, full = 0, closedH = 0;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      QTest::keyClicks(page->findChild<QLineEdit*>(), guiTestImage());
      auto* pv = page->findChild<QPushButton*>();
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      settle([] { return false; }, 700);
      QCheckBox* crop = nullptr;
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { crop = c; break; }
      if (!crop) { dlg->reject(); return; }
      QLabel* dims = dlg->cropDims;
      before = dlg->height();
      crop->setChecked(true);
      for (int i = 0; i < 12; ++i) { settle([] { return false; }, 50); inward << dims->height(); }
      full = dims->height();
      after = dlg->height();
      crop->setChecked(false);
      for (int i = 0; i < 12; ++i) { settle([] { return false; }, 50); outward << dims->height(); }
      closedH = dlg->height();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(full > 8, qPrintable(QString("the read-out never opened (%1px)").arg(full)));
    QVERIFY2(after > before, "the window makes room for it");
    // Heights between nothing and the whole line — what a visibility toggle cannot produce.
    const auto middles = [full](const QList<int>& hs) {
      int n = 0; for (int h : hs) if (h > 0 && h < full) ++n; return n; };
    QVERIFY2(middles(inward) >= 2, "sliding in means heights between nothing and the line");
    QVERIFY2(middles(outward) >= 2, "…and out");
    QCOMPARE(outward.last(), 0);
    // Back to the height it left: the slide and the window ease share one clock, so the
    // column cannot settle a line short of, or over, its own content.
    QCOMPARE(closedH, before);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImageReadout.gui.moc"
