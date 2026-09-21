// MainWindow GUI e2e — the Open Image crop stage reacting to what happens AROUND it: an
// edited URL clearing its stale rect at once (not waiting for a new decode), and a cached
// image tab not keeping a video tab's Frame row. Shared ground in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include <QCheckBox>
#include <QLineEdit>
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

  // Editing the URL mid-crop clears the STAGE instantly — its rect belongs to the old pixels — without
  // waiting for a new decode, while the plain picture stays up per stalePreview's own contract.
  void editingTheUrlMidCropClearsTheStageInstantly() {
    bool stagedBefore = false, stagedAfter = true, pictureVisible = false, stillChecked = false;
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
      auto* pv = page->findChild<QPushButton*>();
      QTest::keyClicks(url, guiTestImage());
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return dlg->cropStage != nullptr; }, 2000);
      stagedBefore = dlg->cropStage != nullptr;
      QTest::keyClicks(url, "x");
      settle([] { return false; }, 100);
      stagedAfter = dlg->cropStage != nullptr;
      pictureVisible = dlg->previewLabel->isVisible() && !dlg->previewLabel->pixmap().isNull();
      stillChecked = crop->isChecked();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(stagedBefore, "Crop must have staged a rect to begin with");
    QVERIFY2(!stagedAfter, "editing the URL must clear the old rect at once, not after a decode");
    QVERIFY2(pictureVisible, "the plain picture stays up while the text is corrected");
    QVERIFY2(stillChecked, "the choice itself is untouched — only the stale rect goes");
  }

  // A cached IMAGE tab must not keep a VIDEO tab's Frame row: nothing hides it but the restore itself,
  // so both tabs are pre-cached and reached by a REAL tab click, as applyMode()'s cache branch runs.
  void restoringAnImageTabHidesTheOtherTabsFrameRow() {
    bool frameShownAfterVideo = false, frameShownAfterImage = true;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      const QString localPath = guiTestImage();
      const QString urlSrc = QStringLiteral("https://example.com/vid.mp4");
      QImage still(240, 160, QImage::Format_RGB32); still.fill(Qt::white);
      QImage frame(320, 240, QImage::Format_RGB32); frame.fill(Qt::darkGreen);
      dlg->path->setText(localPath);
      auto& fileCache = dlg->tabCache[stencil::gui::TabFile];
      fileCache.valid = true; fileCache.source = localPath;
      fileCache.isVideo = false; fileCache.previewImage = still;
      dlg->url->setText(urlSrc);
      auto& urlCache = dlg->tabCache[stencil::gui::TabUrl];
      urlCache.valid = true;
      urlCache.source = urlSrc;
      urlCache.isVideo = true;
      urlCache.frameImage = frame;
      urlCache.previewImage = frame;
      urlCache.scrubFps = 30.0;
      dlg->previewedSource.clear();   // neither tab has been "arrived at" yet
      tabs->setCurrentIndex(1);
      settle([] { return false; }, 200);
      frameShownAfterVideo = dlg->frameRow->isVisible();
      tabs->setCurrentIndex(0);
      settle([] { return false; }, 200);
      frameShownAfterImage = dlg->frameRow->isVisible();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(frameShownAfterVideo, "the cached video must have shown a Frame row to lose");
    QVERIFY2(!frameShownAfterImage,
             "the image tab must not keep the video tab's Frame row");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImageCropEdit.gui.moc"
