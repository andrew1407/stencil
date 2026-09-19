// MainWindow GUI e2e — where the Open Image dialog's CROP CHOICE lives: with the picture
// during a retyped URL, with the TAB across a switch (tick, stage AND the video-only Frame
// row), and the caption beside the box — that box's own label (browser `<label for=…>`).
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include "../src/support/DisintegrateOverlay.hpp"
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTabWidget>

using stencil::gui::OpenImageDialog;

namespace {

  // The dialog's crop toggle: the one visible check with no text of its own.
  QCheckBox* cropBox(OpenImageDialog* dlg) {
    for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
      if (c->isVisible() && c->text().isEmpty()) return c;
    return nullptr;
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Re-typing the URL leaves the picture up (stalePreview) — the Crop row must stay with
  // it: opening re-resolves the typed url and crops THAT.
  void retypingTheUrlKeepsTheCropRowOverThePictureItStillShows() {
    bool rowShown = false, stillTicked = false, pictureShown = false;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      auto* url = page->findChild<QLineEdit*>();
      QTest::keyClicks(url, guiTestImage());
      auto* pv = page->findChild<QPushButton*>();
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([] { return false; }, 300);
      QTest::keyClicks(url, "x");   // the text moves on; the picture does not
      settle([] { return false; }, 200);
      rowShown = dlg->quickcropRow_->isVisible();
      stillTicked = crop->isChecked();
      pictureShown = dlg->previewLabel_->isVisible();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(pictureShown, "the previous URL's picture stays up while the text is corrected");
    QVERIFY2(rowShown, "…so its Crop row must stay too, not vanish with the pixels");
    QVERIFY2(stillTicked, "…still ticked");
  }

  // A tab keeps its own crop: away and back brings the tick AND the stage it stands for.
  // The tick came back alone before — restored signals-blocked, nothing rebuilt the rect.
  void aTabBringsBackItsCropStageNotJustItsTick() {
    bool ticked = false, staged = false;
    int clouds = 0;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
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
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return dlg->cropStage_ != nullptr; }, 2000);
      tabs->setCurrentIndex(2);            // Blank — the crop has nothing to sit on
      settle([] { return false; }, 400);
      tabs->setCurrentIndex(1);            // …and back
      settle([] { return false; }, 400);
      ticked = cropBox(dlg) && cropBox(dlg)->isChecked();
      staged = dlg->cropStage_ != nullptr;
      clouds = dlg->findChildren<QWidget*>(
                     QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)).size();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(ticked, "the tab's own Crop choice comes back");
    QVERIFY2(staged, "…with the stage it stands for, not just the tick");
    QCOMPARE(clouds, 0);   // a tab switch is not a toggle: the read-out does not re-fly
  }

  // …and the picture survives that round trip: losing it to a half-typed address is not a
  // tab switch's doing (browser twin: showTabPreview's stale branch).
  void aTabSwitchNeverLosesThePictureToARetypedUrl() {
    bool shown = false;
    QSize pix;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      auto* url = page->findChild<QLineEdit*>();
      QTest::keyClicks(url, guiTestImage());
      auto* pv = page->findChild<QPushButton*>();
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      QTest::keyClicks(url, "x");          // the address moves on…
      settle([] { return false; }, 200);
      tabs->setCurrentIndex(2);            // …away…
      settle([] { return false; }, 400);
      tabs->setCurrentIndex(1);            // …and back
      settle([] { return false; }, 500);
      shown = dlg->previewLabel_->isVisible();
      pix = dlg->previewLabel_->pixmap().size();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(shown, "coming back to the tab must still show what it was showing");
    QVERIFY2(pix.width() > 0 && pix.height() > 0, "…the picture, not an empty label");
  }


  // A tab switch is silent, even for the crop rows it tears down: leaving a tab with Crop on rebuilt the
  // stage before the quiet flag was armed (user report: closing particles over the Incognito row).
  void switchingAwayFromACroppedTabPlaysNoClosingCloud() {
    const auto motion = withMotion();
    int clouds = 999;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
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
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return dlg->cropStage_ != nullptr; }, 2000);
      settle([] { return false; }, 700);   // the tick's own arrival cloud lands and clears
      tabs->setCurrentIndex(0);            // Local file — nothing chosen, a plain switch
      clouds = dlg->findChildren<QWidget*>(
                     QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)).size();
      dlg->reject();
    });
    win.openImage();
    QCOMPARE(clouds, 0);   // no closing flourish — a switch is not a toggle
  }

  // The words beside the box are its label: clicking them toggles it.
  void clickingTheCaptionTogglesTheBoxBesideIt() {
    bool toggled = false, hadCaption = false;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      // Incognito's caption is up from the start — no preview needed for this one.
      QCheckBox* box = dlg->incognito_;
      QLabel* caption = nullptr;
      for (QLabel* l : dlg->incogRow_->findChildren<QLabel*>())
        if (l->isVisible() && l->text().size() > 12) { caption = l; break; }
      if (!box || !caption) { dlg->reject(); return; }
      hadCaption = true;
      const bool was = box->isChecked();
      const QPointF at(caption->width() / 2.0, caption->height() / 2.0);
      QMouseEvent press(QEvent::MouseButtonPress, at, caption->mapToGlobal(at), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(caption, &press);
      QMouseEvent release(QEvent::MouseButtonRelease, at, caption->mapToGlobal(at),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(caption, &release);
      toggled = box->isChecked() != was;
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(hadCaption, "the Incognito row must carry a caption beside its box");
    QVERIFY2(toggled, "clicking the caption must toggle the box it labels");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImageCropState.gui.moc"
