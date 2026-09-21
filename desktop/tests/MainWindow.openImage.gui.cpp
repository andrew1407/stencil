// MainWindow GUI e2e — The Open Image window's floor, its per-tab crop, and the Choose control.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "../src/support/icon/iconSpin.hpp"
#include "../src/support/control/scrubBar.hpp"
#include "../src/support/motion/DisintegrateOverlay.hpp"
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>

using stencil::gui::OpenImageDialog;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The dialog OPENS at the browser's own floor (openImage.css min-height 430) and centred on its
  // parent: the three tabs' hints come to barely 280px, and Qt centres at the pre-measurement size.
  void openImageOpensAtTheBrowsersFloorAndCentred() {
    MainWindow win(nullptr, false);
    win.resize(1250, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    int h = 0, dx = 0, dy = 0;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      settle([] { return false; }, 500);   // past the first-show measurement and its ease
      h = dlg->height();
      dx = dlg->geometry().center().x() - win.geometry().center().x();
      dy = dlg->geometry().center().y() - win.geometry().center().y();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(h >= stencil::gui::OI_MIN_H,
             qPrintable(QString("opened %1px tall; the browser's floor is %2")
                            .arg(h).arg(stencil::gui::OI_MIN_H)));
    QVERIFY2(qAbs(dx) <= 2 && qAbs(dy) <= 2,
             qPrintable(QString("off its parent's centre by (%1,%2)").arg(dx).arg(dy)));
  }

  // Each source tab owns its OWN crop choice and its own picture: ticking Crop on the URL tab must not
  // leave that tab's cropped image on the Local tab (browser twin: tabCrop + resetPreviewState).
  void cropAndItsStageBelongToTheTabThatMadeThem() {
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool stageOnUrl = false, stageLeakedToFile = true, cropLeakedToFile = true;
    bool cropRestored = false;
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
      QCheckBox* crop = nullptr;
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { crop = c; break; }
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([] { return false; }, 700);
      const auto stageUp = [dlg] {
        auto* s = dlg->findChild<stencil::gui::CropPreview*>();
        return s && s->isVisible();
      };
      stageOnUrl = stageUp();
      tabs->setCurrentIndex(0);   // Local file — nothing chosen here
      settle([] { return false; }, 700);
      stageLeakedToFile = stageUp();
      cropLeakedToFile = crop->isChecked();
      tabs->setCurrentIndex(1);   // …and the URL tab gets its own choice back
      settle([] { return false; }, 700);
      cropRestored = crop->isChecked();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(stageOnUrl, "ticking Crop must put the stage up on the tab that ticked it");
    QVERIFY2(!stageLeakedToFile, "the cropped picture must not stand on a tab with no source");
    QVERIFY2(!cropLeakedToFile, "nor must the Crop tick itself cross to the other tab");
    QVERIFY2(cropRestored, "…and coming back must restore that tab's own choice");
  }

  // ONE control, not a button beside a field: the accent CTA butted against the path readout inside a
  // single outlined box (browser .oi-file), in that order, no seam — and the readout still opens it.
  void chooseAndItsPathReadoutAreOneOutlinedControl() {
    MainWindow win(nullptr, false);
    win.resize(1250, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool found = false, sameBox = false, buttonLeads = false, readsLive = false;
    int seam = -1;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      settle([] { return false; }, 300);   // past the first-show measurement
      // By objectName: those names are the QSS hooks that carry the outline and drop the
      // halves' own borders, so a rename silently returns the two-control look.
      auto* box = dlg->findChild<QFrame*>(QStringLiteral("oiFileBox"));
      auto* choose = dlg->findChild<QPushButton*>(QStringLiteral("oiChooseBtn"));
      auto* path = dlg->findChild<QLineEdit*>(QStringLiteral("oiPathField"));
      found = box && choose && path;
      if (!found) { dlg->reject(); return; }
      sameBox = choose->parentWidget() == box && path->parentWidget() == box;
      buttonLeads = choose->x() < path->x();
      seam = path->x() - (choose->x() + choose->width());
      readsLive = path->isReadOnly() && !path->toolTip().isEmpty()
                  && path->cursor().shape() == Qt::PointingHandCursor;   // clickActivates
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(found, "#oiFileBox / #oiChooseBtn / #oiPathField must all be there to be styled");
    QVERIFY2(sameBox, "Choose and the path readout belong inside the one outlined box");
    QVERIFY2(buttonLeads, "the accent CTA leads and the readout follows it, as .oi-file does");
    QVERIFY2(seam >= 0 && seam <= 1,
             qPrintable(QString("a %1px seam between the halves; they butt together").arg(seam)));
    QVERIFY2(readsLive, "the read-only half opens the chooser too, so it must read as live");
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImage.gui.moc"
