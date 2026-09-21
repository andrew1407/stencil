// MainWindow GUI e2e — The scrub bar, the swap glyph, and the window easing between tab and preview sizes.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "../../../src/support/icon/iconSpin.hpp"
#include "../../../src/support/control/scrubBar.hpp"
#include "../../../src/support/motion/DisintegrateOverlay.hpp"
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

  // The scrub bar is a PLAYER's line (browser .oi-scrub): tapping it seeks THERE, and the APP
  // STYLESHEET paints it — a per-widget QStyle drops that sheet and leaves Qt's square handle.
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

  // The swap glyph the Album / Portrait buttons wear answers a CLICK with one clockwise turn
  // (iconMotion.json `extras.swap-click-turn`), pinned on the shared helper: the re-tint masks it.
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
      // The dialog sizes ITSELF from the scroll body's content (refitWindowHeight) and EASES there, so a
      // height sampled mid-climb is a bar the shrink can never clear.
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

  // A wider window fits a bigger preview (user report: it stayed pinned at its 440x300 floor), and the
  // box must SETTLE: sizing it from size.bodyContent's own width fed its effect on that width back in.
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
      dlg->path->setText(img);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      settle([] { return false; }, 400);
      wBefore = dlg->previewLabel->maximumWidth();
      dlg->resize(1000, dlg->height());
      settle([] { return false; }, 200);
      wAfter = dlg->previewLabel->maximumWidth();
      settle([] { return false; }, 400);
      wSettled = dlg->previewLabel->maximumWidth();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(wAfter > wBefore, "a wider window must grow the preview's own box");
    QCOMPARE(wSettled, wAfter);   // never keeps climbing on its own
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImageFit.gui.moc"
