// MainWindow GUI e2e — a tab's crop starts from ITS OWN orientation, never a stale opinion
// left over from another tab: the outgoing stage used to survive just long enough into the
// arriving tab's showPreview() to recompute (and persist) a rect from the WRONG aspect.
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include <QCheckBox>
#include <QDir>
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

  // A tab's crop must start from ITS OWN orientation: the outgoing stage used to survive into the
  // arriving tab's showPreview() and persist a rect recomputed from the wrong aspect.
  void aTabsFirstCropStartsFromItsOwnOrientationNotTheOtherTabs() {
    bool localAlbum = false, urlAlbum = true;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      auto* tabs = dlg->findChild<QTabWidget*>(QStringLiteral("oiTabs"));
      // Local: a LANDSCAPE image, crop ON (its own default is Album).
      const QString localImg = QDir::temp().filePath(QStringLiteral("stencil_oi_wide.png"));
      QImage wide(2880, 2037, QImage::Format_RGB32);
      wide.fill(Qt::darkRed);
      QVERIFY(wide.save(localImg, "PNG"));
      tabs->setCurrentIndex(0);
      dlg->path->setText(localImg);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return dlg->cropStage != nullptr; }, 2000);
      localAlbum = dlg->cropStage->getAlbum();
      // URL: a cached PORTRAIT video (its own default is Portrait), crop ticked fresh.
      const QString urlSrc = QStringLiteral("https://example.com/vid.mp4");
      QImage frame(2874, 4064, QImage::Format_RGB32);
      frame.fill(Qt::darkGreen);
      dlg->url->setText(urlSrc);
      auto& urlCache = dlg->tabCache[stencil::gui::TabUrl];
      urlCache.valid = true; urlCache.source = urlSrc; urlCache.isVideo = true;
      urlCache.frameImage = frame; urlCache.previewImage = frame; urlCache.scrubFps = 30.0;
      tabs->setCurrentIndex(1);
      settle([] { return false; }, 300);
      QCheckBox* crop2 = cropBox(dlg);
      if (crop2 && !crop2->isChecked()) crop2->setChecked(true);
      settle([&] { return dlg->cropStage != nullptr; }, 2000);
      urlAlbum = dlg->cropStage ? dlg->cropStage->getAlbum() : true;
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(localAlbum, "the wide image's own default is Album");
    QVERIFY2(!urlAlbum, "the portrait video's own default is Portrait, not the other tab's Album");
  }


  // Pressing the Album/Portrait button must flip the stage on screen: syncQuickcropEnabled's full
  // rebuild re-derived the picture's own default orientation and overwrote the press.
  void pressingTheOrientationButtonFlipsTheStage() {
    bool albumBefore = false, albumAfter = true, rectChanged = false;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      const QString img = QDir::temp().filePath(QStringLiteral("stencil_oi_orient.png"));
      QImage wide(2400, 1600, QImage::Format_RGB32);
      wide.fill(Qt::darkBlue);
      QVERIFY(wide.save(img, "PNG"));
      dlg->path->setText(img);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return dlg->cropStage != nullptr; }, 2000);
      if (!dlg->cropStage) { dlg->reject(); return; }
      albumBefore = dlg->cropStage->getAlbum();
      const auto rectBefore = dlg->cropStage->cropRect();
      dlg->cropAlbum->click();
      settle([] { return false; }, 100);
      albumAfter = dlg->cropStage ? dlg->cropStage->getAlbum() : albumBefore;
      const auto rectAfter = dlg->cropStage ? dlg->cropStage->cropRect() : rectBefore;
      rectChanged = rectAfter.width != rectBefore.width || rectAfter.height != rectBefore.height;
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(albumAfter != albumBefore, "the press must flip the stage's own orientation");
    QVERIFY2(rectChanged, "a flip re-centres the rect to the new aspect, not the old one");
  }

  // Picking a page size is never a flip: every ISO A/B/C size shares one ratio, and Custom's genuinely
  // different aspect resets to a fresh default — there is no reciprocal to carry the old box across.
  void pickingARatioReshapesTheStageToTheNewAspect() {
    bool ratio11 = false, ratio23Distinct = false, customReshaped = false, projectPageUntouched = true;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      const QString img = QDir::temp().filePath(QStringLiteral("stencil_oi_pagesize.png"));
      QImage wide(2400, 1600, QImage::Format_RGB32);
      wide.fill(Qt::darkBlue);
      QVERIFY(wide.save(img, "PNG"));
      const QString projectPage = dlg->getCropPageSize();
      dlg->path->setText(img);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return dlg->cropStage != nullptr; }, 2000);
      if (!dlg->cropStage) { dlg->reject(); return; }

      const int idx11 = dlg->cropPageSize->findData(QStringLiteral("1:1"));
      QVERIFY(idx11 >= 0);
      dlg->cropPageSize->setCurrentIndex(idx11);
      // cropAspect normalizes by ORIENTATION, not argument order: album (still the stage's
      // own choice — a ratio change is never a flip) puts the LONG side horizontal.
      const auto after11 = dlg->cropStage->cropRect();
      ratio11 = qAbs(after11.width / after11.height - 1.0) < 0.05;

      const int idx23 = dlg->cropPageSize->findData(QStringLiteral("2:3"));
      QVERIFY(idx23 >= 0);
      dlg->cropPageSize->setCurrentIndex(idx23);
      const auto after23 = dlg->cropStage->cropRect();
      ratio23Distinct = qAbs(after23.width / after23.height - 1.5) < 0.05;

      const int customIdx = dlg->cropPageSize->findData(QStringLiteral("custom"));
      QVERIFY(customIdx >= 0);
      dlg->cropPageSize->setCurrentIndex(customIdx);
      dlg->cropSizeW->setValue(10.0);
      dlg->cropSizeH->setValue(80.0);
      const auto afterCustom = dlg->cropStage->cropRect();
      customReshaped = qAbs(afterCustom.width / afterCustom.height - 8.0) < 0.1;
      projectPageUntouched = dlg->getCropPageSize() == projectPage;
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(ratio11, "1:1 gives a perfectly square rect");
    QVERIFY2(ratio23Distinct, "2:3 gives a distinctly different ratio from 1:1");
    QVERIFY2(customReshaped, "Custom's own aspect actually reshapes the stage");
    QVERIFY2(projectPageUntouched, "picking a crop ratio never touches the project's own page");
  }

  // "Page" reads as the default choice, not a cm/in size; the Album/Portrait button never
  // resizes between its two words; the ratio selector never stretches past its own content.
  void thePageEntryReadsAsDefaultAndTheOrientationButtonNeverResizes() {
    QString pageLabel;
    int wPortrait = -1, wAlbum = -1, wSelector = -1, wHint = -1, wRow = -1;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      const QString img = QDir::temp().filePath(QStringLiteral("stencil_oi_ratiolabel.png"));
      QImage wide(2400, 1600, QImage::Format_RGB32);
      wide.fill(Qt::darkBlue);
      QVERIFY(wide.save(img, "PNG"));
      dlg->path->setText(img);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
      settle([&] { return !dlg->previewedImage().isNull(); }, 4000);
      QCheckBox* crop = cropBox(dlg);
      if (!crop) { dlg->reject(); return; }
      crop->setChecked(true);
      settle([&] { return dlg->cropStage != nullptr; }, 2000);
      pageLabel = dlg->cropPageSize->itemText(dlg->cropPageSize->findData(QStringLiteral("page")));
      wPortrait = dlg->cropAlbum->width();
      dlg->cropAlbum->click();
      settle([] { return false; }, 100);
      wAlbum = dlg->cropAlbum->width();
      wSelector = dlg->cropPageSize->width();
      wHint = dlg->cropPageSize->sizeHint().width();
      wRow = dlg->cropSizeRow->width();
      dlg->reject();
    });
    win.openImage();
    QCOMPARE(pageLabel, QStringLiteral("Page — Default"));
    QCOMPARE(wAlbum, wPortrait);
    // Its own content width, never stretched (browser twin: .oi-crop-size is inline-flex).
    QVERIFY2(wSelector <= wHint + 2,
             qPrintable(QString("selector %1px, own hint %2px, row %3px").arg(wSelector).arg(wHint).arg(wRow)));
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImageCropOrient.gui.moc"
