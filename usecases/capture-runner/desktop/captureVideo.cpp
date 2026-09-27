// The Open Image dialog with a clip in it: off disk, from a link, and under the quick-crop box.
#include "captureShared.hpp"

#include "OpenImageDialog.hpp"
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  // The clip the video shots open, and the same clip over http. desktop.mjs makes it with
  // ffmpeg into the scratch dir and serves it, so no binary is committed; empty = skip.
  const QString CLIP_FILE = qEnvironmentVariable("STENCIL_DOCS_CLIP");
  const QString CLIP_URL = qEnvironmentVariable("STENCIL_DOCS_CLIP_URL");
}  // namespace

// The Open Image dialog with a VIDEO in it: taken mid-clip, so the scrub bar's accent fill
// reads as a position rather than an empty rail. Browser twin: browser/videoSteps.mjs.
void MainWindowGuiTest::grabVideoDialog(MainWindow& win, const QString& name,
                                      const std::function<void(OpenImageDialog*)>& load,
                                      bool crop) {
  bool done = false;
  QTimer::singleShot(0, [&] {
    auto* dlg = qobject_cast<OpenImageDialog*>(QApplication::activeModalWidget());
    if (!dlg) return;
    load(dlg);
    // Offscreen has no GPU decoder on every host, so a clip that never arrives is a skip.
    if (!waitUntil([dlg] { return !dlg->previewedImage().isNull() && dlg->isVideo(); }, 20000)) {
      std::printf("  %s SKIPPED (the clip did not decode)\n", qPrintable(name));
      done = true;
      dlg->reject();
      return;
    }
    if (dlg->frameSlider) dlg->frameSlider->setValue(dlg->frameSlider->maximum() / 2);
    waitUntil([] { return false; }, 600);   // let the seek land on the player
    if (crop && dlg->cropPage) {
      dlg->cropPage->setChecked(true);
      waitUntil([] { return false; }, 700);   // the box eases in over the picture
    }
    pumpFor(400);
    saveOver(name, &win, dlg);
    done = true;
    dlg->reject();
  });
  QTimer::singleShot(30000, [] { if (QWidget* stuck = QApplication::activeModalWidget()) stuck->close(); });
  win.parts.sourceOpener.openImage();
  waitUntil([&] { return done; }, 32000);
  pumpFor(200);
}

void MainWindowGuiTest::videoShots(MainWindow& win, const ShotSet& shots) {
  // The clip off disk, the clip from a link, and the crop box over the player.
  const QStringList videoShots = {"open-video-local", "open-video-url", "crop-video"};
  if (shots.hasAny(videoShots) && CLIP_FILE.isEmpty())
    std::printf("  video shots SKIPPED (STENCIL_DOCS_CLIP unset)\n");
  else if (shots.hasAny(videoShots)) {
    // The tail of OpenImageDialog::browse(), which is what a picked file runs.
    const auto fromFile = [](OpenImageDialog* dlg) {
      dlg->path->setText(CLIP_FILE);
      dlg->resetPreviewState();
      dlg->refreshButtons();
      dlg->doPreview();
    };
    const auto fromUrl = [](OpenImageDialog* dlg) {
      dlg->tabs->setCurrentIndex(1);   // URL link
      QTest::keyClicks(dlg->url, CLIP_URL);   // setText alone never fires textEdited,
      waitUntil([dlg] { return dlg->previewBtn->isEnabled(); }, 2000);   // so Preview stays off
      dlg->previewBtn->click();
    };
    if (shots.has("open-video-local")) grabVideoDialog(win, "open-video-local", fromFile, false);
    // A URL clip goes straight to QMediaPlayer::setSource, whose platform media stack refuses the
    // capture's own little server — this shot wants a real web server.
    if (shots.has("open-video-url") && !CLIP_URL.isEmpty())
      grabVideoDialog(win, "open-video-url", fromUrl, false);
    if (shots.has("crop-video")) grabVideoDialog(win, "crop-video", fromFile, true);
  }
}
