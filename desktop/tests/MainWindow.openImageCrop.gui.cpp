// MainWindow GUI e2e — the Open Image dialog's CROP: the draggable stage, the room the
// window makes for it, and the rect surviving a handoff. Ground in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "launchOptions.hpp"
#include "CanvasWidget.hpp"
#include "../src/support/DisintegrateOverlay.hpp"
#include <QGraphicsOpacityEffect>
#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>

using stencil::gui::OpenImageDialog;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A STILL's crop draws a rect on the picture already on screen — nothing arrives, so
  // nothing plays and nothing is ever veiled. (A video's stage IS a new canvas below the
  // player; that one is veiled before its first paint so it cannot blink in. Qt Multimedia
  // will not decode here, so only the still half is reachable.)
  void croppingAStillNeverVeilsThePictureItAlreadyShows() {
    const auto motion = withMotion();   // the blink only exists when the dust plays
    MainWindow win(nullptr, false);
    win.resize(1250, 980); win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool veiledAtBirth = false, unveiled = false;
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
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { c->setChecked(true); break; }
      // IMMEDIATELY after the tick: the stage must already be veiled, never shown first.
      auto* stage = dlg->findChild<stencil::gui::CropPreview*>();
      auto* fx = stage ? qobject_cast<QGraphicsOpacityEffect*>(stage->graphicsEffect()) : nullptr;
      veiledAtBirth = fx && fx->opacity() == 0.0;
      settle([] { return false; }, 600);
      unveiled = stage && !stage->graphicsEffect();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(!veiledAtBirth, "a still's crop must not veil the picture it already shows");
    QVERIFY2(unveiled, "and must never be left with an effect on it");
  }

  // Crop on: a DRAGGABLE box over the picture, its size read out underneath, and its OWN
  // page-size picker (never the project's own page). A still swaps out the preview for it.
  void croppingAStillShowsTheDraggableStageAndItsSize() {
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool stageUp = false, dimsUnderStage = false, pagePicker = false;
    QString dimsText;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      QTest::keyClicks(page->findChild<QLineEdit*>(), guiTestImage());
      auto* preview = page->findChild<QPushButton*>();
      settle([&] { return preview->isEnabled(); }, 1000);
      preview->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { c->setChecked(true); break; }
      settle([] { return false; }, 500);
      auto* stage = dlg->findChild<stencil::gui::CropPreview*>();
      stageUp = stage && stage->isVisible() && stage->cropRect().width > 0;
      for (QLabel* l : dlg->findChildren<QLabel*>())
        if (l->isVisible() && l->text().contains(QLatin1String("px"))) {
          dimsText = l->text();
          dimsUnderStage = stage && l->mapTo(dlg, QPoint(0, 0)).y() > stage->mapTo(dlg, QPoint(0, 0)).y();
        }
      const auto* combo = dlg->findChild<QComboBox*>();
      pagePicker = combo && combo->isVisible();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(stageUp, "cropping must put a draggable stage over the picture");
    QVERIFY2(dimsUnderStage, "its size belongs UNDER the stage, not beside the checkbox");
    QVERIFY2(dimsText.contains(QLatin1String("px")) && dimsText.contains(QLatin1String("Album")),
             "the read-out is the crop editor's own line");
    QVERIFY2(pagePicker, "its own page-size picker shows while cropping");
  }

  // Ticking Crop adds a whole stage to the column, and the window has to TAKE that room:
  // left at its old height the stage is cut off by the footer and the body scrolls for it.
  void croppingGrowsTheWindowToHoldTheStage() {
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    int wantedAfter = 0, heightAfter = 0, screenCap = 0;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      tabs->setCurrentIndex(1);
      QWidget* page = tabs->currentWidget();
      QTest::keyClicks(page->findChild<QLineEdit*>(), guiTestImage());
      auto* preview = page->findChild<QPushButton*>();
      settle([&] { return preview->isEnabled(); }, 1000);
      preview->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      settle([] { return false; }, 400);
      for (QCheckBox* c : dlg->findChildren<QCheckBox*>())
        if (c->isVisible() && c->text().isEmpty()) { c->setChecked(true); break; }
      settle([] { return false; }, 900);   // past the height ease
      // Flip the orientation twice: a flip REBUILDS the stage, where a stale child
      // would be counted into the window's height.
      for (QPushButton* b : dlg->findChildren<QPushButton*>())
        if (b->isCheckable() && b->isVisible()) {
          b->click(); settle([] { return false; }, 700);
          b->click(); settle([] { return false; }, 700);
          break;
        }
      auto* scroll = dlg->findChild<QScrollArea*>();
      QWidget* content = scroll ? scroll->widget() : nullptr;
      QLayout* cl = content ? content->layout() : nullptr;   // heightForWidth-aware, per wantedHeight()
      const int contentH = cl && content && cl->hasHeightForWidth()
          ? cl->totalHeightForWidth(content->width()) : (content ? content->sizeHint().height() : 0);
      wantedAfter = (scroll && content) ? dlg->height() - scroll->height() + contentH : 0;
      heightAfter = dlg->height();
      if (QScreen* s = dlg->screen()) screenCap = int(s->availableGeometry().height() * 0.92);
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(wantedAfter > 0, "the dialog must have a scrolling body to measure");
    QVERIFY2(heightAfter >= qMin(wantedAfter, screenCap),
             "the window must take the room the crop stage asks for");
    // …and NO MORE than that: a flip rebuilds the stage, and the outgoing one is a live
    // child until the event loop spins, so a height measured before that counts two.
    QVERIFY2(heightAfter <= qMin(wantedAfter, screenCap) + 4,
             qPrintable(QString("the window took %1 for content asking %2 — an extra stage?")
                            .arg(heightAfter).arg(wantedAfter)));
  }



  // The VIDEO branch, which Qt Multimedia will not reach here (no decoder under the
  // offscreen platform): the dialog is handed a frame through the test seam instead. A
  // video's crop is the same one stage — its frame takes the picture's place, the scrub bar
  // keeps the STAGE's painted width, and the read-out appears with it.
  void aVideosCropIsTheSameOneStageWithTheBarUnderIt() {
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    bool labelHidden = false, stageUp = false, barMatchesStage = false, dimsUp = false;
    int barW = 0, stageW = 0;
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      // Stand in for a decoded video: a frame, flagged as one, straight into the preview.
      QImage frame(320, 180, QImage::Format_RGB32);
      frame.fill(Qt::darkMagenta);
      dlg->previewIsVideo_ = true;
      dlg->frameImage_ = frame;
      dlg->updateVideoPreview();
      dlg->showQuickcrop(frame.width(), frame.height());
      settle([] { return false; }, 300);
      dlg->cropPage_->setChecked(true);
      settle([] { return false; }, 700);
      auto* stage = dlg->findChild<stencil::gui::CropPreview*>();
      stageUp = stage && stage->isVisible();
      labelHidden = !dlg->previewLabel_->isVisible();
      dimsUp = dlg->cropDims_->isVisible();
      if (stage) {
        stageW = stage->paintedRect().width();
        barW = dlg->frameSlider_->width();
        barMatchesStage = dlg->frameSlider_->isVisible() && barW == stageW;
      }
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(stageUp, "a video's crop must put the same draggable stage up");
    QVERIFY2(labelHidden, "…in the picture's PLACE — showing both would show the frame twice");
    QVERIFY2(dimsUp, "and the size read-out comes with it");
    // Sanity, not a guard: the label's pixmap and the stage's picture fit the same box, so
    // both width sources agree by construction.
    QVERIFY2(barMatchesStage,
             qPrintable(QString("the scrub bar is %1px under a %2px picture").arg(barW).arg(stageW)));
  }

  // "Open in new window" must carry the box the user DRAGGED, not re-centre one: the handoff
  // travels as LaunchOptions, so a rect dropped there is a rect the new window never sees
  // (browser twin: openOpts()'s crop rides openImageNewTab). Driven through the same
  // LaunchOptions the dialog fills, since the real path spawns a second window.
  void aDraggedCropRectSurvivesTheNewWindowHandoff() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::gui::LaunchOptions opts;
    opts.src = guiTestImage();
    opts.hasCropOverride = true;
    opts.cropToPage = true;
    opts.cropX = 11; opts.cropY = 22; opts.cropW = 120; opts.cropH = 84;
    win.applyLaunchOptions(opts);
    settle([&] { return win.canvas_->hasImage() && win.canvas_->cropRect().width > 0; }, 6000);
    const stencil::core::CropRect got = win.canvas_->cropRect();
    QVERIFY2(win.canvas_->hasImage(), "the handoff never loaded its image");
    QCOMPARE(qRound(got.x), 11);
    QCOMPARE(qRound(got.y), 22);
    QCOMPARE(qRound(got.width), 120);
    QCOMPARE(qRound(got.height), 84);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImageCrop.gui.moc"
