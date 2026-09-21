// MainWindow GUI e2e — the Open Image dialog's PICTURE clouds: the one a replacement blows
// away, and the rule that a cloud never outlives the tab or the window that raised it. Both
// raise while the widget is still shown — DisintegrateFactory refuses an invisible source.
// The crop read-out's own cloud is in MainWindow.openImageReadout.gui.cpp.
// Shared ground in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

#include "OpenImageDialog.hpp"
#include "../src/support/DisintegrateOverlay.hpp"
#include <QCheckBox>
#include <QLabel>
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

  // Swapping the source blows the OLD picture away first (browser twin: loadPreviewMedia's scatter).
  // The cloud must be raised while the label is still shown — overRect refuses an invisible source.
  void replacingThePreviewScattersTheOldPicture() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A second picture, so the swap really swaps.
    const QString other = QDir::temp().filePath(QStringLiteral("stencil_oi_swap.png"));
    QImage second(200, 300, QImage::Format_RGB32);
    second.fill(Qt::darkCyan);
    QVERIFY(second.save(other, "PNG"));
    int cloudsOnSwap = 0;
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
      // A DIFFERENT source: the old picture must leave on a cloud of its own.
      url->clear();
      QTest::keyClicks(url, other);
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      // A SHORT window on purpose: the departure is raised the moment Preview is pressed, while the new
      // picture cannot land before its decode plus OI_RESIZE_MS, so a cloud this early is the old one.
      const auto clouds = [dlg] {
        return dlg->findChildren<QWidget*>(
                     QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)).size();
      };
      settle([&] { return clouds() > 0; }, 120);
      cloudsOnSwap = clouds();
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(cloudsOnSwap > 0, "the replaced picture must blow away, not just disappear");
  }

  // A cloud belongs to the moment that raised it: leaving the tab, or dismissing the dialog, takes it
  // along and lifts the veil it was standing in for.
  void leavingTakesTheCloudAlongAndLiftsItsVeil() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // A second picture: an arrival plays once per source (animatedSources), so the close
    // has to be watched over a source that has not flown yet.
    const QString other = QDir::temp().filePath(QStringLiteral("stencil_oi_leave.png"));
    QImage second(240, 180, QImage::Format_RGB32);
    second.fill(Qt::darkMagenta);
    QVERIFY(second.save(other, "PNG"));
    int mid = -1, onSwitch = -1, again = -1, onClose = -1;
    bool veiled = true;
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
      settle([&] { return clouds() > 0; }, 2000);   // the arrival, mid-flight
      mid = clouds();
      tabs->setCurrentIndex(2);                     // …and away, before it lands
      onSwitch = clouds();
      veiled = dlg->previewLabel->graphicsEffect() != nullptr;
      tabs->setCurrentIndex(1);                     // back, then a source that is new here
      auto* url = page->findChild<QLineEdit*>();
      url->clear();
      QTest::keyClicks(url, other);
      settle([&] { return pv->isEnabled(); }, 1000);
      pv->click();
      settle([&] { return clouds() > 0; }, 4000);
      again = clouds();
      dlg->reject();                                // closing is the same rule
      onClose = clouds();
    });
    win.openImage();
    QVERIFY2(mid > 0, "the arriving picture must raise a cloud to begin with");
    QCOMPARE(onSwitch, 0);
    QVERIFY2(!veiled, "the veil the cloud stood in for must lift with it");
    QVERIFY2(again > 0, "a new source raises a cloud of its own");
    QCOMPARE(onClose, 0);
  }

  // The Album/Portrait button's cloud follows its row: the window's own height-ease (the stage growing
  // above it) moves the row out from under a cloud raised once at its starting position.
  void albumButtonDustFollowsTheRowAsTheWindowEases() {
    const auto motion = withMotion();
    bool followed = true, sawAny = false;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      const QString img = QDir::temp().filePath(QStringLiteral("stencil_oi_dustfollow.png"));
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
      for (int i = 0; i < 10; i++) {
        settle([] { return false; }, 40);
        for (QWidget* c : dlg->findChildren<QWidget*>(
                 QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME))) {
          if (c->width() > 40 && c->width() < 130 && c->height() < 40) {   // the button's own cloud
            const QPoint want = dlg->cropAlbum->mapTo(dlg->cropAlbum->parentWidget(), QPoint());
            sawAny = true;
            if ((c->pos() - want).manhattanLength() > 4) followed = false;
          }
        }
      }
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(sawAny && followed, "the button's cloud tracks the row through the whole height ease");
  }

  // Picking Custom (or leaving it) forms and falls with its own cloud, like every other arrival and
  // departure in this dialog (user report; browser twin: openImageModal.js's syncCropSizeCustom).
  void pickingCustomFormsAndFallsWithItsOwnCloud() {
    const auto motion = withMotion();
    bool sawArrive = false, sawLeave = false;
    MainWindow win(nullptr, false);
    win.resize(1250, 980);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QTimer::singleShot(0, &win, [&] {
      auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
      if (!dlg) return;
      const QString img = QDir::temp().filePath(QStringLiteral("stencil_oi_customdust.png"));
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
      const auto clouds = [dlg] {
        return dlg->findChildren<QWidget*>(
                     QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)).size();
      };
      const int customIdx = dlg->cropPageSize->findData(QStringLiteral("custom"));
      dlg->cropPageSize->setCurrentIndex(customIdx);
      settle([&] { return clouds() > 0; }, 1500);
      sawArrive = clouds() > 0;
      settle([&] { return clouds() == 0; }, 3000);
      const int pageIdx = dlg->cropPageSize->findData(QStringLiteral("page"));
      dlg->cropPageSize->setCurrentIndex(pageIdx);
      settle([&] { return clouds() > 0; }, 1500);
      sawLeave = clouds() > 0;
      dlg->reject();
    });
    win.openImage();
    QVERIFY2(sawArrive, "picking Custom must form its W×H fields out of particles");
    QVERIFY2(sawLeave, "leaving Custom must scatter them, not just hide the group");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.openImageDust.gui.moc"
