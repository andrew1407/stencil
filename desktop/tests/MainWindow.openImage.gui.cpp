// MainWindow GUI e2e — the Open Image dialog's chrome: the height tracking its CURRENT tab
// (a tall preview on one tab must not linger as dead space on an empty one), the frame scrub
// bar, the Album/Portrait glyph and where each tab puts the keyboard. The crop stage has its
// own binary (MainWindow.openImageCrop.gui.cpp). Shared ground in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "../src/support/iconSpin.hpp"
#include "../src/support/scrubBar.hpp"
#include "../src/support/DisintegrateOverlay.hpp"
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

  // The dialog OPENS at the browser's own floor (openImage.css min-height 430) and centred
  // on its parent: the three tabs' own hints come to barely 280px, and Qt centres a window
  // at the size it had before the first-show measurement grew it.
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

  // Each source tab owns its OWN crop choice and its own picture: ticking Crop on the URL
  // tab must not leave that tab's cropped image standing on the Local tab, which has no
  // source at all (browser twin: tabCrop + resetPreviewState dropping the stage).
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

  // ONE control, not a button beside a field: the accent CTA butted straight against the
  // path readout inside a single outlined box (browser twin: .oi-file). Both halves must
  // sit in that box, in that order, with no seam — a gap, or the readout leading, is the
  // two-control shape this replaced. The read-only half still opens the chooser, so it has
  // to read as live rather than as a field that ignores the pointer.
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

  // The scrub bar is a PLAYER's line (browser .oi-scrub): tapping it seeks THERE, and the
  // APP STYLESHEET is what paints it — a per-widget QStyle silently drops that sheet, which
  // leaves Qt's own chunky square handle behind (#oiFrameScrub draws a 13px round one).
  void scrubBarSeeksWhereItIsTappedAndKeepsItsSheet() {
    QSlider bar(Qt::Horizontal);
    stencil::gui::makeScrubBar(&bar);
    bar.setRange(0, 100);
    bar.resize(200, 16);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    QVERIFY2(bar.style() == QApplication::style(),
             "the bar must keep the app's style, or the sheet that paints it is dropped");

    QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier, QPoint(bar.width() * 3 / 4, 8));
    QVERIFY2(qAbs(bar.value() - 75) <= 6,
             qPrintable(QString("a tap at three quarters must seek there, got %1").arg(bar.value())));
  }

  // The swap glyph the Album / Portrait buttons wear (Open Image + the crop editor)
  // answers a CLICK with one clockwise turn — iconMotion.json `extras.swap-click-turn`,
  // whose browser twin is css/animations/iconClick.css. Pinned on the shared helper: the
  // buttons themselves also re-tint when checked, which masks the turn.
  void swapGlyphTurnsOnceOnClickAndComesBack() {
    const auto motion = withMotion();
    QPushButton b;
    b.setIcon(stencil::gui::labelIcon(QStringLiteral("swap"), QColor("#ffffff"), 15));
    b.setIconSize(QSize(21, 21));
    b.show();
    const QImage rest = b.icon().pixmap(b.iconSize()).toImage();
    stencil::support::spinIconOnce(&b);
    const bool startedSpinning = b.property("stencilIconSpinning").toBool();
    settle([] { return false; }, 150);
    const QImage mid = b.icon().pixmap(b.iconSize()).toImage();
    settle([&] { return !b.property("stencilIconSpinning").toBool(); }, 3000);
    const QImage back = b.icon().pixmap(b.iconSize()).toImage();
    QVERIFY2(startedSpinning, "a click must start the turn");
    QVERIFY2(mid != rest, "the glyph must actually be rotated mid-turn");
    QVERIFY2(back == rest, "and must come back to exactly the glyph it started as");
  }

  // Load a preview on the URL tab, then switch to the empty Local-file tab: the dialog
  // must shrink back down, not hold the tall preview's height as dead space (image #13/#15).
  void openImageDialogShrinksOnTabSwitch() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    int wantTall = 0, shortH = 0;
    bool urlFocused = false, chooseFocused = false;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      if (!tabs) { dlg->reject(); return; }
      tabs->setCurrentIndex(1);  // URL link
      QWidget* urlPage = tabs->currentWidget();
      auto* url = urlPage ? urlPage->findChild<QLineEdit*>() : nullptr;
      urlFocused = url && dlg->focusWidget() == url;   // offscreen never ACTIVATES a window,
                                                      // so ask the dialog, not hasFocus()
      auto* previewBtn = urlPage ? urlPage->findChild<QPushButton*>() : nullptr;
      if (!url || !previewBtn) { dlg->reject(); return; }
      QTest::keyClicks(url, guiTestImage());  // a real edit — setText() alone never fires
                                               // textEdited, so Preview would stay disabled
      settle([&] { return previewBtn->isEnabled(); }, 1000);
      previewBtn->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 3000);
      // The dialog sizes ITSELF from the scroll body's content (refitWindowHeight) and EASES
      // there, so the grown height is only itself once the flight has landed — sampled
      // mid-climb, "shrink below this" is a bar the shrink can never clear.
      settle([] { return false; }, 500);
      wantTall = dlg->height();
      tabs->setCurrentIndex(0);  // Local file — nothing chosen, should shrink back down
      if (QWidget* filePage = tabs->currentWidget())
        if (auto* choose = filePage->findChild<QPushButton*>())
          chooseFocused = dlg->focusWidget() == choose;
      settle([&] { return dlg->height() < wantTall; }, 2500);   // the height EASES there
      shortH = dlg->height();
      dlg->reject();
    });
    win.openImage();

    QVERIFY2(wantTall > 0, "the URL preview must have loaded and grown the sizeHint");
    QVERIFY2(shortH > 0 && shortH < wantTall,
             qPrintable(QString("the empty Local-file tab kept %1px of the URL tab's %2px")
                            .arg(shortH).arg(wantTall)));
    QVERIFY2(urlFocused, "the URL tab must hand the keyboard to its field");
    QVERIFY2(chooseFocused, "…and Local to its Choose button (its path field is read-only)");
  }

  // A wider window fits a bigger preview (user report: it stayed pinned at its historical
  // 440x300 floor). The box must also SETTLE, not keep climbing on its own — sizing it from
  // size_.bodyContent's own width fed the box's effect on that width back in as the next frame's
  // input (measured: 558 -> 756px with no further user input at all).
  void resizingTheWindowGrowsThePreviewAndSettles() {
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    int wBefore = 0, wAfter = 0, wSettled = 0;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      const QString img = QDir::temp().filePath(QStringLiteral("stencil_oi_resize.png"));
      QImage wide(2400, 1600, QImage::Format_RGB32);
      wide.fill(Qt::darkBlue);
      QVERIFY(wide.save(img, "PNG"));
      dlg->path_->setText(img);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      settle([] { return false; }, 400);
      wBefore = dlg->previewLabel_->maximumWidth();
      dlg->resize(1000, dlg->height());
      settle([] { return false; }, 200);
      wAfter = dlg->previewLabel_->maximumWidth();
      settle([] { return false; }, 400);
      wSettled = dlg->previewLabel_->maximumWidth();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(wAfter > wBefore, "a wider window must grow the preview's own box");
    QCOMPARE(wSettled, wAfter);   // never keeps climbing on its own
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImage.gui.moc"
